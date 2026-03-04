/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
HarmonyVisualizerAudioProcessor::HarmonyVisualizerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

HarmonyVisualizerAudioProcessor::~HarmonyVisualizerAudioProcessor()
{
}

//==============================================================================
const juce::String HarmonyVisualizerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool HarmonyVisualizerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool HarmonyVisualizerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool HarmonyVisualizerAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double HarmonyVisualizerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int HarmonyVisualizerAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int HarmonyVisualizerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void HarmonyVisualizerAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String HarmonyVisualizerAudioProcessor::getProgramName (int index)
{
    return {};
}

void HarmonyVisualizerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void HarmonyVisualizerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    reset();
}

void HarmonyVisualizerAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool HarmonyVisualizerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void HarmonyVisualizerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // これが呼ばれること自体がオーディオエンジン起動の証明
    /*
    static int counter = 0;
    if (counter++ % 100 == 0) {
        juce::Logger::writeToLog("Heartbeat: Audio Engine is running!");
    }
    */
    buffer.clear();
    
    // MIDIが来ているかチェック（デバッグ用）
    if (!midiMessages.isEmpty())
    {
        int numMessages = midiMessages.getNumEvents();
        juce::Logger::writeToLog("MIDI messages received: " + juce::String(numMessages));
    }
    
    // 届いたMIDIメッセージを一つずつチェック
    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();
        juce::ScopedLock sl (noteLock); // ロックをかける
        
        if (message.isNoteOn())
        {
            // 重複を避けて追加
            if (std::find(activeNotes.begin(), activeNotes.end(), message.getNoteNumber()) == activeNotes.end())
                activeNotes.push_back(message.getNoteNumber());
        }
        else if (message.isNoteOff())
        {
            // リストから削除
            activeNotes.erase(std::remove(activeNotes.begin(), activeNotes.end(), message.getNoteNumber()), activeNotes.end());
        }
    }
}

//==============================================================================
bool HarmonyVisualizerAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* HarmonyVisualizerAudioProcessor::createEditor()
{
    return new HarmonyVisualizerAudioProcessorEditor (*this);
}

//==============================================================================
void HarmonyVisualizerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void HarmonyVisualizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonyVisualizerAudioProcessor();
}
