// Pitch Snap (our addition): Transpose + Fine Tune settle on octaves and fifths (Steps) or whole semitones (Semitones).
//  1. the snapping function: exact over the whole +-48 range; Off returns its input unchanged
//  2. Off is the old sound: a setting already on the grid renders bit-for-bit like Off, and a setting off the grid does not
//  3. through the engine: the sounding pitch follows the snapped value (measured on a drone, within 5 cents)
//  4. every engine stays finite and bounded in all three modes while the offset sweeps its whole range
// It says nothing about how anything sounds.
#include "PlaitsEngine.h"
#include "stmlib/utils/random.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>
#include <functional>

using E = PlaitsEngine;

// Renders `seconds` of one held note (interleaved L,R). `params` is called before every block, so a test can sweep.
static std::vector<float> render (double sr, double seconds, float note, const std::function<E::Params (int block)>& params)
{
    stmlib::Random::Seed (1);       // the firmware's noise source is a global: same start every time, so runs can be compared bit for bit
    E e; e.prepare (sr);
    const int N = 512;
    std::vector<float> oL (N), oR (N), nt (N, note), vel (N, 0.8f), all;
    std::vector<uint8_t> trig (N, 0), gate (N, 1);
    const int blocks = (int) (sr * seconds / N);
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (trig.begin(), trig.end(), (uint8_t) 0);
        if (b == 0) trig[0] = 1;
        E::Control c; c.trigger = trig.data(); c.gate = gate.data(); c.note = nt.data(); c.velocity = vel.data();
        e.process (oL.data(), oR.data(), N, params (b), c);
        for (int i = 0; i < N; ++i) { all.push_back (oL[(size_t) i]); all.push_back (oR[(size_t) i]); }
    }
    return all;
}

static double peakHz (const std::vector<float>& x2, double sr, size_t from, size_t count, double lo, double hi)
{
    std::vector<float> x (count);
    for (size_t i = 0; i < count; ++i)
        x[i] = x2[2 * (from + i)] * (float) (0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) count));
    double bestF = 0, bestM = -1;
    for (double f = lo; f < hi; f *= 1.0002)
    {
        const double w = 2.0 * M_PI * f / sr, cr = std::cos (w), ci = std::sin (w);
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

static E::Params drone (int engine, float transpose, int snap)
{
    E::Params p; p.engine = engine; p.envelope = E::EnvDrone; p.timbre = 0.5f; p.morph = 0.0f;
    p.transpose = transpose; p.pitchSnap = snap;
    return p;
}

int main()
{
    int fails = 0;

    // 1. the function
    {
        static const float steps[] = { 0, 7, 12, 19, 24, 31, 36, 43, 48 };
        bool ok = true; float prev = -100.f; int distinct = 0; float last = 1e9f;
        for (int i = -4900; i <= 4900; ++i)          // a little past +-48: Transpose (+-48) plus Fine (+-1) reaches 49
        {
            const float k = (float) i * 0.01f;
            const float st = E::snapPitch (k, E::SnapSteps);
            const float off = E::snapPitch (k, E::SnapOff);
            float bestDist = 1e9f; for (float s : steps) bestDist = std::min (bestDist, std::fabs (std::fabs (k) - s));
            bool onStep = false; for (float s : steps) onStep = onStep || std::fabs (std::fabs (st) - s) < 1e-6f;
            ok = ok && onStep && st >= prev - 1e-6f && std::fabs (std::fabs (st) - std::fabs (k)) <= bestDist + 1e-4f
                    && (st == 0.f || (st < 0.f) == (k < 0.f))
                    && std::fabs (E::snapPitch (k, E::SnapSemitones) - std::round (k)) < 1e-6f
                    && std::memcmp (&k, &off, sizeof (float)) == 0;
            prev = st; if (st != last) { ++distinct; last = st; }
        }
        ok = ok && distinct == 17;
        printf ("snap function over -49..+49: Steps visit exactly 17 values (%d seen), the nearest one, never backwards; Semitones round; Off returns the same bits  %s\n",
                distinct, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // 2. Off is the old sound
    {
        bool ok = true;
        for (int engine : { 0, 5, 9, 14, 21 })
            for (int env : { (int) E::EnvPing, (int) E::EnvDrone })
            {
                if (engine == 21 && env == E::EnvDrone) continue;   // the bass drum is silent without a trigger, so pitch cannot show
                auto make = [&] (float t, int snap) { return [=] (int) { E::Params p = drone (engine, t, snap); p.envelope = env; return p; }; };
                const auto off12  = render (48000.0, 1.0, 57.0f, make (12.0f, E::SnapOff));
                const auto st12   = render (48000.0, 1.0, 57.0f, make (12.0f, E::SnapSteps));      // 12 is a step
                const auto sm12   = render (48000.0, 1.0, 57.0f, make (12.0f, E::SnapSemitones));  // 12 is a whole semitone
                const auto off34  = render (48000.0, 1.0, 57.0f, make (3.4f,  E::SnapOff));
                const auto off34b = render (48000.0, 1.0, 57.0f, make (3.4f,  E::SnapOff));
                const auto st34   = render (48000.0, 1.0, 57.0f, make (3.4f,  E::SnapSteps));      // snaps to 0
                const auto sm34   = render (48000.0, 1.0, 57.0f, make (3.4f,  E::SnapSemitones));  // snaps to 3
                const bool same  = off12 == st12 && off12 == sm12 && off34 == off34b;
                const bool moved = off34 != st34 && off34 != sm34;
                printf ("Off == on-grid Steps/Semitones bit for bit, off-grid differs: engine %2d env %d  %s\n", engine, env, same && moved ? "OK" : "FAIL");
                ok = ok && same && moved;
            }
        fails += ! ok;
    }

    // 3. through the engine: the sounding pitch (engine 9, drone, MIDI note 57 = 220 Hz)
    {
        struct C { float knob; int snap; double offset; };
        const C cs[] = {
            { 11.8f, E::SnapSteps, 12 }, { 4.0f, E::SnapSteps, 7 },  { 3.0f, E::SnapSteps, 0 },  { -9.0f, E::SnapSteps, -7 },
            { -10.0f, E::SnapSteps, -12 }, { 21.0f, E::SnapSteps, 19 }, { 30.0f, E::SnapSteps, 31 }, { 35.0f, E::SnapSteps, 36 },
            { 4.4f, E::SnapSemitones, 4 }, { 4.6f, E::SnapSemitones, 5 }, { -2.6f, E::SnapSemitones, -3 },
            { 4.4f, E::SnapOff, 4.4 }, { 7.0f, E::SnapOff, 7.0 } };
        bool ok = true; double worst = 0;
        for (double sr : { 44100.0, 48000.0 })
            for (const auto& c : cs)
            {
                const auto a = render (sr, 3.0, 57.0f, [&] (int) { return drone (9, c.knob, c.snap); });
                const double want = 440.0 * std::pow (2.0, (57.0 + c.offset - 69.0) / 12.0);
                const double got = peakHz (a, sr, (size_t) (sr * 2.0), 32768, want / 1.5, want * 1.5);
                const double cents = 1200.0 * std::log2 (got / want);
                worst = std::max (worst, std::fabs (cents));
                if (std::fabs (cents) >= 5.0) { ok = false; printf ("  knob %+.1f mode %d sr %.0f: want %.2f Hz got %.2f Hz (%+.1f cents)\n", c.knob, c.snap, sr, want, got, cents); }
            }
        printf ("pitch snap through the engine: %d cases x 2 sample rates land on the snapped note; worst error %.2f cents  %s\n",
                (int) (sizeof (cs) / sizeof (cs[0])), worst, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // 4. every engine, every mode, offset sweeping -49..+49 (a harsh case: the value jumps between steps)
    {
        bool ok = true;
        for (int engine = 0; engine < E::kNumEngines; ++engine)
            for (int snap : { (int) E::SnapOff, (int) E::SnapSteps, (int) E::SnapSemitones })
            {
                const auto a = render (48000.0, 2.0, 48.0f, [=] (int b)
                {
                    E::Params p; p.engine = engine; p.envelope = E::EnvDrone; p.pitchSnap = snap;
                    p.transpose = 49.0f * std::sin ((float) b * 0.13f);
                    return p;
                });
                bool fin = true; double e = 0, pk = 0;
                for (float v : a) { fin = fin && std::isfinite (v); e += (double) v * v; pk = std::max (pk, (double) std::fabs (v)); }
                const bool good = fin && e > 0.05 && pk <= 1.6;
                if (! good) { ok = false; printf ("  engine %d mode %d: finite=%d energy=%.2f peak=%.2f\n", engine, snap, (int) fin, e, pk); }
            }
        printf ("all 24 engines x 3 modes stay finite, audible and bounded while the offset sweeps  %s\n", ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    printf ("%d failed\n", fails);
    return fails;
}
