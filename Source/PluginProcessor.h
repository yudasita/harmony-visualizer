/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

//==============================================================================
/**
    リアルタイムスレッドとUIスレッド間で受け渡す固定長MIDIイベント構造体
*/
struct QueuedMidiEvent
{
    enum class Type : uint8_t
    {
        other = 0,
        noteOn = 1,
        noteOff = 2,
        controlChange = 3,
        allNotesOff = 4,
        pitchWheel = 5
    };

    Type type { Type::other };
    uint8_t channel { 1 };
    uint8_t noteOrController { 0 }; // ノート番号、またはCC番号（pitchWheel時はLSB 7bit）
    uint8_t value { 0 };            // ベロシティ、またはCC値（pitchWheel時はMSB 7bit）
    int sampleOffset { 0 };         // バッファ内サンプル位置
};

//==============================================================================
/**
*/
class HarmonyVisualizerAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    HarmonyVisualizerAudioProcessor();
    ~HarmonyVisualizerAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // 設定パネル（ValueTree状態管理）API
    static constexpr int defaultMaxHistorySize = 8;
    static constexpr double defaultQuantizeWindowMs = 60.0;
    static constexpr double defaultArpeggioThresholdMs = 450.0;

    int getMaxHistorySize() const noexcept;
    void setMaxHistorySize (int size);

    double getQuantizeWindowMs() const noexcept;
    void setQuantizeWindowMs (double ms);

    double getArpeggioThresholdMs() const noexcept;
    void setArpeggioThresholdMs (double ms);

    void resetSettingsToDefaults();

    juce::ValueTree& getSettingsState() noexcept { return settingsState; }
    const juce::ValueTree& getSettingsState() const noexcept { return settingsState; }
    uint32_t getSettingsRevision() const noexcept { return settingsRevision.load (std::memory_order_relaxed); }

    //==============================================================================
    /** UIスレッドなどの単一コンシューマスレッドから未読のMIDIイベントを取り出す */
    template <typename Handler>
    void popMidiEvents (Handler&& handler)
    {
        const int numReady = midiFifo.getNumReady();
        if (numReady <= 0)
            return;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        midiFifo.prepareToRead (numReady, start1, size1, start2, size2);

        for (int i = 0; i < size1; ++i)
            handler (midiFifoBuffer[static_cast<size_t> (start1 + i)]);

        for (int i = 0; i < size2; ++i)
            handler (midiFifoBuffer[static_cast<size_t> (start2 + i)]);

        midiFifo.finishedRead (size1 + size2);
    }

private:
    //==============================================================================
    static constexpr int midiFifoCapacity = 2048;
    std::array<QueuedMidiEvent, midiFifoCapacity> midiFifoBuffer {};
    juce::AbstractFifo midiFifo { midiFifoCapacity };

    juce::ValueTree settingsState { "HarmonyVisualizerSettings" };
    std::atomic<uint32_t> settingsRevision { 1 };

    void pushMidiEvent (const QueuedMidiEvent& event) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonyVisualizerAudioProcessor)
};
