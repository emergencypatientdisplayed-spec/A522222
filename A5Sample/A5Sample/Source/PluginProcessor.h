#pragma once
#include <JuceHeader.h>

// One chop of the loaded sample plus its per-pad settings.
struct Slice
{
    int start = 0, end = 0;          // sample range in the loaded file
    float level = 1.0f;              // 0..1.5
    float filter = 0.5f;             // 0 = low-pass closed, 0.5 = off, 1 = high-pass closed
    float attack = 0.002f;           // seconds
    float release = 0.08f;           // seconds
    float semitones = 0.0f;          // key shift (resampling pitch shift)
    bool reverse = false;
};

class A5SampleProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numPads = 32;
    static constexpr int baseNote = 36;   // pad 0 = C1 (MIDI 36)
    static constexpr int maxVoices = 16;

    A5SampleProcessor();
    ~A5SampleProcessor() override = default;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "A5 Sample"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Sample + slicing (called from the UI thread)
    bool loadFile (const juce::File&);
    void autoSlice();                    // transient-based
    void gridSlice (int numSlices);      // equal divisions
    int  getNumSlices() const;
    Slice getSlice (int index) const;
    void setSlice (int index, const Slice&);
    void moveBoundary (int sliceIndex, int newStart);
    int  getTotalSamples() const;
    std::vector<float> computePeaks (int columns) const;   // min,max pairs
    juce::String getLoadedName() const { return loadedFile.getFileName(); }

    std::atomic<bool>  monoMode { false };
    std::atomic<float> masterLevel { 1.0f };
    juce::MidiKeyboardState keyboardState;

private:
    struct Voice
    {
        bool active = false, releasing = false;
        int note = -1;
        uint32_t age = 0;
        double pos = 0.0, inc = 1.0;
        float env = 0.0f, attackRate = 0.01f, releaseRate = 0.001f;
        bool filterOn = false;
        juce::IIRFilter filtL, filtR;
        Slice s;
    };

    void startVoice (int note, float velocity);
    void releaseVoices (int note);
    void renderVoices (juce::AudioBuffer<float>&, int startSample, int numSamples);
    void applySlicePoints (const std::vector<int>& starts);
    std::vector<int> detectOnsets (int maxSlices) const;

    mutable juce::CriticalSection lock;
    juce::AudioFormatManager formats;
    juce::AudioBuffer<float> sample;
    double fileSampleRate = 44100.0;
    double hostSampleRate = 44100.0;
    juce::File loadedFile;
    std::vector<Slice> slices;
    Voice voices[maxVoices];
    uint32_t voiceCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (A5SampleProcessor)
};
