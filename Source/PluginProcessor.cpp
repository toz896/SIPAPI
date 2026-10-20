#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MilsDenoiseProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reduction", 1 }, "Reduccion",
           NormalisableRange<float> (0.f, 40.f, 0.1f), 18.f, AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sensitivity", 1 }, "Sensibilidad",
           NormalisableRange<float> (0.f, 100.f, 0.1f), 50.f, AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "smoothing", 1 }, "Suavizado",
           NormalisableRange<float> (0.f, 100.f, 0.1f), 60.f, AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "gate", 1 }, "Gate",
           NormalisableRange<float> (-80.f, 0.f, 0.1f), -80.f,
           AudioParameterFloatAttributes().withLabel ("dB")
               .withStringFromValueFunction ([] (float v, int) { return v <= -79.5f ? String ("Off") : String (v, 1) + " dB"; })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "breath", 1 }, "Respiracion", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "keys", 1 },   "Teclado",     false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Salida",
           NormalisableRange<float> (-12.f, 12.f, 0.1f), 0.f, AudioParameterFloatAttributes().withLabel ("dB")));
    return l;
}

MilsDenoiseProcessor::MilsDenoiseProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pReduction   = apvts.getRawParameterValue ("reduction");
    pSensitivity = apvts.getRawParameterValue ("sensitivity");
    pSmoothing   = apvts.getRawParameterValue ("smoothing");
    pGate        = apvts.getRawParameterValue ("gate");
    pOutput      = apvts.getRawParameterValue ("output");
    pBreath      = apvts.getRawParameterValue ("breath");
    pKeys        = apvts.getRawParameterValue ("keys");
}

bool MilsDenoiseProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in = l.getMainInputChannelSet();
    const auto& out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void MilsDenoiseProcessor::prepareToPlay (double sampleRate, int)
{
    denoiser.prepare (sampleRate, getTotalNumInputChannels());
    setLatencySamples (denoiser.getLatencySamples());
    lastGain = juce::Decibels::decibelsToGain (pOutput->load());
}

void MilsDenoiseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = std::min (buffer.getNumChannels(), 2);
    if (nch < 1 || n < 1) return;

    nsup::Params p;
    p.reductionDb = pReduction->load();
    p.sensitivity = pSensitivity->load() * 0.01f;
    p.smoothing   = pSmoothing->load() * 0.01f;
    p.gateThresholdDb = pGate->load();
    p.suppressBreath  = pBreath->load() > 0.5f;
    p.suppressKeys    = pKeys->load()   > 0.5f;
    denoiser.setParams (p);

    float* ch[2] = { buffer.getWritePointer (0), nch > 1 ? buffer.getWritePointer (1) : nullptr };
    denoiser.process (ch, n);

    const float target = juce::Decibels::decibelsToGain (pOutput->load());
    buffer.applyGainRamp (0, n, lastGain, target);
    lastGain = target;
}

juce::AudioProcessorEditor* MilsDenoiseProcessor::createEditor() { return new MilsDenoiseEditor (*this); }

void MilsDenoiseProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void MilsDenoiseProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MilsDenoiseProcessor(); }
