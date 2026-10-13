#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using API = juce::AudioParameterInt;

// The 24 engines in the firmware's own order (plaits/dsp/voice.cc, Voice::Init): three banks of eight.
// The names are my reading of that order plus the module's published engine list, not something I could listen to.
const juce::StringArray& PlaitsAudioProcessor::engineNames()
{
    static const juce::StringArray n {
        // bank 1 (the newer engines)
        "1.1 Virtual analog with VCF", "1.2 Phase distortion", "1.3 Six-op FM (bank A)", "1.4 Six-op FM (bank B)",
        "1.5 Six-op FM (bank C)", "1.6 Wave terrain", "1.7 String machine", "1.8 Chiptune",
        // bank 2 (the original engines)
        "2.1 Pair of classic waveforms", "2.2 Waveshaping oscillator", "2.3 Two-operator FM", "2.4 Granular formant oscillator",
        "2.5 Harmonic oscillator", "2.6 Wavetable oscillator", "2.7 Chords", "2.8 Speech",
        // bank 3 (noise, physical models, drums)
        "3.1 Swarm", "3.2 Filtered noise", "3.3 Particle noise", "3.4 Inharmonic string",
        "3.5 Modal resonator", "3.6 Analog bass drum", "3.7 Analog snare drum", "3.8 Analog hi-hat" };
    return n;
}

const juce::StringArray& PlaitsAudioProcessor::envelopeNames()
{
    static const juce::StringArray n { "Ping (decay knob)", "Gate (velocity)", "Drone (no trigger)" };
    return n;
}

juce::AudioProcessorValueTreeState::ParameterLayout PlaitsAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto unit = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.f, 1.f, 0.0001f), def,
                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); }));
    };
    auto bipolar = [] (const char* id, const char* name)
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (-1.f, 1.f, 0.0001f), 0.f,
                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); }));
    };

    p.push_back (std::make_unique<APC> (juce::ParameterID { "engine", 1 }, "Engine", engineNames(), 8));
    p.push_back (unit ("harmonics", "Harmonics", 0.5f));
    p.push_back (unit ("timbre",    "Timbre",    0.5f));
    p.push_back (unit ("morph",     "Morph",     0.5f));
    p.push_back (std::make_unique<API> (juce::ParameterID { "transpose", 1 }, "Transpose", -48, 48, 0,
                 juce::AudioParameterIntAttributes().withLabel ("st")));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "fine", 1 }, "Fine Tune",
                 juce::NormalisableRange<float> (-1.f, 1.f, 0.001f), 0.f,
                 juce::AudioParameterFloatAttributes().withLabel ("st")));
    p.push_back (bipolar ("fm_amount",     "FM Amount"));
    p.push_back (bipolar ("timbre_amount", "Timbre Amount"));
    p.push_back (bipolar ("morph_amount",  "Morph Amount"));
    p.push_back (unit ("decay",      "Decay",      0.5f));
    p.push_back (unit ("lpg_colour", "LPG Colour", 0.5f));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "envelope", 1 }, "Envelope", envelopeNames(), 0));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "mono_out", 1 }, "Mono Out", false));
    return { p.begin(), p.end() };
}

PlaitsAudioProcessor::PlaitsAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool PlaitsAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PlaitsAudioProcessor::prepareToPlay (double sr, int maxBlock)
{
    engine.prepare (sr);
    triggerFlags.assign ((size_t) maxBlock, 0);
    gateFlags.assign ((size_t) maxBlock, 0);
    noteValues.assign ((size_t) maxBlock, currentNote);
    velValues.assign ((size_t) maxBlock, currentVel);
    scratchL.assign ((size_t) maxBlock, 0.f);
    scratchR.assign ((size_t) maxBlock, 0.f);
    held.clear(); gate = false; bend = 0.0f;
    setLatencySamples (engine.latencyInHostSamples());
}

PlaitsEngine::Params PlaitsAudioProcessor::readParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    PlaitsEngine::Params p;
    p.engine = (int) v ("engine");
    p.harmonics = v ("harmonics"); p.timbre = v ("timbre"); p.morph = v ("morph");
    p.transpose = (float) std::lround (v ("transpose")) + v ("fine");
    p.fmAmount = v ("fm_amount"); p.timbreAmount = v ("timbre_amount"); p.morphAmount = v ("morph_amount");
    p.decay = v ("decay"); p.lpgColour = v ("lpg_colour");
    p.envelope = (int) v ("envelope");
    p.monoOut = v ("mono_out") > 0.5f;
    return p;
}

void PlaitsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();

    if ((size_t) n > scratchL.size())
    {   // host exceeded its promised max block size
        scratchL.resize ((size_t) n); scratchR.resize ((size_t) n);
        triggerFlags.resize ((size_t) n); gateFlags.resize ((size_t) n);
        noteValues.resize ((size_t) n); velValues.resize ((size_t) n);
    }

    const auto params = readParams();

    // MIDI -> monophonic voice: last-note priority. A note-on triggers; releasing the sounding note while another is
    // still held goes back to that one without retriggering. Pitch bend is +-2 semitones (my addition: the module has none).
    std::fill (triggerFlags.begin(), triggerFlags.begin() + n, (uint8_t) 0);
    {
        int pos = 0;
        auto fillTo = [&] (int upTo)
        {
            for (; pos < upTo; ++pos)
            {
                gateFlags[(size_t) pos] = gate ? 1 : 0;
                noteValues[(size_t) pos] = currentNote + bend;
                velValues[(size_t) pos] = currentVel;
            }
        };
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const int at = juce::jlimit (0, n, meta.samplePosition);
            fillTo (at);
            if (msg.isNoteOn())
            {
                const int note = msg.getNoteNumber();
                held.erase (std::remove (held.begin(), held.end(), note), held.end());
                held.push_back (note);
                currentNote = (float) note;
                currentVel = msg.getFloatVelocity();
                gate = true;
                if (at < n) triggerFlags[(size_t) at] = 1;
            }
            else if (msg.isNoteOff())
            {
                held.erase (std::remove (held.begin(), held.end(), msg.getNoteNumber()), held.end());
                if (held.empty()) gate = false;
                else currentNote = (float) held.back();
            }
            else if (msg.isPitchWheel())
                bend = 2.0f * ((float) (msg.getPitchWheelValue() - 8192) / 8192.0f);
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                held.clear(); gate = false;
            }
        }
        fillTo (n);
    }
    midi.clear();

    PlaitsEngine::Control ctl;
    ctl.trigger = triggerFlags.data(); ctl.gate = gateFlags.data(); ctl.note = noteValues.data(); ctl.velocity = velValues.data();
    engine.process (scratchL.data(), scratchR.data(), n, params, ctl);

    if (chans > 1)
    {
        buffer.copyFrom (0, 0, scratchL.data(), n);
        buffer.copyFrom (1, 0, scratchR.data(), n);
    }
    else if (chans == 1)
    {
        float* o = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i) o[i] = 0.7071f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
    }
}

juce::AudioProcessorEditor* PlaitsAudioProcessor::createEditor()
{
    return new PlaitsEditor (*this);
}

void PlaitsAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PlaitsAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PlaitsAudioProcessor(); }
