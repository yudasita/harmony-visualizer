/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

//==============================================================================
HarmonyVisualizerAudioProcessorEditor::HarmonyVisualizerAudioProcessorEditor (HarmonyVisualizerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (400, 400);
    startTimerHz (60);
    setResizable (true, true);
    setResizeLimits (300, 300, 1000, 1000);
}

HarmonyVisualizerAudioProcessorEditor::~HarmonyVisualizerAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void HarmonyVisualizerAudioProcessorEditor::timerCallback()
{
    const double currentTimeMs = juce::Time::getMillisecondCounterHiRes();

    // ロックフリーFIFOから未読イベントを取り出してコード境界判定エンジンへ渡す
    audioProcessor.popMidiEvents ([this, currentTimeMs] (const QueuedMidiEvent& event)
    {
        chordDetector.processMidiEvent (event, currentTimeMs);
    });

    chordDetector.update (currentTimeMs);

    repaint();
}

void HarmonyVisualizerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour); // 背景
    
    g.setColour (circleColour);
    g.drawEllipse (circleCenter.x - circleRadius, circleCenter.y - circleRadius, circleDiameter, circleDiameter, circleThickness);

    // コード境界判定エンジンから確定状態を取得
    const auto& chordState = chordDetector.getCurrentChordState();
    const std::vector<int>& notes = chordState.notes;
    const float alphaMultiplier = chordState.isDimmed ? 0.35f : 1.0f;

    // 座標を計算してプロット
    juce::Array<int> activeIdx;
    int rootNote = -1;
    int rootIdx = -1;

    if (!notes.empty())
    {
        rootNote = *std::min_element (notes.begin(), notes.end());
    }
    
    for (int note : notes)
    {
        int pc = note % 12; // オクターブを消す
        int circleIdx = (pc * 7) % 12; // 五度圏変換
        
        // まだ activeIdx に入っていない場合だけ追加
        if (!activeIdx.contains (circleIdx)) {
            activeIdx.add (circleIdx);
        }
        
        if (note == rootNote) {
            rootIdx = circleIdx;
        }
    }

    // 線で結ぶ
    if (activeIdx.size() > 0)
    {
        for (int i = 0; i < activeIdx.size(); ++i) {
            juce::Point<float> startPoint = circleCenter.getPointOnCircumference (circleRadius, getAngle (activeIdx[i]));
            
            for (int j = i + 1; j < activeIdx.size(); ++j) {
                int dist = std::abs (activeIdx[i] - activeIdx[j]);
                if (dist > 6)
                    dist = 12 - dist;
                
                juce::Point<float> endPoint = circleCenter.getPointOnCircumference (circleRadius, getAngle (activeIdx[j]));
                
                auto it = pitchLine_colours.find (dist);
                juce::Colour lineColour = (it != pitchLine_colours.end() ? it->second : juce::Colours::white);
                g.setColour (lineColour.withMultipliedAlpha (alphaMultiplier));

                g.drawLine (juce::Line<float> (startPoint, endPoint), pitchLine_thickness);
            }
            
            juce::Colour ptColour = (activeIdx[i] == rootIdx ? pointColour_root : pointColour);
            g.setColour (ptColour.withMultipliedAlpha (alphaMultiplier));
            g.fillEllipse (startPoint.x - pitchPoint_radius, startPoint.y - pitchPoint_radius, pitchPoint_diameter, pitchPoint_diameter);
        }
    }
    
    // 五度圏の音名
    g.setFont (labelCircle_fontsize); // 文字の大きさ

    for (int i = 0; i < 12; ++i)
    {
        float angle = getAngle (i);
        auto p = circleCenter.getPointOnCircumference (labelCircle_radius, angle);
        
        bool isActive = activeIdx.contains (i);
        juce::Colour textColour = (isActive ? pointColour.withMultipliedAlpha (alphaMultiplier) : juce::Colours::darkgrey);
        g.setColour (textColour);

        labelCircle_rect.setCentre (p);
        g.drawText (circleNoteNames[i], labelCircle_rect, juce::Justification::centred);
    }
    
    // 構成音
    juce::String pitches = "";
    for (int pitchIdx : activeIdx) {
        pitches += circleNoteNames[pitchIdx] + " ";
    }
    
    g.setColour (juce::Colours::white.withMultipliedAlpha (alphaMultiplier));
    g.setFont (labelCircle_fontsize);
    
    labelCentre_rect.setCentre (circleCenter);
    g.drawText (pitches.trim(), labelCentre_rect, juce::Justification::centred);
}

void HarmonyVisualizerAudioProcessorEditor::resized()
{
    drawingArea = getLocalBounds().toFloat().reduced (0.0f);
    
    circleCenter = drawingArea.getCentre();
    circleRadius = juce::jmin (drawingArea.getWidth(), drawingArea.getHeight()) * 0.4f;
    circleDiameter = 2.0f * circleRadius;
    circleThickness = 0.02f / 3.0f * circleRadius;
    
    labelCircle_rect = { 0.15f * circleRadius, 0.15f * circleRadius };
    labelCircle_fontsize = 0.1f * circleRadius;
    labelCircle_radius = 1.1f * circleRadius;
    
    pitchLine_thickness = 0.01f * circleRadius;
    
    pitchPoint_radius = 0.02f * circleRadius;
    pitchPoint_diameter = 2.0f * pitchPoint_radius;
    
    labelCentre_fontsize = 0.1f * circleRadius;
    labelCentre_rect = { circleDiameter, 0.6f * circleRadius };
}
