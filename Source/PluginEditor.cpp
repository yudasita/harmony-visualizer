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
    // ロックフリーFIFOから未読イベントを取り出して現在の発音ノートを更新
    audioProcessor.popMidiEvents ([this] (const QueuedMidiEvent& event)
    {
        const int noteNumber = static_cast<int> (event.noteOrController);

        if (event.type == QueuedMidiEvent::Type::noteOn)
        {
            if (event.value > 0)
            {
                if (std::find (currentActiveNotes.begin(), currentActiveNotes.end(), noteNumber) == currentActiveNotes.end())
                    currentActiveNotes.push_back (noteNumber);
            }
            else
            {
                currentActiveNotes.erase (std::remove (currentActiveNotes.begin(), currentActiveNotes.end(), noteNumber),
                                          currentActiveNotes.end());
            }
        }
        else if (event.type == QueuedMidiEvent::Type::noteOff)
        {
            currentActiveNotes.erase (std::remove (currentActiveNotes.begin(), currentActiveNotes.end(), noteNumber),
                                      currentActiveNotes.end());
        }
        else if (event.type == QueuedMidiEvent::Type::allNotesOff)
        {
            currentActiveNotes.clear();
        }
    });

    repaint();
}

void HarmonyVisualizerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour); // 背景
    
    g.setColour (circleColour);
    g.drawEllipse (circleCenter.x - circleRadius, circleCenter.y - circleRadius, circleDiameter, circleDiameter, circleThickness);

    // UIスレッド内のアクティブノートを使用（排他ロック不要）
    const std::vector<int> notes = currentActiveNotes;

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
                if (it != pitchLine_colours.end())
                    g.setColour (it->second);
                else
                    g.setColour (juce::Colours::white);

                g.drawLine (juce::Line<float> (startPoint, endPoint), pitchLine_thickness);
            }
            
            g.setColour (activeIdx[i] == rootIdx ? pointColour_root : pointColour);
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
