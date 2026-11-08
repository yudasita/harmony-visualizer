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

    // 後踏みペダリング（シンコペーション・ペダル）による残響音パージ時の最新コード更新コールバック
    chordDetector.setUpdateChordCallback ([this] (const std::vector<int>& notes)
    {
        chordHistory.updateLatestChord (ChordEvent::fromMidiNotes (notes));
    });

    auto configureSlider = [this] (juce::Slider& slider,
                                   double minVal,
                                   double maxVal,
                                   double stepVal,
                                   const juce::String& suffix)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setRange (minVal, maxVal, stepVal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
        slider.setTextValueSuffix (suffix);
        slider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff232a3b));
        slider.setColour (juce::Slider::trackColourId, juce::Colour (0xff38bdf8));
        slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xfff1f5f9));
        slider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe2e8f0));
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff141824));
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff2e374d));
        addChildComponent (slider);
    };

    configureSlider (historySizeSlider, 2.0, 16.0, 1.0, " chords");
    historySizeSlider.onValueChange = [this]()
    {
        const int newSize = static_cast<int> (std::round (historySizeSlider.getValue()));
        audioProcessor.setMaxHistorySize (newSize);
        chordHistory.setMaxHistorySize (static_cast<size_t> (newSize));
        lastSyncedSettingsRevision = audioProcessor.getSettingsRevision();
    };

    configureSlider (quantizeWindowSlider, 15.0, 200.0, 5.0, " ms");
    quantizeWindowSlider.onValueChange = [this]()
    {
        const double newMs = quantizeWindowSlider.getValue();
        audioProcessor.setQuantizeWindowMs (newMs);
        chordDetector.setQuantizeWindowMs (newMs);
        lastSyncedSettingsRevision = audioProcessor.getSettingsRevision();
    };

    configureSlider (arpeggioThresholdSlider, 100.0, 1200.0, 25.0, " ms");
    arpeggioThresholdSlider.onValueChange = [this]()
    {
        const double newMs = arpeggioThresholdSlider.getValue();
        audioProcessor.setArpeggioThresholdMs (newMs);
        chordDetector.setArpeggioThresholdMs (newMs);
        lastSyncedSettingsRevision = audioProcessor.getSettingsRevision();
    };

    resetSettingsButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff242c3d));
    resetSettingsButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff334155));
    resetSettingsButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcbd5e1));
    resetSettingsButton.onClick = [this]()
    {
        audioProcessor.resetSettingsToDefaults();
        syncSettingsFromProcessor();
    };
    addAndMakeVisible (resetSettingsButton);
    resetSettingsButton.setVisible (false);

    syncSettingsFromProcessor();

    startTimerHz (60);
    setResizable (true, false);
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

    if (audioProcessor.getSettingsRevision() != lastSyncedSettingsRevision)
    {
        syncSettingsFromProcessor();
    }

    // ロックフリーFIFOから未読イベントを取り出してコード境界判定エンジンへ渡す
    audioProcessor.popMidiEvents ([this, currentTimeMs] (const QueuedMidiEvent& event)
    {
        chordDetector.processMidiEvent (event, currentTimeMs);
    });

    chordDetector.update (currentTimeMs);

    // コード履歴キューのアニメーション更新
    chordHistory.updateAnimation (0.2f);

    // 設定ドロワーの表示・非表示アニメーション更新
    const float targetModalAlpha = isSettingsModalOpen ? 1.0f : 0.0f;
    if (std::abs (settingsModalAlpha - targetModalAlpha) > 0.004f)
    {
        settingsModalAlpha += (targetModalAlpha - settingsModalAlpha) * 0.26f;
        updateSettingsModalLayout();
    }
    else if (settingsModalAlpha != targetModalAlpha)
    {
        settingsModalAlpha = targetModalAlpha;
        updateSettingsModalLayout();
    }

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

    // 右上の設定アイコン（歯車ボタン）と右端サイドドロワーの描画
    drawSettingsGearButton (g);
    if (settingsModalAlpha > 0.005f)
    {
        drawSettingsModalOverlay (g);
    }
}

void HarmonyVisualizerAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    juce::ignoreUnused (g);
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
    // 最前面（Z=0）との視覚的セパレーション（段差）を明確にするため、過去レイヤーの基準アルファを抑制
    for (size_t idx = chords.size() - 1; idx >= 1; --idx)
    {
        const auto& pastChord = chords[idx];
        if (pastChord.isEmpty())
            continue;

        const float layerAlpha = pastChord.alpha * 0.48f;
        drawChordRingAndPolygon (g, pastChord, layerAlpha, false);
    }
}

void HarmonyVisualizerAudioProcessorEditor::drawVoiceLeadingConnections (juce::Graphics& g)
{
    const auto& chords = chordHistory.getHistory();
    if (chords.size() <= 1)
        return;

    // 手前側（Z=0）では最前面コードの多角形・頂点ノードが主役として際立つよう、
    // ボイスリーディング線の終端アルファを適度に抑えて「頂点の背後へ自然に潜り込む」バランスにする
    constexpr float fgAlpha = 0.72f;

    // コード履歴全体のノート構成スナップショットを比較し、変化があった場合のみ複数世代ソルバーを再計算
    bool cacheNeedsUpdate = (voiceLeadingCache.chordNotesSnapshot.size() != chords.size());
    if (!cacheNeedsUpdate)
    {
        for (size_t i = 0; i < chords.size(); ++i)
        {
            if (voiceLeadingCache.chordNotesSnapshot[i] != chords[i].midiNotes)
            {
                cacheNeedsUpdate = true;
                break;
            }
        }
    }

    if (cacheNeedsUpdate)
    {
        voiceLeadingCache.chordNotesSnapshot.clear();
        voiceLeadingCache.chordNotesSnapshot.reserve (chords.size());
        for (const auto& ch : chords)
            voiceLeadingCache.chordNotesSnapshot.push_back (ch.midiNotes);

        voiceLeadingCache.connections = VoiceLeadingSolver::solveMultiGeneration (chords);
    }

    const auto& connections = voiceLeadingCache.connections;

    // 1. 一般声部（PivotNote / Stepwise / Converged / TritoneResolution）を先に描画
    for (const auto& conn : connections)
    {
        if (conn.isBassLine())
            continue;

        const size_t fromIdx = static_cast<size_t> (conn.fromGenerationIndex);
        const size_t toIdx   = static_cast<size_t> (conn.toGenerationIndex);
        if (fromIdx >= chords.size() || toIdx >= chords.size())
            continue;

        const auto& fromChord = chords[fromIdx];
        const auto& toChord   = chords[toIdx];

        if (fromChord.isEmpty() || toChord.isEmpty())
            continue;

        const float fromZ = fromChord.zPosition;
        const float toZ   = (toIdx == 0) ? 0.0f : toChord.zPosition;
        const bool isLatestTransition = (toIdx == 0);

        // 一般声部は奥へ行くほど繊細に、トライトーン解決は奥でも緊張の軌跡として存在感を保つ
        const float normalGenDamp = isLatestTransition ? 0.88f : (conn.isSkippedGeneration() ? 0.38f : 0.44f);
        const float tensionGenDamp = isLatestTransition ? 0.92f : 0.72f;
        const float damp = conn.isTritoneResolution() ? tensionGenDamp : normalGenDamp;

        const float fromAlpha = fromChord.alpha * 0.52f * damp;
        const float toAlpha   = ((toIdx == 0) ? (fgAlpha * 0.85f) : (toChord.alpha * 0.58f)) * damp;
        drawSingleVoiceLeadingThread (g, conn, fromZ, toZ, fromAlpha, toAlpha, isLatestTransition);
    }

    // 2. ベースライン（骨格）をその上に重ねて描画（奥に行っても他よりしっかり残る減衰率）
    for (const auto& conn : connections)
    {
        if (!conn.isBassLine())
            continue;

        const size_t fromIdx = static_cast<size_t> (conn.fromGenerationIndex);
        const size_t toIdx   = static_cast<size_t> (conn.toGenerationIndex);
        if (fromIdx >= chords.size() || toIdx >= chords.size())
            continue;

        const auto& fromChord = chords[fromIdx];
        const auto& toChord   = chords[toIdx];

        if (fromChord.isEmpty() || toChord.isEmpty())
            continue;

        const float fromZ = fromChord.zPosition;
        const float toZ   = (toIdx == 0) ? 0.0f : toChord.zPosition;
        const bool isLatestTransition = (toIdx == 0);

        const float bassGenDamp = isLatestTransition ? 0.92f : (conn.isSkippedGeneration() ? 0.58f : 0.72f);
        const float fromAlpha = fromChord.alpha * 0.62f * bassGenDamp;
        const float toAlpha   = ((toIdx == 0) ? (fgAlpha * 0.95f) : (toChord.alpha * 0.68f)) * bassGenDamp;
        drawSingleVoiceLeadingThread (g, conn, fromZ, toZ, fromAlpha, toAlpha, isLatestTransition);
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
    const float minScale = conn.isBassLine() ? 0.48f : 0.34f;
    const float baseThickness = projection.getProjectedSize (pitchLine_thickness, midZ, minScale);

    float thicknessMultiplier = 1.0f;
    float alphaBoost = 1.0f;
    float aerialResistance = 0.0f;
    juce::Colour rawStartColour = pointColour;
    juce::Colour rawEndColour   = pointColour;

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
            outEnd   = outStart.brighter (0.16f);
        }
    };

    switch (conn.type)
    {
        case VoiceLeadingConnection::Type::BassLine:
        {
            // ルート音（ベースライン）の遷移色は他の遷移と同じ色体系に揃え、
            // 「線の厚み」と「空気遠近への減衰耐性」で進行の骨格を表現する（最前面多角形を食わない太さに調整）
            thicknessMultiplier = isLatestTransition ? 1.42f : 1.30f;
            alphaBoost = isLatestTransition ? 0.92f : 1.08f;
            aerialResistance = 0.36f;
            resolveMovementColours (rawStartColour, rawEndColour);
            break;
        }
        case VoiceLeadingConnection::Type::PivotNote:
        {
            // 共通音の保持は平行に伸びるクリーンなエレクトリックシアンの光軸
            thicknessMultiplier = isLatestTransition ? 0.95f : 0.75f;
            alphaBoost = 0.78f;
            aerialResistance = 0.10f;
            rawStartColour = juce::Colour (0xff7dd3fc);
            rawEndColour   = juce::Colour (0xff38bdf8);
            break;
        }
        case VoiceLeadingConnection::Type::TritoneResolution:
        {
            // トライトーン解決は緊張色（クリムゾンローズ）から解決色（ミント/シアン）へのグラデーション
            // 霧の奥でも解決の軌跡が微かに発光し続けるよう高い空気遠近耐性を持たせる
            thicknessMultiplier = isLatestTransition ? 1.35f : 1.15f;
            alphaBoost = 0.98f;
            aerialResistance = 0.58f;
            rawStartColour = pitchLine_colours[6]; // クリムゾンローズ（緊張）
            rawEndColour   = pitchLine_colours[1].interpolatedWith (pitchLine_colours[4], 0.45f); // シアン〜ミント（解決）
            break;
        }
        case VoiceLeadingConnection::Type::Stepwise:
        {
            // 半音・全音の近接移動は五度圏距離に応じた鮮やかな調和カラー
            thicknessMultiplier = isLatestTransition ? 1.00f : 0.78f;
            alphaBoost = 0.86f;
            aerialResistance = (conn.circleDistance >= 5) ? 0.28f : 0.0f;
            resolveMovementColours (rawStartColour, rawEndColour);
            break;
        }
        case VoiceLeadingConnection::Type::Converged:
        {
            // 合流ラインは少し繊細な線幅で解決先へ吸い込まれるように描画
            thicknessMultiplier = isLatestTransition ? 0.82f : 0.62f;
            alphaBoost = 0.72f;
            aerialResistance = 0.0f;
            const int dist = juce::jlimit (1, 6, conn.circleDistance > 0 ? conn.circleDistance : 2);
            rawStartColour = pitchLine_colours[static_cast<size_t> (dist)];
            rawEndColour   = pointColour.interpolatedWith (rawStartColour, 0.4f);
            break;
        }
    }

    // 世代スキップ接続（span >= 2）は中間層の視認性を損なわないよう、やや繊細で洗練された線幅にする
    if (conn.isSkippedGeneration())
    {
        thicknessMultiplier *= (conn.generationSpan == 2) ? 0.88f : 0.80f;
        aerialResistance = std::max (aerialResistance, 0.22f);
    }

    const float finalFromAlpha = juce::jlimit (0.0f, 1.0f, fromAlpha * alphaBoost);
    const float finalToAlpha   = juce::jlimit (0.0f, 1.0f, toAlpha   * alphaBoost);
    const float strokeWidth    = std::max (0.9f, baseThickness * thicknessMultiplier);

    // 始点（奥 fromZ）と終点（手前 toZ）それぞれに深度に応じた空気遠近法（彩度低下＋背景スレートブレンド）を適用
    const juce::Colour startColour = applyAerialPerspective (rawStartColour, fromZ, 1.0f, aerialResistance);
    const juce::Colour endColour   = applyAerialPerspective (rawEndColour,   toZ,   1.0f, aerialResistance);

    const juce::Line<float> line (pFrom, pTo);

    // =========================================================================
    // 多層ブルーム（Multi-layer Bloom）＋ 被写界深度（Depth of Field）レンダリング
    // 奥ほどシャープな芯（Core）を溶かし、太く柔らかい拡散光（Halo）の比率を高めてレンズボケを表現する。
    // 手前（toZ=0）側では最前面コード多角形の背後へ自然に潜り込むよう、終端輝度とブルーム広がりを抑制する。
    // =========================================================================
    const float fromBlur = getDepthOfFieldBlurFactor (fromZ);
    const float toBlur   = getDepthOfFieldBlurFactor (toZ);
    const float avgBlur  = 0.5f * (fromBlur + toBlur);

    // Layer 1: 最外層ソフトブラー（奥ほどボケ半径が広がり、柔らかい拡散光になる）
    const float haloWidth = strokeWidth * (conn.isBassLine() ? 3.4f : 3.0f) * (1.0f + avgBlur * 0.65f);
    juce::ColourGradient broadGlowGrad (
        startColour.withMultipliedAlpha (finalFromAlpha * (0.13f + fromBlur * 0.13f)), pFrom.x, pFrom.y,
        endColour.withMultipliedAlpha   (finalToAlpha   * (0.12f + toBlur   * 0.10f)), pTo.x,   pTo.y,
        false);
    g.setGradientFill (broadGlowGrad);
    g.drawLine (line, haloWidth);

    // Layer 2: 中間層ソフトガウスローブ（ボケの中核を担う柔らかい光帯）
    const float bloomWidth = strokeWidth * (conn.isBassLine() ? 1.9f : 1.65f) * (1.0f + avgBlur * 0.40f);
    juce::ColourGradient innerBloomGrad (
        startColour.withMultipliedAlpha (finalFromAlpha * (0.32f + fromBlur * 0.10f)), pFrom.x, pFrom.y,
        endColour.withMultipliedAlpha   (finalToAlpha   * (0.30f + toBlur   * 0.08f)), pTo.x,   pTo.y,
        false);
    g.setGradientFill (innerBloomGrad);
    g.drawLine (line, bloomWidth);

    // Layer 3: メインのカラー光線ビーム（手前終端では最前面コードに主役を譲るよう適度に収束）
    juce::ColourGradient mainBeamGrad (
        startColour.withMultipliedAlpha (finalFromAlpha * (0.80f * (1.0f - fromBlur * 0.45f))), pFrom.x, pFrom.y,
        endColour.withMultipliedAlpha   (finalToAlpha   * (0.74f * (1.0f - toBlur   * 0.45f))), pTo.x,   pTo.y,
        false);
    g.setGradientFill (mainBeamGrad);
    g.drawLine (line, strokeWidth * (1.0f + avgBlur * 0.15f));

    // Layer 4: 中心コア（色味を残した上品な芯線とし、純白ハイライトは最前面コード多角形に譲る）
    const float coreWidth = std::max (0.75f, strokeWidth * 0.34f);
    const float fromFog = getAerialFogFactor (fromZ, aerialResistance);
    const float toFog   = getAerialFogFactor (toZ,   aerialResistance);
    const float fromCoreSharpness = std::max (0.04f, 1.0f - fromBlur * 0.88f);
    const float toCoreSharpness   = std::max (0.04f, 1.0f - toBlur   * 0.88f);

    const juce::Colour coreStart = applyAerialPerspective (
        juce::Colours::white.interpolatedWith (rawStartColour, 0.42f + fromFog * 0.45f),
        fromZ, finalFromAlpha * 0.65f * fromCoreSharpness, aerialResistance);
    const juce::Colour coreEnd = applyAerialPerspective (
        juce::Colours::white.interpolatedWith (rawEndColour, 0.45f + toFog * 0.45f),
        toZ, finalToAlpha * 0.58f * toCoreSharpness, aerialResistance);

    juce::ColourGradient coreGrad (
        coreStart, pFrom.x, pFrom.y,
        coreEnd,   pTo.x,   pTo.y,
        false);
    g.setGradientFill (coreGrad);
    g.drawLine (line, coreWidth);

    // Layer 5: 端点（音ノードとの接続部）の控えめな収束グロー（最前面頂点ハロの背後に馴染ませる）
    if (isLatestTransition)
    {
        const float dotGlowRadius = strokeWidth * 1.15f;
        g.setColour (endColour.withMultipliedAlpha (finalToAlpha * 0.16f));
        g.fillEllipse (pTo.x - dotGlowRadius, pTo.y - dotGlowRadius, dotGlowRadius * 2.0f, dotGlowRadius * 2.0f);
    }
}

void HarmonyVisualizerAudioProcessorEditor::drawForegroundChordLayer (juce::Graphics& g)
{
    const auto& chordState = chordDetector.getCurrentChordState();
    // 離鍵時も最前面コード全体のアルファは 1.0f を維持し、1つ奥の世代より薄くなる逆転現象を防ぐ
    constexpr float alphaMultiplier = 1.0f;

    if (cachedForegroundNotes != chordState.notes)
    {
        cachedForegroundNotes = chordState.notes;
        cachedForegroundChord = ChordEvent::fromMidiNotes (cachedForegroundNotes);
    }

    cachedForegroundChord.zPosition = 0.0f;
    cachedForegroundChord.alpha = alphaMultiplier;
    cachedForegroundChord.generation = 0;
    const auto& foregroundChord = cachedForegroundChord;

    // 最前面のリング・ガイドドット・インターバル多角形・構成音ポイントを描画
    drawChordRingAndPolygon (g, foregroundChord, alphaMultiplier, true);

    // 最前面（Z=0）の外周12音名ラベルを描画（中央テキストは廃止し3Dタイムトンネルの消失点視認性を確保）
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
            const float ptHaloRadius = pitchPoint_radius * 2.8f;
            juce::ColourGradient ptGlow (
                glowBase.withAlpha (0.48f), ringPt.x, ringPt.y,
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
            const juce::Colour textColour = isInChord ? pointColour.withAlpha (0.92f) : inactiveNoteColour;
            g.setColour (textColour);
            g.drawText (circleNoteNames[i], labelBounds, juce::Justification::centred);
        }
    }
}

void HarmonyVisualizerAudioProcessorEditor::drawChordRingAndPolygon (
    juce::Graphics& g,
    const ChordEvent& chord,
    float effectiveAlpha,
    bool isForeground)
{
    const float z = chord.zPosition;
    const float dofBlur = isForeground ? 0.0f : getDepthOfFieldBlurFactor (z);
    const float projectedRadius = projection.getProjectedRadius (z);
    const float projectedDiameter = projectedRadius * 2.0f;
    const float ringThickness = projection.getProjectedSize (circleThickness, z, 0.35f);

    // 1. 深度 Z における五度圏リング（最前面は明度・リムハイライトを高め、奥のリングほど柔らかくボケて霞ませる）
    if (isForeground)
    {
        // 最前面（Z=0）リングのソフトアウターグローと高コントラストなメインリム
        g.setColour (circleColour.brighter (0.35f).withAlpha (0.26f * effectiveAlpha));
        g.drawEllipse (circleCenter.x - projectedRadius,
                       circleCenter.y - projectedRadius,
                       projectedDiameter,
                       projectedDiameter,
                       ringThickness * 2.4f);

        g.setColour (circleColour.brighter (0.28f).withAlpha (effectiveAlpha));
        g.drawEllipse (circleCenter.x - projectedRadius,
                       circleCenter.y - projectedRadius,
                       projectedDiameter,
                       projectedDiameter,
                       ringThickness * 1.25f);

        g.setColour (juce::Colours::white.withAlpha (0.12f * effectiveAlpha));
        g.drawEllipse (circleCenter.x - projectedRadius,
                       circleCenter.y - projectedRadius,
                       projectedDiameter,
                       projectedDiameter,
                       ringThickness * 0.55f);
    }
    else
    {
        const float ringAlpha = juce::jlimit (0.08f, 0.52f, effectiveAlpha * 0.68f);
        if (dofBlur > 0.05f)
        {
            const juce::Colour ringBlurCol = applyAerialPerspective (
                circleColour, z, ringAlpha * 0.34f * dofBlur, 0.08f);
            g.setColour (ringBlurCol);
            g.drawEllipse (circleCenter.x - projectedRadius,
                           circleCenter.y - projectedRadius,
                           projectedDiameter,
                           projectedDiameter,
                           ringThickness * (1.0f + dofBlur * 1.9f));
        }

        const float ringCoreAlpha = ringAlpha * (1.0f - dofBlur * 0.52f);
        const juce::Colour ringCol = applyAerialPerspective (circleColour, z, ringCoreAlpha, 0.08f);
        g.setColour (ringCol);
        g.drawEllipse (circleCenter.x - projectedRadius,
                       circleCenter.y - projectedRadius,
                       projectedDiameter,
                       projectedDiameter,
                       ringThickness);
    }

    // 2. 最前面のみ12音位置の控えめなガイドドットを描画
    if (isForeground)
    {
        const float guideDotRadius = pitchPoint_radius * 0.38f;
        const float guideDotDiameter = guideDotRadius * 2.0f;
        for (int i = 0; i < 12; ++i)
        {
            if (!chord.hasCircleIndex (i))
            {
                const auto p = projection.projectCircleIndex (i, 0.0f, 1.0f);
                g.setColour (inactiveDotColour.brighter (0.14f));
                g.fillEllipse (p.x - guideDotRadius, p.y - guideDotRadius, guideDotDiameter, guideDotDiameter);
            }
        }
    }

    if (chord.circleIndices.empty())
        return;

    const float defaultLineThickness = projection.getProjectedSize (pitchLine_thickness, z, 0.3f);
    const float ptRadius = projection.getProjectedSize (pitchPoint_radius, z, 0.35f);
    const float ptDiameter = ptRadius * 2.0f;

    // 3. 構成音間のインターバル線を透視投影座標＋空気遠近法で描画
    // 最前面（Z=0）はアウターブルーム＋高輝度コアハイライトで立体空間の主役として鮮やかに発光させる。
    // 奥の世代（Z>=1）は安定音程（dist=1..4）ほど霞んで背景に溶け込み、緊張音程（dist=5,6）のみ微かに残響発光させる。
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

            const juce::Colour baseIntervalCol = (dist >= 1 && dist <= 6)
                ? pitchLine_colours[static_cast<size_t> (dist)]
                : juce::Colours::white;

            if (isForeground)
            {
                const float fgLineScale = (dist == 6) ? 1.42f : ((dist == 5) ? 1.30f : 1.22f);
                const float fgLineThickness = defaultLineThickness * fgLineScale;

                // Layer A: 最前面インターバル線のソフトアウターブルーム
                g.setColour (baseIntervalCol.withMultipliedAlpha (effectiveAlpha * 0.24f));
                g.drawLine (intervalLine, fgLineThickness * 3.4f);

                // Layer B: 中間グロウ層（色彩の彩度・発光感を支える）
                g.setColour (baseIntervalCol.brighter (0.15f).withMultipliedAlpha (effectiveAlpha * 0.48f));
                g.drawLine (intervalLine, fgLineThickness * 1.95f);

                // Layer C: 鮮やかなメインインターバル線
                g.setColour (baseIntervalCol.brighter (0.12f).withMultipliedAlpha (effectiveAlpha * 0.96f));
                g.drawLine (intervalLine, fgLineThickness);

                // Layer D: 高輝度コアハイライト（白〜インターバル色のシャープな芯線）
                const float fgCoreWidth = std::max (0.9f, fgLineThickness * 0.42f);
                const juce::Colour fgCoreCol = juce::Colours::white
                    .interpolatedWith (baseIntervalCol, 0.32f)
                    .withMultipliedAlpha (effectiveAlpha * 0.88f);
                g.setColour (fgCoreCol);
                g.drawLine (intervalLine, fgCoreWidth);
            }
            else
            {
                float lineAlpha = effectiveAlpha;
                float lineThickness = defaultLineThickness;
                float aerialResistance = 0.0f;

                if (dist == 6) // トライトーン（最大緊張）: ペアごとに未解決なら空気遠近耐性と発光を持続、解決済みなら落ち着かせる
                {
                    const bool pairResolved = chord.isTritonePairResolved (idxA, idxB);
                    if (!pairResolved)
                    {
                        lineAlpha = juce::jlimit (0.0f, 1.0f, effectiveAlpha * 1.38f);
                        lineThickness = projection.getProjectedSize (pitchLine_thickness * 1.35f, z, 0.40f);
                        aerialResistance = 0.58f;
                    }
                    else
                    {
                        // 既に解決したトライトーンペアは発光を消し、安定音程に近い落ち着いた輝度へ減衰
                        lineAlpha = effectiveAlpha * 0.58f;
                        lineThickness = projection.getProjectedSize (pitchLine_thickness * 1.05f, z, 0.35f);
                        aerialResistance = 0.12f;
                    }
                }
                else if (dist == 5) // 半音緊張（短2度/長7度）: 中程度の空気遠近耐性
                {
                    lineAlpha = juce::jlimit (0.0f, 1.0f, effectiveAlpha * 1.15f);
                    lineThickness = projection.getProjectedSize (pitchLine_thickness * 1.18f, z, 0.38f);
                    aerialResistance = 0.30f;
                }
                else // 安定音程（完全5度/4度、長短3度/6度など）: 空気遠近で奥の霧へ滑らかに減衰
                {
                    lineAlpha = effectiveAlpha * 0.52f;
                    aerialResistance = 0.0f;
                }

                // 被写界深度（DoF）ソフトガウスブラー層：奥の線ほど太く柔らかい光帯として滲ませる
                const float outerBlurWidth = lineThickness * (1.8f + dofBlur * 2.2f);
                const float outerBlurAlpha = lineAlpha * ((dist == 6) ? 0.28f : 0.18f) * (0.35f + dofBlur * 0.65f);
                g.setColour (applyAerialPerspective (baseIntervalCol, z, outerBlurAlpha, aerialResistance));
                g.drawLine (intervalLine, outerBlurWidth);

                const float midBlurWidth = lineThickness * (1.2f + dofBlur * 0.95f);
                const float midBlurAlpha = lineAlpha * 0.36f * (0.4f + dofBlur * 0.6f);
                g.setColour (applyAerialPerspective (baseIntervalCol, z, midBlurAlpha, aerialResistance));
                g.drawLine (intervalLine, midBlurWidth);

                // 奥の世代の中心芯線：奥に行くほど芯のアルファを落としてボケに溶かす
                const float coreLineAlpha = lineAlpha * (1.0f - dofBlur * 0.65f);
                g.setColour (applyAerialPerspective (baseIntervalCol, z, coreLineAlpha, aerialResistance));
                g.drawLine (intervalLine, lineThickness);
            }
        }
    }

    // 4. 構成音ポイント（頂点ノード）をインターバル線の上に重ねて描画
    for (size_t i = 0; i < chord.circleIndices.size(); ++i)
    {
        const int idxA = chord.circleIndices[i];
        const juce::Point<float> startPoint = projection.projectCircleIndex (idxA, z, 1.0f);
        const bool isRoot = (idxA == chord.bassCircleIndex);
        const juce::Colour rawPtColour = isRoot ? pointColour_root : pointColour;
        const float ptResistance = isRoot ? 0.35f : 0.0f;

        if (isForeground)
        {
            // 最前面（Z=0）の構成音頂点ノード：ソフトな光彩ハロ＋鮮やかな本体＋高輝度コアで
            // ボイスリーディング線の端点に埋もれずくっきりと最前面に浮き立たせる
            const float haloRadius = ptRadius * (isRoot ? 2.75f : 2.25f);
            const juce::Colour haloCol = isRoot
                ? pointColour_root
                : juce::Colour (0xff38bdf8).interpolatedWith (pointColour, 0.45f);

            juce::ColourGradient ptHalo (
                haloCol.withAlpha (effectiveAlpha * (isRoot ? 0.46f : 0.36f)), startPoint.x, startPoint.y,
                haloCol.withAlpha (0.0f), startPoint.x + haloRadius, startPoint.y,
                true);
            ptHalo.addColour (0.48, haloCol.withAlpha (effectiveAlpha * (isRoot ? 0.18f : 0.13f)));
            g.setGradientFill (ptHalo);
            g.fillEllipse (startPoint.x - haloRadius, startPoint.y - haloRadius, haloRadius * 2.0f, haloRadius * 2.0f);

            const float fgPtRadius = ptRadius * (isRoot ? 1.26f : 1.14f);
            g.setColour (rawPtColour.withMultipliedAlpha (effectiveAlpha));
            g.fillEllipse (startPoint.x - fgPtRadius, startPoint.y - fgPtRadius, fgPtRadius * 2.0f, fgPtRadius * 2.0f);

            const float coreRadius = fgPtRadius * (isRoot ? 0.48f : 0.52f);
            const juce::Colour coreCol = isRoot
                ? juce::Colours::white.interpolatedWith (pointColour_root, 0.22f)
                : juce::Colours::white;
            g.setColour (coreCol.withMultipliedAlpha (effectiveAlpha * 0.95f));
            g.fillEllipse (startPoint.x - coreRadius, startPoint.y - coreRadius, coreRadius * 2.0f, coreRadius * 2.0f);
        }
        else
        {
            if (dofBlur > 0.05f)
            {
                // 奥の頂点ポイントの玉ボケ（Bokeh）ハロー
                const float bokehRadius = ptRadius * (1.35f + dofBlur * 1.15f);
                const juce::Colour bokehCol = applyAerialPerspective (
                    rawPtColour, z, effectiveAlpha * 0.32f * dofBlur, ptResistance);
                g.setColour (bokehCol);
                g.fillEllipse (startPoint.x - bokehRadius, startPoint.y - bokehRadius, bokehRadius * 2.0f, bokehRadius * 2.0f);
            }

            const float ptCoreAlpha = effectiveAlpha * (1.0f - dofBlur * 0.52f);
            const juce::Colour finalPtColour = applyAerialPerspective (rawPtColour, z, ptCoreAlpha, ptResistance);
            g.setColour (finalPtColour);
            g.fillEllipse (startPoint.x - ptRadius, startPoint.y - ptRadius, ptDiameter, ptDiameter);
        }
    }
}

void HarmonyVisualizerAudioProcessorEditor::resized()
{
    drawingArea = getLocalBounds().toFloat().reduced (0.0f);
    
    // 五度圏の中央配置は常に維持（サイドドロワー表示時もメイン領域のシフトは行わない）
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

    // 右上の歯車アイコンボタン領域
    constexpr float gearBtnSize = 28.0f;
    constexpr float gearMargin = 10.0f;
    gearButtonBounds = juce::Rectangle<float> (static_cast<float> (getWidth()) - gearBtnSize - gearMargin,
                                               gearMargin,
                                               gearBtnSize,
                                               gearBtnSize);

    updateSettingsModalLayout();
}

void HarmonyVisualizerAudioProcessorEditor::syncSettingsFromProcessor()
{
    const int historySize = audioProcessor.getMaxHistorySize();
    const double quantizeMs = audioProcessor.getQuantizeWindowMs();
    const double arpeggioMs = audioProcessor.getArpeggioThresholdMs();

    chordHistory.setMaxHistorySize (static_cast<size_t> (historySize));
    chordDetector.setQuantizeWindowMs (quantizeMs);
    chordDetector.setArpeggioThresholdMs (arpeggioMs);

    historySizeSlider.setValue (static_cast<double> (historySize), juce::dontSendNotification);
    quantizeWindowSlider.setValue (quantizeMs, juce::dontSendNotification);
    arpeggioThresholdSlider.setValue (arpeggioMs, juce::dontSendNotification);

    lastSyncedSettingsRevision = audioProcessor.getSettingsRevision();
}

void HarmonyVisualizerAudioProcessorEditor::setSettingsModalOpen (bool shouldOpen)
{
    if (isSettingsModalOpen == shouldOpen)
        return;

    isSettingsModalOpen = shouldOpen;
    if (isSettingsModalOpen)
        syncSettingsFromProcessor();

    updateSettingsModalLayout();
    repaint();
}

void HarmonyVisualizerAudioProcessorEditor::updateSettingsModalLayout()
{
    const float w = static_cast<float> (getWidth());
    const float h = static_cast<float> (getHeight());

    // 右端からスライドインするスリムなサイドドロワー（インスペクターパネル）
    const float drawerW = juce::jlimit (190.0f, 240.0f, w * 0.48f);
    const float slideOffsetX = (1.0f - settingsModalAlpha) * drawerW;

    modalCardBounds = juce::Rectangle<float> (w - drawerW + slideOffsetX,
                                              0.0f,
                                              drawerW,
                                              h);

    modalCloseButtonBounds = juce::Rectangle<float> (modalCardBounds.getRight() - 36.0f,
                                                     12.0f,
                                                     24.0f,
                                                     24.0f);

    const bool showControls = (settingsModalAlpha > 0.05f);
    historySizeSlider.setVisible (showControls);
    quantizeWindowSlider.setVisible (showControls);
    arpeggioThresholdSlider.setVisible (showControls);
    resetSettingsButton.setVisible (showControls);

    historySizeSlider.setInterceptsMouseClicks (isSettingsModalOpen, isSettingsModalOpen);
    quantizeWindowSlider.setInterceptsMouseClicks (isSettingsModalOpen, isSettingsModalOpen);
    arpeggioThresholdSlider.setInterceptsMouseClicks (isSettingsModalOpen, isSettingsModalOpen);
    resetSettingsButton.setInterceptsMouseClicks (isSettingsModalOpen, isSettingsModalOpen);

    if (showControls)
    {
        historySizeSlider.setAlpha (settingsModalAlpha);
        quantizeWindowSlider.setAlpha (settingsModalAlpha);
        arpeggioThresholdSlider.setAlpha (settingsModalAlpha);
        resetSettingsButton.setAlpha (settingsModalAlpha);

        const int contentX = static_cast<int> (std::round (modalCardBounds.getX() + 14.0f));
        const int contentW = static_cast<int> (std::round (modalCardBounds.getWidth() - 28.0f));
        const int baseTop = static_cast<int> (std::round (modalCardBounds.getY() + 54.0f));
        const int rowStep = 50;

        historySizeSlider.setBounds (contentX, baseTop + 18, contentW, 24);
        quantizeWindowSlider.setBounds (contentX, baseTop + rowStep + 18, contentW, 24);
        arpeggioThresholdSlider.setBounds (contentX, baseTop + rowStep * 2 + 18, contentW, 24);

        const int resetBtnH = 26;
        const int resetBtnY = static_cast<int> (std::round (modalCardBounds.getBottom() - 78.0f));
        resetSettingsButton.setBounds (contentX, resetBtnY, contentW, resetBtnH);
    }
}

void HarmonyVisualizerAudioProcessorEditor::drawSettingsGearButton (juce::Graphics& g)
{
    // ドロワー展開時はヘッダー右上の閉じる（×）ボタンにスムーズにバトンタッチする
    const float visibilityAlpha = juce::jlimit (0.0f, 1.0f, 1.0f - settingsModalAlpha * 1.4f);
    if (visibilityAlpha <= 0.01f)
        return;

    const float bgAlpha = (isGearButtonHovered ? 0.18f : 0.08f) * visibilityAlpha;
    g.setColour (juce::Colours::white.withAlpha (bgAlpha));
    g.fillRoundedRectangle (gearButtonBounds, 6.0f);

    if (isGearButtonHovered)
    {
        g.setColour (juce::Colour (0xff38bdf8).withAlpha (0.35f * visibilityAlpha));
        g.drawRoundedRectangle (gearButtonBounds, 6.0f, 1.0f);
    }

    const juce::Point<float> c = gearButtonBounds.getCentre();
    const float outerR = gearButtonBounds.getWidth() * 0.30f;
    const float innerR = outerR * 0.72f;
    const float holeR  = outerR * 0.36f;
    constexpr int numTeeth = 8;

    juce::Path gearPath;
    const int totalSteps = numTeeth * 4;
    for (int i = 0; i < totalSteps; ++i)
    {
        const float angle = static_cast<float> (i) * juce::MathConstants<float>::twoPi / static_cast<float> (totalSteps);
        const bool isOuter = ((i % 4) == 0 || (i % 4) == 1);
        const float r = isOuter ? outerR : innerR;
        const float px = c.x + std::cos (angle) * r;
        const float py = c.y + std::sin (angle) * r;
        if (i == 0)
            gearPath.startNewSubPath (px, py);
        else
            gearPath.lineTo (px, py);
    }
    gearPath.closeSubPath();
    gearPath.addCentredArc (c.x, c.y, holeR, holeR, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
    gearPath.closeSubPath();
    gearPath.setUsingNonZeroWinding (false);

    const juce::Colour iconColour = (isGearButtonHovered
        ? juce::Colour (0xfff1f5f9)
        : inactiveNoteColour.brighter (0.25f)).withMultipliedAlpha (visibilityAlpha);
    g.setColour (iconColour);
    g.fillPath (gearPath);
}

void HarmonyVisualizerAudioProcessorEditor::drawSettingsModalOverlay (juce::Graphics& g)
{
    const float a = juce::jlimit (0.0f, 1.0f, settingsModalAlpha);

    // 1. ドロワー左外側のソフトドロップシャドウ（全画面暗転バックドロップは廃止し、ライブプレビュー性を維持）
    const float shadowWidth = 24.0f;
    juce::ColourGradient shadowGrad (
        juce::Colour (0xff07090e).withAlpha (0.0f),  modalCardBounds.getX() - shadowWidth, 0.0f,
        juce::Colour (0xff07090e).withAlpha (0.48f * a), modalCardBounds.getX(), 0.0f,
        false);
    g.setGradientFill (shadowGrad);
    g.fillRect (modalCardBounds.getX() - shadowWidth,
                modalCardBounds.getY(),
                shadowWidth,
                modalCardBounds.getHeight());

    // 2. 右端サイドドロワー本体（半透明 Dark Slate ガラス調サーフェス）
    g.setColour (juce::Colour (0xff161b28).withAlpha (0.86f * a));
    g.fillRect (modalCardBounds);

    // 左端の境界線（ボーダー）と微かなインナーハイライト
    g.setColour (juce::Colour (0xff334155).withAlpha (0.88f * a));
    g.drawLine (modalCardBounds.getX(), modalCardBounds.getY(),
                modalCardBounds.getX(), modalCardBounds.getBottom(), 1.2f);

    g.setColour (juce::Colour (0xff38bdf8).withAlpha (0.14f * a));
    g.drawLine (modalCardBounds.getX() + 1.0f, modalCardBounds.getY(),
                modalCardBounds.getX() + 1.0f, modalCardBounds.getBottom(), 1.0f);

    // 3. ヘッダータイトルと閉じるボタン（×）
    const float contentX = modalCardBounds.getX() + 14.0f;
    const float contentW = modalCardBounds.getWidth() - 28.0f;

    const juce::Rectangle<float> headerArea (contentX,
                                             modalCardBounds.getY() + 11.0f,
                                             contentW - 28.0f,
                                             26.0f);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.setColour (juce::Colour (0xfff1f5f9).withAlpha (a));
    g.drawText ("Settings", headerArea, juce::Justification::centredLeft, false);

    // ヘッダー区切り線
    const float headerLineY = modalCardBounds.getY() + 44.0f;
    g.setColour (juce::Colour (0xff2a3245).withAlpha (a));
    g.drawHorizontalLine (static_cast<int> (headerLineY),
                          modalCardBounds.getX() + 12.0f,
                          modalCardBounds.getRight() - 12.0f);

    // 閉じる（×）ボタン
    if (isCloseButtonHovered)
    {
        g.setColour (juce::Colours::white.withAlpha (0.14f * a));
        g.fillRoundedRectangle (modalCloseButtonBounds, 5.0f);
    }
    g.setColour ((isCloseButtonHovered ? juce::Colour (0xfff8fafc) : juce::Colour (0xff94a3b8)).withAlpha (a));
    const juce::Point<float> closeCenter = modalCloseButtonBounds.getCentre();
    const float crossHalf = 4.5f;
    g.drawLine (closeCenter.x - crossHalf, closeCenter.y - crossHalf,
                closeCenter.x + crossHalf, closeCenter.y + crossHalf, 1.6f);
    g.drawLine (closeCenter.x + crossHalf, closeCenter.y - crossHalf,
                closeCenter.x - crossHalf, closeCenter.y + crossHalf, 1.6f);

    // 4. 各スライダー項目のラベル
    const float baseTop = modalCardBounds.getY() + 54.0f;
    const float rowStep = 50.0f;

    g.setFont (juce::FontOptions (11.5f));
    g.setColour (juce::Colour (0xffcbd5e1).withAlpha (a));
    g.drawText ("History Generations",
                juce::Rectangle<float> (contentX, baseTop, contentW, 18.0f),
                juce::Justification::centredLeft, false);

    g.drawText ("Quantize Window",
                juce::Rectangle<float> (contentX, baseTop + rowStep, contentW, 18.0f),
                juce::Justification::centredLeft, false);

    g.drawText ("Arpeggio Window",
                juce::Rectangle<float> (contentX, baseTop + rowStep * 2.0f, contentW, 18.0f),
                juce::Justification::centredLeft, false);

    // 5. フッター区切り線 ＆ About（バージョン・ビルド情報）表示
    const float footerLineY = modalCardBounds.getBottom() - 44.0f;
    g.setColour (juce::Colour (0xff2a3245).withAlpha (a));
    g.drawHorizontalLine (static_cast<int> (footerLineY),
                          modalCardBounds.getX() + 12.0f,
                          modalCardBounds.getRight() - 12.0f);

    const juce::Rectangle<float> aboutBounds (contentX,
                                              modalCardBounds.getBottom() - 36.0f,
                                              contentW,
                                              28.0f);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (juce::Colour (0xff94a3b8).withAlpha (a));
    const juce::String versionTitle = "MeLoom v" + juce::String (ProjectInfo::versionString);
    g.drawText (versionTitle, aboutBounds.withHeight (14.0f), juce::Justification::centredLeft, false);

    g.setFont (juce::FontOptions (9.5f));
    g.setColour (juce::Colour (0xff64748b).withAlpha (a));
#if JUCE_DEBUG || defined(_DEBUG) || defined(DEBUG)
    const juce::String buildInfo = "Debug Build (" + juce::String (__DATE__) + ")";
#else
    const juce::String buildInfo = "Release Build (" + juce::String (__DATE__) + ")";
#endif
    g.drawText (buildInfo, aboutBounds.withTrimmedTop (14.0f), juce::Justification::centredLeft, false);
}

void HarmonyVisualizerAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    const juce::Point<float> pos = event.position;

    if (isSettingsModalOpen)
    {
        if (modalCloseButtonBounds.contains (pos) || gearButtonBounds.contains (pos) || !modalCardBounds.contains (pos))
        {
            setSettingsModalOpen (false);
            return;
        }
    }
    else if (gearButtonBounds.contains (pos))
    {
        setSettingsModalOpen (true);
        return;
    }
}

void HarmonyVisualizerAudioProcessorEditor::mouseMove (const juce::MouseEvent& event)
{
    const juce::Point<float> pos = event.position;
    const bool newGearHover = !isSettingsModalOpen && gearButtonBounds.contains (pos);
    const bool newCloseHover = isSettingsModalOpen && (modalCloseButtonBounds.contains (pos) || gearButtonBounds.contains (pos));

    if (newGearHover != isGearButtonHovered || newCloseHover != isCloseButtonHovered)
    {
        isGearButtonHovered = newGearHover;
        isCloseButtonHovered = newCloseHover;
        setMouseCursor ((isGearButtonHovered || isCloseButtonHovered)
                            ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void HarmonyVisualizerAudioProcessorEditor::mouseExit (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);
    if (isGearButtonHovered || isCloseButtonHovered)
    {
        isGearButtonHovered = false;
        isCloseButtonHovered = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
}
