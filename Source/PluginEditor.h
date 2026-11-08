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
#include "VoiceLeadingSolver.h"
#include <array>
#include <vector>
#include <cmath>

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
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;
    
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

    /**
     * 深度 Z に応じた空気遠近法（Aerial Perspective）のフォグブレンド率（0.0f〜0.88f）を計算
     * @param z          深度（0.0f = 最前面、正の値 = 奥）
     * @param resistance 減衰耐性（0.0f = 標準減衰、0.5f〜0.65f = トライトーン等の緊張音程・骨格）
     */
    float getAerialFogFactor (float z, float resistance = 0.0f) const noexcept
    {
        if (z <= 0.0f)
            return 0.0f;

        const float maxZ = std::max (1.0f, static_cast<float> (chordHistory.getMaxHistorySize()) * chordHistory.getZSpacing());
        const float normalizedZ = juce::jlimit (0.0f, 1.0f, z / maxZ);

        // 距離に応じた滑らかな大気減衰カーブ（手前はクリア、中〜奥でスレート背景へ溶け込む）
        const float baseFog = 1.0f - std::exp (-2.2f * normalizedZ);
        const float clampedResistance = juce::jlimit (0.0f, 0.85f, resistance);

                return juce::jlimit (0.0f, 0.88f, baseFog * (1.0f - clampedResistance));
    }

    /**
     * 深度 Z に応じた被写界深度（Depth of Field）のボケ率（0.0f = ピント合焦、1.0f = 最大ボケ）を計算
     */
    float getDepthOfFieldBlurFactor (float z) const noexcept
    {
        if (z <= 0.0f)
            return 0.0f;

        const float maxZ = std::max (1.0f, static_cast<float> (chordHistory.getMaxHistorySize()) * chordHistory.getZSpacing());
        const float normalizedZ = juce::jlimit (0.0f, 1.0f, z / maxZ);

        // 1世代奥から緩やかにボケ始め、中〜奥で滑らかにピントが外れる光学レンズ特性を近似
        return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-2.6f * normalizedZ));
    }

    /**
     * 空気遠近法（彩度低下 ＋ 背景スレートチャコールへの色ブレンド ＋ アルファ減衰）を適用した色を返す
     * @param baseColour 元の描画色
     * @param z          深度（0.0f = 最前面）
     * @param alpha      不透明度（0.0f〜1.0f）
     * @param resistance 空気遠近に対する耐性（トライトーンやベースラインは奥でも色味・発光を残す）
     */
    juce::Colour applyAerialPerspective (juce::Colour baseColour,
                                         float z,
                                         float alpha,
                                         float resistance = 0.0f) const noexcept
    {
        const float clampedAlpha = juce::jlimit (0.0f, 1.0f, alpha);
        if (z <= 0.001f)
            return baseColour.withMultipliedAlpha (clampedAlpha);

        const float fog = getAerialFogFactor (z, resistance);

        // 1. 奥へ行くほど彩度を落として大気の霞み感を出す
        const float saturationMultiplier = juce::jlimit (0.20f, 1.0f, 1.0f - fog * 0.58f);
        const juce::Colour desaturated = baseColour.withMultipliedSaturation (saturationMultiplier);

        // 2. 背景色（スレートチャコール）へ補間してコントラストを減衰させる
        const float bgBlendRatio = juce::jlimit (0.0f, 0.78f, fog * 0.72f);
        const juce::Colour fogged = desaturated.interpolatedWith (backgroundColour, bgBlendRatio);

        return fogged.withMultipliedAlpha (clampedAlpha);
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
    void drawSingleVoiceLeadingThread (juce::Graphics& g,
                                       const VoiceLeadingConnection& conn,
                                       float fromZ,
                                       float toZ,
                                       float fromAlpha,
                                       float toAlpha,
                                       bool isLatestTransition);
    void drawForegroundChordLayer (juce::Graphics& g);
    void drawChordRingAndPolygon (juce::Graphics& g, const ChordEvent& chord, float effectiveAlpha, bool isForeground);

    // 設定パネル（右上歯車アイコン＆半透明オーバーレイモーダル）描画・制御ヘルパー
    void drawSettingsGearButton (juce::Graphics& g);
    void drawSettingsModalOverlay (juce::Graphics& g);
    void updateSettingsModalLayout();
    void syncSettingsFromProcessor();
    void setSettingsModalOpen (bool shouldOpen);

    bool isSettingsModalOpen { false };
    bool isGearButtonHovered { false };
    bool isCloseButtonHovered { false };
    float settingsModalAlpha { 0.0f };
    uint32_t lastSyncedSettingsRevision { 0 };

    juce::Rectangle<float> gearButtonBounds;
    juce::Rectangle<float> modalCardBounds;
    juce::Rectangle<float> modalCloseButtonBounds;

    juce::Slider historySizeSlider;
    juce::Slider quantizeWindowSlider;
    juce::Slider arpeggioThresholdSlider;
    juce::TextButton resetSettingsButton { juce::String::fromUTF8 ("デフォルトに戻す") };

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
