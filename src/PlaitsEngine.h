// JUCE-free wrapper around the Mutable Instruments Plaits DSP (MIT licensed, Emilie Gillet).
#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include "Resampler.h"

namespace plaits { class Voice; }

class PlaitsEngine
{
public:
    // What the module's TRIG / LEVEL jacks are "patched" to. The module changes behaviour depending on what is plugged in.
    enum Envelope
    {
        EnvPing  = 0,   // trigger patched, LEVEL not patched: every note-on pings the low-pass gate, DECAY sets how long it rings
        EnvGate  = 1,   // trigger + LEVEL patched: the low-pass gate follows MIDI gate x velocity (notes sustain until released)
        EnvDrone = 2    // nothing patched: the voice just sounds continuously (MIDI sets pitch only)
    };

    // Pitch Snap (our addition, not in the module): what the whole Transpose + Fine Tune offset settles on.
    //   Steps     - octaves and fifths across the +-48 range: 0, 7, 12, 19, 24, 31, 36, 43, 48 (both directions)
    //   Semitones - whole semitones
    enum PitchSnap { SnapOff = 0, SnapSteps = 1, SnapSemitones = 2 };
    static float snapPitch (float semitones, int mode);   // Off returns its input unchanged

    struct Params
    {
        int   engine = 8;                 // 0..23, the firmware's engine order (see the README)
        float harmonics = 0.5f, timbre = 0.5f, morph = 0.5f;           // the three main knobs, 0..1
        float transpose = 0.0f;           // semitones (Transpose + Fine Tune), added to the played MIDI note
        int   pitchSnap = SnapOff;        // what that offset settles on (see snapPitch)
        float fmAmount = 0.0f, timbreAmount = 0.0f, morphAmount = 0.0f; // the three attenuverters, -1..1
        float decay = 0.5f, lpgColour = 0.5f;                           // the module's two hidden parameters, 0..1
        int   envelope = EnvPing;
        bool  monoOut = false;            // sum OUT and AUX into both channels
    };

    // Per-host-sample MIDI-derived control (all pointers required, length = numSamples).
    //   trigger[i] != 0 : a note-on happened at sample i
    //   gate[i]  != 0   : at least one note is held at sample i
    //   note[i]         : pitch in MIDI note numbers (float, includes pitch bend)
    //   velocity[i]     : 0..1, velocity of the most recent note-on
    struct Control { const uint8_t* trigger = nullptr; const uint8_t* gate = nullptr;
                     const float* note = nullptr; const float* velocity = nullptr; };

    PlaitsEngine();
    ~PlaitsEngine();

    void prepare (double hostSampleRate);

    // Renders OUT to outL and AUX to outR (or the mono mix of both, if Params::monoOut).
    void process (float* outL, float* outR, int numSamples, const Params& p, const Control& c);

    int latencyInHostSamples() const;

    // The module's real sample rate, not a round 48000: the firmware derives its pitch tables from 47872.34 Hz
    // (plaits/dsp/dsp.h: kCorrectedSampleRate, the I2S clock divider's actual output). Resampling from this rate
    // to the host rate keeps A4 at 440 Hz instead of 4.6 cents sharp.
    static constexpr double kRate = 47872.34;
    static constexpr size_t kBlock = 12;       // the firmware renders 12-sample blocks (plaits::kBlockSize)
    static constexpr int kNumEngines = 24;

private:
    struct Frame { uint8_t gate; uint8_t trig; float note; float vel; };

    void runBlocks (const Params& p);

    // The firmware keeps its Voice in a global, i.e. zeroed memory, and several engines rely on that: some members
    // (e.g. the 2-op FM engine's downsampler memory) are never explicitly initialised. A plain `new` hands back whatever
    // junk the heap held, which can inject a NaN into the low-pass gate and silence every engine that uses it. So the
    // Voice is always constructed in zero-filled memory.
    struct VoiceDeleter { void operator() (plaits::Voice*) const; };
    static plaits::Voice* makeZeroedVoice();
    std::unique_ptr<plaits::Voice, VoiceDeleter> voice;
    std::vector<char> sharedRam;      // the firmware's shared engine RAM

    double hostRate = 48000.0;
    double phase = 0.0;                // fractional 48 kHz frames owed
    uint8_t pendTrig = 0, pendGate = 0;
    float pendNote = 60.0f, pendVel = 0.8f;

    // firmware-style control smoothing (per 12-sample block)
    float lpHarmonics = 0.5f, lpTimbre = 0.5f, lpMorph = 0.5f;
    float lpTranspose = 0.0f, lpFm = 0.0f, lpTimbreAmt = 0.0f, lpMorphAmt = 0.0f;
    float lpDecay = 0.5f, lpColour = 0.5f;
    float pitchLp = 60.0f;
    bool  smoothInit = false;
    bool  trigOut = false, retrig = false;
    bool  voiceStarted = false;

    SincResampler rsOutL, rsOutR;
    std::vector<Frame> inQueue;
    size_t inQueueHead = 0;
    std::vector<float> procOutL, procOutR;
    std::vector<float> hostQL, hostQR;
    size_t hostQHead = 0;
};
