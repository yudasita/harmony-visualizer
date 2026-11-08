/*
  ==============================================================================

    PerspectiveProjection.h
    ChordWeaver - 擬似3D透視投影変換エンジン

    3D空間座標 (x, y, z) を2D画面座標 (x', y') へ射影変換する計算ヘルパー。
    Z=0 を最前面（等倍スケール = 1.0f）とし、Zが大きくなるほど画面中心（消失点）へ
    向かって縮小投影を行う。

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <algorithm>

struct Point3D
{
    float x { 0.0f };
    float y { 0.0f };
    float z { 0.0f };
};

class PerspectiveProjection
{
public:
    PerspectiveProjection() = default;

    /**
        ウィンドウリサイズ時等に、画面中心・最前面リング半径・奥行きの強さを設定する
        @param center       画面中心（消失点）の2D座標
        @param radiusAtZero Z=0（最前面）における五度圏リングの半径
        @param depthFactor  奥行きの縮小率係数（大きいほど奥のリングが急激に小さくなる）
    */
    void setViewport (juce::Point<float> center, float radiusAtZero, float depthFactor = 0.28f) noexcept
    {
        vanishingPoint = center;
        baseRadius = std::max (1.0f, radiusAtZero);
        zDepthFactor = std::max (0.01f, depthFactor);
    }

    void setDepthFactor (float depthFactor) noexcept
    {
        zDepthFactor = std::max (0.01f, depthFactor);
    }

    float getDepthFactor() const noexcept { return zDepthFactor; }
    juce::Point<float> getVanishingPoint() const noexcept { return vanishingPoint; }
    float getBaseRadius() const noexcept { return baseRadius; }

    /**
        深度 Z に応じた透視投影スケール率を計算する
        scale(z) = 1.0f / (1.0f + z * depthFactor)
        @param z 深度（0.0f = 最前面、正の値 = 奥）
    */
    float getScaleAtZ (float z) const noexcept
    {
        const float clampedZ = std::max (0.0f, z);
        return 1.0f / (1.0f + clampedZ * zDepthFactor);
    }

    /**
        深度 Z における五度圏リングの投影半径を取得する
    */
    float getProjectedRadius (float z) const noexcept
    {
        return baseRadius * getScaleAtZ (z);
    }

    /**
        深度 Z における線幅や点サイズのスケーリング値を取得する
        奥へ行っても完全に細くなりすぎないよう最小スケールを担保する
    */
    float getProjectedSize (float baseSize, float z, float minScale = 0.25f) const noexcept
    {
        const float scale = std::max (minScale, getScaleAtZ (z));
        return baseSize * scale;
    }

    /**
        中心からの相対3D座標 (x, y, z) を2D画面座標へ透視変換する
        @param p 消失点を中心とした相対3D座標（x, y は Z=0 平面でのオフセット量）
    */
    juce::Point<float> project3Dto2D (const Point3D& p) const noexcept
    {
        const float scale = getScaleAtZ (p.z);
        return {
            vanishingPoint.x + p.x * scale,
            vanishingPoint.y + p.y * scale
        };
    }

    /**
        五度圏上の角度（ラジアン）と深度 Z から、2D画面上の円周座標を取得する
        （JUCEの getPointOnCircumference と同じ時計回り・12時方向=0rad 基準）
        @param angleRadians 12時方向を0とし時計回りに増加する角度（ラジアン）
        @param z            深度（0.0f = 最前面）
        @param radiusRatio  基準半径に対する倍率（1.0f = リング上、1.1f = ラベル位置など）
    */
    juce::Point<float> projectCirclePoint (float angleRadians, float z, float radiusRatio = 1.0f) const noexcept
    {
        const float r = baseRadius * radiusRatio;
        const Point3D p3d {
            r * std::sin (angleRadians),
            -r * std::cos (angleRadians),
            z
        };
        return project3Dto2D (p3d);
    }

    /**
        五度圏インデックス（0〜11）と深度 Z から、2D画面上の円周座標を取得する
    */
    juce::Point<float> projectCircleIndex (int circleIdx, float z, float radiusRatio = 1.0f) const noexcept
    {
        const float angle = juce::MathConstants<float>::twoPi * static_cast<float> (circleIdx) / 12.0f;
        return projectCirclePoint (angle, z, radiusRatio);
    }

private:
    juce::Point<float> vanishingPoint { 200.0f, 200.0f };
    float baseRadius { 160.0f };
    float zDepthFactor { 0.28f };
};
