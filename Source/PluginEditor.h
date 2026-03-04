/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class ChordVisualizer_synthAudioProcessorEditor  : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    ChordVisualizer_synthAudioProcessorEditor (ChordVisualizer_synthAudioProcessor&);
    ~ChordVisualizer_synthAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    
    void timerCallback() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    ChordVisualizer_synthAudioProcessor& audioProcessor;
    
    struct LineParam {
        juce::Colour colour;
        float thickness;
    };
    
    // 五度圏順（Cから時計回り）の音名
    const juce::StringArray circleNoteNames
        { "C", "G", "D", "A", "E", "B", "Gb", "Db", "Ab", "Eb", "Bb", "F" };
    
    const std::unordered_map<juce::String, juce::Array<int>> triad = {
        { "major", {0, 1, 4} },
        { "minor", {0, 1, 9} },
        { "dim", {0, 1, 9} },
        { "aug", {0, 8, 4} },
    };
    
    const juce::Colour backgroundColour = juce::Colours::black;
    const juce::Colour pointColour_root = juce::Colours::orangered;
    const juce::Colour pointColour = juce::Colours::white;
    
    const float lineS = 0.5f;
    const float lineV = 1.0f;
    const float lineA = 1.0f;
    const std::unordered_map<int, LineParam> lineParams = {
        { 1, {juce::Colour::fromHSV (0.67f, lineS, lineV, lineA), 1.5f} },
        { 2, {juce::Colour::fromHSV (0.07f, lineS, lineV, lineA), 1.5f} },
        { 3, {juce::Colour::fromHSV (0.33f, lineS, lineV, lineA), 1.5f} },
        { 4, {juce::Colour::fromHSV (0.22f, lineS, lineV, lineA), 1.5f} },
        { 5, {juce::Colour::fromHSV (0.07f, lineS, lineV, lineA), 1.5f} },
        { 6, {juce::Colour::fromHSV (0.0f, lineS, lineV, lineA), 1.5f} },
    };
    
    float getAngle(int circleIdx){
        return juce::MathConstants<float>::twoPi * circleIdx / 12.0f;;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordVisualizer_synthAudioProcessorEditor)
};
