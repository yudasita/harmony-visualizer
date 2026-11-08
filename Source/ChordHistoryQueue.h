/*
  ==============================================================================

    ChordHistoryQueue.h
    MeLoom - コード履歴キューとトライトーン解決追跡ライフサイクル

    タイムトンネル描画用として、最新コード（Z=0）および過去数世代の
    ChordEvent を履歴キュー（std::deque）で管理・アニメーション更新する。
    トライトーンを含む緊張コードが後続コードで解決されるまで奥に保持し、
    解決完了または上限到達時に滑らかにイージング・フェードアウトさせて破棄する。

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ChordEvent.h"
#include <deque>
#include <algorithm>
#include <cmath>

class ChordHistoryQueue
{
public:
    explicit ChordHistoryQueue (size_t maxSize = 8,
                                float zSpacing = 1.0f,
                                size_t maxUnresolvedSize = 12)
        : maxHistorySize (std::max<size_t> (1, maxSize)),
          maxUnresolvedHistorySize (std::max (maxSize, maxUnresolvedSize)),
          spacingBetweenGenerations (std::max (0.1f, zSpacing))
    {
    }

    void setMaxHistorySize (size_t newSize)
    {
        maxHistorySize = std::max<size_t> (1, newSize);
        if (maxUnresolvedHistorySize < maxHistorySize)
            maxUnresolvedHistorySize = maxHistorySize + 4;
        evaluateRetirementStates();
    }

    size_t getMaxHistorySize() const noexcept { return maxHistorySize; }

    void setMaxUnresolvedHistorySize (size_t newMaxUnresolved) noexcept
    {
        maxUnresolvedHistorySize = std::max (maxHistorySize, newMaxUnresolved);
        evaluateRetirementStates();
    }

    size_t getMaxUnresolvedHistorySize() const noexcept { return maxUnresolvedHistorySize; }

    void setZSpacing (float spacing) noexcept
    {
        spacingBetweenGenerations = std::max (0.1f, spacing);
    }

    float getZSpacing() const noexcept { return spacingBetweenGenerations; }

    /** 新コードが確定した際にキューの最前面（Z=0）へ追加する */
    void pushChord (ChordEvent chord)
    {
        // 1. 既存の過去コードを1世代奥へシフトし、新コードによるトライトーン解決を判定
        for (auto& existing : history)
        {
            existing.generation += 1;
            existing.targetZ = static_cast<float> (existing.generation) * spacingBetweenGenerations;

            if (existing.hasUnresolvedTritone())
            {
                // 各トライトーンペアごとに個別に解決（または新トライトーンへの交代）を判定・更新し、
                // すべてのペアが解決完了した時点で resolvedAtGeneration を記録する
                if (existing.updatePairResolutionsAgainst (chord))
                {
                    existing.resolvedAtGeneration = existing.generation;
                }
            }
        }

        // 2. 新コードを最前面（generation = 0, Z = 0）として挿入
        chord.generation = 0;
        chord.zPosition = 0.0f;
        chord.targetZ = 0.0f;
        chord.alpha = 1.0f;
        chord.tritonePairResolved.assign (chord.tritonePairs.size(), false);
        chord.tritoneResolved = !chord.hasTritone();
        chord.resolvedAtGeneration = -1;
        chord.isRetiring = false;

        history.push_front (std::move (chord));

        // 3. 解決状態と世代数に基づいて退役（フェードアウト開始）フラグを更新
        evaluateRetirementStates();
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
        front.tritonePairResolved.assign (front.tritonePairs.size(), false);
        front.tritoneResolved = !front.hasTritone();
        front.isRetiring = false;

        // 最新コードの構成音更新に伴い、過去の未解決トライトーンペアが解決されたかも個別に再チェック
        for (size_t i = 1; i < history.size(); ++i)
        {
            auto& past = history[i];
            if (past.hasUnresolvedTritone())
            {
                if (past.updatePairResolutionsAgainst (front))
                {
                    past.resolvedAtGeneration = past.generation;
                }
            }
        }

        evaluateRetirementStates();
    }

    /**
        60Hzタイマーコールバック内で呼ばれるアニメーション更新処理
        - Z深度のイージング補間（currentZ += (targetZ - currentZ) * smoothingFactor）
        - 未解決トライトーン保持コードの視認性維持（最小アルファ保証）
        - 退役コード（isRetiring）のスムーズなフェードアウトとデキュー破棄
    */
    void updateAnimation (float smoothingFactor = 0.2f)
    {
        const float clampedFactor = juce::jlimit (0.01f, 1.0f, smoothingFactor);
        const float normalMaxZ = static_cast<float> (maxHistorySize) * spacingBetweenGenerations;
        const float extendedMaxZ = static_cast<float> (maxUnresolvedHistorySize) * spacingBetweenGenerations;

        for (auto& item : history)
        {
            // Z位置のスムーズ補間 (exponential easing)
            item.zPosition += (item.targetZ - item.zPosition) * clampedFactor;

            if (item.isRetiring)
            {
                // 退役中のコードは奥へ後退しながらアルファ値を 0 へ滑らかに減衰
                item.alpha += (0.0f - item.alpha) * std::min (1.0f, clampedFactor * 1.35f);
                if (item.alpha < 0.015f)
                    item.alpha = 0.0f;
            }
            else if (item.hasUnresolvedTritone())
            {
                // 未解決のトライトーンを含むコードは解決されるまで奥にとどまり、
                // 通常の減衰より粘り強い最小アルファ（0.28f）を維持して緊張の残存を可視化する
                const float normalizedZ = item.zPosition / std::max (1.0f, extendedMaxZ);
                const float targetAlpha = juce::jlimit (0.28f, 1.0f, 1.0f - normalizedZ * 0.72f);
                item.alpha += (targetAlpha - item.alpha) * clampedFactor;
            }
            else
            {
                // 通常のコード（または解決完了後のコード）の深度アルファ減衰
                const float normalizedZ = item.zPosition / std::max (1.0f, normalMaxZ);
                const float targetAlpha = juce::jlimit (0.05f, 1.0f, 1.0f - normalizedZ * 0.85f);
                item.alpha += (targetAlpha - item.alpha) * clampedFactor;
            }
        }

        // フェードアウトが完了した末尾の退役コードをクリーンに破棄
        pruneExpiredChords();
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
    size_t maxUnresolvedHistorySize { 12 };
    int postResolutionHoldGenerations { 1 }; // トライトーン解決後、カタルシスの余韻として保持する世代数
    float spacingBetweenGenerations { 1.0f };
    std::deque<ChordEvent> history;

    /**
        各コードの世代番号とトライトーン解決状態に応じて退役（isRetiring）状態を判定する
    */
    void evaluateRetirementStates()
    {
        for (size_t i = 1; i < history.size(); ++i)
        {
            auto& item = history[i];
            if (item.isRetiring)
                continue;

            const size_t gen = static_cast<size_t> (std::max (0, item.generation));

            if (item.hasUnresolvedTritone())
            {
                // 未解決トライトーンは通常の maxHistorySize を超えても保持し、
                // maxUnresolvedHistorySize（ハード上限）に達した場合のみ退役させる
                if (gen >= maxUnresolvedHistorySize)
                {
                    markAsRetiring (item);
                }
            }
            else if (item.hasTritone() && item.tritoneResolved && item.resolvedAtGeneration > 0)
            {
                // トライトーンが解決されたコードは、解決直後の余韻（postResolutionHoldGenerations）を
                // 経てから、かつ maxHistorySize 以上になった時点で退役させる
                const int generationsSinceResolved = item.generation - item.resolvedAtGeneration;
                if (gen >= maxHistorySize && generationsSinceResolved >= postResolutionHoldGenerations)
                {
                    markAsRetiring (item);
                }
            }
            else
            {
                // トライトーンを含まない通常コードは maxHistorySize 到達で退役開始
                if (gen >= maxHistorySize)
                {
                    markAsRetiring (item);
                }
            }
        }

        // メモリ安全性のため、万が一キューサイズがハード上限 + 4 を超えた場合は末尾を即時破棄
        const size_t hardCap = maxUnresolvedHistorySize + 4;
        while (history.size() > hardCap)
        {
            history.pop_back();
        }
    }

    void markAsRetiring (ChordEvent& item) noexcept
    {
        item.isRetiring = true;
        item.targetZ = (static_cast<float> (item.generation) + 0.85f) * spacingBetweenGenerations;
    }

    void pruneExpiredChords()
    {
        while (history.size() > 1)
        {
            const auto& back = history.back();
            if (back.isRetiring && back.alpha <= 0.02f)
            {
                history.pop_back();
            }
            else
            {
                break;
            }
        }
    }
};
