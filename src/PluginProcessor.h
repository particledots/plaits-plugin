#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PlaitsEngine.h"

class PlaitsAudioProcessor : public juce::AudioProcessor
{
public:
    PlaitsAudioProcessor();
    ~PlaitsAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    static const juce::StringArray& engineNames();
    static const juce::StringArray& envelopeNames();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    PlaitsEngine::Params readParams() const;

    PlaitsEngine engine;
    std::vector<uint8_t> triggerFlags, gateFlags;
    std::vector<float> noteValues, velValues, scratchL, scratchR;

    // monophonic note handling: last-note priority, legato when notes overlap
    std::vector<int> held;
    float currentNote = 60.0f, currentVel = 0.8f, bend = 0.0f;
    bool gate = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaitsAudioProcessor)
};
