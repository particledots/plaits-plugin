#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg      (0xff1a1c20);
    const juce::Colour kPanel   (0xff23262c);
    const juce::Colour kTrack   (0xff3a3e47);
    const juce::Colour kAccent  (0xffe0a458);
    const juce::Colour kText    (0xffe8e6e1);
    const juce::Colour kDim     (0xff8d909a);
}

PlaitsLookAndFeel::PlaitsLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kDim);
    setColour (juce::ComboBox::backgroundColourId, kPanel);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::outlineColourId, kTrack);
    setColour (juce::ComboBox::arrowColourId, kAccent);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
}

void PlaitsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
    const float thick = juce::jmax (3.0f, d * 0.09f);
    const auto arcR = r.reduced (thick * 0.5f);

    juce::Path track;
    track.addCentredArc (arcR.getCentreX(), arcR.getCentreY(), arcR.getWidth() * 0.5f, arcR.getHeight() * 0.5f, 0, a0, a1, true);
    g.setColour (kTrack);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float angle = a0 + pos * (a1 - a0);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;   // bipolar controls fill from the centre
    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    juce::Path val;
    val.addCentredArc (arcR.getCentreX(), arcR.getCentreY(), arcR.getWidth() * 0.5f, arcR.getHeight() * 0.5f, 0,
                       juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.isEnabled() ? kAccent : kDim);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto c = r.getCentre();
    const float len = d * 0.5f - thick * 2.2f;
    juce::Line<float> line (c.getPointOnCircumference (len * 0.35f, angle),
                            c.getPointOnCircumference (len, angle));
    g.setColour (kText);
    g.drawLine (line, juce::jmax (2.0f, d * 0.045f));
}

void PlaitsLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? kAccent : kPanel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? kAccent : kTrack);
    g.drawRoundedRectangle (r, 6.0f, 1.2f);
    g.setColour (on ? juce::Colours::black : kText);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

PlaitsEditor::PlaitsEditor (PlaitsAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    addKnob (harmonics,    "harmonics",     "HARMONICS");
    addKnob (timbre,       "timbre",        "TIMBRE");
    addKnob (morph,        "morph",         "MORPH");
    addKnob (fmAmount,     "fm_amount",     "FM AMOUNT");
    addKnob (timbreAmount, "timbre_amount", "TIMBRE AMOUNT");
    addKnob (morphAmount,  "morph_amount",  "MORPH AMOUNT");
    addKnob (transpose,    "transpose",     "TRANSPOSE");
    addKnob (fine,         "fine",          "FINE TUNE");
    addKnob (decay,        "decay",         "DECAY");
    addKnob (lpgColour,    "lpg_colour",    "LPG COLOUR");
    transpose.slider.setNumDecimalPlacesToDisplay (0);
    fine.slider.setNumDecimalPlacesToDisplay (2);

    addChoice (engine,   "engine",   "ENGINE",   PlaitsAudioProcessor::engineNames());
    addChoice (envelope, "envelope", "ENVELOPE", PlaitsAudioProcessor::envelopeNames());

    addAndMakeVisible (monoOut);
    monoOutAtt = std::make_unique<ButtonAtt> (proc.apvts, "mono_out", monoOut);

    setSize (640, 560);
}

PlaitsEditor::~PlaitsEditor() { setLookAndFeel (nullptr); }

void PlaitsEditor::addKnob (Knob& k, const juce::String& id, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    k.slider.setPopupDisplayEnabled (false, false, nullptr);
    k.slider.setNumDecimalPlacesToDisplay (2);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, kText);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.slider);
}

void PlaitsEditor::addChoice (Choice& c, const juce::String& id, const juce::String& name, const juce::StringArray& items)
{
    c.box.addItemList (items, 1);
    c.label.setText (name, juce::dontSendNotification);
    c.label.setJustificationType (juce::Justification::centredLeft);
    c.label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    addAndMakeVisible (c.box);
    addAndMakeVisible (c.label);
    c.att = std::make_unique<ComboAtt> (proc.apvts, id, c.box);
}

void PlaitsEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    g.drawText ("UNOFFICIAL PLAITS", 20, 10, 400, 26, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("macro oscillator - community port, not a Mutable Instruments product", 20, 34, 500, 16, juce::Justification::centredLeft);

    g.setColour (kPanel);
    const float w = (float) getWidth() - 24.0f;
    g.fillRoundedRectangle (12.0f, 58.0f,  w, 70.0f,  10.0f);   // engine + envelope + mono
    g.fillRoundedRectangle (12.0f, 136.0f, w, 150.0f, 10.0f);   // harmonics / timbre / morph
    g.fillRoundedRectangle (12.0f, 294.0f, w, 120.0f, 10.0f);   // attenuverters
    g.fillRoundedRectangle (12.0f, 422.0f, w, 126.0f, 10.0f);   // transpose / fine / decay / lpg colour
}

void PlaitsEditor::resized()
{
    const int W = getWidth();

    auto place = [&] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (16));
        k.slider.setBounds (r);
    };
    auto placeChoice = [&] (Choice& c, juce::Rectangle<int> r)
    {
        c.label.setBounds (r.removeFromTop (16));
        c.box.setBounds (r.removeFromTop (28));
    };

    placeChoice (engine,   juce::Rectangle<int> (24, 66, 300, 48));
    placeChoice (envelope, juce::Rectangle<int> (336, 66, 170, 48));
    monoOut.setBounds (W - 12 - 100 - 8, 84, 100, 36);

    const int col3 = (W - 24) / 3;
    Knob* mainRow[3] = { &harmonics, &timbre, &morph };
    for (int i = 0; i < 3; ++i)
        place (*mainRow[i], juce::Rectangle<int> (12 + i * col3, 140, col3, 142).reduced (20, 0));

    Knob* amtRow[3] = { &fmAmount, &timbreAmount, &morphAmount };
    for (int i = 0; i < 3; ++i)
        place (*amtRow[i], juce::Rectangle<int> (12 + i * col3, 298, col3, 112).reduced (30, 0));

    const int col4 = (W - 24) / 4;
    Knob* lowRow[4] = { &transpose, &fine, &decay, &lpgColour };
    for (int i = 0; i < 4; ++i)
        place (*lowRow[i], juce::Rectangle<int> (12 + i * col4, 426, col4, 118).reduced (14, 0));
}
