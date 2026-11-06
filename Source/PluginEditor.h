/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ChordBoundaryDetector.h"
#include <unordered_map>
#include <vector>

//==============================================================================
/**
*/
class HarmonyVisualizerAudioProcessorEditor  : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    HarmonyVisualizerAudioProcessorEditor (HarmonyVisualizerAudioProcessor&);
    ~HarmonyVisualizerAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    
    void timerCallback() override;

    ChordBoundaryDetector& getChordDetector() noexcept { return chordDetector; }

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    HarmonyVisualizerAudioProcessor& audioProcessor;
    
    ChordBoundaryDetector chordDetector;

    juce::Rectangle<float> drawingArea;
    juce::Point<float> circleCenter;
    float circleRadius { 0.0f };
    float circleDiameter { 0.0f };
    float circleThickness { 0.0f };

    juce::Rectangle<float> labelCircle_rect;
    float labelCircle_fontsize { 0.0f };
    float labelCircle_radius { 0.0f };

    float pitchLine_thickness { 0.0f };
    float pitchPoint_radius { 0.0f };
    float pitchPoint_diameter { 0.0f };
    
    juce::Rectangle<float> labelCentre_rect;
    float labelCentre_fontsize { 0.0f };
    
    // 五度圏順（Cから時計回り）の音名
    const juce::StringArray circleNoteNames
        { "C", "G", "D", "A", "E", "B", "Gb", "Db", "Ab", "Eb", "Bb", "F" };

    const std::unordered_map<juce::String, juce::Array<int>> triad = {
        { "major", {0, 1, 4} },
        { "minor", {0, 1, 9} },
        { "dim",   {0, 1, 9} },
        { "aug",   {0, 8, 4} },
    };
    
    const juce::Colour backgroundColour = juce::Colours::black;
    const juce::Colour circleColour = juce::Colours::darkgrey;
    const juce::Colour pointColour_root = juce::Colours::orangered;
    const juce::Colour pointColour = juce::Colours::white;
    
    const float lineS = 0.5f;
    const float lineV = 1.0f;
    const float lineA = 1.0f;
    const std::unordered_map<int, juce::Colour> pitchLine_colours = {
        { 1, juce::Colour::fromHSV (0.67f, lineS, lineV, lineA) },
        { 2, juce::Colour::fromHSV (0.07f, lineS, lineV, lineA) },
        { 3, juce::Colour::fromHSV (0.33f, lineS, lineV, lineA) },
        { 4, juce::Colour::fromHSV (0.22f, lineS, lineV, lineA) },
        { 5, juce::Colour::fromHSV (0.07f, lineS, lineV, lineA) },
        { 6, juce::Colour::fromHSV (0.0f,  lineS, lineV, lineA) },
    };
    
    float getAngle (int circleIdx) const
    {
        return juce::MathConstants<float>::twoPi * static_cast<float> (circleIdx) / 12.0f;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonyVisualizerAudioProcessorEditor)
};
