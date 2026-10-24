#include "PluginEditor.h"

namespace
{
    const juce::Colour bgTop    (0xff0b0b0f);
    const juce::Colour bgBottom (0xff1a1022);
    const juce::Colour accent   (0xffb6ff3b);   // verde ácido
    const juce::Colour dim      (0xff8a8a96);
}

VivratoAudioProcessorEditor::VivratoAudioProcessorEditor (VivratoAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (700, 380);

    modeBox.addItemList ({ "Continuous", "Progressive" }, 1);
    modeBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff22222c));
    modeBox.setColour (juce::ComboBox::textColourId, accent);
    modeBox.setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.5f));
    modeBox.setColour (juce::ComboBox::arrowColourId, accent);
    addAndMakeVisible (modeBox);
    modeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "mode", modeBox);
    modeBox.onChange = [this] { updateEnabledState(); };

    modeLabel.setText ("MODE", juce::dontSendNotification);
    modeLabel.setJustificationType (juce::Justification::centred);
    modeLabel.setColour (juce::Label::textColourId, dim);
    modeLabel.setFont (juce::FontOptions (12.f, juce::Font::bold));
    addAndMakeVisible (modeLabel);

    addKnob ("rate",   "RATE");
    addKnob ("depth",  "DEPTH");
    addKnob ("delay",  "ONSET");
    addKnob ("ramp",   "RAMP");
    addKnob ("thresh", "THRESH");
    addKnob ("jitter", "JITTER");
    addKnob ("spread", "STEREO");
    addKnob ("grit",   "GRIT");
    addKnob ("mix",    "MIX");
    addKnob ("output", "OUTPUT");

    updateEnabledState();
}

void VivratoAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& name)
{
    auto k = std::make_unique<Knob>();

    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k->slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff2c2c38));
    k->slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    k->slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k->slider);

    k->label.setText (name, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setColour (juce::Label::textColourId, dim);
    k->label.setFont (juce::FontOptions (12.f, juce::Font::bold));
    addAndMakeVisible (k->label);

    k->attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k->slider);
    knobs.push_back (std::move (k));
}

void VivratoAudioProcessorEditor::updateEnabledState()
{
    // Onset / Ramp / Threshold sólo actúan en modo Progressive
    const bool prog = modeBox.getSelectedId() == 2;
    for (int idx : { 2, 3, 4 })
    {
        knobs[(size_t) idx]->slider.setEnabled (prog);
        knobs[(size_t) idx]->slider.setAlpha (prog ? 1.f : 0.35f);
        knobs[(size_t) idx]->label.setAlpha (prog ? 1.f : 0.35f);
    }
}

void VivratoAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (bgTop, 0, 0, bgBottom, 0, (float) getHeight(), false));
    g.fillAll();

    g.setColour (accent);
    g.setFont (juce::FontOptions (34.f, juce::Font::bold));
    g.drawText ("VIVRATO", 24, 14, 300, 44, juce::Justification::centredLeft);

    g.setColour (dim);
    g.setFont (juce::FontOptions (12.f));
    g.drawText ("underground vibrato", 26, 52, 300, 18, juce::Justification::centredLeft);

    g.setColour (accent.withAlpha (0.25f));
    g.drawLine (24.f, 80.f, (float) getWidth() - 24.f, 80.f, 1.f);
}

void VivratoAudioProcessorEditor::resized()
{
    modeLabel.setBounds (getWidth() - 224, 14, 200, 16);
    modeBox.setBounds   (getWidth() - 224, 32, 200, 30);

    auto area = getLocalBounds().reduced (24).withTrimmedTop (64);
    const int cols = 5, rows = 2;
    const int w = area.getWidth() / cols, h = area.getHeight() / rows;

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto cell = juce::Rectangle<int> (area.getX() + (int) (i % cols) * w,
                                          area.getY() + (int) (i / cols) * h, w, h).reduced (6);
        knobs[i]->label.setBounds  (cell.removeFromTop (16));
        knobs[i]->slider.setBounds (cell);
    }
}
