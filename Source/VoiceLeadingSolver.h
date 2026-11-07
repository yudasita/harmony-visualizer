/*
  ==============================================================================

    VoiceLeadingSolver.h
    HarmonyVisualizer - ボイスリーディング解析と声部接続ソルバー

    直前コード（奥・fromChord）から現在コード（手前・toChord）への遷移における
    声部連結（ボイスリーディング）の最小移動コストペアリングを計算する。
    最低音（ベースライン優先）、共通音（Pivot Note 保持）、トライトーン解決、
    最短半音移動、多対1の合流マッチングを提供する。

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ChordEvent.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <array>
#include <limits>

struct VoiceLeadingConnection
{
    enum class Type
    {
        BassLine,           // 最低音同士の接続（音程差不問・最優先）
        PivotNote,          // 共通音の保持（同ピッチクラス・平行接続）
        Stepwise,           // 半音/全音進行（1〜2半音の近接移動）
        TritoneResolution,  // トライトーン解決（トライトーン構成音の半音・全音解決）
        Converged           // 多対1の合流（複数声部が同一ノートへ収束）
    };

    int fromMidiNote { -1 };
    int fromPitchClass { -1 };
    int fromCircleIndex { -1 };

    int toMidiNote { -1 };
    int toPitchClass { -1 };
    int toCircleIndex { -1 };

    Type type { Type::Stepwise };
    int semitoneDistance { 0 };       // toMidiNote - fromMidiNote
    int circleDistance { 0 };         // 五度圏上の最短距離（0〜6）

    VoiceLeadingConnection() = default;

    VoiceLeadingConnection (int fromNote, int toNote, Type connectionType)
        : fromMidiNote (fromNote),
          toMidiNote (toNote),
          type (connectionType),
          semitoneDistance (toNote - fromNote)
    {
        fromPitchClass = ((fromNote % 12) + 12) % 12;
        fromCircleIndex = (fromPitchClass * 7) % 12;

        toPitchClass = ((toNote % 12) + 12) % 12;
        toCircleIndex = (toPitchClass * 7) % 12;

        int cDist = std::abs (fromCircleIndex - toCircleIndex);
        if (cDist > 6)
            cDist = 12 - cDist;
        circleDistance = cDist;
    }

    bool isBassLine() const noexcept          { return type == Type::BassLine; }
    bool isPivotNote() const noexcept         { return type == Type::PivotNote; }
    bool isStepwise() const noexcept          { return type == Type::Stepwise; }
    bool isTritoneResolution() const noexcept { return type == Type::TritoneResolution; }
    bool isConverged() const noexcept         { return type == Type::Converged; }

    static juce::String getTypeName (Type t)
    {
        switch (t)
        {
            case Type::BassLine:          return "BassLine";
            case Type::PivotNote:         return "PivotNote";
            case Type::Stepwise:          return "Stepwise";
            case Type::TritoneResolution: return "TritoneResolution";
            case Type::Converged:         return "Converged";
        }
        return "Unknown";
    }
};

class VoiceLeadingSolver
{
public:
    VoiceLeadingSolver() = default;

    /**
        2つのコードイベント（直前コード fromChord → 現在コード toChord）間の
        声部連結リストを計算する。

        アルゴリズム:
        1. ルール1 (ベースライン優先):
           最低音同士 (bassNote) は音程距離に関わらず必ず最優先で接続 (Type::BassLine)。
        2. ルール2 (共通音の保持 / PivotNote):
           残りの上声部について、同一ピッチクラスを持つノート同士を最小半音差で平行接続 (Type::PivotNote)。
        3. ルール3 (最短半音移動 ＆ 合流マッチング):
           残りの上声部について、半音・全音の近い音へ最小コストでペアリング。
           - 距離1〜2半音かつトライトーン構成音の場合は Type::TritoneResolution。
           - その他の1対1近接ペアリングは Type::Stepwise。
           - 多対1の合流（残余ノートの収束）は Type::Converged。

        @param fromChord 奥のコード（過去の世代）
        @param toChord   手前のコード（最新または直前の世代）
        @return 計算された声部接続リスト
    */
    static std::vector<VoiceLeadingConnection> solve (const ChordEvent& fromChord,
                                                     const ChordEvent& toChord)
    {
        std::vector<VoiceLeadingConnection> connections;

        if (fromChord.isEmpty() || toChord.isEmpty())
            return connections;

        // ---------------------------------------------------------------------
        // ルール1: ベースライン優先接続
        // ---------------------------------------------------------------------
        const int fromBass = (fromChord.bassNote >= 0) ? fromChord.bassNote : fromChord.midiNotes.front();
        const int toBass   = (toChord.bassNote >= 0)   ? toChord.bassNote   : toChord.midiNotes.front();

        connections.emplace_back (fromBass, toBass, VoiceLeadingConnection::Type::BassLine);

        // 上声部（最低音を除いた残り）の抽出
        std::vector<int> fromUpper;
        if (fromChord.midiNotes.size() > 1)
            fromUpper.assign (fromChord.midiNotes.begin() + 1, fromChord.midiNotes.end());

        std::vector<int> toUpper;
        if (toChord.midiNotes.size() > 1)
            toUpper.assign (toChord.midiNotes.begin() + 1, toChord.midiNotes.end());

        if (fromUpper.empty())
            return connections;

        // ---------------------------------------------------------------------
        // ルール2: 共通音の保持（PivotNote）
        // ---------------------------------------------------------------------
        struct PivotPair
        {
            size_t fromIdx;
            size_t toIdx;
            int semitoneDistance;
        };

        std::vector<PivotPair> pivotCandidates;
        for (size_t i = 0; i < fromUpper.size(); ++i)
        {
            const int fPc = ((fromUpper[i] % 12) + 12) % 12;
            for (size_t j = 0; j < toUpper.size(); ++j)
            {
                const int tPc = ((toUpper[j] % 12) + 12) % 12;
                if (fPc == tPc)
                {
                    pivotCandidates.push_back ({ i, j, std::abs (fromUpper[i] - toUpper[j]) });
                }
            }
        }

        std::sort (pivotCandidates.begin(), pivotCandidates.end(),
                   [] (const PivotPair& a, const PivotPair& b) {
                       return a.semitoneDistance < b.semitoneDistance;
                   });

        std::vector<bool> fromMatched (fromUpper.size(), false);
        std::vector<bool> toMatched (toUpper.size(), false);

        for (const auto& cand : pivotCandidates)
        {
            if (!fromMatched[cand.fromIdx] && !toMatched[cand.toIdx])
            {
                fromMatched[cand.fromIdx] = true;
                toMatched[cand.toIdx] = true;
                connections.emplace_back (fromUpper[cand.fromIdx],
                                          toUpper[cand.toIdx],
                                          VoiceLeadingConnection::Type::PivotNote);
            }
        }

        // ---------------------------------------------------------------------
        // ルール3: 最短半音移動 ＆ 合流マッチング
        // ---------------------------------------------------------------------
        std::vector<int> remFrom;
        for (size_t i = 0; i < fromUpper.size(); ++i)
            if (!fromMatched[i])
                remFrom.push_back (fromUpper[i]);

        std::vector<int> remTo;
        for (size_t j = 0; j < toUpper.size(); ++j)
            if (!toMatched[j])
                remTo.push_back (toUpper[j]);

        if (remFrom.empty())
            return connections;

        // fromChord にてトライトーンを形成していたピッチクラスかどうかを判定
        auto isTritonePitchClassInFromChord = [&] (int note) -> bool
        {
            const int pc = ((note % 12) + 12) % 12;
            for (int otherNote : fromChord.midiNotes)
            {
                const int otherPc = ((otherNote % 12) + 12) % 12;
                if (std::abs (pc - otherPc) == 6)
                    return true;
            }
            return false;
        };

        // remTo が空の場合、remFrom の全ノートは toChord の最も近い音（ベースまたは既存上声部）へ合流
        if (remTo.empty())
        {
            for (int f : remFrom)
            {
                int bestTarget = toBass;
                int bestDist = std::abs (f - toBass);

                for (int t : toChord.midiNotes)
                {
                    const int d = std::abs (f - t);
                    if (d < bestDist)
                    {
                        bestDist = d;
                        bestTarget = t;
                    }
                }

                connections.emplace_back (f, bestTarget, VoiceLeadingConnection::Type::Converged);
            }
            return connections;
        }

        // 単調増加DPによる最小移動コストマッチング
        const size_t M = remFrom.size();
        const size_t K = remTo.size();

        // 1対1ペアの解決種別判定ヘルパー
        auto determineConnectionType = [&] (int fromNote, int toNote) -> VoiceLeadingConnection::Type
        {
            const int dist = std::abs (toNote - fromNote);
            if (isTritonePitchClassInFromChord (fromNote) && dist >= 1 && dist <= 2)
                return VoiceLeadingConnection::Type::TritoneResolution;

            return VoiceLeadingConnection::Type::Stepwise;
        };

        if (M <= K)
        {
            // M個の from ノートを K個の to ノートの部分集合と1対1マッチング
            std::vector<size_t> matchedToIndices = matchMonotonic (remFrom, remTo);

            for (size_t i = 0; i < M; ++i)
            {
                const int f = remFrom[i];
                const int t = remTo[matchedToIndices[i]];
                connections.emplace_back (f, t, determineConnectionType (f, t));
            }
        }
        else
        {
            // M > K: K個の to ノートすべてに最も近い K個の from ノートを1対1マッチング
            std::vector<size_t> matchedFromIndices = matchMonotonic (remTo, remFrom);

            std::vector<bool> fromPaired (M, false);
            for (size_t j = 0; j < K; ++j)
            {
                const size_t fromIdx = matchedFromIndices[j];
                fromPaired[fromIdx] = true;
                const int f = remFrom[fromIdx];
                const int t = remTo[j];
                connections.emplace_back (f, t, determineConnectionType (f, t));
            }

            // 余った M - K 個の from ノートは最も近い remTo へ合流
            for (size_t i = 0; i < M; ++i)
            {
                if (!fromPaired[i])
                {
                    const int f = remFrom[i];
                    int bestTarget = remTo.front();
                    int bestDist = std::abs (f - bestTarget);

                    for (int t : remTo)
                    {
                        const int d = std::abs (f - t);
                        if (d < bestDist)
                        {
                            bestDist = d;
                            bestTarget = t;
                        }
                    }

                    connections.emplace_back (f, bestTarget, VoiceLeadingConnection::Type::Converged);
                }
            }
        }

        return connections;
    }

private:
    /**
        ソート済みの source 配列（サイズ S）の各要素を、
        同じくソート済みの target 配列（サイズ T, T >= S）の要素と
        順序を保持したまま（声部交差なし）最小総距離で1対1ペアリングする。
        @return source[i] にペアリングされた target のインデックス配列（サイズ S）
    */
    static std::vector<size_t> matchMonotonic (const std::vector<int>& source,
                                               const std::vector<int>& target)
    {
        const size_t S = source.size();
        const size_t T = target.size();
        jassert (S <= T);

        if (S == 0)
            return {};

        if (S == T)
        {
            std::vector<size_t> result (S);
            for (size_t i = 0; i < S; ++i)
                result[i] = i;
            return result;
        }

        // DP テーブル: dp[i][j] = source の先頭 i 個を target の先頭 j 個の部分集合とマッチさせる最小コスト
        constexpr int INF = std::numeric_limits<int>::max() / 2;
        std::vector<std::vector<int>> dp (S + 1, std::vector<int> (T + 1, INF));

        for (size_t j = 0; j <= T; ++j)
            dp[0][j] = 0;

        for (size_t i = 1; i <= S; ++i)
        {
            for (size_t j = i; j <= T; ++j)
            {
                const int costMatch = dp[i - 1][j - 1] + std::abs (source[i - 1] - target[j - 1]);
                const int costSkip  = dp[i][j - 1];
                dp[i][j] = std::min (costMatch, costSkip);
            }
        }

        // バックトラックで選択された target インデックスを復元
        std::vector<size_t> result (S);
        size_t i = S;
        size_t j = T;

        while (i > 0)
        {
            if (j > i && dp[i][j] == dp[i][j - 1])
            {
                --j;
            }
            else
            {
                result[i - 1] = j - 1;
                --i;
                --j;
            }
        }

        return result;
    }
};
