#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

class VivratoAudioProcessor : public juce::AudioProcessor
{
public:
    VivratoAudioProcessor();
    ~VivratoAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vivrato"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

private:
    // Delay lines (one per channel)
    std::array<std::vector<float>, 2> delayBuf;
    int mask = 0, writePos = 0, centerSamples = 0;
    double sr = 44100.0;

    // Smoothed parameters
    juce::SmoothedValue<float> rateSm, depthSm, mixSm, outSm;

    // Envelope follower / gate (for Progressive mode)
    float env = 0.f, envAtk = 0.f, envRel = 0.f;
    bool gate = false;
    int holdCount = 0, holdSamples = 0;
    double sinceOnset = 0.0;
    float rampSm = 0.f, rampCoef = 0.f;

    // LFO + organic drift
    float phase = 0.f;
    float noiseTarget = 0.f, noiseSm = 0.f, noiseCoef = 0.f;
    int noiseCounter = 0, noiseInterval = 1;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VivratoAudioProcessor)
};
