/*
  ==============================================================================

    ChordBoundaryDetector.h
    HarmonyVisualizer - コード境界判定エンジン

    クオンタイズ窓（同時打鍵のズレ吸収）とダンパーペダル（CC64）監視により、
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

class ChordBoundaryDetector
{
public:
    struct ChordState
    {
        std::vector<int> notes;          // 構成音（MIDIノート番号ソート済み）
        bool isDimmed { false };         // 演奏待機状態（離鍵＆ペダルOFFで薄く表示）
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
                handlePedalChange (event.value >= 64, currentTimeMs);
            }
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
                commitAccumulatedNotes();
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

    using NewChordCallback = std::function<void (const std::vector<int>& notes)>;
    void setNewChordCallback (NewChordCallback callback) { onNewChord = std::move (callback); }

    void reset()
    {
        currentlyHeldNotes.clear();
        accumulatedNotes.clear();
        isPedalDown = false;
        pedalReleasedSinceLastChord = false;
        isWindowOpen = false;
        windowStartTimeMs = 0.0;
        currentState = ChordState();
    }

private:
    double quantizeWindowMs { 60.0 };
    double windowStartTimeMs { 0.0 };
    bool isWindowOpen { false };

    bool isPedalDown { false };
    bool pedalReleasedSinceLastChord { false };

    std::set<int> currentlyHeldNotes;
    std::set<int> accumulatedNotes;

    ChordState currentState;
    NewChordCallback onNewChord;

    void handleNoteOn (int noteNumber, double currentTimeMs)
    {
        currentlyHeldNotes.insert (noteNumber);

        // 新コード判定の契機:
        // 1. 直前の全離鍵状態（dimmed状態）からの最初の打鍵
        // 2. ペダル踏み直し（ペダルOFF後に新たな打鍵が開始された）
        // 3. 現在クオンタイズ窓が開いておらず、かつ全鍵盤が離された後の打鍵
        const bool wasAllReleased = (currentlyHeldNotes.size() == 1 && !isPedalDown);
        const bool shouldStartNewChord = currentState.isDimmed || pedalReleasedSinceLastChord || wasAllReleased;

        if (shouldStartNewChord && !isWindowOpen)
        {
            accumulatedNotes.clear();
            pedalReleasedSinceLastChord = false;
        }

        accumulatedNotes.insert (noteNumber);
        currentState.isDimmed = false;

        // クオンタイズ窓の開始または継続
        if (!isWindowOpen)
        {
            isWindowOpen = true;
            windowStartTimeMs = currentTimeMs;
        }
    }

    void handleNoteOff (int noteNumber, double currentTimeMs)
    {
        juce::ignoreUnused (currentTimeMs);
        currentlyHeldNotes.erase (noteNumber);

        // ペダルを踏んでいない場合、離鍵された音は現在のコードから除外するか？
        // クオンタイズ窓が閉じている状態で鍵盤を離した場合は、ペダルがなければ持続しない
        if (!isPedalDown && !isWindowOpen)
        {
            // ペダルなしで離鍵された場合、現在鳴っている音のみに更新
            if (!currentlyHeldNotes.empty())
            {
                std::vector<int> activeList (currentlyHeldNotes.begin(), currentlyHeldNotes.end());
                if (activeList != currentState.notes)
                {
                    currentState.notes = std::move (activeList);
                }
            }
        }
    }

    void handlePedalChange (bool down, double currentTimeMs)
    {
        juce::ignoreUnused (currentTimeMs);

        if (isPedalDown && !down)
        {
            // ペダルが離された（OFF）
            pedalReleasedSinceLastChord = true;

            // 鍵盤も押されていなければ、直ちにdimmed待機状態へ
            if (currentlyHeldNotes.empty() && !currentState.notes.empty())
            {
                currentState.isDimmed = true;
            }
            else
            {
                // 鍵盤が押されている音だけを残す
                accumulatedNotes = currentlyHeldNotes;
            }
        }

        isPedalDown = down;
    }

    void handleAllNotesOff (double currentTimeMs)
    {
        juce::ignoreUnused (currentTimeMs);
        currentlyHeldNotes.clear();
        accumulatedNotes.clear();
        isWindowOpen = false;

        if (!currentState.notes.empty())
            currentState.isDimmed = true;
    }

    void commitAccumulatedNotes()
    {
        if (accumulatedNotes.empty())
            return;

        std::vector<int> newNotes (accumulatedNotes.begin(), accumulatedNotes.end());

        // 直前の確定コードと異なる場合、または待機状態からの復帰時に確定イベントを発行
        const bool isDifferentChord = (newNotes != currentState.notes);
        currentState.notes = newNotes;
        currentState.isDimmed = false;
        currentState.isChordCommitted = true;

        if (isDifferentChord && onNewChord)
        {
            onNewChord (currentState.notes);
        }
    }
};
