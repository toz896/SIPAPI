#include "PluginEditor.h"

void MilsDenoiseEditor::addKnob (Knob& k, MilsDenoiseProcessor& p, const juce::String& id,
                                 const juce::String& text, const juce::String& suffix)
{
    k.slider.setTextValueSuffix (suffix);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff4fc3f7));
    k.slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, id, k.slider);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
}

MilsDenoiseEditor::MilsDenoiseEditor (MilsDenoiseProcessor& p) : AudioProcessorEditor (&p)
{
    addKnob (reduction,   p, "reduction",   "Reduccion",    " dB");
    addKnob (sensitivity, p, "sensitivity", "Sensibilidad", " %");
    addKnob (smoothing,   p, "smoothing",   "Suavizado",    " %");
    addKnob (gate,        p, "gate",        "Gate",         " dB");
    gate.slider.textFromValueFunction = [] (double v) { return v <= -79.5 ? juce::String ("Off") : juce::String (v, 1) + " dB"; };
    gate.slider.valueFromTextFunction = [] (const juce::String& t) { return t.containsIgnoreCase ("off") ? -80.0 : t.getDoubleValue(); };
    gate.slider.updateText();
    addKnob (output,      p, "output",      "Salida",       " dB");
    for (auto* b : { &breathBtn, &keysBtn })
    {
        b->setColour (juce::ToggleButton::textColourId, juce::Colour (0xffd2d6dc));
        b->setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff4fc3f7));
        b->setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (0xff3a4048));
        addAndMakeVisible (*b);
    }
    breathAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "breath", breathBtn);
    keysAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "keys",   keysBtn);
    setSize (540, 275);
}

void MilsDenoiseEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1f24));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("MilsDenoise", 12, 8, 200, 24, juce::Justification::left);
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("supresor de ruido + gate + respiracion + teclado", 12, 30, 400, 16, juce::Justification::left);
}

void MilsDenoiseEditor::resized()
{
    auto area = getLocalBounds().withTrimmedTop (56).reduced (8);
    auto checks = area.removeFromBottom (40);
    breathBtn.setBounds (checks.removeFromLeft (checks.getWidth() / 2).reduced (12, 6));
    keysBtn.setBounds (checks.reduced (12, 6));
    const int w = area.getWidth() / 5;
    for (auto* k : { &reduction, &sensitivity, &smoothing, &gate, &output })
    {
        auto col = area.removeFromLeft (w);
        k->label.setBounds (col.removeFromTop (20));
        k->slider.setBounds (col);
    }
}
