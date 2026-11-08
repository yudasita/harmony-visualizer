/*
  ==============================================================================

    ChordBoundaryDetector.h
    HarmonyVisualizer - コード境界判定エンジン

    クオンタイズ窓（同時打鍵のズレ吸収）とダンパーペダル（CC64 / ピッチベンド代用）監視、
    およびアルペジオ演奏判定とダイアド（2音以上）フィルタリングにより、
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

        if (isWindowOpen)
        {
            if (currentTimeMs >= windowStartTimeMs + quantizeWindowMs)
            {
                // クオンタイズ窓が満了
                commitAccumulatedNotes (currentTimeMs);
                isWindowOpen = false;
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
        isCc64Down = false;
        isPitchBendPedalDown = false;
        isPedalDown = false;
        pedalReleasedSinceLastChord = false;
        isWindowOpen = false;
        windowStartTimeMs = 0.0;
        lastCommitTimeMs = -9999.0;
        lastNoteOnTimeMs = -9999.0;
        hasActiveCommittedChord = false;
        currentState = ChordState();
    }

private:
    double quantizeWindowMs { 60.0 };
    double windowStartTimeMs { 0.0 };
    bool isWindowOpen { false };

    int minNotesForChord { 2 };              // コード認識に必要な最低音数（単音を排除）
    double arpeggioThresholdMs { 450.0 };    // アルペジオ許容時間窓(ms)
    double lastNoteOnTimeMs { -9999.0 };     // 直前の打鍵時刻
    bool hasActiveCommittedChord { false };  // 現在最前面にコミット済みコードがあるか

    bool isCc64Down { false };
    bool isPitchBendPedalDown { false };
    bool isPedalDown { false };
    bool pedalReleasedSinceLastChord { false };

    std::set<int> currentlyHeldNotes;
    std::set<int> accumulatedNotes;
    std::set<int> sustainedNotes;

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
        // 1. ペダルが踏まれている、または直前からの押鍵が残っている（レガート/和音押さえ）
        // 2. または、直前のノートオンから arpeggioThresholdMs 以内の打鍵である
        // 3. かつ、ペダル解放（踏み直し）直後ではなく、演奏待機状態（dimmed）でもない
        const bool isLegatoOrPedal = isPedalDown || (currentlyHeldNotes.size() > 1);
        const bool isWithinArpeggioWindow = (elapsedSinceLastNote <= arpeggioThresholdMs);
        const bool isArpeggioContinuation = (isLegatoOrPedal || isWithinArpeggioWindow)
                                            && !pedalReleasedSinceLastChord
                                            && !currentState.isDimmed;

        if (!isArpeggioContinuation)
        {
            // 新規コードの開始：過去の古い蓄積をクリアし、現在押さえている音から再蓄積
            accumulatedNotes = currentlyHeldNotes;
            pedalReleasedSinceLastChord = false;
            hasActiveCommittedChord = false;
        }
        else
        {
            // アルペジオ継続：既存の蓄積に新しい音を追加
            accumulatedNotes.insert (noteNumber);
        }

        currentState.isDimmed = false;
        refreshSoundingNotes();

        // クオンタイズ窓の開始または延長
        isWindowOpen = true;
        windowStartTimeMs = currentTimeMs;
    }

    void handleNoteOff (int noteNumber, double currentTimeMs)
    {
        juce::ignoreUnused (currentTimeMs);
        currentlyHeldNotes.erase (noteNumber);
        refreshSoundingNotes();

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
        }
        else if (isPedalDown && !down)
        {
            // ペダルが離された（OFF）
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
        isWindowOpen = false;
        hasActiveCommittedChord = false;
        refreshSoundingNotes();

        if (!currentState.notes.empty())
            currentState.isDimmed = true;
    }

    void commitAccumulatedNotes (double currentTimeMs)
    {
        if (accumulatedNotes.empty())
            return;

        // 最低音数フィルタ: ピッチクラス数が minNotesForChord 未満の場合はコードとして確定しない
        // （単音演奏時は外周リングの soundingNotes のみ点灯させ、コード履歴キューへは送らない）
        std::set<int> uniquePitchClasses;
        for (int n : accumulatedNotes)
            uniquePitchClasses.insert (((n % 12) + 12) % 12);

        if (uniquePitchClasses.size() < static_cast<size_t> (minNotesForChord))
        {
            return;
        }

        std::vector<int> newNotes (accumulatedNotes.begin(), accumulatedNotes.end());
        const bool isDifferentChord = (newNotes != currentState.notes);

        // 既存の最前面コードに対する「同一コードの成長（アルペジオ追加）」かどうかの判定:
        // 過去の構成音のピッチクラスが、新しい構成音のピッチクラスにすべて含まれている（包含されている）場合
        bool isSubsetGrowth = false;
        if (hasActiveCommittedChord && !currentState.notes.empty())
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

        currentState.notes = newNotes;
        currentState.isDimmed = false;
        currentState.isChordCommitted = true;
        lastCommitTimeMs = currentTimeMs;

        if (isDifferentChord)
        {
            if (hasActiveCommittedChord && isSubsetGrowth)
            {
                // アルペジオによる構成音追加：最前面（Z=0）を上書き更新（奥へは押し出さない）
                if (onUpdateChord)
                    onUpdateChord (currentState.notes);
            }
            else
            {
                // 新コードへの進行：直前のコードを奥へ送り、新コードを最前面へプッシュ
                hasActiveCommittedChord = true;
                if (onNewChord)
                    onNewChord (currentState.notes);
            }
        }
    }
};
