// Drives the real PlaitsAudioProcessor (parameters -> engine, state) without a host or a window, for Pitch Snap:
//  - Pitch Snap defaults to Off, and a project saved before it existed loads with it Off
//  - Steps / Semitones / Off through the real "pitch_snap", "transpose" and "fine" parameters give the expected pitch
//  - the setting survives a state save and restore
// Run: build/plugin_test
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static void setParam (PlaitsAudioProcessor& p, const char* id, float plainValue)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}

// one held note for 3 s; returns the left channel
static std::vector<float> holdNote (PlaitsAudioProcessor& p, int note)
{
    const int block = 512, blocks = 281;
    std::vector<float> out;
    juce::AudioBuffer<float> buf (2, block);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
        buf.clear();
        p.processBlock (buf, midi);
        out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
    }
    return out;
}

static double peakHz (const std::vector<float>& x0, double sr, size_t from, size_t count, double lo, double hi)
{
    std::vector<float> x (count);
    for (size_t i = 0; i < count; ++i)
        x[i] = x0[from + i] * (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) count));
    double bestF = 0, bestM = -1;
    for (double f = lo; f < hi; f *= 1.0002)
    {
        const double w = 2.0 * 3.14159265358979323846 * f / sr, cr = std::cos (w), ci = std::sin (w);
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

static std::unique_ptr<PlaitsAudioProcessor> fresh()
{
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto* p = static_cast<PlaitsAudioProcessor*> (base.release());
    p->setPlayConfigDetails (0, 2, 48000.0, 512);
    p->prepareToPlay (48000.0, 512);
    return std::unique_ptr<PlaitsAudioProcessor> (p);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    int fails = 0;
    auto check = [&] (bool ok, const char* what) { if (! ok) { ++fails; std::printf ("FAIL: %s\n", what); } else std::printf ("ok:   %s\n", what); };

    {   // default
        auto p = fresh();
        check ((int) p->apvts.getRawParameterValue ("pitch_snap")->load() == 0, "Pitch Snap defaults to Off");
        check (p->apvts.getParameter ("pitch_snap")->getCurrentValueAsText() == "Off", "Pitch Snap menu text is Off");
    }

    {   // pitch through the real parameters: waveshaping engine (menu index 9), drone, note 57 = 220 Hz
        struct C { int snap; float transpose, fine; double offset; };
        const C cs[] = { { 0, 4, 0.0f, 4.0 }, { 0, 4, 0.6f, 4.6 }, { 1, 4, 0.0f, 7 }, { 1, 11, 0.9f, 12 }, { 1, -9, 0.0f, -7 },
                         { 2, 4, 0.6f, 5 }, { 2, 4, -0.6f, 3 }, { 2, -3, 0.2f, -3 } };
        for (const auto& c : cs)
        {
            auto p = fresh();
            setParam (*p, "engine", 9.0f); setParam (*p, "envelope", 2.0f); setParam (*p, "timbre", 0.5f); setParam (*p, "morph", 0.0f);
            setParam (*p, "pitch_snap", (float) c.snap); setParam (*p, "transpose", c.transpose); setParam (*p, "fine", c.fine);
            const auto a = holdNote (*p, 57);
            const double want = 440.0 * std::pow (2.0, (57.0 + c.offset - 69.0) / 12.0);
            const double got = peakHz (a, 48000.0, 96000, 32768, want / 1.5, want * 1.5);
            const double cents = 1200.0 * std::log2 (got / want);
            char msg[160]; std::snprintf (msg, sizeof msg, "processor: snap=%d transpose=%+.0f fine=%+.1f -> %+.1f st (want %.2f Hz, got %.2f Hz, %+.1f cents)",
                                          c.snap, c.transpose, c.fine, c.offset, want, got, cents);
            check (std::fabs (cents) < 5.0, msg);
        }
    }

    {   // state round trip, and a project saved before Pitch Snap existed
        auto p = fresh();
        setParam (*p, "pitch_snap", 1.0f); setParam (*p, "harmonics", 0.9f);
        juce::MemoryBlock state; p->getStateInformation (state);

        auto q = fresh();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check ((int) q->apvts.getRawParameterValue ("pitch_snap")->load() == 1, "Pitch Snap restored from state");

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        bool removed = false;
        if (xml != nullptr)
            for (auto* child = xml->getFirstChildElement(); child != nullptr; child = child->getNextElement())
                if (child->getStringAttribute ("id") == "pitch_snap") { xml->removeChildElement (child, true); removed = true; break; }
        check (removed, "test setup: found pitch_snap in the saved state");
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
        auto r = fresh();
        r->setStateInformation (old.getData(), (int) old.getSize());
        check ((int) r->apvts.getRawParameterValue ("pitch_snap")->load() == 0, "a state saved without Pitch Snap loads with it Off");
        check (std::fabs (r->apvts.getRawParameterValue ("harmonics")->load() - 0.9f) < 1e-3f, "...and its other settings still load");
    }

    std::printf("%d failures\n", fails);
    return fails != 0;
}
