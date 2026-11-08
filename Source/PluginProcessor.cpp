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
    juce::ignoreUnused (index);
}

const juce::String HarmonyVisualizerAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void HarmonyVisualizerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void HarmonyVisualizerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
    reset();
}

void HarmonyVisualizerAudioProcessor::releaseResources()
{
    reset();
}

void HarmonyVisualizerAudioProcessor::reset()
{
    midiFifo.reset();
    QueuedMidiEvent event;
    event.type = QueuedMidiEvent::Type::allNotesOff;
    pushMidiEvent (event);
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

void HarmonyVisualizerAudioProcessor::pushMidiEvent (const QueuedMidiEvent& event) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    midiFifo.prepareToWrite (1, start1, size1, start2, size2);

    if (size1 > 0)
        midiFifoBuffer[static_cast<size_t> (start1)] = event;
    else if (size2 > 0)
        midiFifoBuffer[static_cast<size_t> (start2)] = event;

    midiFifo.finishedWrite (size1 + size2);
}

void HarmonyVisualizerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        const int sampleOffset = metadata.samplePosition;

        QueuedMidiEvent event;
        event.sampleOffset = sampleOffset;
        event.channel = static_cast<uint8_t> (juce::jlimit (1, 16, message.getChannel()));

        if (message.isNoteOn())
        {
            event.type = QueuedMidiEvent::Type::noteOn;
            event.noteOrController = static_cast<uint8_t> (message.getNoteNumber());
            event.value = static_cast<uint8_t> (message.getVelocity());
            pushMidiEvent (event);
        }
        else if (message.isNoteOff())
        {
            event.type = QueuedMidiEvent::Type::noteOff;
            event.noteOrController = static_cast<uint8_t> (message.getNoteNumber());
            event.value = static_cast<uint8_t> (message.getVelocity());
            pushMidiEvent (event);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            event.type = QueuedMidiEvent::Type::allNotesOff;
            pushMidiEvent (event);
        }
        else if (message.isController())
        {
            const int ctrlNum = message.getControllerNumber();
            if (ctrlNum == 120 || ctrlNum == 121 || ctrlNum == 123)
            {
                event.type = QueuedMidiEvent::Type::allNotesOff;
                pushMidiEvent (event);
            }
            else
            {
                event.type = QueuedMidiEvent::Type::controlChange;
                event.noteOrController = static_cast<uint8_t> (ctrlNum);
                event.value = static_cast<uint8_t> (message.getControllerValue());
                pushMidiEvent (event);
            }
        }
        else if (message.isPitchWheel())
        {
            event.type = QueuedMidiEvent::Type::pitchWheel;
            const int pitchValue = message.getPitchWheelValue(); // 0..16383, センター8192
            event.noteOrController = static_cast<uint8_t> (pitchValue & 0x7F);        // LSB 7bit
            event.value = static_cast<uint8_t> ((pitchValue >> 7) & 0x7F);            // MSB 7bit
            pushMidiEvent (event);
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
int HarmonyVisualizerAudioProcessor::getMaxHistorySize() const noexcept
{
    const int val = static_cast<int> (settingsState.getProperty ("maxHistorySize", defaultMaxHistorySize));
    return juce::jlimit (2, 16, val);
}

void HarmonyVisualizerAudioProcessor::setMaxHistorySize (int size)
{
    const int clamped = juce::jlimit (2, 16, size);
    settingsState.setProperty ("maxHistorySize", clamped, nullptr);
    settingsRevision.fetch_add (1, std::memory_order_relaxed);
}

double HarmonyVisualizerAudioProcessor::getQuantizeWindowMs() const noexcept
{
    const double val = static_cast<double> (settingsState.getProperty ("quantizeWindowMs", defaultQuantizeWindowMs));
    return juce::jlimit (15.0, 200.0, val);
}

void HarmonyVisualizerAudioProcessor::setQuantizeWindowMs (double ms)
{
    const double clamped = juce::jlimit (15.0, 200.0, ms);
    settingsState.setProperty ("quantizeWindowMs", clamped, nullptr);
    settingsRevision.fetch_add (1, std::memory_order_relaxed);
}

double HarmonyVisualizerAudioProcessor::getArpeggioThresholdMs() const noexcept
{
    const double val = static_cast<double> (settingsState.getProperty ("arpeggioThresholdMs", defaultArpeggioThresholdMs));
    return juce::jlimit (100.0, 1200.0, val);
}

void HarmonyVisualizerAudioProcessor::setArpeggioThresholdMs (double ms)
{
    const double clamped = juce::jlimit (100.0, 1200.0, ms);
    settingsState.setProperty ("arpeggioThresholdMs", clamped, nullptr);
    settingsRevision.fetch_add (1, std::memory_order_relaxed);
}

void HarmonyVisualizerAudioProcessor::resetSettingsToDefaults()
{
    settingsState.setProperty ("maxHistorySize", defaultMaxHistorySize, nullptr);
    settingsState.setProperty ("quantizeWindowMs", defaultQuantizeWindowMs, nullptr);
    settingsState.setProperty ("arpeggioThresholdMs", defaultArpeggioThresholdMs, nullptr);
    settingsRevision.fetch_add (1, std::memory_order_relaxed);
}

//==============================================================================
void HarmonyVisualizerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = settingsState.createXml())
        copyXmlToBinary (*xml, destData);
}

void HarmonyVisualizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (settingsState.getType()))
        {
            auto restored = juce::ValueTree::fromXml (*xml);
            if (restored.isValid())
            {
                setMaxHistorySize (static_cast<int> (restored.getProperty ("maxHistorySize", defaultMaxHistorySize)));
                setQuantizeWindowMs (static_cast<double> (restored.getProperty ("quantizeWindowMs", defaultQuantizeWindowMs)));
                setArpeggioThresholdMs (static_cast<double> (restored.getProperty ("arpeggioThresholdMs", defaultArpeggioThresholdMs)));
            }
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonyVisualizerAudioProcessor();
}
