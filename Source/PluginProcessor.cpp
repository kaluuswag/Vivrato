#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr float kCenterSec = 0.015f;   // retardo central (15 ms)
    constexpr float kMaxAmpSec = 0.0135f;  // modulación máxima de retardo
    constexpr float kTwoPi     = 6.28318530717958647692f;

    inline float hermite (const std::vector<float>& b, int mask, int wp, float d)
    {
        const int   di = (int) d;
        const float f  = d - (float) di;
        const float y0 = b[(size_t) ((wp - di + 1) & mask)];
        const float y1 = b[(size_t) ((wp - di)     & mask)];
        const float y2 = b[(size_t) ((wp - di - 1) & mask)];
        const float y3 = b[(size_t) ((wp - di - 2) & mask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout VivratoAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "mode", 1 }, "Mode",
        StringArray { "Continuous", "Progressive" }, 0));

    auto addFloat = [&p] (const char* id, const char* name, float lo, float hi,
                          float def, float skew, const char* unit)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name,
            NormalisableRange<float> (lo, hi, 0.01f, skew), def,
            AudioParameterFloatAttributes().withLabel (unit)));
    };

    addFloat ("rate",   "Rate",         0.1f,  12.f,   5.5f, 0.5f, "Hz");
    addFloat ("depth",  "Depth",        0.f,   100.f,  30.f, 0.7f, "cents");
    addFloat ("delay",  "Onset Delay",  0.f,   2000.f, 150.f, 0.6f, "ms");
    addFloat ("ramp",   "Ramp Time",    10.f,  5000.f, 600.f, 0.5f, "ms");
    addFloat ("thresh", "Threshold",   -60.f, -6.f,  -40.f, 1.f,  "dB");
    addFloat ("jitter", "Jitter",       0.f,   100.f,  20.f, 1.f,  "%");
    addFloat ("spread", "Stereo",       0.f,   100.f,  0.f,  1.f,  "%");
    addFloat ("grit",   "Grit",         0.f,   100.f,  0.f,  1.f,  "%");
    addFloat ("mix",    "Mix",          0.f,   100.f,  100.f, 1.f, "%");
    addFloat ("output", "Output",      -12.f,  12.f,   0.f,  1.f,  "dB");

    return { p.begin(), p.end() };
}

VivratoAudioProcessor::VivratoAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VIVRATO", createLayout())
{
}

bool VivratoAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == l.getMainInputChannelSet();
}

//==============================================================================
void VivratoAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;

    const int size = juce::nextPowerOfTwo ((int) (sr * 0.1));
    mask = size - 1;
    for (auto& b : delayBuf) b.assign ((size_t) size, 0.f);
    writePos = 0;

    centerSamples = (int) std::round (kCenterSec * sr);
    setLatencySamples (centerSamples);   // FL Studio compensa la latencia (PDC)

    rateSm.reset  (sr, 0.03); rateSm.setCurrentAndTargetValue  (*apvts.getRawParameterValue ("rate"));
    depthSm.reset (sr, 0.03); depthSm.setCurrentAndTargetValue (*apvts.getRawParameterValue ("depth"));
    mixSm.reset   (sr, 0.03); mixSm.setCurrentAndTargetValue   (*apvts.getRawParameterValue ("mix") * 0.01f);
    outSm.reset   (sr, 0.03); outSm.setCurrentAndTargetValue   (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));

    envAtk      = 1.f - std::exp (-1.f / (0.002f * (float) sr));
    envRel      = 1.f - std::exp (-1.f / (0.050f * (float) sr));
    holdSamples = (int) (0.06 * sr);
    rampCoef    = 1.f - std::exp (-1.f / (0.020f * (float) sr));

    noiseInterval = (int) (0.12 * sr);
    noiseCoef     = 1.f - std::exp (-1.f / (0.050f * (float) sr));

    env = 0.f; gate = false; holdCount = 0; sinceOnset = 0.0;
    rampSm = 0.f; phase = 0.f; noiseSm = noiseTarget = 0.f; noiseCounter = 0;
}

//==============================================================================
void VivratoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int nCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n   = buffer.getNumSamples();

    for (int ch = nCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    const bool  progressive = (int) *apvts.getRawParameterValue ("mode") == 1;
    const float delaySamp   = *apvts.getRawParameterValue ("delay") * 0.001f * (float) sr;
    const float rampSamp    = juce::jmax (1.f, *apvts.getRawParameterValue ("ramp") * 0.001f * (float) sr);
    const float thrLin      = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("thresh")->load());
    const float jitter      = *apvts.getRawParameterValue ("jitter") * 0.01f;
    const float spread      = *apvts.getRawParameterValue ("spread") * 0.01f;
    const float grit        = *apvts.getRawParameterValue ("grit") * 0.01f;
    const float gritDrive   = 1.f + grit * 5.f;
    const float gritNorm    = 1.f / std::tanh (gritDrive);

    rateSm.setTargetValue  (*apvts.getRawParameterValue ("rate"));
    depthSm.setTargetValue (*apvts.getRawParameterValue ("depth"));
    mixSm.setTargetValue   (*apvts.getRawParameterValue ("mix") * 0.01f);
    outSm.setTargetValue   (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load()));

    float* data[2] = { buffer.getWritePointer (0), nCh > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i = 0; i < n; ++i)
    {
        // ---- detector de nivel (para modo Progressive) ----
        float det = 0.f;
        for (int ch = 0; ch < nCh; ++ch) det = juce::jmax (det, std::abs (data[ch][i]));
        env += (det - env) * (det > env ? envAtk : envRel);

        if (env > thrLin)
        {
            holdCount = 0;
            if (! gate) { gate = true; sinceOnset = 0.0; phase = 0.f; }   // nueva nota: reinicia LFO
        }
        else if (env < thrLin * 0.7f)
        {
            if (gate && ++holdCount > holdSamples) gate = false;
        }
        else holdCount = 0;

        // ---- rampa del vibrato ----
        float rampTarget = 1.f;
        if (progressive)
        {
            if (gate)
            {
                sinceOnset += 1.0;
                float x = juce::jlimit (0.f, 1.f, ((float) sinceOnset - delaySamp) / rampSamp);
                rampTarget = x * x * (3.f - 2.f * x);   // smoothstep
            }
            else rampTarget = 0.f;
        }
        rampSm += (rampTarget - rampSm) * rampCoef;

        // ---- parámetros suavizados ----
        const float rate  = rateSm.getNextValue();
        const float depth = depthSm.getNextValue();
        const float mix   = mixSm.getNextValue();
        const float outG  = outSm.getNextValue();

        // ---- ruido orgánico (deriva lenta de tono) ----
        if (++noiseCounter >= noiseInterval) { noiseCounter = 0; noiseTarget = rng.nextFloat() * 2.f - 1.f; }
        noiseSm += (noiseTarget - noiseSm) * noiseCoef;

        phase += (rate * (1.f + jitter * 0.35f * noiseSm)) / (float) sr;
        phase -= std::floor (phase);

        float ampSec = (std::exp2 (depth / 1200.f) - 1.f) / (kTwoPi * rate);
        ampSec *= (1.f + jitter * 0.5f * noiseSm);
        ampSec = juce::jlimit (0.f, kMaxAmpSec, ampSec);
        const float drift = jitter * noiseSm * 0.0004f;

        // ---- escritura + lectura ----
        for (int ch = 0; ch < nCh; ++ch)
            delayBuf[(size_t) ch][(size_t) writePos] = data[ch][i];

        for (int ch = 0; ch < nCh; ++ch)
        {
            float ph = phase + (ch == 1 ? spread * 0.5f : 0.f);
            ph -= std::floor (ph);
            const float lfo = std::sin (kTwoPi * ph);

            float dSamp = ((float) kCenterSec + (ampSec * lfo + drift) * rampSm) * (float) sr;
            dSamp = juce::jlimit (3.f, (float) (mask - 4), dSamp);

            float wet = hermite (delayBuf[(size_t) ch], mask, writePos, dSamp);
            if (grit > 0.001f) wet = std::tanh (wet * gritDrive) * gritNorm;

            const float dry = delayBuf[(size_t) ch][(size_t) ((writePos - centerSamples) & mask)];
            data[ch][i] = (dry * (1.f - mix) + wet * mix) * outG;
        }

        writePos = (writePos + 1) & mask;
    }
}

//==============================================================================
juce::AudioProcessorEditor* VivratoAudioProcessor::createEditor()
{
    return new VivratoAudioProcessorEditor (*this);
}

void VivratoAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VivratoAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VivratoAudioProcessor();
}
