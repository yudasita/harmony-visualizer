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
#include "ChordEvent.h"
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
        uint16_t soundingCircleMask { 0 }; // 現在発音中の五度圏インデックスビットマスク（O(1)描画判定用）
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

        uint16_t pcMask = 0;
        std::vector<int> uniquePcs;
        uniquePcs.reserve (midiNotes.size());

        for (int note : midiNotes)
        {
            const int pc = ChordEvent::normalizePitchClass (note);
            if ((pcMask & (1u << pc)) == 0)
            {
                pcMask |= static_cast<uint16_t> (1u << pc);
                uniquePcs.push_back (pc);
            }
        }

        if (uniquePcs.size() < 2)
            return 1.0f;

        float score = 0.0f;

        // 1. 各音対の五度圏距離に基づく協和度・緊張度
        for (size_t i = 0; i < uniquePcs.size(); ++i)
        {
            const int circleI = ChordEvent::pitchClassToCircleIndex (uniquePcs[i]);
            for (size_t j = i + 1; j < uniquePcs.size(); ++j)
            {
                const int circleJ = ChordEvent::pitchClassToCircleIndex (uniquePcs[j]);
                const int dist = ChordEvent::circleDistance (circleI, circleJ);

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

        // 2. メジャー三和音 / マイナー三和音の骨格ボーナス（ビットマスクによるO(1)判定）
        for (int root : uniquePcs)
        {
            const int majorThird = (root + 4) % 12;
            const int minorThird = (root + 3) % 12;
            const int fifth = (root + 7) % 12;

            const bool hasFifth = (pcMask & (1u << fifth)) != 0;
            const bool hasM3    = (pcMask & (1u << majorThird)) != 0;
            const bool hasm3    = (pcMask & (1u << minorThird)) != 0;

            if (hasFifth && (hasM3 || hasm3))
            {
                score += 5.0f;

                // ベース音がこの三和音のルートであればさらに安定ボーナス
                const int bassPc = ChordEvent::normalizePitchClass (midiNotes.front());
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

    /** リペダリング猶予時間窓（ms）を設定する */
    void setRepedalWindowMs (double windowMs) noexcept
    {
        repedalWindowMs = juce::jlimit (50.0, 500.0, windowMs);
    }

    double getRepedalWindowMs() const noexcept { return repedalWindowMs; }

    /** タイマー駆動等の周期呼び出しでクオンタイズ窓の満了を判定する */
    void update (double currentTimeMs)
    {
        currentState.isChordCommitted = false;

        checkAndExpireInitialArpeggio (currentTimeMs);

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

        // リペダリング猶予時間（仮想サスティン）の満了判定
        if (isInRepedalWindow && (currentTimeMs - repedalWindowStartTimeMs >= repedalWindowMs))
        {
            isInRepedalWindow = false;
            repedalNotes.clear();
            refreshSoundingNotes();

            if (currentlyHeldNotes.empty() && !isPedalDown)
            {
                hasActiveCommittedChord = false;
                isBuildingInitialChord = false;
                if (!currentState.notes.empty())
                {
                    currentState.isDimmed = true;
                }
            }
        }

        // 離鍵状態（現在押鍵数0、ペダルOFF、かつリペダリング仮想サスティン保持なし）の判定
        updateDimmedState();
    }

    const ChordState& getCurrentChordState() const noexcept { return currentState; }

    /** 指定された五度圏インデックス（0〜11）が現在発音中（押鍵中・ペダル保持中）か判定 */
    bool isCircleIndexSounding (int circleIdx) const noexcept
    {
        return (circleIdx >= 0 && circleIdx < 12) && ((currentState.soundingCircleMask & (1u << circleIdx)) != 0);
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
        repedalNotes.clear();
        staleHeldNotes.clear();
        committedHeldNotes.clear();
        notesInCurrentWindow.clear();
        isCc64Down = false;
        isPitchBendPedalDown = false;
        isPedalDown = false;
        pedalReleasedSinceLastChord = false;
        isInRepedalWindow = false;
        repedalWindowStartTimeMs = -9999.0;
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
    void checkAndExpireInitialArpeggio (double currentTimeMs) noexcept
    {
        if (isBuildingInitialChord && lastNoteOnTimeMs > 0.0 && (currentTimeMs - lastNoteOnTimeMs >= arpeggioThresholdMs))
        {
            isBuildingInitialChord = false;
            isInitialArpeggioForPedalAllowed = false;
        }
    }

    void updateDimmedState() noexcept
    {
        const bool hasVirtualSustain = isInRepedalWindow && !repedalNotes.empty();
        if (currentlyHeldNotes.empty() && !isPedalDown && !hasVirtualSustain && !currentState.notes.empty())
        {
            currentState.isDimmed = true;
        }
    }

    double quantizeWindowMs { 60.0 };
    double windowStartTimeMs { 0.0 };
    bool isWindowOpen { false };

    int minNotesForChord { 2 };              // コード認識に必要な最低音数（単音を排除）
    double arpeggioThresholdMs { 450.0 };    // アルペジオ許容時間窓(ms)
    double melodyStepThresholdMs { 1500.0 };  // メロディ経過音ステップ判定許容時間窓(ms)
    double appoggiaturaResolutionWindowMs { 1000.0 }; // 倚音・掛留音の離鍵解決許容時間窓(ms)
    double repedalWindowMs { 150.0 };        // リペダリング時の新規打鍵ノート仮想サスティン猶予時間(ms)
    double repedalWindowStartTimeMs { -9999.0 };
    bool isInRepedalWindow { false };

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
    std::set<int> repedalNotes;              // ペダルOFF後の踏み直し猶予中に新規打鍵された仮想サスティン音
    std::set<int> staleHeldNotes;            // ペダルOFF前から押しっぱなしの直前コード由来ノート（次コード混入防止用）
    std::set<int> committedHeldNotes;        // 直前のコード確定時点から継続して押鍵されているノート
    std::vector<int> notesInCurrentWindow;   // 現在のクオンタイズ窓内に新規打鍵された音

    ChordState currentState;
    NewChordCallback onNewChord;
    UpdateChordCallback onUpdateChord;

    double lastCommitTimeMs { -9999.0 };
    double syncopationThresholdMs { 400.0 }; // 後踏みペダル（シンコペーション・ペダル）許容時間窓(ms)

    void refreshSoundingNotes()
    {
        currentState.soundingNotes.clear();
        currentState.soundingNotes.reserve (currentlyHeldNotes.size() + sustainedNotes.size() + repedalNotes.size());

        currentState.soundingNotes.insert (currentState.soundingNotes.end(),
                                           currentlyHeldNotes.begin(), currentlyHeldNotes.end());

        if (isPedalDown)
            currentState.soundingNotes.insert (currentState.soundingNotes.end(),
                                               sustainedNotes.begin(), sustainedNotes.end());
        else if (isInRepedalWindow)
            currentState.soundingNotes.insert (currentState.soundingNotes.end(),
                                               repedalNotes.begin(), repedalNotes.end());

        std::sort (currentState.soundingNotes.begin(), currentState.soundingNotes.end());
        currentState.soundingNotes.erase (std::unique (currentState.soundingNotes.begin(), currentState.soundingNotes.end()),
                                          currentState.soundingNotes.end());

        uint16_t mask = 0;
        for (int note : currentState.soundingNotes)
        {
            const int circleIdx = ChordEvent::pitchClassToCircleIndex (ChordEvent::normalizePitchClass (note));
            mask |= static_cast<uint16_t> (1u << circleIdx);
        }
        currentState.soundingCircleMask = mask;
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

        if (isInRepedalWindow && (currentTimeMs - repedalWindowStartTimeMs > repedalWindowMs))
        {
            isInRepedalWindow = false;
            repedalNotes.clear();
        }

        // 新規に打鍵されたノートは過去の押しっぱなし除外対象から解除
        staleHeldNotes.erase (noteNumber);
        committedHeldNotes.erase (noteNumber);

        currentlyHeldNotes.insert (noteNumber);
        if (isPedalDown)
        {
            sustainedNotes.insert (noteNumber);
        }
        else if (isInRepedalWindow)
        {
            // ペダルOFF後の踏み直し猶予期間中に打鍵されたノートを仮想サスティン保持
            repedalNotes.insert (noteNumber);
            repedalWindowStartTimeMs = currentTimeMs;
        }

        // アルペジオ継続（同一コードの蓄積・成長）かどうかの判定:
        // 1. ペダルが踏まれている場合:
        //    ペダルを踏んでから「最初のアルペジオ構築中（isInitialArpeggioForPedalAllowed）」であり、
        //    かつ直前の打鍵から arpeggioThresholdMs 以内である場合のみ継続とする。
        //    （ペダルを踏み続けたままの反復打鍵・ジャンジャン演奏が誤ってアルペジオ継続されるのを防止）
        // 2. ペダルが踏まれていない場合:
        //    直前からの新規押鍵またはリペダリング仮想サスティン保持音が残っている、または arpeggioThresholdMs 以内の打鍵であること
        // 3. かつ、ペダル解放（踏み直し）直後の初回打鍵ではなく、演奏待機状態（dimmed）でもない
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
                size_t activeFreshHeldCount = 0;
                for (int n : currentlyHeldNotes)
                {
                    if (staleHeldNotes.find (n) == staleHeldNotes.end())
                        ++activeFreshHeldCount;
                }
                const bool hasRepedalHeld = isInRepedalWindow && (repedalNotes.size() > 1);
                const bool isLegatoOrArpeggio = (activeFreshHeldCount > 1)
                                             || hasRepedalHeld
                                             || (elapsedSinceLastNote <= arpeggioThresholdMs);
                if (isLegatoOrArpeggio)
                {
                    isArpeggioContinuation = true;
                }
            }
        }

        if (!isArpeggioContinuation)
        {
            // 新規コードの開始：ペダルOFF前から押しっぱなしだった古いノート（staleHeldNotes）を除外し、
            // 新しく打鍵されたノートおよびリペダリング仮想保持ノートのみから再蓄積
            accumulatedNotes.clear();
            for (int n : currentlyHeldNotes)
            {
                if (staleHeldNotes.find (n) == staleHeldNotes.end())
                    accumulatedNotes.insert (n);
            }
            if (isInRepedalWindow)
            {
                accumulatedNotes.insert (repedalNotes.begin(), repedalNotes.end());
            }
            accumulatedNotes.insert (noteNumber);

            if (pedalReleasedSinceLastChord || !hasActiveCommittedChord)
            {
                hasActiveCommittedChord = false;
                isBuildingInitialChord = true;
            }
            pedalReleasedSinceLastChord = false;
            isNewChordStrike = true;
        }
        else
        {
            // アルペジオ継続：既存の蓄積に新しい音（およびリペダリング保持音）を追加
            if (isInRepedalWindow)
            {
                accumulatedNotes.insert (repedalNotes.begin(), repedalNotes.end());
            }
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
        staleHeldNotes.erase (noteNumber);
        committedHeldNotes.erase (noteNumber);
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

                if (remainingNotes.size() >= static_cast<size_t> (minNotesForChord))
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
        // 全鍵盤が離鍵され、ペダルもOFFかつリペダリング仮想サスティン保持もなければ待機状態（dimmed）へ移行する。
        updateDimmedState();
    }

    void handlePedalChange (bool down, double currentTimeMs)
    {
        if (!isPedalDown && down)
        {
            // ペダルが踏まれた（ON）：現在押している鍵盤、およびリペダリング猶予中に打鍵された仮想保持音を正式なサスティン対象に引き継ぐ
            sustainedNotes = currentlyHeldNotes;
            if (isInRepedalWindow)
            {
                sustainedNotes.insert (repedalNotes.begin(), repedalNotes.end());
                isInRepedalWindow = false;
                repedalNotes.clear();
            }
            isInitialArpeggioForPedalAllowed = true;
        }
        else if (isPedalDown && !down)
        {
            // ペダルが離された（OFF）
            isInitialArpeggioForPedalAllowed = true;
            pedalReleasedSinceLastChord = true;
            sustainedNotes.clear();

            // リペダリング猶予期間（仮想サスティン窓）を開始
            isInRepedalWindow = true;
            repedalWindowStartTimeMs = currentTimeMs;
            repedalNotes.clear();

            // 直前のコード確定時点から押しっぱなしになっている古いノートを除外対象（staleHeldNotes）として記録し、
            // ペダルOFF直前に新しく打鍵されたノート（後踏みペダル用の新和音など）のみを抽出する
            staleHeldNotes = committedHeldNotes;
            std::vector<int> newlyHeldNotes;
            for (int n : currentlyHeldNotes)
            {
                if (staleHeldNotes.find (n) == staleHeldNotes.end())
                    newlyHeldNotes.push_back (n);
            }

            if (currentlyHeldNotes.empty())
            {
                accumulatedNotes.clear();
                if (!currentState.notes.empty())
                {
                    currentState.isDimmed = true;
                    hasActiveCommittedChord = false;
                }
            }
            else if (newlyHeldNotes.empty())
            {
                // 現在押鍵されているノートがすべて直前コードから押しっぱなしの古いノート（または同一コード）である場合：
                // 重複コミット（onNewChord）を抑止し、古いノートが次コードに混ざらないよう蓄積をリセットする
                accumulatedNotes.clear();
            }
            else
            {
                // 後踏みペダル等で、直前コード確定後に新しく打鍵されたノートが押さえられている場合：
                // 踏み直し中の一時的な離鍵でも音が欠けないよう仮想サスティン（repedalNotes）へ保持
                accumulatedNotes.clear();
                for (int n : newlyHeldNotes)
                {
                    accumulatedNotes.insert (n);
                    repedalNotes.insert (n);
                }

                if (isWindowOpen)
                {
                    // 打鍵のクオンタイズ窓が開いている最中であれば、クオンタイズ窓の満了に確定を委ねる
                    hasActiveCommittedChord = false;
                    pedalReleasedSinceLastChord = false;
                }
                else if (newlyHeldNotes.size() >= static_cast<size_t> (minNotesForChord))
                {
                    // ペダル踏下中に既にクオンタイズ窓を過ぎて保持されていた新規和音であれば、
                    // 直前の確定コードと異なる場合のみここで新コードとして確定する
                    if (newlyHeldNotes != currentState.notes)
                    {
                        currentState.notes = newlyHeldNotes;
                        currentState.isChordCommitted = true;
                        currentState.isDimmed = false;
                        hasActiveCommittedChord = true;
                        isBuildingInitialChord = false;
                        isInitialArpeggioForPedalAllowed = false;
                        pedalReleasedSinceLastChord = false;
                        lastCommitTimeMs = currentTimeMs;
                        committedHeldNotes = currentlyHeldNotes;

                        if (onNewChord)
                            onNewChord (currentState.notes);
                    }
                }
                else
                {
                    // 新規押鍵音がまだ最低音数に満たない場合（アルペジオ開始など）
                    hasActiveCommittedChord = false;
                    isBuildingInitialChord = true;
                    pedalReleasedSinceLastChord = false;
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
        repedalNotes.clear();
        staleHeldNotes.clear();
        committedHeldNotes.clear();
        notesInCurrentWindow.clear();
        isInRepedalWindow = false;
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

        // 最低音数フィルタ: 異なるMIDIノート数が minNotesForChord（2音）未満の場合はコードとして確定しない
        // （単音演奏時は外周リングの soundingNotes のみ点灯させ、オクターブユニゾン等の2音以上はコードとして確定する）
        if (accumulatedNotes.size() < static_cast<size_t> (minNotesForChord))
        {
            return;
        }

        std::set<int> uniquePitchClasses;
        for (int n : accumulatedNotes)
            uniquePitchClasses.insert (ChordEvent::normalizePitchClass (n));

        const double elapsedSinceLastNote = (lastNoteOnTimeMs > 0.0) ? (currentTimeMs - lastNoteOnTimeMs) : 99999.0;
        if (numNewNotes >= 3 || (numNewNotes >= minNotesForChord && elapsedSinceLastNote > arpeggioThresholdMs))
        {
            // 同時打鍵で完全な和音が鳴らされた場合（和音ストローク）、アルペジオ初期構築は完了
            isInitialArpeggioForPedalAllowed = false;
            isBuildingInitialChord = false;
        }

        checkAndExpireInitialArpeggio (currentTimeMs);

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
                    const int newPc = ChordEvent::normalizePitchClass (newNote);
                    bool pcAlreadyInChord = false;
                    for (size_t i = 0; i + 1 < candidateNotes.size(); ++i)
                    {
                        if (ChordEvent::normalizePitchClass (candidateNotes[i]) == newPc)
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
                        committedHeldNotes = currentlyHeldNotes;
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
                uint16_t prevPcMask = 0;
                size_t prevPcCount = 0;
                for (int n : currentState.notes)
                {
                    const int pc = ChordEvent::normalizePitchClass (n);
                    if ((prevPcMask & (1u << pc)) == 0)
                    {
                        prevPcMask |= static_cast<uint16_t> (1u << pc);
                        ++prevPcCount;
                    }
                }

                uint16_t candPcMask = 0;
                for (int p : uniquePitchClasses)
                    candPcMask |= static_cast<uint16_t> (1u << p);

                if (prevPcMask != 0 && (prevPcMask & ~candPcMask) == 0 && uniquePitchClasses.size() >= prevPcCount)
                    isSubsetGrowth = true;
            }
        }

        // ペダル保持中のコード変化防止:
        // ペダルが踏まれており、既にコードが確定している場合、
        // ペダル保持中に打鍵されたメロディ・経過音・倚音・反復打鍵等による新コード生成は無視する。
        // （コード進行はペダル踏み直し時に確定される）
        if (isPedalDown && hasActiveCommittedChord && !(isSubsetGrowth && !isNewChordStrike))
        {
            // 最前面コード構成音は維持し、onNewChord（新リング送出）は行わない
            currentState.isDimmed = false;
            currentState.isChordCommitted = true;
            lastCommitTimeMs = currentTimeMs;
            if (!newNotes.empty())
            {
                lastTopNote = newNotes.back();
                lastTopNoteTimeMs = currentTimeMs;
            }
            isNewChordStrike = false;
            return;
        }

        currentState.notes = newNotes;
        committedHeldNotes = currentlyHeldNotes;
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
