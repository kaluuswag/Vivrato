#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <memory>
#include <vector>

class VivratoAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit VivratoAudioProcessorEditor (VivratoAudioProcessor&);
    ~VivratoAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    void addKnob (const juce::String& id, const juce::String& name);
    void updateEnabledState();

    VivratoAudioProcessor& processor;

    juce::ComboBox modeBox;
    juce::Label    modeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttach;

    std::vector<std::unique_ptr<Knob>> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VivratoAudioProcessorEditor)
};
