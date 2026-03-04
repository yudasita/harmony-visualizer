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

    auto area = getLocalBounds().toFloat().reduced(0.0f);
    auto center = area.getCentre();
    auto radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.4f;
    
    g.setColour(juce::Colours::darkgrey);
    g.drawEllipse (center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

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
            juce::Point<float> startPoint = center.getPointOnCircumference(radius, getAngle(activeIdx[i]));
            
            for (int j = i+1; j < activeIdx.size(); ++j) {
                int dist = std::abs(activeIdx[i] - activeIdx[j]);
                if (dist > 6) dist = 12 - dist;
                
                juce::Point<float> endPoint = center.getPointOnCircumference(radius, getAngle(activeIdx[j]));
                
                auto lp = lineParams.find(dist)->second;
                
                g.setColour(lp.colour);
                
                juce::Path p;
                p.startNewSubPath(startPoint);
                p.lineTo(endPoint);
                
                g.strokePath(p, juce::PathStrokeType(lp.thickness));
            }
            
            g.setColour(activeIdx[i] == rootIdx ? pointColour_root : pointColour);
            g.fillEllipse(startPoint.x - 3, startPoint.y - 3, 6, 6);
        }
    }
    
    // 五度圏の音名
    g.setFont (15.0f); // 文字の大きさ

    for (int i = 0; i < 12; ++i)
    {
        float labelRadius = radius + 18.0f;
        float angle = getAngle(i);
        auto p = center.getPointOnCircumference (labelRadius, angle);
        
        bool isActive = activeIdx.contains(i);
        g.setColour (isActive ? pointColour : juce::Colours::darkgrey);

        juce::Rectangle<float> textRect (40.0f, 20.0f);
        textRect.setCentre (p);
        g.drawText (circleNoteNames[i], textRect, juce::Justification::centred);
    }
    
    // 構成音
    juce::String pitches = "";
    for (int pitchIdx : activeIdx) {
        pitches += circleNoteNames[pitchIdx] + " ";
    }
    
    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    
    auto centerArea = juce::Rectangle<float> (radius * 0.6f, radius * 0.6f);
    centerArea.setCentre (center);
    
    g.drawText (pitches.trim(), centerArea, juce::Justification::centred);
}

void HarmonyVisualizerAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
}
