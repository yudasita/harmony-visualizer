/*
  ==============================================================================

    ChordHistoryQueue.h
    HarmonyVisualizer - コード履歴キュー

    タイムトンネル描画用として、最新コード（Z=0）および過去数世代の
    ChordEvent を履歴キュー（std::deque）で管理・アニメーション更新する。

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ChordEvent.h"
#include <deque>

class ChordHistoryQueue
{
public:
    explicit ChordHistoryQueue (size_t maxSize = 8, float zSpacing = 1.0f)
        : maxHistorySize (maxSize), spacingBetweenGenerations (zSpacing)
    {
    }

    void setMaxHistorySize (size_t newSize)
    {
        maxHistorySize = std::max<size_t> (1, newSize);
        shrinkToFit();
    }

    size_t getMaxHistorySize() const noexcept { return maxHistorySize; }

    void setZSpacing (float spacing) noexcept
    {
        spacingBetweenGenerations = std::max (0.1f, spacing);
    }

    float getZSpacing() const noexcept { return spacingBetweenGenerations; }

    /** 新コードが確定した際にキューの最前面（Z=0）へ追加する */
    void pushChord (ChordEvent chord)
    {
        // 既存のコードを1世代奥へシフト
        for (auto& existing : history)
        {
            existing.generation += 1;
            existing.targetZ = static_cast<float> (existing.generation) * spacingBetweenGenerations;
        }

        chord.generation = 0;
        chord.zPosition = 0.0f;
        chord.targetZ = 0.0f;
        chord.alpha = 1.0f;

        history.push_front (std::move (chord));
        shrinkToFit();
    }

    /** 最前面の最新コードを上書き更新（アルペジオ等で同一コード内の構成音が増えた場合など） */
    void updateLatestChord (const ChordEvent& chord)
    {
        if (history.empty())
        {
            pushChord (chord);
            return;
        }

        auto& front = history.front();
        const float currentZ = front.zPosition;
        const float targetZ = front.targetZ;
        const float alpha = front.alpha;
        const int gen = front.generation;

        front = chord;
        front.zPosition = currentZ;
        front.targetZ = targetZ;
        front.alpha = alpha;
        front.generation = gen;
    }

    /** アニメーション（Z深度のスムーズ補間・アルファ減衰）を更新 */
    void updateAnimation (float smoothingFactor = 0.2f)
    {
        const float maxZ = static_cast<float> (maxHistorySize) * spacingBetweenGenerations;

        for (auto& item : history)
        {
            // Z位置のスムーズ補間 (exponential lerp)
            item.zPosition += (item.targetZ - item.zPosition) * smoothingFactor;

            // 奥（Zが大きい）ほどアルファ値をフェードアウト
            const float normalizedZ = item.zPosition / maxZ;
            item.alpha = juce::jlimit (0.0f, 1.0f, 1.0f - normalizedZ * 0.85f);
        }
    }

    const std::deque<ChordEvent>& getChords() const noexcept { return history; }
    std::deque<ChordEvent>& getChords() noexcept { return history; }

    const ChordEvent* getLatestChord() const noexcept
    {
        return history.empty() ? nullptr : &history.front();
    }

    bool isEmpty() const noexcept { return history.empty(); }
    size_t size() const noexcept { return history.size(); }

    void clear()
    {
        history.clear();
    }

private:
    size_t maxHistorySize { 8 };
    float spacingBetweenGenerations { 1.0f };
    std::deque<ChordEvent> history;

    void shrinkToFit()
    {
        while (history.size() > maxHistorySize)
        {
            history.pop_back();
        }
    }
};
