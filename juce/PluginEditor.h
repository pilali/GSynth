/*
 * GSynth — JUCE editor.
 *
 * Reproduces the MOD modgui look: dark pedal surface, brand header, 13 vertical
 * faders laid out in three groups (VOICE MIX / FILTER / MISC), drawn natively by
 * GSynthLookAndFeel so they stay crisp at any size/DPI.
 */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "PluginProcessor.h"

/* Glossy silver fader cap + grooved track, modelled on the modgui slider. */
class GSynthLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GSynthLookAndFeel();
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
};

class GSynthAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit GSynthAudioProcessorEditor(GSynthAudioProcessor&);
    ~GSynthAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    GSynthAudioProcessor& processorRef;
    GSynthLookAndFeel lnf;

    std::vector<std::unique_ptr<juce::Slider>>     sliders;
    std::vector<std::unique_ptr<juce::Label>>      labels;
    std::vector<std::unique_ptr<SliderAttachment>> attachments;

    // cached layout rectangles for paint()
    juce::Rectangle<int> headerArea, footerArea, faderArea;
    std::array<juce::Rectangle<int>, 3> groupLabelBounds;
    std::array<int, 2> separatorX { 0, 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GSynthAudioProcessorEditor)
};
