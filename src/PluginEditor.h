#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Simple flat look for rotary knobs.
class PlaitsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PlaitsLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

class PlaitsEditor : public juce::AudioProcessorEditor
{
public:
    explicit PlaitsEditor (PlaitsAudioProcessor&);
    ~PlaitsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAtt> att;
    };

    void addKnob (Knob&, const juce::String& paramId, const juce::String& name);
    void addChoice (Choice&, const juce::String& paramId, const juce::String& name, const juce::StringArray& items);

    PlaitsAudioProcessor& proc;
    PlaitsLookAndFeel laf;

    Knob harmonics, timbre, morph, fmAmount, timbreAmount, morphAmount, transpose, fine, decay, lpgColour;
    Choice engine, envelope, pitchSnap;
    std::unique_ptr<juce::ParameterAttachment> snapReadoutAtt, fineReadoutAtt;   // keep the Transpose readout in step

    juce::ToggleButton monoOut { "MONO OUT" };
    std::unique_ptr<ButtonAtt> monoOutAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaitsEditor)
};
