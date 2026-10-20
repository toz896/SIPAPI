#pragma once
#include "PluginProcessor.h"

class MilsDenoiseEditor : public juce::AudioProcessorEditor
{
public:
    explicit MilsDenoiseEditor (MilsDenoiseProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    void addKnob (Knob& k, MilsDenoiseProcessor& p, const juce::String& id, const juce::String& text, const juce::String& suffix);

    Knob reduction, sensitivity, smoothing, gate, output;
    juce::ToggleButton breathBtn { "Respiracion" }, keysBtn { "Teclado" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> breathAtt, keysAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MilsDenoiseEditor)
};
