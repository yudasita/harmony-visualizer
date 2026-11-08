/*
  ==============================================================================

    ChordBoundaryDetector.h
    HarmonyVisualizer - コード境界判定エンジン

    クオンタイズ窓（同時打鍵のズレ吸収）とダンパーペダル（CC64 / ピッチベンド代用）監視、
    アルペジオ演奏判定とダイアド（2音以上）フィルタリング、および
    メロディ経過音（非和声音）の自動判定とコード安定度に基づく後追い解決により、
    コードの切り替え境界を正確に検出する。

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <vector>
#include <set>
#include <functional>
#include <algorithm>
#include <cmath>

class ChordBoundaryDetector
{
public:
    struct ChordState
    {
        std::vector<int> notes;          // 確定した構成音（MIDIノート番号ソート済み）
        std::vector<int> soundingNotes;  // 現在実際に発音中の音（押鍵中、またはペダル保持中）
        bool isDimmed { false };         // 演奏待機状態（全離鍵＆ペダルOFF状態）
        bool isChordCommitted { false }; // 今回の更新で新コードが確定したかのトリガー
    };

    explicit ChordBoundaryDetector (double windowMs = 60.0)
        : quantizeWindowMs (windowMs)
    {
    }

    void setQuantizeWindowMs (double ms) noexcept
    {
        quantizeWindowMs = juce::jlimit (10.0, 300.0, ms);
    }

    double getQuantizeWindowMs() const noexcept { return quantizeWindowMs; }

    void setMinNotesForChord (int minNotes) noexcept
    {
        minNotesForChord = std::max (1, minNotes);
    }

    int getMinNotesForChord() const noexcept { return minNotesForChord; }

    void setArpeggioThresholdMs (double ms) noexcept
    {
        arpeggioThresholdMs = juce::jlimit (100.0, 1500.0, ms);
    }

    double getArpeggioThresholdMs() const noexcept { return arpeggioThresholdMs; }

    void setMelodyStepThresholdMs (double ms) noexcept
    {
        melodyStepThresholdMs = juce::jlimit (200.0, 3000.0, ms);
    }

    double getMelodyStepThresholdMs() const noexcept { return melodyStepThresholdMs; }

    void setAppoggiaturaResolutionWindowMs (double ms) noexcept
    {
        appoggiaturaResolutionWindowMs = juce::jlimit (200.0, 2000.0, ms);
    }

    double getAppoggiaturaResolutionWindowMs() const noexcept { return appoggiaturaResolutionWindowMs; }

    /** 和音の協和度・安定度スコアを計算する（五度圏距離・協和音程・三和音ボーナスに基づく） */
    static float calculateChordStability (const std::vector<int>& midiNotes) noexcept
    {
        if (midiNotes.empty())
            return 0.0f;

        std::vector<int> uniquePcs;
        for (int note : midiNotes)
        {
            const int pc = ((note % 12) + 12) % 12;
            if (std::find (uniquePcs.begin(), uniquePcs.end(), pc) == uniquePcs.end())
                uniquePcs.push_back (pc);
        }

        if (uniquePcs.size() < 2)
            return 1.0f;

        float score = 0.0f;

        // 1. 各音対の五度圏距離に基づく協和度・緊張度
        for (size_t i = 0; i < uniquePcs.size(); ++i)
        {
            const int circleI = (uniquePcs[i] * 7) % 12;
            for (size_t j = i + 1; j < uniquePcs.size(); ++j)
            {
                const int circleJ = (uniquePcs[j] * 7) % 12;
                int dist = std::abs (circleI - circleJ);
                if (dist > 6)
                    dist = 12 - dist;

                switch (dist)
                {
                    case 1: score += 3.0f; break;  // 完全5度 / 4度（最も安定した音響的骨格）
                    case 3: score += 2.5f; break;  // 短3度 / 長6度（柔和な協和）
                    case 4: score += 2.5f; break;  // 長3度 / 短6度（明るい協和）
                    case 2: score -= 1.5f; break;  // 長2度 / 短7度（全音の穏やかな緊張）
                    case 5: score -= 4.5f; break;  // 短2度 / 長7度（半音の鋭い不協和・濁り）
                    case 6: score -= 2.5f; break;  // 増4度 / 減5度（トライトーン緊張）
                    default: break;
                }
            }
        }

        // 2. メジャー三和音 / マイナー三和音の骨格ボーナス
        for (int root : uniquePcs)
        {
            const int majorThird = (root + 4) % 12;
            const int minorThird = (root + 3) % 12;
            const int fifth = (root + 7) % 12;

            const bool hasFifth = (std::find (uniquePcs.begin(), uniquePcs.end(), fifth) != uniquePcs.end());
            const bool hasM3 = (std::find (uniquePcs.begin(), uniquePcs.end(), majorThird) != uniquePcs.end());
            const bool hasm3 = (std::find (uniquePcs.begin(), uniquePcs.end(), minorThird) != uniquePcs.end());

            if (hasFifth && (hasM3 || hasm3))
            {
                score += 5.0f;

                // ベース音がこの三和音のルートであればさらに安定ボーナス
                const int bassPc = ((midiNotes.front() % 12) + 12) % 12;
                if (bassPc == root)
                    score += 2.0f;

                break;
            }
        }

        // 3. 実音での半音ぶつかり（隣接短2度）ペナルティ
        for (size_t i = 0; i < midiNotes.size(); ++i)
        {
            for (size_t j = i + 1; j < midiNotes.size(); ++j)
            {
                if (std::abs (midiNotes[i] - midiNotes[j]) == 1)
                {
                    score -= 3.0f;
                }
            }
        }

        return score;
    }

    /** 生MIDIイベントを処理する */
    void processMidiEvent (const QueuedMidiEvent& event, double currentTimeMs)
    {
        if (event.type == QueuedMidiEvent::Type::noteOn && event.value > 0)
        {
            handleNoteOn (static_cast<int> (event.noteOrController), currentTimeMs);
        }
        else if (event.type == QueuedMidiEvent::Type::noteOff ||
                 (event.type == QueuedMidiEvent::Type::noteOn && event.value == 0))
        {
            handleNoteOff (static_cast<int> (event.noteOrController), currentTimeMs);
        }
        else if (event.type == QueuedMidiEvent::Type::controlChange)
        {
            if (event.noteOrController == 64) // CC64: ダンパー/サスティンペダル
            {
                isCc64Down = (event.value >= 64);
                updatePedalState (currentTimeMs);
            }
            else if (event.noteOrController == 120 || event.noteOrController == 121 || event.noteOrController == 123)
            {
                handleAllNotesOff (currentTimeMs);
            }
        }
        else if (event.type == QueuedMidiEvent::Type::pitchWheel)
        {
            // ピッチベンドホイールによるペダル代用（ペダルがない環境向けの動作確認用）
            // 0..16383（センター8192）。ホイールを傾けている間をペダルON、中央に戻したらOFF
            const int pitchValue = (static_cast<int> (event.value) << 7) | static_cast<int> (event.noteOrController);
            isPitchBendPedalDown = (std::abs (pitchValue - 8192) > 2000);
            updatePedalState (currentTimeMs);
        }
        else if (event.type == QueuedMidiEvent::Type::allNotesOff)
        {
            handleAllNotesOff (currentTimeMs);
        }
    }

    /** タイマー駆動等の周期呼び出しでクオンタイズ窓の満了を判定する */
    void update (double currentTimeMs)
    {
        currentState.isChordCommitted = false;

        if (isBuildingInitialChord && lastNoteOnTimeMs > 0.0 && (currentTimeMs - lastNoteOnTimeMs >= arpeggioThresholdMs))
        {
            isBuildingInitialChord = false;
            isInitialArpeggioForPedalAllowed = false;
        }

        if (isWindowOpen)
        {
            if (currentTimeMs >= windowStartTimeMs + quantizeWindowMs)
            {
                // クオンタイズ窓が満了
                commitAccumulatedNotes (currentTimeMs);
                isWindowOpen = false;
                notesInCurrentWindow.clear();
            }
        }

        // 離鍵状態（現在押鍵数0、かつペダルOFF）の判定
        if (currentlyHeldNotes.empty() && !isPedalDown)
        {
            if (!currentState.notes.empty())
            {
                currentState.isDimmed = true;
            }
        }
    }

    const ChordState& getCurrentChordState() const noexcept { return currentState; }

    /** 指定された五度圏インデックス（0〜11）が現在発音中（押鍵中・ペダル保持中）か判定 */
    bool isCircleIndexSounding (int circleIdx) const noexcept
    {
        if (circleIdx < 0 || circleIdx >= 12)
            return false;

        for (int note : currentState.soundingNotes)
        {
            const int pc = ((note % 12) + 12) % 12;
            if (((pc * 7) % 12) == circleIdx)
                return true;
        }
        return false;
    }

    using NewChordCallback = std::function<void (const std::vector<int>& notes)>;
    using UpdateChordCallback = std::function<void (const std::vector<int>& notes)>;

    void setNewChordCallback (NewChordCallback callback) { onNewChord = std::move (callback); }
    void setUpdateChordCallback (UpdateChordCallback callback) { onUpdateChord = std::move (callback); }

    void reset()
    {
        currentlyHeldNotes.clear();
        accumulatedNotes.clear();
        sustainedNotes.clear();
        notesInCurrentWindow.clear();
        isCc64Down = false;
        isPitchBendPedalDown = false;
        isPedalDown = false;
        pedalReleasedSinceLastChord = false;
        isWindowOpen = false;
        windowStartTimeMs = 0.0;
        lastCommitTimeMs = -9999.0;
        lastNoteOnTimeMs = -9999.0;
        lastTopNote = -1;
        lastTopNoteTimeMs = -9999.0;
        hasActiveCommittedChord = false;
        isBuildingInitialChord = false;
        isInitialArpeggioForPedalAllowed = true;
        isNewChordStrike = false;
        currentState = ChordState();
    }

private:
    double quantizeWindowMs { 60.0 };
    double windowStartTimeMs { 0.0 };
    bool isWindowOpen { false };

    int minNotesForChord { 2 };              // コード認識に必要な最低音数（単音を排除）
    double arpeggioThresholdMs { 450.0 };    // アルペジオ許容時間窓(ms)
    double melodyStepThresholdMs { 1500.0 };  // メロディ経過音ステップ判定許容時間窓(ms)
    double appoggiaturaResolutionWindowMs { 1000.0 }; // 倚音・掛留音の離鍵解決許容時間窓(ms)

    double lastNoteOnTimeMs { -9999.0 };     // 直前の打鍵時刻
    int lastTopNote { -1 };                  // 直前のメロディ最高音（トップノート）
    double lastTopNoteTimeMs { -9999.0 };    // 直前の最高音打鍵時刻
    bool hasActiveCommittedChord { false };  // 現在最前面にコミット済みコードがあるか
    bool isBuildingInitialChord { false };   // 和音の初期構築（アルペジオ等）フェーズ中フラグ
    bool isInitialArpeggioForPedalAllowed { true }; // ペダル踏下後の初回アルペジオ許容フラグ
    bool isNewChordStrike { false };         // アルペジオ継続ではない新規和音打鍵フラグ

    bool isCc64Down { false };
    bool isPitchBendPedalDown { false };
    bool isPedalDown { false };
    bool pedalReleasedSinceLastChord { false };

    std::set<int> currentlyHeldNotes;
    std::set<int> accumulatedNotes;
    std::set<int> sustainedNotes;
    std::vector<int> notesInCurrentWindow;   // 現在のクオンタイズ窓内に新規打鍵された音

    ChordState currentState;
    NewChordCallback onNewChord;
    UpdateChordCallback onUpdateChord;

    double lastCommitTimeMs { -9999.0 };
    double syncopationThresholdMs { 400.0 }; // 後踏みペダル（シンコペーション・ペダル）許容時間窓(ms)

    void refreshSoundingNotes()
    {
        std::set<int> combined = currentlyHeldNotes;
        if (isPedalDown)
        {
            combined.insert (sustainedNotes.begin(), sustainedNotes.end());
        }
        currentState.soundingNotes.assign (combined.begin(), combined.end());
    }

    void updatePedalState (double currentTimeMs)
    {
        const bool effectivePedal = (isCc64Down || isPitchBendPedalDown);
        if (effectivePedal != isPedalDown)
        {
            handlePedalChange (effectivePedal, currentTimeMs);
        }
    }

    void handleNoteOn (int noteNumber, double currentTimeMs)
    {
        const double elapsedSinceLastNote = (lastNoteOnTimeMs > 0.0) ? (currentTimeMs - lastNoteOnTimeMs) : 99999.0;
        lastNoteOnTimeMs = currentTimeMs;

        currentlyHeldNotes.insert (noteNumber);
        if (isPedalDown)
            sustainedNotes.insert (noteNumber);

        // アルペジオ継続（同一コードの蓄積・成長）かどうかの判定:
        // 1. ペダルが踏まれている場合:
        //    ペダルを踏んでから「最初のアルペジオ構築中（isInitialArpeggioForPedalAllowed）」であり、
        //    かつ直前の打鍵から arpeggioThresholdMs 以内である場合のみ継続とする。
        //    （ペダルを踏み続けたままの反復打鍵・ジャンジャン演奏が誤ってアルペジオ継続されるのを防止）
        // 2. ペダルが踏まれていない場合:
        //    直前からの押鍵が残っている（レガート/和音押さえ）、または arpeggioThresholdMs 以内の打鍵であること
        // 3. かつ、ペダル解放（踏み直し）直後ではなく、演奏待機状態（dimmed）でもない
        bool isArpeggioContinuation = false;
        if (!currentState.isDimmed && !pedalReleasedSinceLastChord)
        {
            if (isPedalDown)
            {
                if (isInitialArpeggioForPedalAllowed && (elapsedSinceLastNote <= arpeggioThresholdMs))
                {
                    isArpeggioContinuation = true;
                }
            }
            else
            {
                const bool isLegatoOrArpeggio = (currentlyHeldNotes.size() > 1) || (elapsedSinceLastNote <= arpeggioThresholdMs);
                if (isLegatoOrArpeggio)
                {
                    isArpeggioContinuation = true;
                }
            }
        }

        if (!isArpeggioContinuation)
        {
            // 新規コードの開始：過去の古い蓄積をクリアし、現在押さえている音から再蓄積
            accumulatedNotes = currentlyHeldNotes;
            pedalReleasedSinceLastChord = false;
            if (!hasActiveCommittedChord)
            {
                isBuildingInitialChord = true;
            }
            isNewChordStrike = true;
        }
        else
        {
            // アルペジオ継続：既存の蓄積に新しい音を追加
            accumulatedNotes.insert (noteNumber);
        }

        currentState.isDimmed = false;
        refreshSoundingNotes();

        // クオンタイズ窓の開始または延長
        if (!isWindowOpen)
        {
            notesInCurrentWindow.clear();
        }
        notesInCurrentWindow.push_back (noteNumber);
        isWindowOpen = true;
        windowStartTimeMs = currentTimeMs;
    }

    void handleNoteOff (int noteNumber, double currentTimeMs)
    {
        currentlyHeldNotes.erase (noteNumber);
        refreshSoundingNotes();

        // コード安定度に基づく後追い解決（倚音・掛留音の離鍵による収束）:
        // 和音確定直後（appoggiaturaResolutionWindowMs以内）かつアルペジオ構築完了後に、
        // 【最高音（トップノート）として打鍵された不安定音】が離鍵され、
        // 残った保持音の安定度が向上した場合にのみ、確定コードからその不安定音を除外して安定コードへ上書き更新する。
        if (hasActiveCommittedChord && !isBuildingInitialChord && !currentState.notes.empty()
            && noteNumber == lastTopNote
            && (currentTimeMs - lastCommitTimeMs) <= appoggiaturaResolutionWindowMs)
        {
            auto it = std::find (currentState.notes.begin(), currentState.notes.end(), noteNumber);
            if (it != currentState.notes.end())
            {
                std::vector<int> remainingNotes = currentState.notes;
                remainingNotes.erase (std::remove (remainingNotes.begin(), remainingNotes.end(), noteNumber), remainingNotes.end());

                std::set<int> remainingPcs;
                for (int n : remainingNotes)
                    remainingPcs.insert (((n % 12) + 12) % 12);

                if (remainingPcs.size() >= static_cast<size_t> (minNotesForChord))
                {
                    const float currentStability = calculateChordStability (currentState.notes);
                    const float remainingStability = calculateChordStability (remainingNotes);

                    if (remainingStability > currentStability + 0.5f)
                    {
                        currentState.notes = remainingNotes;
                        accumulatedNotes = std::set<int> (remainingNotes.begin(), remainingNotes.end());
                        if (!currentState.notes.empty())
                        {
                            lastTopNote = currentState.notes.back();
                            lastTopNoteTimeMs = currentTimeMs;
                        }

                        if (onUpdateChord)
                            onUpdateChord (currentState.notes);
                    }
                }
            }
        }

        if (noteNumber == lastTopNote)
        {
            lastTopNote = !currentState.notes.empty() ? currentState.notes.back() : -1;
            lastTopNoteTimeMs = currentTimeMs;
        }

        // 指が1本ずつ離れるミリ秒単位のズレで構成音を削ってしまわないよう、
        // 直前の確定コード（currentState.notes）はそのまま維持する。
        // 全鍵盤が離鍵され、ペダルもOFFになったら直ちに待機状態（dimmed）へ移行する。
        if (currentlyHeldNotes.empty() && !isPedalDown && !currentState.notes.empty())
        {
            currentState.isDimmed = true;
        }
    }

    void handlePedalChange (bool down, double currentTimeMs)
    {
        juce::ignoreUnused (currentTimeMs);

        if (!isPedalDown && down)
        {
            // ペダルが踏まれた（ON）：現在押している鍵盤をサスティン対象に追加
            sustainedNotes = currentlyHeldNotes;
            isInitialArpeggioForPedalAllowed = true;
        }
        else if (isPedalDown && !down)
        {
            // ペダルが離された（OFF）
            isInitialArpeggioForPedalAllowed = true;
            pedalReleasedSinceLastChord = true;
            sustainedNotes.clear();

            // 鍵盤も押されていなければ、直ちにdimmed待機状態へ
            if (currentlyHeldNotes.empty() && !currentState.notes.empty())
            {
                currentState.isDimmed = true;
            }
            else
            {
                // 鍵盤が押されている音だけを残す
                accumulatedNotes = currentlyHeldNotes;

                // 後踏みペダリング（シンコペーション・ペダル）対策：
                // ペダルがOFF（踏み直しのリリース）になった際、
                // ペダルのサスティン残響音によって混ざっていた過去コード音をパージする。
                if (!currentlyHeldNotes.empty())
                {
                    std::vector<int> heldNotesVec (currentlyHeldNotes.begin(), currentlyHeldNotes.end());

                    // 最低音数（2音以上）を満たしている場合のみ更新
                    std::set<int> heldPitchClasses;
                    for (int n : heldNotesVec)
                        heldPitchClasses.insert (((n % 12) + 12) % 12);

                    const bool meetsMinNotes = (heldPitchClasses.size() >= static_cast<size_t> (minNotesForChord));

                    if (isWindowOpen)
                    {
                        // クオンタイズ窓の最中であれば、蓄積バッファから古いサスティン音を即座に排除
                        accumulatedNotes = currentlyHeldNotes;
                    }
                    else if (meetsMinNotes && (currentTimeMs - lastCommitTimeMs) <= syncopationThresholdMs && currentState.notes != heldNotesVec)
                    {
                        // 直前にペダル残響を巻き込んでコミットしてしまった場合：
                        // 現在実際に指で押さえている新コード構成音のみに再確定・更新する
                        currentState.notes = heldNotesVec;
                        currentState.isChordCommitted = true;
                        hasActiveCommittedChord = true;

                        if (onUpdateChord)
                        {
                            onUpdateChord (currentState.notes);
                        }
                    }
                }
            }
        }

        isPedalDown = down;
        refreshSoundingNotes();
    }

    void handleAllNotesOff (double currentTimeMs)
    {
        juce::ignoreUnused (currentTimeMs);
        currentlyHeldNotes.clear();
        accumulatedNotes.clear();
        sustainedNotes.clear();
        notesInCurrentWindow.clear();
        isWindowOpen = false;
        hasActiveCommittedChord = false;
        isBuildingInitialChord = false;
        lastTopNote = -1;
        lastTopNoteTimeMs = -9999.0;
        refreshSoundingNotes();

        if (!currentState.notes.empty())
            currentState.isDimmed = true;
    }

    void commitAccumulatedNotes (double currentTimeMs)
    {
        if (accumulatedNotes.empty())
            return;

        const int numNewNotes = static_cast<int> (notesInCurrentWindow.size());
        const int singleNewNote = (numNewNotes == 1) ? notesInCurrentWindow.front() : -1;

        int melodyCandidateNote = singleNewNote;
        if (numNewNotes > 1 && hasActiveCommittedChord && !isBuildingInitialChord && currentState.notes.size() >= 3)
        {
            std::vector<int> sortedWindowNotes = notesInCurrentWindow;
            std::sort (sortedWindowNotes.begin(), sortedWindowNotes.end());

            const int potentialTopNote = sortedWindowNotes.back();
            const int potentialBassNote = sortedWindowNotes.front();
            const int currentBass = currentState.notes.front();

            const bool sameBassPc = (((potentialBassNote % 12) + 12) % 12) == (((currentBass % 12) + 12) % 12);
            const bool bassNotLower = (potentialBassNote >= currentBass);

            if (sameBassPc && bassNotLower)
            {
                bool lowerNotesMatchAccompaniment = true;
                std::set<int> currentPcs;
                for (int n : currentState.notes)
                    currentPcs.insert (((n % 12) + 12) % 12);

                for (size_t i = 0; i + 1 < sortedWindowNotes.size(); ++i)
                {
                    const int pc = ((sortedWindowNotes[i] % 12) + 12) % 12;
                    if (currentPcs.find (pc) == currentPcs.end())
                    {
                        lowerNotesMatchAccompaniment = false;
                        break;
                    }
                }

                if (lowerNotesMatchAccompaniment && potentialTopNote > potentialBassNote)
                {
                    melodyCandidateNote = potentialTopNote;
                }
            }
        }

        // 最低音数フィルタ: ピッチクラス数が minNotesForChord 未満の場合はコードとして確定しない
        // （単音演奏時は外周リングの soundingNotes のみ点灯させ、コード履歴キューへは送らない）
        std::set<int> uniquePitchClasses;
        for (int n : accumulatedNotes)
            uniquePitchClasses.insert (((n % 12) + 12) % 12);

        if (uniquePitchClasses.size() < static_cast<size_t> (minNotesForChord))
        {
            return;
        }

        const double elapsedSinceLastNote = (lastNoteOnTimeMs > 0.0) ? (currentTimeMs - lastNoteOnTimeMs) : 99999.0;
        if (numNewNotes >= 3 || (numNewNotes >= minNotesForChord && elapsedSinceLastNote > arpeggioThresholdMs))
        {
            // 同時打鍵で完全な和音が鳴らされた場合（和音ストローク）、アルペジオ初期構築は完了
            isInitialArpeggioForPedalAllowed = false;
            isBuildingInitialChord = false;
        }

        if (isBuildingInitialChord && lastNoteOnTimeMs > 0.0 && (currentTimeMs - lastNoteOnTimeMs >= arpeggioThresholdMs))
        {
            isBuildingInitialChord = false;
            isInitialArpeggioForPedalAllowed = false;
        }

        // =========================================================================
        // Issue 47: メロディ・経過音（非和声音）の自動判定とコード安定度に基づく後追い解決
        // 以下の全条件を満たす場合のみメロディ・経過音判定を実施する:
        // 1. 和音構築（アルペジオ）フェーズが完了していること（!isBuildingInitialChord）
        // 2. 既に伴奏コードが3音以上（三和音以上）で確立していること（currentState.notes.size() >= 3）
        // 3. メロディ候補音（単音打鍵、または伴奏維持での最高音移動）が存在すること（melodyCandidateNote >= 0）
        // 4. 最前面にアクティブなコードが存在すること（hasActiveCommittedChord）
        // =========================================================================
        if (hasActiveCommittedChord && !isBuildingInitialChord && currentState.notes.size() >= 3 && melodyCandidateNote >= 0)
        {
            const int newNote = melodyCandidateNote;
            const int currentBass = currentState.notes.front();
            const int prevTop = (lastTopNote > 0 && (currentTimeMs - lastTopNoteTimeMs) <= melodyStepThresholdMs)
                                    ? lastTopNote
                                    : currentState.notes.back();

            // 1. 低音・ベースラインの明確な変化があった場合はコード進行（新世代）へ委ねる
            const bool isBassChange = (newNote < currentBass)
                || (std::find (currentState.soundingNotes.begin(), currentState.soundingNotes.end(), currentBass) == currentState.soundingNotes.end());

            if (isBassChange)
            {
                isBuildingInitialChord = true;
            }
            else
            {
                // 伴奏（ベースおよび内声）が保持されているか判定
                int accompanimentSoundingCount = 0;
                for (size_t i = 0; i + 1 < currentState.notes.size(); ++i)
                {
                    const int note = currentState.notes[i];
                    if (std::find (currentState.soundingNotes.begin(), currentState.soundingNotes.end(), note)
                        != currentState.soundingNotes.end())
                    {
                        accompanimentSoundingCount++;
                    }
                }

                // ペダル保持中、または鍵盤上で伴奏和音が押さえられている
                const bool hasHeldAccompaniment = (accompanimentSoundingCount >= 1)
                                                  && (isPedalDown || currentlyHeldNotes.size() >= 2);

                const int semitoneDiff = std::abs (newNote - prevTop);
                const bool isStepwise = (semitoneDiff == 1 || semitoneDiff == 2);
                const bool isTopVoice = (newNote > currentBass)
                                        && (newNote >= currentState.notes[std::max (0, static_cast<int> (currentState.notes.size()) - 2)]);

                if (hasHeldAccompaniment && isStepwise && isTopVoice)
                {
                    // 2. コード安定度に基づく後追い解決の判定
                    // 直前の不安定音（prevTop、または直前コードの最高音）を newNote で置換・解決した候補を生成
                    std::vector<int> candidateNotes = currentState.notes;

                    // 新しい音のピッチクラスが既に含まれているか確認
                    const int newPc = ((newNote % 12) + 12) % 12;
                    bool pcAlreadyInChord = false;
                    for (size_t i = 0; i + 1 < candidateNotes.size(); ++i)
                    {
                        if ((((candidateNotes[i] % 12) + 12) % 12) == newPc)
                        {
                            pcAlreadyInChord = true;
                            break;
                        }
                    }

                    if (pcAlreadyInChord)
                    {
                        // 解決音が既に内声に含まれていた場合（例: F4 -> E4 で E音に収束）、不安定音を除外
                        if (candidateNotes.size() > static_cast<size_t> (minNotesForChord))
                            candidateNotes.pop_back();
                        else
                            candidateNotes.back() = newNote;
                    }
                    else
                    {
                        candidateNotes.back() = newNote;
                    }

                    std::sort (candidateNotes.begin(), candidateNotes.end());

                    const float currentStability = calculateChordStability (currentState.notes);
                    const float candidateStability = calculateChordStability (candidateNotes);

                    if (candidateStability > currentStability + 0.5f)
                    {
                        // 安定度が向上 -> 不安定音（倚音・テンション）が安定コードトーンへ解決！
                        currentState.notes = candidateNotes;
                        accumulatedNotes = std::set<int> (candidateNotes.begin(), candidateNotes.end());
                        currentState.isDimmed = false;
                        currentState.isChordCommitted = true;
                        lastCommitTimeMs = currentTimeMs;
                        lastTopNote = newNote;
                        lastTopNoteTimeMs = currentTimeMs;

                        if (onUpdateChord)
                            onUpdateChord (currentState.notes);

                        return;
                    }
                    else
                    {
                        // 安定度が向上しない -> メロディ経過音（Passing tone / 装飾音）
                        // コード構成音には含めず、最前面コードを濁らせない
                        accumulatedNotes.erase (newNote);
                        lastTopNote = newNote;
                        lastTopNoteTimeMs = currentTimeMs;
                        isNewChordStrike = false;
                        return;
                    }
                }
            }
        }

        std::vector<int> newNotes (accumulatedNotes.begin(), accumulatedNotes.end());
        const bool isDifferentChord = (newNotes != currentState.notes);

        // 既存の最前面コードに対する「同一コードの成長（アルペジオ追加）」かどうかの判定:
        // 過去の構成音のピッチクラスが、新しい構成音のピッチクラスにすべて含まれている（包含されている）場合
        // かつ、ベース音（最低音）が変化していないこと（低音・ベースラインの変化は新コード進行とする）
        bool isSubsetGrowth = false;
        if (hasActiveCommittedChord && !currentState.notes.empty())
        {
            const bool bassUnchanged = (!newNotes.empty() && newNotes.front() == currentState.notes.front());

            if (bassUnchanged)
            {
                std::set<int> prevPitchClasses;
                for (int n : currentState.notes)
                    prevPitchClasses.insert (((n % 12) + 12) % 12);

                bool allContained = true;
                for (int pc : prevPitchClasses)
                {
                    if (uniquePitchClasses.find (pc) == uniquePitchClasses.end())
                    {
                        allContained = false;
                        break;
                    }
                }
                if (allContained && uniquePitchClasses.size() >= prevPitchClasses.size())
                    isSubsetGrowth = true;
            }
        }

        currentState.notes = newNotes;
        currentState.isDimmed = false;
        currentState.isChordCommitted = true;
        lastCommitTimeMs = currentTimeMs;
        if (!currentState.notes.empty())
        {
            lastTopNote = currentState.notes.back();
            lastTopNoteTimeMs = currentTimeMs;
        }

        if (isDifferentChord)
        {
            if (hasActiveCommittedChord && isSubsetGrowth && !isNewChordStrike)
            {
                // アルペジオによる構成音追加：最前面（Z=0）を上書き更新（奥へは押し出さない）
                if (onUpdateChord)
                    onUpdateChord (currentState.notes);
            }
            else
            {
                // 新コードへの進行または新規和音打鍵：直前のコードを奥へ送り、新コードを最前面へプッシュ
                hasActiveCommittedChord = true;
                if (onNewChord)
                    onNewChord (currentState.notes);
            }
        }
        isNewChordStrike = false;
    }
};
