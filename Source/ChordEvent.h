/*
  ==============================================================================

    ChordEvent.h
    HarmonyVisualizer - コードイベントデータ構造

    1つのコード（和音）の構成音、ピッチクラス、五度圏位置、
    最低音、トライトーン情報、および3Dタイムトンネル用のZ深度・アルファ値・
    トライトーン解決追跡ライフサイクル状態を保持する。

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
    uint16_t circleMask { 0 };                      // 五度圏ビットマスク（ビット0〜11、O(1)描画判定用）

    int bassNote { -1 };                            // 最低音（ベースノート番号）
    int bassCircleIndex { -1 };                     // 最低音の五度圏インデックス

    std::vector<std::pair<int, int>> tritonePairs;  // トライトーン（半音6離れた音）の五度圏インデックスペア
    std::vector<bool> tritonePairResolved;          // 各トライトーンペアごとの個別解決状態フラグ

    float zPosition { 0.0f };                       // 現在の描画Z位置（0.0f = 最前面）
    float targetZ { 0.0f };                         // 遷移先目標Z位置
    float alpha { 1.0f };                           // 描画透明度（1.0f = 不透明）
    int generation { 0 };                           // 世代番号（0 = 最新のコード, 1, 2... = 過去コード）

    // トライトーン解決追跡・ライフサイクル管理用フラグ
    bool tritoneResolved { true };                  // トライトーンを含まないか、全ペアが解決完了した場合に true
    int resolvedAtGeneration { -1 };                // 全ペアの解決が完了した時点の世代番号（余韻保持用）
    bool isRetiring { false };                      // 規定世代を超えフェードアウト消滅中かどうか

    bool isEmpty() const noexcept
    {
        return midiNotes.empty();
    }

    bool hasTritone() const noexcept
    {
        return !tritonePairs.empty();
    }

    bool hasUnresolvedTritone() const noexcept
    {
        return hasTritone() && !tritoneResolved;
    }

    bool hasCircleIndex (int circleIdx) const noexcept
    {
        return (circleIdx >= 0 && circleIdx < 12) && ((circleMask & (1u << circleIdx)) != 0);
    }

    bool hasPitchClass (int pc) const noexcept
    {
        const int normPc = ((pc % 12) + 12) % 12;
        return hasCircleIndex ((normPc * 7) % 12);
    }

    /**
        五度圏インデックス（0〜11）からピッチクラス（0〜11）へ逆変換する
        (pc * 7) % 12 == circleIdx  <=>  pc = (circleIdx * 7) % 12
    */
    static int circleIndexToPitchClass (int circleIdx) noexcept
    {
        return (((circleIdx % 12) + 12) * 7) % 12;
    }

    /**
        1組のトライトーンペア（五度圏インデックスペア）が後続コード subsequentChord によって
        音楽理論的に解決されたかを判定する。

        判定基準:
        1. 同一のトライトーンペアが subsequentChord 内にそのまま残存している場合は未解決。
        2. トライトーンをなす2音 (pcA, pcB) のそれぞれが、subsequentChord の構成音へ
           半音または全音（1〜2半音）で順次解決していること。
        3. 一方の音が1〜2半音で移動し、もう一方が共通音として保持される場合でも、
           保持された音が subsequentChord 内で新たなトライトーンや掛留音（sus4 = ベースから5半音）等の
           未解決不協和を形成していない場合のみ解決とみなす（G7 -> Csus4 -> C の段階的解決に対応）。
    */
    static bool isTritonePairResolvedBy (const std::pair<int, int>& tritoneCirclePair,
                                         const ChordEvent& subsequentChord) noexcept
    {
        if (subsequentChord.isEmpty())
            return false;

        // 同一トライトーンがまだ鳴っている場合は未解決
        if (subsequentChord.hasCircleIndex (tritoneCirclePair.first)
            && subsequentChord.hasCircleIndex (tritoneCirclePair.second))
        {
            return false;
        }

        const int pcA = circleIndexToPitchClass (tritoneCirclePair.first);
        const int pcB = circleIndexToPitchClass (tritoneCirclePair.second);

        auto movesByStepToChord = [&] (int pc) -> bool
        {
            return subsequentChord.hasPitchClass (pc + 1)
                || subsequentChord.hasPitchClass (pc + 11)
                || subsequentChord.hasPitchClass (pc + 2)
                || subsequentChord.hasPitchClass (pc + 10);
        };

        auto isConsonantCommonToneInChord = [&] (int pc) -> bool
        {
            if (!subsequentChord.hasPitchClass (pc))
                return false;

            // ベース音に対して完全4度（5半音 = sus4）またはトライトーン（6半音）を形成している場合は未解決の緊張音
            if (subsequentChord.bassNote >= 0)
            {
                const int bassPc = ((subsequentChord.bassNote % 12) + 12) % 12;
                const int intervalFromBass = (pc - bassPc + 12) % 12;
                if (intervalFromBass == 5 || intervalFromBass == 6)
                    return false;
            }

            // 他音とトライトーンまたは短2度を形成している場合も未解決
            for (int otherPc : subsequentChord.pitchClasses)
            {
                if (otherPc == pc)
                    continue;
                const int diff = std::min (std::abs (pc - otherPc), 12 - std::abs (pc - otherPc));
                if (diff == 6 || diff == 1)
                    return false;
            }
            return true;
        };

        const bool stepA = movesByStepToChord (pcA);
        const bool stepB = movesByStepToChord (pcB);
        const bool heldA = subsequentChord.hasPitchClass (pcA);
        const bool heldB = subsequentChord.hasPitchClass (pcB);

        // もし一方の音がそのまま保持されており（例: G7 -> Csus4 における F音保持）、
        // それが sus4 等の未解決緊張音である場合は、まだ解決途上（未解決）と判定する
        if (heldA && !isConsonantCommonToneInChord (pcA))
            return false;
        if (heldB && !isConsonantCommonToneInChord (pcB))
            return false;

        // 両音が1〜2半音で解決したか、一方が1〜2半音解決＋他方が協和共通音として落ち着いた場合に解決完了
        if (stepA && stepB)
            return true;
        if ((stepA && isConsonantCommonToneInChord (pcB)) || (stepB && isConsonantCommonToneInChord (pcA)))
            return true;

        // オクターブユニゾン（単一ピッチクラス・2音以上）への収束解決（例: G7 -> C2+C3）:
        // 第3音等が省略されるため、トライトーン構成音の片方（導音 B->C 等）がユニゾン音へ順次解決していれば解決完了とみなす
        if (subsequentChord.pitchClasses.size() == 1 && subsequentChord.midiNotes.size() >= 2)
        {
            if (stepA || stepB)
                return true;
        }

        return false;
    }

    /**
        指定した五度圏インデックスのトライトーンペア (idxA, idxB) が既に解決済みかを返す
    */
    bool isTritonePairResolved (int idxA, int idxB) const noexcept
    {
        const int first = std::min (idxA, idxB);
        const int second = std::max (idxA, idxB);
        for (size_t k = 0; k < tritonePairs.size(); ++k)
        {
            const int pFirst = std::min (tritonePairs[k].first, tritonePairs[k].second);
            const int pSecond = std::max (tritonePairs[k].first, tritonePairs[k].second);
            if (pFirst == first && pSecond == second)
            {
                return (k < tritonePairResolved.size()) ? tritonePairResolved[k] : tritoneResolved;
            }
        }
        return true;
    }

    /**
        後続コード subsequentChord に対して未解決のトライトーンペアを個別に判定・更新し、
        すべてのペアが解決完了したかどうかを返す。
        - dim7 のように2組のトライトーンを持つ場合、片方だけ解決した段階ではそのペアのみ解決済みとなり、
          もう片方が解決されるまでコード自体は保持される。
        - また、後続コードが新たな（別ペアの）トライトーンを持つ場合は緊張の交代（上書き）として解決扱いにする。
    */
    bool updatePairResolutionsAgainst (const ChordEvent& subsequentChord) noexcept
    {
        if (tritonePairs.empty())
        {
            tritoneResolved = true;
            return true;
        }

        if (tritonePairResolved.size() != tritonePairs.size())
            tritonePairResolved.assign (tritonePairs.size(), false);

        bool allResolved = true;
        for (size_t k = 0; k < tritonePairs.size(); ++k)
        {
            if (!tritonePairResolved[k])
            {
                const auto& pair = tritonePairs[k];
                const bool samePairRemains = subsequentChord.hasCircleIndex (pair.first)
                                          && subsequentChord.hasCircleIndex (pair.second);

                // 1. このペア自身が和声学的に順次解決した場合
                // 2. または、このペアは残っておらず後続コードに新しい別のトライトーンが出現した場合（緊張の交代上書き）
                if (isTritonePairResolvedBy (pair, subsequentChord)
                    || (!samePairRemains && subsequentChord.hasTritone()))
                {
                    tritonePairResolved[k] = true;
                }
            }

            if (!tritonePairResolved[k])
                allResolved = false;
        }

        tritoneResolved = allResolved;
        return allResolved;
    }

    /**
        このコードが持つすべてのトライトーンが subsequentChord によって解決されるかを判定する（const版）
    */
    bool checkResolutionAgainst (const ChordEvent& subsequentChord) const noexcept
    {
        if (tritonePairs.empty())
            return true;

        for (size_t k = 0; k < tritonePairs.size(); ++k)
        {
            const bool already = (k < tritonePairResolved.size()) ? tritonePairResolved[k] : false;
            if (!already && !isTritonePairResolvedBy (tritonePairs[k], subsequentChord))
                return false;
        }
        return true;
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
        chord.bassCircleIndex = (((chord.bassNote % 12) + 12) * 7) % 12;

        std::vector<int> uniquePcs;
        std::vector<int> uniqueCircles;

        for (int note : chord.midiNotes)
        {
            const int pc = ((note % 12) + 12) % 12;
            const int circleIdx = (pc * 7) % 12;

            if (std::find (uniquePcs.begin(), uniquePcs.end(), pc) == uniquePcs.end())
                uniquePcs.push_back (pc);

            if (std::find (uniqueCircles.begin(), uniqueCircles.end(), circleIdx) == uniqueCircles.end())
            {
                uniqueCircles.push_back (circleIdx);
                chord.circleMask |= static_cast<uint16_t> (1u << circleIdx);
            }
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

        chord.tritonePairResolved.assign (chord.tritonePairs.size(), false);
        chord.tritoneResolved = chord.tritonePairs.empty();
        return chord;
    }
};
