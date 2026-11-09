#pragma once
#include <JuceHeader.h>
#include "BounceEngine.h"
#include "Presets.h"

class EpsilonBounceProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    EpsilonBounceProcessor();
    ~EpsilonBounceProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- message thread API (editor) ----
    juce::String loadFile(const juce::File& file);          // returns an error message, empty on success
    std::shared_ptr<const bounce::LoopData> getLoop() const;
    juce::String getLoopName() const { return loopName; }
    bounce::GrooveSettings currentGroove() const;
    double playbackBpm() const;                             // host tempo if known, else internal tempo
    bool isAnalysing() const { return analysing.load(); }
    int getPlayingSlice() const { return playingSlice.load(std::memory_order_relaxed); }
    void setPreview(bool on) { preview.store(on); }
    bool isPreviewing() const { return preview.load(); }
    bool hostIsPlaying() const { return hostPlaying.load(); }
    juce::File renderToFile(int loops);                     // offline bounce for drag-out
    int getLoopSerial() const { return loopSerial.load(); }
    float getCpuPercent() const { return cpu.load(); }
    double timeFactor() const;                              // 0.5 half-time, 1, 2 double-time
    double gridBpm() const { return playbackBpm() * timeFactor(); }   // tempo of the loop's own grid

    // Presets (message thread)
    void applyPreset(int index);
    int getCurrentPreset() const { return currentPreset; }

    // Groove Learn: copy a reference loop's feel into the LEARNED groove (message thread).
    juce::String learnFromFile(const juce::File& file);     // returns an error, empty on success (runs async)
    bool isLearning() const { return learning.load(); }
    struct LearnInfo { juce::String name; int hits = 0, steps = 0; float swingPercent = 50.0f; bool valid = false;
                       std::array<bool, bounce::stepsPerBar> heard {}; };
    bounce::Material material() const;
    bool holdActive() const;                                // Hold resolved (AUTO = on for melodic / grid)
    LearnInfo getLearnInfo() const { const juce::ScopedLock sl(loopLock); return learnInfo; }
    int getLearnSerial() const { return learnSerial.load(); }

    juce::AudioProcessorValueTreeState state;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    // Loop hand-off: the audio thread reads `live` raw; old loops are retired here and freed only
    // once the audio thread has moved past them.
    std::atomic<const bounce::LoopData*> live { nullptr };
    std::atomic<const bounce::LoopData*> inUse { nullptr };
    std::shared_ptr<const bounce::LoopData> current;
    std::vector<std::shared_ptr<const bounce::LoopData>> retired;
    mutable juce::CriticalSection loopLock;                  // message/worker threads only

    std::vector<float> rawL, rawR;                            // the loaded audio (kept for re-analysis / state)
    double rawRate = 44100.0;
    juce::String loopName;
    std::atomic<bool> analysing { false };
    std::atomic<int> loopSerial { 0 };
    juce::ThreadPool worker { 1 };
    juce::String lastSliceKey;                               // re-analyse when any slicing control changes
    juce::String sliceKey() const;
    int currentPreset = -1;

    // learned groove, readable from the audio thread without locks
    std::array<std::atomic<float>, bounce::stepsPerBar * 2> learnedAtoms {};
    std::atomic<bool> learning { false };
    std::atomic<int> learnSerial { 0 };
    LearnInfo learnInfo;                                     // guarded by loopLock
    void setLearned(const bounce::GrooveTemplate& t, const LearnInfo& info);
    bounce::GrooveTemplate readLearned() const;

    bounce::Player player;
    bounce::GrooveSettings lastSettings {};
    double lastBpm = -1.0;
    const bounce::LoopData* lastLoop = nullptr;
    double internalStep = 0.0, expectedStep = -1e9;
    std::atomic<bool> preview { false }, hostPlaying { false };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<int> playingSlice { -1 };
    std::atomic<float> cpu { 0.0f };
    double rate = 44100.0;

    std::atomic<float>* pGroove; std::atomic<float>* pAmount; std::atomic<float>* pSwing; std::atomic<float>* pTighten;
    std::atomic<float>* pHumanize; std::atomic<float>* pAccent; std::atomic<float>* pFeel; std::atomic<float>* pOutput;
    std::atomic<float>* pSync; std::atomic<float>* pTempo; std::atomic<float>* pSensitivity; std::atomic<float>* pBars;
    std::atomic<float>* pTime; std::atomic<float>* pMaterial; std::atomic<float>* pChop; std::atomic<float>* pHold;
    std::atomic<float>* pShift; std::atomic<float>* pAlign;

    void setRawAudio(std::vector<float> l, std::vector<float> r, double sr, const juce::String& name);
    void startAnalysis();
    void publish(std::shared_ptr<const bounce::LoopData> loop);
    void timerCallback() override;                           // frees retired loops, re-analyses on slicing changes
    bounce::GrooveSettings readGroove() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpsilonBounceProcessor)
};
