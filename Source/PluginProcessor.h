#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DenoiserCore.h"

class MilsDenoiseProcessor : public juce::AudioProcessor
{
public:
    MilsDenoiseProcessor();
    ~MilsDenoiseProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MilsDenoise"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    nsup::Denoiser denoiser;
    std::atomic<float>* pReduction   = nullptr;
    std::atomic<float>* pSensitivity = nullptr;
    std::atomic<float>* pSmoothing   = nullptr;
    std::atomic<float>* pGate        = nullptr;
    std::atomic<float>* pOutput      = nullptr;
    std::atomic<float>* pBreath      = nullptr;
    std::atomic<float>* pKeys        = nullptr;
    float lastGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MilsDenoiseProcessor)
};
