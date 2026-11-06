/*
  ==============================================================================

    ChordEvent.h
    HarmonyVisualizer - コードイベントデータ構造

    1つのコード（和音）の構成音、ピッチクラス、五度圏位置、
    最低音、トライトーン情報、および3Dタイムトンネル用のZ深度・アルファ値を保持する。

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <utility>
#include <algorithm>
#include <cmath>

struct ChordEvent
{
    std::vector<int> midiNotes;                     // 生MIDIノート番号一覧（ソート済み）
    std::vector<int> pitchClasses;                  // ピッチクラス（0〜11、重複排除済み）
    std::vector<int> circleIndices;                 // 五度圏インデックス（0〜11、重複排除済み）

    int bassNote { -1 };                           // 最低音（ベースノート番号）
    int bassCircleIndex { -1 };                    // 最低音の五度圏インデックス

    std::vector<std::pair<int, int>> tritonePairs; // トライトーン（半音6離れた音）の五度圏インデックスペア

    float zPosition { 0.0f };                      // 現在の描画Z位置（0.0f = 最前面）
    float targetZ { 0.0f };                        // 遷移先目標Z位置
    float alpha { 1.0f };                          // 描画透明度（1.0f = 不透明）
    int generation { 0 };                          // 世代番号（0 = 最新のコード, 1, 2... = 過去コード）

    bool isEmpty() const noexcept
    {
        return midiNotes.empty();
    }

    /**
        MIDIノート番号の集合から ChordEvent を構築するファクトリメソッド
    */
    static ChordEvent fromMidiNotes (const std::vector<int>& notes)
    {
        ChordEvent chord;
        if (notes.empty())
            return chord;

        chord.midiNotes = notes;
        std::sort (chord.midiNotes.begin(), chord.midiNotes.end());

        chord.bassNote = chord.midiNotes.front();
        chord.bassCircleIndex = ((chord.bassNote % 12) * 7) % 12;

        std::vector<int> uniquePcs;
        std::vector<int> uniqueCircles;

        for (int note : chord.midiNotes)
        {
            const int pc = ((note % 12) + 12) % 12;
            const int circleIdx = (pc * 7) % 12;

            if (std::find (uniquePcs.begin(), uniquePcs.end(), pc) == uniquePcs.end())
                uniquePcs.push_back (pc);

            if (std::find (uniqueCircles.begin(), uniqueCircles.end(), circleIdx) == uniqueCircles.end())
                uniqueCircles.push_back (circleIdx);
        }

        std::sort (uniquePcs.begin(), uniquePcs.end());
        std::sort (uniqueCircles.begin(), uniqueCircles.end());

        chord.pitchClasses = std::move (uniquePcs);
        chord.circleIndices = std::move (uniqueCircles);

        // トライトーン（半音6 / 五度圏対角線距離6）ペアの検出
        for (size_t i = 0; i < chord.circleIndices.size(); ++i)
        {
            for (size_t j = i + 1; j < chord.circleIndices.size(); ++j)
            {
                const int dist = std::abs (chord.circleIndices[i] - chord.circleIndices[j]);
                if (dist == 6)
                {
                    chord.tritonePairs.emplace_back (chord.circleIndices[i], chord.circleIndices[j]);
                }
            }
        }

        return chord;
    }
};
