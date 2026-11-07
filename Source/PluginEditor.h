/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ChordBoundaryDetector.h"
#include "ChordEvent.h"
#include "ChordHistoryQueue.h"
#include "PerspectiveProjection.h"
#include <array>
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
    ChordHistoryQueue& getChordHistory() noexcept { return chordHistory; }
    const ChordHistoryQueue& getChordHistory() const noexcept { return chordHistory; }
    PerspectiveProjection& getProjection() noexcept { return projection; }
    const PerspectiveProjection& getProjection() const noexcept { return projection; }

    /**
     * 五度圏上の距離（1〜6）に応じたインターバルカラーを取得
     * @param dist 五度圏上の距離（1〜6）
     * @param depthAlpha 深度フェードやアクティブ状態に応じたアルファ倍率（0.0f〜1.0f）
     */
    juce::Colour getIntervalColour (int dist, float depthAlpha = 1.0f) const
    {
        const juce::Colour baseColour = (dist >= 1 && dist <= 6)
            ? pitchLine_colours[static_cast<size_t> (dist)]
            : juce::Colours::white;
        return baseColour.withMultipliedAlpha (juce::jlimit (0.0f, 1.0f, depthAlpha));
    }

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    HarmonyVisualizerAudioProcessor& audioProcessor;
    
    ChordBoundaryDetector chordDetector;
    ChordHistoryQueue chordHistory { 8, 1.0f };
    PerspectiveProjection projection;

    // Zソート（ペインターズアルゴリズム）描画用ヘルパー
    void drawBackgroundGuides (juce::Graphics& g);
    void drawPastChordLayers (juce::Graphics& g);
    void drawVoiceLeadingConnections (juce::Graphics& g);
    void drawForegroundChordLayer (juce::Graphics& g);
    void drawChordRingAndPolygon (juce::Graphics& g, const ChordEvent& chord, float effectiveAlpha, bool isForeground);

    juce::Rectangle<float> drawingArea;
    juce::Point<float> circleCenter;
    float circleRadius { 0.0f };
    float circleDiameter { 0.0f };
    float circleThickness { 0.0f };

    float labelCircle_size { 0.0f };
    float labelCircle_fontsize { 0.0f };

    float pitchLine_thickness { 0.0f };
    float pitchPoint_radius { 0.0f };
    float pitchPoint_diameter { 0.0f };
    
    float labelCentre_height { 0.0f };
    float labelCentre_fontsize { 0.0f };
    
    // 五度圏順（Cから時計回り）の音名
    const juce::StringArray circleNoteNames
        { "C", "G", "D", "A", "E", "B", "Gb", "Db", "Ab", "Eb", "Bb", "F" };
    
    // Studio Professional / Dark Slate カラーパレット
    const juce::Colour backgroundColour   { 0xff141720 }; // 深みのあるスレートチャコール
    const juce::Colour circleColour       { 0xff2a3142 }; // 五度圏メインリング
    const juce::Colour guideGridColour    { 0x24ffffff }; // 中心クロスヘア・ガイド目盛
    const juce::Colour inactiveNoteColour { 0xff5a6378 }; // 未発音の音名テキスト
    const juce::Colour inactiveDotColour  { 0xff252b38 }; // 未発音の12音ガイドドット
    const juce::Colour pointColour_root   { 0xffff5a5f }; // ルート音（洗練されたコーラルアクセント）
    const juce::Colour pointColour        { 0xfff1f5f9 }; // 通常の構成音（クリーンオフホワイト）
    const juce::Colour centerTextColour   { 0xffe2e8f0 }; // 中央の構成音表示テキスト
    
    // 五度圏上の距離（1〜6）に応じたインターバルカラー（Studio Slate 調和パレット）
    // 0: 未使用
    // 1: 完全5度/4度 (最協和) -> クールシアン
    // 2: 長2度/短7度 (モード) -> ウォームアンバー
    // 3: 短3度/長6度 (マイナー) -> セージグリーン
    // 4: 長3度/短6度 (メジャー) -> ミントライム
    // 5: 短2度/長7度 (半音緊張) -> ビビッドタンジェリン
    // 6: 増4度/減5度 (トライトーン) -> クリムゾンローズ
    const std::array<juce::Colour, 7> pitchLine_colours = {
        juce::Colours::transparentBlack,
        juce::Colour (0xff38bdf8),
        juce::Colour (0xfffbbf24),
        juce::Colour (0xff34d399),
        juce::Colour (0xffa3e635),
        juce::Colour (0xfff97316),
        juce::Colour (0xfff43f5e)
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonyVisualizerAudioProcessorEditor)
};
