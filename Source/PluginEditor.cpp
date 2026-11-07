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
    // 3. ボイスリーディング遷移ライン（世代間の声部接続・多層ブルーム光の糸）
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

        const float layerAlpha = pastChord.alpha * 0.55f;
        drawChordRingAndPolygon (g, pastChord, layerAlpha, false);
    }
}

void HarmonyVisualizerAudioProcessorEditor::drawVoiceLeadingConnections (juce::Graphics& g)
{
    const auto& chords = chordHistory.getChords();
    if (chords.size() <= 1)
        return;

    // 離鍵時も最前面コードの明瞭さを維持し、奥との前後関係が逆転しないようにする（fgAlpha = 1.0f）
    constexpr float fgAlpha = 1.0f;

    // 世代間のボイスリーディング遷移ライン（光の糸）を奥（末尾）から手前（index 0）へ順に描画
    for (size_t idx = chords.size() - 1; idx >= 1; --idx)
    {
        const auto& fromChord = chords[idx];
        const auto& toChord   = chords[idx - 1];

        if (fromChord.isEmpty() || toChord.isEmpty())
            continue;

        const float fromZ = fromChord.zPosition;
        const float toZ   = (idx - 1 == 0) ? 0.0f : toChord.zPosition;

        const bool isLatestTransition = (idx == 1);

        // 一般声部は奥へ行くほど繊細に、ベースラインとトライトーン解決は奥でも骨格・緊張の軌跡として存在感を保つ
        const float normalGenDamp = isLatestTransition ? 1.0f : 0.45f;
        const float bassOrTensionGenDamp = isLatestTransition ? 1.0f : 0.80f;

        const auto connections = VoiceLeadingSolver::solve (fromChord, toChord);

        // 1. 一般声部（PivotNote / Stepwise / Converged / TritoneResolution）を先に描画
        for (const auto& conn : connections)
        {
            if (!conn.isBassLine())
            {
                const float damp = conn.isTritoneResolution() ? bassOrTensionGenDamp : normalGenDamp;
                const float fromAlpha = fromChord.alpha * 0.55f * damp;
                const float toAlpha   = ((idx - 1 == 0) ? fgAlpha : (toChord.alpha * 0.65f)) * damp;
                drawSingleVoiceLeadingThread (g, conn, fromZ, toZ, fromAlpha, toAlpha, isLatestTransition);
            }
        }

        // 2. ベースライン（骨格）をその上に重ねて描画（奥に行っても他よりしっかり残る減衰率）
        for (const auto& conn : connections)
        {
            if (conn.isBassLine())
            {
                const float fromAlpha = fromChord.alpha * 0.65f * bassOrTensionGenDamp;
                const float toAlpha   = ((idx - 1 == 0) ? fgAlpha : (toChord.alpha * 0.75f)) * bassOrTensionGenDamp;
                drawSingleVoiceLeadingThread (g, conn, fromZ, toZ, fromAlpha, toAlpha, isLatestTransition);
            }
        }
    }
}

void HarmonyVisualizerAudioProcessorEditor::drawSingleVoiceLeadingThread (
    juce::Graphics& g,
    const VoiceLeadingConnection& conn,
    float fromZ,
    float toZ,
    float fromAlpha,
    float toAlpha,
    bool isLatestTransition)
{
    const juce::Point<float> pFrom = projection.projectCircleIndex (conn.fromCircleIndex, fromZ, 1.0f);
    const juce::Point<float> pTo   = projection.projectCircleIndex (conn.toCircleIndex,   toZ,   1.0f);

    const float midZ = (fromZ + toZ) * 0.5f;
    // ベースラインは奥に行っても細くなりすぎないよう最小スケールを高めに確保
    const float minScale = conn.isBassLine() ? 0.50f : 0.35f;
    const float baseThickness = projection.getProjectedSize (pitchLine_thickness, midZ, minScale);

    float thicknessMultiplier = 1.0f;
    float alphaBoost = 1.0f;
    juce::Colour startColour = pointColour;
    juce::Colour endColour   = pointColour;

    // 他の遷移と同じ色体系（同音保持ならシアン系、移動なら五度圏距離に応じた調和カラー）を算出
    auto resolveMovementColours = [&] (juce::Colour& outStart, juce::Colour& outEnd)
    {
        if (conn.circleDistance == 0)
        {
            outStart = juce::Colour (0xff7dd3fc);
            outEnd   = juce::Colour (0xff38bdf8);
        }
        else
        {
            const int dist = juce::jlimit (1, 6, conn.circleDistance);
            outStart = pitchLine_colours[static_cast<size_t> (dist)];
            outEnd   = outStart.brighter (0.2f);
        }
    };

    switch (conn.type)
    {
        case VoiceLeadingConnection::Type::BassLine:
        {
            // ルート音（ベースライン）の遷移色は他の遷移と同じ色体系に揃え、
            // 手前で色浮きさせず「線の厚み」と「奥での残りやすさ」で進行の骨格を表現する
            thicknessMultiplier = isLatestTransition ? 1.85f : 1.65f;
            alphaBoost = isLatestTransition ? 1.05f : 1.25f;
            resolveMovementColours (startColour, endColour);
            break;
        }
        case VoiceLeadingConnection::Type::PivotNote:
        {
            // 共通音の保持は平行に伸びるクリーンなエレクトリックシアンの光軸
            thicknessMultiplier = isLatestTransition ? 1.15f : 0.85f;
            alphaBoost = 0.90f;
            startColour = juce::Colour (0xff7dd3fc);
            endColour   = juce::Colour (0xff38bdf8);
            break;
        }
        case VoiceLeadingConnection::Type::TritoneResolution:
        {
            // トライトーン解決は緊張色（クリムゾンローズ）から解決色（ミント/シアン）へのグラデーション
            thicknessMultiplier = isLatestTransition ? 1.75f : 1.40f;
            alphaBoost = 1.20f;
            startColour = pitchLine_colours[6]; // クリムゾンローズ（緊張）
            endColour   = pitchLine_colours[1].interpolatedWith (pitchLine_colours[4], 0.45f); // シアン〜ミント（解決）
            break;
        }
        case VoiceLeadingConnection::Type::Stepwise:
        {
            // 半音・全音の近接移動は五度圏距離に応じた鮮やかな調和カラー
            thicknessMultiplier = isLatestTransition ? 1.20f : 0.85f;
            alphaBoost = 1.0f;
            resolveMovementColours (startColour, endColour);
            break;
        }
        case VoiceLeadingConnection::Type::Converged:
        {
            // 合流ラインは少し繊細な線幅で解決先へ吸い込まれるように描画
            thicknessMultiplier = isLatestTransition ? 0.95f : 0.70f;
            alphaBoost = 0.80f;
            const int dist = juce::jlimit (1, 6, conn.circleDistance > 0 ? conn.circleDistance : 2);
            startColour = pitchLine_colours[static_cast<size_t> (dist)];
            endColour   = pointColour.interpolatedWith (startColour, 0.4f);
            break;
        }
    }

    const float finalFromAlpha = juce::jlimit (0.0f, 1.0f, fromAlpha * alphaBoost);
    const float finalToAlpha   = juce::jlimit (0.0f, 1.0f, toAlpha   * alphaBoost);
    const float strokeWidth    = std::max (1.0f, baseThickness * thicknessMultiplier);

    const juce::Line<float> line (pFrom, pTo);

    // =========================================================================
    // 多層ブルーム（Multi-layer Bloom）発光レンダリング
    // 濁りをなくし、透き通った光ファイバー・ネオンのような輝きを生み出す
    // =========================================================================

    // Layer 1: 最外層グロー（広域オーラ）
    const float haloWidth = strokeWidth * (conn.isBassLine() ? 4.0f : 3.6f);
    juce::ColourGradient broadGlowGrad (
        startColour.withMultipliedAlpha (finalFromAlpha * 0.14f), pFrom.x, pFrom.y,
        endColour.withMultipliedAlpha   (finalToAlpha   * 0.18f), pTo.x,   pTo.y,
        false);
    g.setGradientFill (broadGlowGrad);
    g.drawLine (line, haloWidth);

    // Layer 2: 中間層ブルーム（高彩度インナーグロー）
    const float bloomWidth = strokeWidth * (conn.isBassLine() ? 2.2f : 1.9f);
    juce::ColourGradient innerBloomGrad (
        startColour.withMultipliedAlpha (finalFromAlpha * 0.36f), pFrom.x, pFrom.y,
        endColour.withMultipliedAlpha   (finalToAlpha   * 0.44f), pTo.x,   pTo.y,
        false);
    g.setGradientFill (innerBloomGrad);
    g.drawLine (line, bloomWidth);

    // Layer 3: メインの彩度ビーム（カラー光線）
    juce::ColourGradient mainBeamGrad (
        startColour.withMultipliedAlpha (finalFromAlpha * 0.85f), pFrom.x, pFrom.y,
        endColour.withMultipliedAlpha   (finalToAlpha   * 0.95f), pTo.x,   pTo.y,
        false);
    g.setGradientFill (mainBeamGrad);
    g.drawLine (line, strokeWidth);

    // Layer 4: 超極細の中心高エネルギーコア（純白に近い光の芯）
    const float coreWidth = std::max (0.8f, strokeWidth * 0.38f);
    const juce::Colour coreStart = juce::Colours::white.interpolatedWith (startColour, 0.25f);
    const juce::Colour coreEnd   = juce::Colours::white.interpolatedWith (endColour,   0.2f);
    juce::ColourGradient coreGrad (
        coreStart.withMultipliedAlpha (finalFromAlpha * 0.80f), pFrom.x, pFrom.y,
        coreEnd.withMultipliedAlpha   (finalToAlpha   * 0.92f), pTo.x,   pTo.y,
        false);
    g.setGradientFill (coreGrad);
    g.drawLine (line, coreWidth);

    // Layer 5: 端点（音ノードとの接続部）の結合グロー
    if (isLatestTransition)
    {
        const float dotGlowRadius = strokeWidth * 1.5f;
        g.setColour (endColour.withMultipliedAlpha (finalToAlpha * 0.32f));
        g.fillEllipse (pTo.x - dotGlowRadius, pTo.y - dotGlowRadius, dotGlowRadius * 2.0f, dotGlowRadius * 2.0f);
    }
}

void HarmonyVisualizerAudioProcessorEditor::drawForegroundChordLayer (juce::Graphics& g)
{
    const auto& chordState = chordDetector.getCurrentChordState();
    // 離鍵時も最前面コード全体のアルファは 1.0f を維持し、1つ奥の世代より薄くなる逆転現象を防ぐ
    constexpr float alphaMultiplier = 1.0f;

    ChordEvent foregroundChord = ChordEvent::fromMidiNotes (chordState.notes);
    foregroundChord.zPosition = 0.0f;
    foregroundChord.alpha = alphaMultiplier;
    foregroundChord.generation = 0;

    // 最前面のリング・ガイドドット・インターバル多角形・構成音ポイントを描画
    drawChordRingAndPolygon (g, foregroundChord, alphaMultiplier, true);

    // 最前面（Z=0）の外周12音名ラベルと中央の構成音テキストを描画
    const juce::Font regularFont (juce::FontOptions (labelCircle_fontsize, juce::Font::plain));
    const juce::Font boldFont    (juce::FontOptions (labelCircle_fontsize * 1.06f, juce::Font::bold));

    for (int i = 0; i < 12; ++i)
    {
        const auto p = projection.projectCircleIndex (i, 0.0f, 1.1f);
        const bool isInChord  = foregroundChord.hasCircleIndex (i);
        const bool isSounding = chordDetector.isCircleIndexSounding (i);
        const bool isBass     = (i == foregroundChord.bassCircleIndex);

        const juce::Rectangle<float> labelBounds (p.x - labelCircle_size * 0.5f,
                                                 p.y - labelCircle_size * 0.5f,
                                                 labelCircle_size,
                                                 labelCircle_size);

        if (isSounding)
        {
            // 現在押鍵中（またはペダル保持中）のアクティブ音：ネオングロー＋太字ハイライト
            const juce::Colour glowBase = isBass ? pointColour_root : juce::Colour (0xff38bdf8);

            // 外周ソフトネオングロー（ラジアルグラデーション）
            const float glowRadius = labelCircle_size * 0.62f;
            juce::ColourGradient labelGlow (
                glowBase.withAlpha (0.36f), p.x, p.y,
                glowBase.withAlpha (0.0f),  p.x + glowRadius, p.y,
                true);
            labelGlow.addColour (0.45, glowBase.withAlpha (0.16f));
            g.setGradientFill (labelGlow);
            g.fillEllipse (p.x - glowRadius, p.y - glowRadius, glowRadius * 2.0f, glowRadius * 2.0f);

            // リング上の頂点ポイントにも発音中の光彩オーラを付与
            const auto ringPt = projection.projectCircleIndex (i, 0.0f, 1.0f);
            const float ptHaloRadius = pitchPoint_radius * 2.6f;
            juce::ColourGradient ptGlow (
                glowBase.withAlpha (0.45f), ringPt.x, ringPt.y,
                glowBase.withAlpha (0.0f),  ringPt.x + ptHaloRadius, ringPt.y,
                true);
            g.setGradientFill (ptGlow);
            g.fillEllipse (ringPt.x - ptHaloRadius, ringPt.y - ptHaloRadius, ptHaloRadius * 2.0f, ptHaloRadius * 2.0f);

            g.setFont (boldFont);
            g.setColour (juce::Colours::white);
            g.drawText (circleNoteNames[i], labelBounds, juce::Justification::centred);
        }
        else
        {
            // 離鍵後の確定構成音はクリーンな通常ハイライト（発光なし）、非構成音は控えめなグレー
            g.setFont (regularFont);
            const juce::Colour textColour = isInChord ? pointColour.withAlpha (0.88f) : inactiveNoteColour;
            g.setColour (textColour);
            g.drawText (circleNoteNames[i], labelBounds, juce::Justification::centred);
        }
    }

    // 中央の構成音テキスト表示
    juce::String pitches;
    for (int pitchIdx : foregroundChord.circleIndices)
    {
        pitches << circleNoteNames[pitchIdx] << ' ';
    }

    g.setColour (centerTextColour.withMultipliedAlpha (alphaMultiplier));
    g.setFont (labelCentre_fontsize);

    const juce::Rectangle<float> centerRect (circleCenter.x - circleRadius,
                                             circleCenter.y - labelCentre_height * 0.5f,
                                             circleDiameter,
                                             labelCentre_height);
    g.drawText (pitches.trim(), centerRect, juce::Justification::centred);
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
        const float guideDotDiameter = guideDotRadius * 2.0f;
        for (int i = 0; i < 12; ++i)
        {
            if (!chord.hasCircleIndex (i))
            {
                const auto p = projection.projectCircleIndex (i, 0.0f, 1.0f);
                g.setColour (inactiveDotColour);
                g.fillEllipse (p.x - guideDotRadius, p.y - guideDotRadius, guideDotDiameter, guideDotDiameter);
            }
        }
    }

    if (chord.circleIndices.empty())
        return;

    const float defaultLineThickness = projection.getProjectedSize (pitchLine_thickness, z, 0.3f);
    const float ptRadius = projection.getProjectedSize (pitchPoint_radius, z, 0.35f);
    const float ptDiameter = ptRadius * 2.0f;

    // 3. 構成音間のインターバル線と頂点ポイントを透視投影座標で描画
    // 奥に行った過去コードでも、トライトーン（dist=6）や短2度（dist=5）などの緊張音程は
    // 安定音程より減衰を抑え、奥でも明瞭に目立たせる
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
            const juce::Line<float> intervalLine (startPoint, endPoint);

            float lineAlpha = effectiveAlpha;
            float lineThickness = defaultLineThickness;

            if (!isForeground)
            {
                if (dist == 6) // トライトーン（最大緊張）: 奥に行っても強く残し、グローも付与
                {
                    lineAlpha = juce::jlimit (0.0f, 1.0f, effectiveAlpha * 1.6f);
                    lineThickness = projection.getProjectedSize (pitchLine_thickness * 1.5f, z, 0.45f);

                    // トライトーン用のソフトな発光グロー
                    g.setColour (pitchLine_colours[6].withMultipliedAlpha (lineAlpha * 0.35f));
                    g.drawLine (intervalLine, lineThickness * 2.4f);
                }
                else if (dist == 5) // 半音緊張（短2度/長7度）: やや粘り強く残す
                {
                    lineAlpha = juce::jlimit (0.0f, 1.0f, effectiveAlpha * 1.3f);
                    lineThickness = projection.getProjectedSize (pitchLine_thickness * 1.25f, z, 0.40f);
                }
                else // 安定音程（完全5度/4度、長短3度/6度など）: 奥へ行くほどスッとフェードアウト
                {
                    lineAlpha = effectiveAlpha * 0.55f;
                }
            }

            g.setColour (getIntervalColour (dist, lineAlpha));
            g.drawLine (intervalLine, lineThickness);
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
    
    labelCircle_size = 0.15f * circleRadius;
    labelCircle_fontsize = 0.1f * circleRadius;
    
    pitchLine_thickness = 0.01f * circleRadius;
    
    pitchPoint_radius = 0.02f * circleRadius;
    pitchPoint_diameter = 2.0f * pitchPoint_radius;
    
    labelCentre_height = 0.6f * circleRadius;
    labelCentre_fontsize = 0.1f * circleRadius;
}
