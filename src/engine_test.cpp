// Offline sanity test of PlaitsEngine: every engine x envelope mode at several host sample rates, plus a few
// behaviour checks (retrigger, pitch mapping). It checks only that output is finite, bounded and not silent,
// and that pitch lands where the MIDI note says. It says nothing about how anything sounds.
#include "PlaitsEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

struct Result { bool ok; double energy, peak; };

struct Rig
{
    explicit Rig (double sr_) : sr (sr_) { e.prepare (sr); }

    // Renders `seconds` of audio. Notes: a note-on every `noteEvery` seconds, held for `hold` seconds, pitch cycling.
    std::vector<float> render (PlaitsEngine::Params p, double seconds, double noteEvery, double hold,
                               float baseNote, bool legato = false)
    {
        const int N = 512;
        std::vector<float> outL (N), outR (N), note (N, baseNote), vel (N, 0.8f);
        std::vector<uint8_t> trig (N, 0), gate (N, 0);
        std::vector<float> all;
        const long noteEverySamples = (long) (sr * noteEvery), holdSamples = (long) (sr * hold);
        const int blocks = (int) (sr * seconds / N);
        long count = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < N; ++i, ++count)
            {
                const long k = count / noteEverySamples, ph = count % noteEverySamples;
                trig[(size_t) i] = ph == 0 ? 1 : 0;                       // exactly ONE host sample wide
                gate[(size_t) i] = (legato || ph < holdSamples) ? 1 : 0;
                note[(size_t) i] = baseNote + (float) ((k % 4) * 3);
            }
            PlaitsEngine::Control c; c.trigger = trig.data(); c.gate = gate.data(); c.note = note.data(); c.velocity = vel.data();
            e.process (outL.data(), outR.data(), N, p, c);
            for (int i = 0; i < N; ++i) { all.push_back (outL[(size_t) i]); all.push_back (outR[(size_t) i]); }
        }
        return all;
    }

    PlaitsEngine e;
    double sr;
};

// The firmware clips its output to 16 bits (|x| <= 1.0), but the plugin resamples from the module's 47.87 kHz to the host
// rate, and a windowed-sinc resampler overshoots a clipped waveform a little (Gibbs ringing). So the limit here is looser.
static Result analyse (const std::vector<float>& a, double /*sr*/)
{
    const double peakLimit = 1.6;
    bool bad = false; double peak = 0, energy = 0;
    for (float v : a)
    {
        if (! std::isfinite (v)) bad = true;
        peak = std::max (peak, (double) std::fabs (v));
        energy += (double) v * v;
    }
    return { ! bad && energy > 0.05 && peak <= peakLimit, energy, peak };
}

// Strongest spectral peak (Hann-windowed DFT scanned in fine steps) of the left channel between lo and hi Hz.
static double peakHz (const std::vector<float>& interleaved, double sr, size_t from, size_t count, double lo, double hi)
{
    std::vector<float> x (count);
    for (size_t i = 0; i < count; ++i)
        x[i] = interleaved[2 * (from + i)] * (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) count));
    double bestF = 0, bestM = -1;
    for (double f = lo; f < hi; f *= 1.0002)
    {
        const double w = 2.0 * 3.14159265358979323846 * f / sr;
        const double cr = std::cos (w), ci = std::sin (w);
        double zr = 1.0, zi = 0.0, re = 0.0, im = 0.0;
        for (size_t i = 0; i < count; ++i)
        {
            re += x[i] * zr; im += x[i] * zi;
            const double nr = zr * cr - zi * ci; zi = zr * ci + zi * cr; zr = nr;
        }
        const double m = re * re + im * im;
        if (m > bestM) { bestM = m; bestF = f; }
    }
    return bestF;
}

int main()
{
    int fails = 0, runs = 0;
    auto report = [&] (const char* what, double sr, const Result& r)
    {
        ++runs; fails += ! r.ok;
        printf ("%-46s sr=%6.0f energy=%10.2f peak=%5.2f  %s\n", what, sr, r.energy, r.peak, r.ok ? "OK" : "FAIL");
    };
    static const char* envNames[] = { "ping", "gate", "drone" };

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // every engine x every envelope mode, MIDI notes every half second held 0.3 s
        for (int engine = 0; engine < PlaitsEngine::kNumEngines; ++engine)
            for (int env = 0; env < 3; ++env)
            {
                PlaitsEngine::Params p; p.engine = engine; p.envelope = env;
                Rig rig (sr);
                char name[96]; snprintf (name, sizeof name, "engine=%2d env=%-5s", engine, envNames[env]);
                report (name, sr, analyse (rig.render (p, 4.0, 0.5, 0.3, 48.0f), sr));
            }
        // knob extremes, on a handful of engines
        for (float v : { 0.0f, 1.0f })
            for (int engine : { 0, 5, 8, 11, 14, 15, 19, 20 })
            {
                PlaitsEngine::Params p; p.engine = engine; p.harmonics = v; p.timbre = v; p.morph = v;
                p.fmAmount = p.timbreAmount = p.morphAmount = v * 2 - 1; p.decay = v; p.lpgColour = v; p.transpose = 48.f * (v * 2 - 1);
                Rig rig (sr);
                char name[96]; snprintf (name, sizeof name, "engine=%2d all knobs at %.0f (+extreme amounts)", engine, v);
                report (name, sr, analyse (rig.render (p, 4.0, 0.5, 0.3, 48.0f), sr));
            }
        // mono out
        { PlaitsEngine::Params p; p.monoOut = true; Rig rig (sr); report ("mono out", sr, analyse (rig.render (p, 3.0, 0.5, 0.3, 48.0f), sr)); }
        // legato (gate never drops, notes keep retriggering)
        { PlaitsEngine::Params p; p.envelope = PlaitsEngine::EnvGate; Rig rig (sr);
          report ("legato retrigger, gate mode", sr, analyse (rig.render (p, 3.0, 0.25, 0.25, 48.0f, true), sr)); }
        // a note shorter than one 12-sample block must still sound
        { PlaitsEngine::Params p; p.envelope = PlaitsEngine::EnvPing; Rig rig (sr);
          report ("very short gate (4 host samples)", sr, analyse (rig.render (p, 3.0, 0.5, 4.0 / sr, 48.0f), sr)); }
    }

    // pitch mapping: waveshaping engine (index 9), drone mode, one held note. The sounding fundamental must land on the
    // note's frequency (within 5 cents), also with Transpose, and at host rates other than the module's own.
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (float midi : { 45.0f, 57.0f, 69.0f })
            for (float transpose : { 0.0f, 12.0f })
            {
                PlaitsEngine::Params p; p.engine = 9; p.envelope = PlaitsEngine::EnvDrone; p.timbre = 0.5f; p.morph = 0.0f;
                p.transpose = transpose;
                Rig rig (sr);
                auto a = rig.render (p, 3.0, 100.0, 100.0, midi, true);   // one note-on at t=0, never released
                const double want = 440.0 * std::pow (2.0, ((double) midi + transpose - 69.0) / 12.0);
                const double got = peakHz (a, sr, (size_t) (sr * 2.0), 32768, want / 1.5, want * 1.5);
                const double cents = 1200.0 * std::log2 (got / want);
                const bool ok = std::fabs (cents) < 5.0;
                ++runs; fails += ! ok;
                printf ("pitch check midi=%2.0f transpose=%+3.0f  want %7.2f Hz  got %7.2f Hz (%+5.1f cents)  sr=%.0f  %s\n",
                        midi, transpose, want, got, cents, sr, ok ? "OK" : "FAIL");
            }

    printf ("%d runs, %d failed\n", runs, fails);
    return fails;
}
