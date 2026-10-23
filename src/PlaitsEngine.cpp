#include "PlaitsEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include "plaits/dsp/voice.h"
#include "plaits/dsp/dsp.h"
#include "stmlib/utils/buffer_allocator.h"

namespace
{
    // The firmware smooths its pots once per audio block with these one-pole coefficients (plaits/ui.cc).
    constexpr float kLpHarmonics = 0.005f, kLpTimbre = 0.01f, kLpMorph = 0.01f;
    constexpr float kLpTranspose = 0.005f, kLpAmount = 0.005f;
    constexpr float kLpHidden = 0.01f;            // decay + LPG colour (the hidden parameters of the TIMBRE / MORPH pots)
    constexpr float kLpPitch = 0.7f;              // pitch CV one-pole (ui.cc)
}

plaits::Voice* PlaitsEngine::makeZeroedVoice()
{
    // calloc, not new + memset: with a memset before the constructor, GCC treats the old contents as dead and deletes it
    // (lifetime dead-store elimination), which silently brings the junk back.
    void* mem = std::calloc (1, sizeof (plaits::Voice));
    if (mem == nullptr) throw std::bad_alloc();
    return new (mem) plaits::Voice();
}

void PlaitsEngine::VoiceDeleter::operator() (plaits::Voice* v) const
{
    if (v != nullptr) { v->~Voice(); std::free (static_cast<void*> (v)); }
}

float PlaitsEngine::snapPitch (float semitones, int mode)
{
    if (mode == SnapSemitones) return std::round (semitones);
    if (mode != SnapSteps) return semitones;
    static const float kSteps[] = { 0.f, 7.f, 12.f, 19.f, 24.f, 31.f, 36.f, 43.f, 48.f };   // mirrored below zero
    const float a = std::fabs (semitones);
    float best = kSteps[0];
    for (float d : kSteps)
        if (std::fabs (a - d) < std::fabs (a - best)) best = d;
    return semitones < 0.f ? -best : best;
}

PlaitsEngine::PlaitsEngine()
    : voice (makeZeroedVoice()), sharedRam (32768, 0)
{
}

PlaitsEngine::~PlaitsEngine() = default;

void PlaitsEngine::prepare (double sr)
{
    hostRate = sr;
    rsOutL.init (kRate, hostRate);
    rsOutR.init (kRate, hostRate);
    inQueue.clear(); inQueueHead = 0;
    hostQL.clear(); hostQR.clear(); hostQHead = 0;
    const size_t prime = kBlock * 2 + SincResampler::kTaps;     // keeps the output resampler from starving
    procOutL.assign (prime, 0.0f); procOutR.assign (prime, 0.0f);
    phase = 0.0;
    pendTrig = 0; pendGate = 0;
    trigOut = false; retrig = false;
    smoothInit = false;

    // (Re)build the voice so a re-prepare starts from a clean state, like a power-up.
    voice.reset (makeZeroedVoice());
    std::fill (sharedRam.begin(), sharedRam.end(), (char) 0);
    stmlib::BufferAllocator allocator (sharedRam.data(), sharedRam.size());
    voice->Init (&allocator);
    voiceStarted = true;
}

int PlaitsEngine::latencyInHostSamples() const
{
    const double procFrames = (double) kBlock * 2 + SincResampler::kTaps;
    if (std::abs (hostRate - kRate) < 1e-6) return (int) procFrames;
    return (int) std::ceil (procFrames * hostRate / kRate);
}

void PlaitsEngine::runBlocks (const Params& p)
{
    const size_t B = kBlock;
    auto lp = [] (float& state, float target, float k) { state += k * (target - state); };

    while (inQueue.size() - inQueueHead >= B)
    {
        // ---- gather this block's events ----
        uint8_t anyTrig = 0;
        const Frame* last = &inQueue[inQueueHead];
        for (size_t i = 0; i < B; ++i)
        {
            const Frame& f = inQueue[inQueueHead + i];
            anyTrig |= f.trig;
            last = &f;
        }
        const bool gateNow = last->gate != 0 || anyTrig != 0;   // a note shorter than one block still gets one block of gate
        const float note = last->note, vel = last->vel;
        inQueueHead += B;

        const float transposeNow = snapPitch (p.transpose, p.pitchSnap);   // Off: exactly p.transpose
        if (! smoothInit)
        {
            lpHarmonics = p.harmonics; lpTimbre = p.timbre; lpMorph = p.morph;
            lpTranspose = transposeNow; lpFm = p.fmAmount; lpTimbreAmt = p.timbreAmount; lpMorphAmt = p.morphAmount;
            lpDecay = p.decay; lpColour = p.lpgColour;
            pitchLp = note;
            smoothInit = true;
        }
        lp (lpHarmonics, p.harmonics, kLpHarmonics);  lp (lpTimbre, p.timbre, kLpTimbre);  lp (lpMorph, p.morph, kLpMorph);
        lp (lpTranspose, transposeNow, kLpTranspose);
        lp (lpFm, p.fmAmount, kLpAmount);  lp (lpTimbreAmt, p.timbreAmount, kLpAmount);  lp (lpMorphAmt, p.morphAmount, kLpAmount);
        lp (lpDecay, p.decay, kLpHidden);  lp (lpColour, p.lpgColour, kLpHidden);
        lp (pitchLp, note, kLpPitch);

        // ---- trigger: the voice needs a low block between two highs to see a new rising edge ----
        bool wantHigh = gateNow;
        if (retrig)
            retrig = false;                              // the one-block-low has happened; go high now
        else if (anyTrig && trigOut)
        {
            wantHigh = false;                            // retrigger while held: go low for one block first
            retrig = gateNow;
        }
        trigOut = wantHigh;

        plaits::Patch patch;
        patch.note = lpTranspose;                        // knob offset; the played note comes in through Modulations::note
        patch.harmonics = std::clamp (lpHarmonics, 0.0f, 1.0f);
        patch.timbre    = std::clamp (lpTimbre,    0.0f, 1.0f);
        patch.morph     = std::clamp (lpMorph,     0.0f, 1.0f);
        patch.frequency_modulation_amount = std::clamp (lpFm, -1.0f, 1.0f);
        patch.timbre_modulation_amount    = std::clamp (lpTimbreAmt, -1.0f, 1.0f);
        patch.morph_modulation_amount     = std::clamp (lpMorphAmt, -1.0f, 1.0f);
        patch.engine    = std::clamp (p.engine, 0, kNumEngines - 1);
        patch.decay     = std::clamp (lpDecay,  0.0f, 1.0f);
        patch.lpg_colour = std::clamp (lpColour, 0.0f, 1.0f);

        plaits::Modulations mod {};
        mod.engine = 0.0f; mod.note = pitchLp; mod.frequency = 0.0f;
        mod.harmonics = 0.0f; mod.timbre = 0.0f; mod.morph = 0.0f;
        mod.trigger = wantHigh ? 1.0f : 0.0f;
        mod.frequency_patched = false; mod.timbre_patched = false; mod.morph_patched = false;
        switch (p.envelope)
        {
            case EnvGate:
                mod.trigger_patched = true;  mod.level_patched = true;
                mod.level = wantHigh ? std::clamp (vel, 0.05f, 1.0f) : 0.0f;
                break;
            case EnvDrone:
                mod.trigger_patched = false; mod.level_patched = false; mod.level = 0.0f;
                break;
            default:   // EnvPing
                mod.trigger_patched = true;  mod.level_patched = false; mod.level = 0.0f;
                break;
        }

        plaits::Voice::Frame frames[plaits::kMaxBlockSize];
        voice->Render (patch, mod, frames, B);

        for (size_t i = 0; i < B; ++i)
        {
            float l = (float) frames[i].out * (1.0f / 32768.0f);
            float r = (float) frames[i].aux * (1.0f / 32768.0f);
            if (p.monoOut) { l = r = 0.7071f * (l + r); }
            procOutL.push_back (l);
            procOutR.push_back (r);
        }
    }
    if (inQueueHead > 4096)
    {
        inQueue.erase (inQueue.begin(), inQueue.begin() + (long) inQueueHead);
        inQueueHead = 0;
    }
}

void PlaitsEngine::process (float* outL, float* outR, int n, const Params& p, const Control& c)
{
    // 1. host-rate events -> module-rate (47.87 kHz) frames. A note-on is OR-ed over the host samples each 48 kHz frame covers,
    //    so it is never dropped when the host runs faster than 48 kHz.
    const double ratio = kRate / hostRate;
    for (int j = 0; j < n; ++j)
    {
        if (c.trigger && c.trigger[j]) pendTrig = 1;
        if (c.gate)     pendGate = c.gate[j];
        if (c.note)     pendNote = c.note[j];
        if (c.velocity) pendVel  = c.velocity[j];
        phase += ratio;
        while (phase >= 1.0)
        {
            phase -= 1.0;
            inQueue.push_back ({ pendGate, pendTrig, pendNote, pendVel });
            pendTrig = 0;
        }
    }

    // 2. run the module DSP
    runBlocks (p);

    // 3. module rate -> host rate
    rsOutL.process (procOutL.data(), procOutL.size(), hostQL);
    rsOutR.process (procOutR.data(), procOutR.size(), hostQR);
    procOutL.clear(); procOutR.clear();

    const size_t avail = std::min (hostQL.size(), hostQR.size()) - hostQHead;
    const size_t take = std::min (avail, (size_t) n);
    for (size_t i = 0; i < take; ++i) { outL[i] = hostQL[hostQHead + i]; outR[i] = hostQR[hostQHead + i]; }
    for (size_t i = take; i < (size_t) n; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }
    hostQHead += take;
    if (hostQHead > 8192)
    {
        hostQL.erase (hostQL.begin(), hostQL.begin() + (long) hostQHead);
        hostQR.erase (hostQR.begin(), hostQR.begin() + (long) hostQHead);
        hostQHead = 0;
    }
}
