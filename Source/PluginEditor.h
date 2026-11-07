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
    
    // Studio Professional / Dark Slate カラーパレット
    const juce::Colour backgroundColour   { 0xff141720 }; // 深みのあるスレートチャコール
    const juce::Colour circleColour       { 0xff2a3142 }; // 五度圏メインリング
    const juce::Colour guideGridColour    { 0x18ffffff }; // 繊細な補助グリッド・インナーリング
    const juce::Colour inactiveNoteColour { 0xff5a6378 }; // 未発音の音名テキスト
    const juce::Colour inactiveDotColour  { 0xff252b38 }; // 未発音の12音ガイドドット
    const juce::Colour pointColour_root   { 0xffff5a5f }; // ルート音（洗練されたコーラルアクセント）
    const juce::Colour pointColour        { 0xfff1f5f9 }; // 通常の構成音（クリーンオフホワイト）
    const juce::Colour centerTextColour   { 0xffe2e8f0 }; // 中央の構成音表示テキスト
    
    // 五度圏上の距離（1〜6）に応じたインターバルカラー（Studio Slate 調和パレット）
    // 1: 完全5度/4度 (最協和) -> クールシアン
    // 2: 長2度/短7度 (モード) -> ウォームアンバー
    // 3: 短3度/長6度 (マイナー) -> セージグリーン
    // 4: 長3度/短6度 (メジャー) -> ミントライム
    // 5: 短2度/長7度 (半音緊張) -> ビビッドタンジェリン
    // 6: 増4度/減5度 (トライトーン) -> クリムゾンローズ
    const std::unordered_map<int, juce::Colour> pitchLine_colours = {
        { 1, juce::Colour (0xff38bdf8) },
        { 2, juce::Colour (0xfffbbf24) },
        { 3, juce::Colour (0xff34d399) },
        { 4, juce::Colour (0xffa3e635) },
        { 5, juce::Colour (0xfff97316) },
        { 6, juce::Colour (0xfff43f5e) },
    };
    
    float getAngle (int circleIdx) const
    {
        return juce::MathConstants<float>::twoPi * static_cast<float> (circleIdx) / 12.0f;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonyVisualizerAudioProcessorEditor)
};
