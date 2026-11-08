/*
  ==============================================================================

    VoiceLeadingSolver.h
    MeLoom - ボイスリーディング解析と声部接続ソルバー

    直前コード（奥・fromChord）から現在コード（手前・toChord）への遷移における
    声部連結（ボイスリーディング）の音楽理論的引力・最小移動コストペアリングを計算する。
    最低音（ベースライン優先）、共通音（Pivot Note 保持）、トライトーン解決引力、
    導音の上行解決、掛留音（sus4）・テンションの下行解決、近接移動、
    および多対1の合流マッチングを提供する。

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
        Stepwise,           // 半音/全音進行（近接移動・導音/掛留音解決を含む）
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
        fromPitchClass = ChordEvent::normalizePitchClass (fromNote);
        fromCircleIndex = ChordEvent::pitchClassToCircleIndex (fromPitchClass);

        toPitchClass = ChordEvent::normalizePitchClass (toNote);
        toCircleIndex = ChordEvent::pitchClassToCircleIndex (toPitchClass);

        circleDistance = ChordEvent::circleDistance (fromCircleIndex, toCircleIndex);
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

/**
    音楽理論に基づく声部移動コスト・引力ボーナス設定パラメータ
*/
struct VoiceLeadingCostParameters
{
    int semitoneStepCost             { 100 }; // 半音（1半音）移動の基本コスト
    int wholeStepCost                { 220 }; // 全音（2半音）移動の基本コスト
    int leapBaseCost                 { 220 }; // 3半音以上の跳躍の基準コスト
    int leapQuadraticWeight          { 180 }; // 3半音以上の跳躍に対する非線形（二次）ペナルティ重み
    int midiDistanceTieBreaker       { 4 };   // 実MIDIノート距離1半音あたりのタイブレイカー重み

    int tritoneHalfStepDownBonus     { 85 };  // トライトーン構成音の半音下行解決ボーナス（例: G7->CM7 の F->E）
    int tritoneHalfStepUpBonus       { 80 };  // トライトーン構成音の半音上行解決ボーナス（例: G7->C の B->C）
    int tritoneWholeStepDownBonus    { 30 };  // トライトーン構成音の全音下行解決ボーナス
    int tritoneAntiResolutionPenalty { 160 }; // 半音解決先があるのに全音上行等で逃げる場合のペナルティ（F->G 抑制）

    int leadingToneUpBonus           { 55 };  // 導音（長3度・長7度等）の主音・構成音への半音上行解決ボーナス
    int suspensionHalfStepDownBonus  { 50 };  // sus4・短6度（b6）・テンション等の半音下行解決ボーナス（F->E, Ab->G）
    int tensionWholeStepDownBonus    { 20 };  // 9th・5th等の全音下行解決ボーナス（D->C）

    int convergencePenalty           { 45 };  // 同一ターゲット音への多対1合流ペナルティ
    int voiceCrossingPenalty         { 35 };  // 声部交差（順序逆転）に対するソフトペナルティ
    int maxPivotOctaveDistance       { 18 };  // 共通音保持（PivotNote）として優先する最大実ノート距離
};

class VoiceLeadingSolver
{
public:
    using CostParameters = VoiceLeadingCostParameters;

    VoiceLeadingSolver() = default;

    /**
        2つのコードイベント（直前コード fromChord → 現在コード toChord）間の
        声部連結リストを計算する。

        アルゴリズム:
        1. ルール1 (ベースライン優先):
           最低音同士 (bassNote) は音程距離に関わらず必ず最優先で接続 (Type::BassLine)。
        2. ルール2 (共通音の保持 / PivotNote):
           残りの上声部について、同一ピッチクラスを持つノート同士を最小半音差で平行接続 (Type::PivotNote)。
           上声部に同一ピッチクラスがない場合でも toBass と同一ピッチクラスであれば保持接続を許可。
        3. ルール3 (音楽理論的引力コスト関数 ＆ 最適割当):
           残りの上声部について、半音・全音移動コスト、3半音以上の跳躍ペナルティ、
           およびトライトーン解決・導音解決・掛留音(sus4)/テンション下行解決の引力ボーナスを含む
           総合コスト関数を最小化するペアリングを計算。
    */
    static std::vector<VoiceLeadingConnection> solve (const ChordEvent& fromChord,
                                                      const ChordEvent& toChord)
    {
        return solve (fromChord, toChord, CostParameters{});
    }

    static std::vector<VoiceLeadingConnection> solve (const ChordEvent& fromChord,
                                                      const ChordEvent& toChord,
                                                      const CostParameters& params)
    {
        std::vector<VoiceLeadingConnection> connections;

        if (fromChord.isEmpty() || toChord.isEmpty())
            return connections;

        // ---------------------------------------------------------------------
        // ルール1: ベースライン優先接続
        // ---------------------------------------------------------------------
        const int fromBass = (fromChord.bassNote >= 0) ? fromChord.bassNote : fromChord.midiNotes.front();
        const int toBass   = (toChord.bassNote >= 0)   ? toChord.bassNote   : toChord.midiNotes.front();
        const int toBassPc = ChordEvent::normalizePitchClass (toBass);

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
            const int fPc = ChordEvent::normalizePitchClass (fromUpper[i]);
            for (size_t j = 0; j < toUpper.size(); ++j)
            {
                const int tPc = ChordEvent::normalizePitchClass (toUpper[j]);
                const int dist = std::abs (fromUpper[i] - toUpper[j]);
                if (fPc == tPc && dist <= params.maxPivotOctaveDistance)
                {
                    pivotCandidates.push_back ({ i, j, dist });
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

        // 上声部で未マッチだが toBass と同一ピッチクラスの共通音がある場合（例: Fm -> C の C音保持）
        for (size_t i = 0; i < fromUpper.size(); ++i)
        {
            if (!fromMatched[i] && ChordEvent::normalizePitchClass (fromUpper[i]) == toBassPc)
            {
                const int dist = std::abs (fromUpper[i] - toBass);
                if (dist <= params.maxPivotOctaveDistance)
                {
                    fromMatched[i] = true;
                    connections.emplace_back (fromUpper[i],
                                              toBass,
                                              VoiceLeadingConnection::Type::PivotNote);
                }
            }
        }

        // ---------------------------------------------------------------------
        // ルール3: 音楽的引力コスト関数による最適ペアリング ＆ 合流制御
        // ---------------------------------------------------------------------
        std::vector<int> remFrom;
        for (size_t i = 0; i < fromUpper.size(); ++i)
            if (!fromMatched[i])
                remFrom.push_back (fromUpper[i]);

        if (remFrom.empty())
            return connections;

        // ターゲット候補リストの構築（toChord の各構成音）
        struct TargetCandidate
        {
            int midiNote;
            int pitchClass;
            int initialUsageCount; // 0 = 未割当の toUpper, 1 = BassLine/PivotNote で既に使用中
        };

        std::vector<TargetCandidate> candidates;
        candidates.push_back ({ toBass, toBassPc, 1 });

        for (size_t j = 0; j < toUpper.size(); ++j)
        {
            const int note = toUpper[j];
            const int pc = ChordEvent::normalizePitchClass (note);
            const int initUse = toMatched[j] ? 1 : 0;

            bool merged = false;
            for (auto& existing : candidates)
            {
                if (existing.pitchClass == pc)
                {
                    if (initUse < existing.initialUsageCount)
                    {
                        existing.midiNote = note;
                        existing.initialUsageCount = initUse;
                    }
                    merged = true;
                    break;
                }
            }
            if (!merged)
                candidates.push_back ({ note, pc, initUse });
        }

        std::sort (candidates.begin(), candidates.end(),
                   [] (const TargetCandidate& a, const TargetCandidate& b) {
                       return a.midiNote < b.midiNote;
                   });

        const size_t M = remFrom.size();
        const size_t C = candidates.size();

        // 各 (fromIdx, candIdx) ペアの基本遷移コスト行列を事前計算
        std::vector<std::vector<int>> pairCosts (M, std::vector<int> (C, 0));
        for (size_t i = 0; i < M; ++i)
        {
            for (size_t c = 0; c < C; ++c)
            {
                pairCosts[i][c] = evaluatePairCost (remFrom[i],
                                                    candidates[c].midiNote,
                                                    fromChord,
                                                    toChord,
                                                    params);
            }
        }

        // 最適割当の探索（M <= 6 の場合は分岐限定法で厳密最小コスト解を探索）
        std::vector<size_t> bestAssignment (M, 0);
        if (M <= 6)
        {
            int bestTotalCost = std::numeric_limits<int>::max() / 2;
            std::vector<size_t> currentAssignment (M, 0);
            std::vector<int> usageCounts (C, 0);
            for (size_t c = 0; c < C; ++c)
                usageCounts[c] = candidates[c].initialUsageCount;

            auto search = [&] (auto& self, size_t idx, int currentCost) -> void
            {
                if (currentCost >= bestTotalCost)
                    return;

                if (idx == M)
                {
                    bestTotalCost = currentCost;
                    bestAssignment = currentAssignment;
                    return;
                }

                for (size_t c = 0; c < C; ++c)
                {
                    int stepCost = pairCosts[idx][c];

                    if (usageCounts[c] > 0)
                        stepCost += params.convergencePenalty * usageCounts[c];

                    const int targetNote = candidates[c].midiNote;
                    for (size_t prev = 0; prev < idx; ++prev)
                    {
                        const int prevTargetNote = candidates[currentAssignment[prev]].midiNote;
                        if (prevTargetNote > targetNote)
                            stepCost += params.voiceCrossingPenalty;
                    }

                    currentAssignment[idx] = c;
                    usageCounts[c] += 1;
                    self (self, idx + 1, currentCost + stepCost);
                    usageCounts[c] -= 1;
                }
            };

            search (search, 0, 0);
        }
        else
        {
            std::vector<int> usageCounts (C, 0);
            for (size_t c = 0; c < C; ++c)
                usageCounts[c] = candidates[c].initialUsageCount;

            for (size_t i = 0; i < M; ++i)
            {
                int bestC = 0;
                int minCost = std::numeric_limits<int>::max() / 2;
                for (size_t c = 0; c < C; ++c)
                {
                    const int cost = pairCosts[i][c] + (usageCounts[c] > 0 ? params.convergencePenalty * usageCounts[c] : 0);
                    if (cost < minCost)
                    {
                        minCost = cost;
                        bestC = static_cast<int> (c);
                    }
                }
                bestAssignment[i] = static_cast<size_t> (bestC);
                usageCounts[static_cast<size_t> (bestC)] += 1;
            }
        }

        // 接続種別（TritoneResolution / Stepwise / Converged）の決定
        std::vector<int> assignedCountPerCand (C, 0);
        std::vector<size_t> primaryVoiceForCand (C, M);

        for (size_t i = 0; i < M; ++i)
        {
            const size_t c = bestAssignment[i];
            assignedCountPerCand[c] += 1;
            if (primaryVoiceForCand[c] == M || pairCosts[i][c] < pairCosts[primaryVoiceForCand[c]][c])
            {
                primaryVoiceForCand[c] = i;
            }
        }

        for (size_t i = 0; i < M; ++i)
        {
            const size_t c = bestAssignment[i];
            const int fromNote = remFrom[i];
            const int toNote   = candidates[c].midiNote;
            const int absPcDist = std::abs (signedPitchClassDelta (fromNote, toNote));

            VoiceLeadingConnection::Type connType = VoiceLeadingConnection::Type::Stepwise;

            if (isTritonePitchClassInChord (fromNote, fromChord) && absPcDist >= 1 && absPcDist <= 2)
            {
                connType = VoiceLeadingConnection::Type::TritoneResolution;
            }
            else if (primaryVoiceForCand[c] != i || (candidates[c].initialUsageCount > 0 && absPcDist > 2))
            {
                connType = VoiceLeadingConnection::Type::Converged;
            }

            connections.emplace_back (fromNote, toNote, connType);
        }

        return connections;
    }

    /**
        単一の声部遷移 (fromNote -> toNote) の音楽理論コストを評価する（テスト・検証用に公開）
    */
    static int evaluatePairCost (int fromNote,
                                 int toNote,
                                 const ChordEvent& fromChord,
                                 const ChordEvent& toChord,
                                 const CostParameters& params = CostParameters{}) noexcept
    {
        const int deltaPc = signedPitchClassDelta (fromNote, toNote);
        const int absPc   = std::abs (deltaPc);

        // 1. 基本の半音距離コスト（近接移動優遇・3半音以上の跳躍に非線形ペナルティ）
        int baseCost = 0;
        if (absPc == 0)
            baseCost = 0;
        else if (absPc == 1)
            baseCost = params.semitoneStepCost;
        else if (absPc == 2)
            baseCost = params.wholeStepCost;
        else
            baseCost = params.leapBaseCost + (absPc - 2) * (absPc - 2) * params.leapQuadraticWeight;

        // オクターブ・実ノート距離の微小タイブレイカー
        const int midiDist = std::min (12, std::abs (toNote - fromNote));
        int totalCost = baseCost + midiDist * params.midiDistanceTieBreaker;

        const int fromPc = ChordEvent::normalizePitchClass (fromNote);
        const int toPc   = ChordEvent::normalizePitchClass (toNote);
        const int fromBassPc = (fromChord.bassNote >= 0)
            ? ChordEvent::normalizePitchClass (fromChord.bassNote)
            : (fromChord.midiNotes.empty() ? -1 : ChordEvent::normalizePitchClass (fromChord.midiNotes.front()));
        const int toBassPc = (toChord.bassNote >= 0)
            ? ChordEvent::normalizePitchClass (toChord.bassNote)
            : (toChord.midiNotes.empty() ? -1 : ChordEvent::normalizePitchClass (toChord.midiNotes.front()));

        // 2. トライトーン解決引力ボーナス ＆ 反解決ペナルティ
        if (isTritonePitchClassInChord (fromNote, fromChord))
        {
            const int halfStepDownPc = (fromPc + 11) % 12;
            const int halfStepUpPc   = (fromPc + 1) % 12;
            const bool hasHalfStepTargetInToChord =
                toChord.hasPitchClass (halfStepDownPc) || toChord.hasPitchClass (halfStepUpPc);

            if (deltaPc == -1)
            {
                totalCost -= params.tritoneHalfStepDownBonus;
            }
            else if (deltaPc == +1)
            {
                totalCost -= params.tritoneHalfStepUpBonus;
            }
            else if (deltaPc == -2)
            {
                totalCost -= params.tritoneWholeStepDownBonus;
            }
            else if (deltaPc == +2 && hasHalfStepTargetInToChord)
            {
                totalCost += params.tritoneAntiResolutionPenalty;
            }
        }

        // 3. 導音（Leading Tone）の上行解決ボーナス（deltaPc == +1）
        if (deltaPc == +1)
        {
            const int intervalFromBass = (fromBassPc >= 0) ? ((fromPc - fromBassPc + 12) % 12) : -1;
            const bool isThirdOrSeventh = (intervalFromBass == 4 || intervalFromBass == 11 || intervalFromBass == 3);
            if (toPc == toBassPc || isThirdOrSeventh)
            {
                totalCost -= params.leadingToneUpBonus;
            }
        }

        // 4. 掛留音（sus4）・短6度（b6）・テンションの下行解決ボーナス
        if (deltaPc == -1)
        {
            const int intervalFromBass = (fromBassPc >= 0) ? ((fromPc - fromBassPc + 12) % 12) : -1;
            const int intervalToBass   = (toBassPc >= 0)   ? ((toPc - toBassPc + 12) % 12)   : -1;

            if (intervalFromBass == 5 || intervalFromBass == 8 || intervalFromBass == 10
                || intervalToBass == 4 || intervalToBass == 7
                || hasDissonanceInChord (fromPc, fromChord))
            {
                totalCost -= params.suspensionHalfStepDownBonus;
            }
            else
            {
                totalCost -= (params.suspensionHalfStepDownBonus / 2);
            }
        }
        else if (deltaPc == -2)
        {
            totalCost -= params.tensionWholeStepDownBonus;
        }

        return totalCost;
    }

private:
    static int signedPitchClassDelta (int fromNote, int toNote) noexcept
    {
        const int fPc = ChordEvent::normalizePitchClass (fromNote);
        const int tPc = ChordEvent::normalizePitchClass (toNote);
        return (tPc - fPc + 18) % 12 - 6;
    }

    static bool isTritonePitchClassInChord (int note, const ChordEvent& chord) noexcept
    {
        const int targetCircleIdx = ChordEvent::pitchClassToCircleIndex (ChordEvent::normalizePitchClass (note));
        for (const auto& pair : chord.tritonePairs)
        {
            if (pair.first == targetCircleIdx || pair.second == targetCircleIdx)
                return true;
        }
        return false;
    }

    static bool hasDissonanceInChord (int pc, const ChordEvent& chord) noexcept
    {
        for (int otherNote : chord.midiNotes)
        {
            const int otherPc = ChordEvent::normalizePitchClass (otherNote);
            if (otherPc == pc)
                continue;
            const int diff = std::min (std::abs (pc - otherPc), 12 - std::abs (pc - otherPc));
            if (diff == 1 || diff == 2 || diff == 5 || diff == 6)
                return true;
        }
        return false;
    }
};
