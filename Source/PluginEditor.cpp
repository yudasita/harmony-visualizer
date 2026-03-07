/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
HarmonyVisualizerAudioProcessorEditor::HarmonyVisualizerAudioProcessorEditor (HarmonyVisualizerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (400, 400);
        
    startTimerHz(60);
    
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
    // 画面を再描画（paint関数を呼び出す）
    repaint();
}

void HarmonyVisualizerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour); // 背景
    
    g.setColour(circleColour);
    g.drawEllipse (circleCenter.x - circleRadius, circleCenter.y - circleRadius, circleDiameter, circleDiameter, circleThickness);

    // Processorから音を取得
    std::vector<int> notes;
    {
        juce::ScopedLock sl (dynamic_cast<HarmonyVisualizerAudioProcessor&>(processor).getNoteLock());
        notes = dynamic_cast<HarmonyVisualizerAudioProcessor&>(processor).getActiveNotes();
    }

    // 座標を計算してプロット
    juce::Array<int> activeIdx;
    int rootNote;
    int rootIdx;
    
    for (int note : notes)
    {
        int pc = note % 12; // オクターブを消す
        int circleIdx = (pc * 7) % 12; // 五度圏変換
        //int circleIdx = pc;
        
        // まだ activeIdx に入っていない場合だけ追加
        if (!activeIdx.contains(circleIdx)) {
            activeIdx.add(circleIdx);
        }
        
        if (note == *std::min_element(notes.begin(), notes.end())){
            rootNote = note;
            rootIdx = circleIdx;
        }
    }

    // 線で結ぶ
    if (activeIdx.size() > 0)
    {
        for (int i = 0; i < activeIdx.size(); ++i) {
            juce::Point<float> startPoint = circleCenter.getPointOnCircumference(circleRadius, getAngle(activeIdx[i]));
            
            for (int j = i+1; j < activeIdx.size(); ++j) {
                int dist = std::abs(activeIdx[i] - activeIdx[j]);
                if (dist > 6) dist = 12 - dist;
                
                juce::Point<float> endPoint = circleCenter.getPointOnCircumference(circleRadius, getAngle(activeIdx[j]));
                
                g.setColour(pitchLine_colours.find(dist)->second);
                g.drawLine(juce::Line<float>(startPoint, endPoint), pitchLine_thickness);
            }
            
            g.setColour(activeIdx[i] == rootIdx ? pointColour_root : pointColour);
            g.fillEllipse(startPoint.x - pitchPoint_radius, startPoint.y - pitchPoint_radius, pitchPoint_diameter, pitchPoint_diameter);
        }
    }
    
    // 五度圏の音名
    g.setFont (labelCircle_fontsize); // 文字の大きさ

    for (int i = 0; i < 12; ++i)
    {
        float angle = getAngle(i);
        auto p = circleCenter.getPointOnCircumference (labelCircle_radius, angle);
        
        bool isActive = activeIdx.contains(i);
        g.setColour (isActive ? pointColour : juce::Colours::darkgrey);

        labelCircle_rect.setCentre (p);
        g.drawText (circleNoteNames[i], labelCircle_rect, juce::Justification::centred);
    }
    
    // 構成音
    juce::String pitches = "";
    for (int pitchIdx : activeIdx) {
        pitches += circleNoteNames[pitchIdx] + " ";
    }
    
    g.setColour (juce::Colours::white);
    g.setFont (labelCircle_fontsize);
    
    labelCentre_rect.setCentre (circleCenter);
    
    g.drawText (pitches.trim(), labelCentre_rect, juce::Justification::centred);
}

void HarmonyVisualizerAudioProcessorEditor::resized()
{
    drawingArea = getLocalBounds().toFloat().reduced(0.0f);
    
    circleCenter = drawingArea.getCentre();
    circleRadius = juce::jmin(drawingArea.getWidth(), drawingArea.getHeight()) * 0.4f;
    circleDiameter = 2 * circleRadius;
    circleThickness = 0.02f / 3 * circleRadius;
    
    labelCircle_rect = {0.15f * circleRadius, 0.15f * circleRadius};
    labelCircle_fontsize = 0.1f * circleRadius;
    labelCircle_radius = 1.1f * circleRadius;
    
    pitchLine_thickness = 0.01f * circleRadius;
    
    pitchPoint_radius = 0.02f * circleRadius;
    pitchPoint_diameter = 2 * pitchPoint_radius;
    
    labelCentre_fontsize = 0.1f * circleRadius;
    labelCentre_rect = {circleDiameter, 0.6f * circleRadius};
}
