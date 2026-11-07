/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

//==============================================================================
HarmonyVisualizerAudioProcessorEditor::HarmonyVisualizerAudioProcessorEditor (HarmonyVisualizerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (400, 400);

    // 新コード確定時の履歴キュー追加コールバック
    chordDetector.setNewChordCallback ([this] (const std::vector<int>& notes)
    {
        ChordEvent newChord = ChordEvent::fromMidiNotes (notes);
        chordHistory.pushChord (std::move (newChord));
    });
    startTimerHz (60);
    setResizable (true, true);
    setResizeLimits (300, 300, 1000, 1000);
}

HarmonyVisualizerAudioProcessorEditor::~HarmonyVisualizerAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void HarmonyVisualizerAudioProcessorEditor::timerCallback()
{
    const double currentTimeMs = juce::Time::getMillisecondCounterHiRes();

    // ロックフリーFIFOから未読イベントを取り出してコード境界判定エンジンへ渡す
    audioProcessor.popMidiEvents ([this, currentTimeMs] (const QueuedMidiEvent& event)
    {
        chordDetector.processMidiEvent (event, currentTimeMs);
    });

    chordDetector.update (currentTimeMs);

    // コード履歴キューのアニメーション更新
    chordHistory.updateAnimation (0.2f);

    repaint();
}

void HarmonyVisualizerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour); // 背景（スレートチャコール）

    // =========================================================================
    // ペインターズアルゴリズム（Zソート: 奥から手前へ順次描画）
    // 1. 背景ガイド（消失点クロスヘア・タイムトンネル奥行き基準）
    // 2. 過去コード履歴レイヤー（Z大 -> Z小 の逆順で奥から手前へ描画）
    // 3. ボイスリーディング遷移ライン（世代間の声部接続）
    // 4. 最前面（Z=0）のメイン五度圏リング・現在コード・音名ラベル
    // =========================================================================
    drawBackgroundGuides (g);
    drawPastChordLayers (g);
    drawVoiceLeadingConnections (g);
    drawForegroundChordLayer (g);
}

void HarmonyVisualizerAudioProcessorEditor::drawBackgroundGuides (juce::Graphics& g)
{
    // 中心クロスヘア（消失点目盛）＆ガイドドット
    g.setColour (guideGridColour);
    const float crossLength = circleRadius * 0.06f;
    g.drawLine (circleCenter.x - crossLength, circleCenter.y, circleCenter.x + crossLength, circleCenter.y, circleThickness * 0.8f);
    g.drawLine (circleCenter.x, circleCenter.y - crossLength, circleCenter.x, circleCenter.y + crossLength, circleThickness * 0.8f);

    const float centerDotRadius = circleThickness * 1.2f;
    g.fillEllipse (circleCenter.x - centerDotRadius, circleCenter.y - centerDotRadius, centerDotRadius * 2.0f, centerDotRadius * 2.0f);
}

void HarmonyVisualizerAudioProcessorEditor::drawPastChordLayers (juce::Graphics& g)
{
    const auto& chords = chordHistory.getChords();
    if (chords.size() <= 1)
        return;

    // Zソート: 最も奥（末尾）から手前（index 1）へ向かって描画する
    for (size_t idx = chords.size() - 1; idx >= 1; --idx)
    {
        const auto& pastChord = chords[idx];
        if (pastChord.isEmpty())
            continue;

        // 過去世代は奥へ行くほど薄く描画
        const float layerAlpha = pastChord.alpha * 0.55f;
        drawChordRingAndPolygon (g, pastChord, layerAlpha, false);
    }
}

void HarmonyVisualizerAudioProcessorEditor::drawVoiceLeadingConnections (juce::Graphics& g)
{
    // Issue #11 にて世代間のボイスリーディング糸（立体直線）を描画予定
    juce::ignoreUnused (g);
}

void HarmonyVisualizerAudioProcessorEditor::drawForegroundChordLayer (juce::Graphics& g)
{
    const auto& chordState = chordDetector.getCurrentChordState();
    const float alphaMultiplier = chordState.isDimmed ? 0.35f : 1.0f;

    ChordEvent foregroundChord = ChordEvent::fromMidiNotes (chordState.notes);
    foregroundChord.zPosition = 0.0f;
    foregroundChord.alpha = alphaMultiplier;
    foregroundChord.generation = 0;

    // 最前面のリング・ガイドドット・インターバル多角形・構成音ポイントを描画
    drawChordRingAndPolygon (g, foregroundChord, alphaMultiplier, true);

    // 最前面（Z=0）のみ外周の12音名ラベルと中央の構成音テキストを描画
    g.setFont (labelCircle_fontsize);

    for (int i = 0; i < 12; ++i)
    {
        const auto p = projection.projectCircleIndex (i, 0.0f, 1.1f);
        const bool isActive = std::find (foregroundChord.circleIndices.begin(),
                                         foregroundChord.circleIndices.end(),
                                         i) != foregroundChord.circleIndices.end();

        juce::Colour textColour = (isActive ? pointColour.withMultipliedAlpha (alphaMultiplier) : inactiveNoteColour);
        g.setColour (textColour);

        labelCircle_rect.setCentre (p);
        g.drawText (circleNoteNames[i], labelCircle_rect, juce::Justification::centred);
    }

    // 中央の構成音テキスト表示
    juce::String pitches = "";
    for (int pitchIdx : foregroundChord.circleIndices)
    {
        pitches += circleNoteNames[pitchIdx] + " ";
    }

    g.setColour (centerTextColour.withMultipliedAlpha (alphaMultiplier));
    g.setFont (labelCentre_fontsize);

    labelCentre_rect.setCentre (circleCenter);
    g.drawText (pitches.trim(), labelCentre_rect, juce::Justification::centred);
}

void HarmonyVisualizerAudioProcessorEditor::drawChordRingAndPolygon (
    juce::Graphics& g,
    const ChordEvent& chord,
    float effectiveAlpha,
    bool isForeground)
{
    const float z = chord.zPosition;
    const float projectedRadius = projection.getProjectedRadius (z);
    const float projectedDiameter = projectedRadius * 2.0f;
    const float ringThickness = projection.getProjectedSize (circleThickness, z, 0.35f);

    // 1. 深度 Z における五度圏リング
    const float ringAlpha = isForeground ? 1.0f : juce::jlimit (0.08f, 0.6f, effectiveAlpha * 0.7f);
    g.setColour (circleColour.withMultipliedAlpha (ringAlpha));
    g.drawEllipse (circleCenter.x - projectedRadius,
                   circleCenter.y - projectedRadius,
                   projectedDiameter,
                   projectedDiameter,
                   ringThickness);

    // 2. 最前面のみ12音位置の控えめなガイドドットを描画
    if (isForeground)
    {
        const float guideDotRadius = pitchPoint_radius * 0.35f;
        for (int i = 0; i < 12; ++i)
        {
            const bool isActive = std::find (chord.circleIndices.begin(),
                                             chord.circleIndices.end(),
                                             i) != chord.circleIndices.end();
            if (!isActive)
            {
                const auto p = projection.projectCircleIndex (i, 0.0f, 1.0f);
                g.setColour (inactiveDotColour);
                g.fillEllipse (p.x - guideDotRadius, p.y - guideDotRadius, guideDotRadius * 2.0f, guideDotRadius * 2.0f);
            }
        }
    }

    if (chord.circleIndices.empty())
        return;

    const float lineThickness = projection.getProjectedSize (pitchLine_thickness, z, 0.3f);
    const float ptRadius = projection.getProjectedSize (pitchPoint_radius, z, 0.35f);
    const float ptDiameter = ptRadius * 2.0f;

    // 3. 構成音間のインターバル線と頂点ポイントを透視投影座標で描画
    for (size_t i = 0; i < chord.circleIndices.size(); ++i)
    {
        const int idxA = chord.circleIndices[i];
        const juce::Point<float> startPoint = projection.projectCircleIndex (idxA, z, 1.0f);

        for (size_t j = i + 1; j < chord.circleIndices.size(); ++j)
        {
            const int idxB = chord.circleIndices[j];
            int dist = std::abs (idxA - idxB);
            if (dist > 6)
                dist = 12 - dist;

            const juce::Point<float> endPoint = projection.projectCircleIndex (idxB, z, 1.0f);

            g.setColour (getIntervalColour (dist, effectiveAlpha));
            g.drawLine (juce::Line<float> (startPoint, endPoint), lineThickness);
        }

        const juce::Colour ptColour = (idxA == chord.bassCircleIndex ? pointColour_root : pointColour);
        g.setColour (ptColour.withMultipliedAlpha (effectiveAlpha));
        g.fillEllipse (startPoint.x - ptRadius, startPoint.y - ptRadius, ptDiameter, ptDiameter);
    }
}

void HarmonyVisualizerAudioProcessorEditor::resized()
{
    drawingArea = getLocalBounds().toFloat().reduced (0.0f);
    
    circleCenter = drawingArea.getCentre();
    circleRadius = juce::jmin (drawingArea.getWidth(), drawingArea.getHeight()) * 0.4f;
    circleDiameter = 2.0f * circleRadius;
    circleThickness = 0.02f / 3.0f * circleRadius;

    // ウィンドウサイズに応じて3D透視投影の基準パラメータを更新
    projection.setViewport (circleCenter, circleRadius, 0.28f);
    
    labelCircle_rect = { 0.15f * circleRadius, 0.15f * circleRadius };
    labelCircle_fontsize = 0.1f * circleRadius;
    labelCircle_radius = 1.1f * circleRadius;
    
    pitchLine_thickness = 0.01f * circleRadius;
    
    pitchPoint_radius = 0.02f * circleRadius;
    pitchPoint_diameter = 2.0f * pitchPoint_radius;
    
    labelCentre_fontsize = 0.1f * circleRadius;
    labelCentre_rect = { circleDiameter, 0.6f * circleRadius };
}
