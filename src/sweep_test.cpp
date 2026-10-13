// Regression test for the "2-op FM silences the plug-in" bug: switches engines while notes play (the way scrolling the Engine
// menu in a DAW does) and reports any segment that is non-finite, silent or stuck at DC.
// Fresh heap blocks can be filled with a junk byte (JUNK=0xNN, optionally only for sizes JUNK_MIN..JUNK_MAX) to expose
// members the DSP never initialises; the firmware gets zeroed memory for free (a global Voice), a plug-in has to ask for it.
// SWEEP_START=n starts the sweep at engine n.
#include "PlaitsEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <new>
#include <vector>

static int g_junk = -1; static size_t g_min = 0, g_max = (size_t) -1;
void* operator new (size_t n) { void* p = malloc (n); if (! p) throw std::bad_alloc(); if (g_junk >= 0 && n >= g_min && n <= g_max) memset (p, g_junk, n); return p; }
void operator delete (void* p) noexcept { free (p); }
void operator delete (void* p, size_t) noexcept { free (p); }

int main (int argc, char** argv)
{
    if (const char* j = getenv ("JUNK")) g_junk = (int) strtol (j, nullptr, 0);
    if (const char* a = getenv ("JUNK_MIN")) g_min = (size_t) strtol (a, nullptr, 0);
    if (const char* b = getenv ("JUNK_MAX")) g_max = (size_t) strtol (b, nullptr, 0);
    const double sr = argc > 1 ? atof (argv[1]) : 48000.0;
    int bad = 0;
    for (int envMode = 0; envMode < 3; ++envMode)
    {
        PlaitsEngine e; e.prepare (sr);
        PlaitsEngine::Params p; p.envelope = envMode;
        const int N = 512;
        std::vector<float> L (N), R (N), note (N, 57.f), vel (N, 0.8f);
        std::vector<uint8_t> trig (N, 0), gate (N, 0);
        long count = 0;
        const long segLen = (long) (sr * 0.6), noteEvery = (long) (sr * 0.2), hold = (long) (sr * 0.12);
        // the sweep the user did: down through all 24, then back up, then bounce around the FM engine
        const int start = getenv ("SWEEP_START") ? atoi (getenv ("SWEEP_START")) : 0;
        std::vector<int> order; for (int i = start; i < 24; ++i) order.push_back (i); for (int i = 22; i >= start; --i) order.push_back (i);
        for (int k = 0; k < 6; ++k) { order.push_back (9); order.push_back (10); order.push_back (11); order.push_back (10); }
        for (size_t s = 0; s < order.size(); ++s)
        {
            p.engine = order[s];
            double energy = 0, sum = 0; long cnt = 0; bool nonfinite = false; long segStart = count;
            while (count - segStart < segLen)
            {
                for (int i = 0; i < N; ++i, ++count) { trig[i] = (count % noteEvery) == 0; gate[i] = (count % noteEvery) < hold; }
                PlaitsEngine::Control c; c.trigger = trig.data(); c.gate = gate.data(); c.note = note.data(); c.velocity = vel.data();
                e.process (L.data(), R.data(), N, p, c);
                if (count - segStart > segLen / 4)   // let the switch settle, then judge
                    for (int i = 0; i < N; ++i) { if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) nonfinite = true; { energy += (double) L[i] * L[i] + (double) R[i] * R[i]; sum += L[i]; ++cnt; } }
            }
            const double mean = cnt ? sum / (double) cnt : 0.0, var = cnt ? energy / (2.0 * (double) cnt) - mean * mean : 0.0;   // var ~ 0: silent or stuck at DC
            if (nonfinite || energy < 0.001 || var < 1e-7) { ++bad; printf ("env=%d step=%2zu engine=%2d  %s  energy=%.4f dc=%.3f\n", envMode, s, order[s], nonfinite ? "NON-FINITE" : (energy < 0.001 ? "SILENT" : "STUCK-DC"), energy, mean); }
        }
    }
    printf ("sr=%.0f junk=%d : %d bad segments\n", sr, g_junk, bad);
    return bad != 0;
}
