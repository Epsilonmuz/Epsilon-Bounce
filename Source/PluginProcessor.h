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

    // Two lanes (v1.5): one loop for the MELODY / sample, one for the DRUMS / percussion. Each is
    // dropped in and dragged out on its own; the Pocket link decides who follows whom.
    enum { melody = 0, drums = 1, numSlots = 2 };
    enum class Link { drumsLead = 0, melodyLeads, groove };   // parameter order: only ever append

    // ---- message thread API (editor) ----
    juce::String loadFile(const juce::File& file, int slot = melody);   // returns an error message, empty on success
    void clearSlot(int slot);
    juce::String getLoadNote() const { return loadNote; }   // e.g. "Melody detected: cutting on note changes"
    int getNoteSerial() const { return noteSerial; }
    std::shared_ptr<const bounce::LoopData> getLoop(int slot = melody) const;
    juce::String getLoopName(int slot = melody) const { const juce::ScopedLock sl(loopLock); return slots[(size_t) slot].name; }
    bounce::GrooveSettings currentGroove() const;           // the melody lane's groove
    bounce::GrooveSettings grooveFor(int slot) const;       // what this lane actually plays with (link resolved)
    double playbackBpm() const;                             // host tempo if known, else internal tempo
    bool isAnalysing(int slot = melody) const { return slots[(size_t) slot].analysing.load(); }
    int getPlayingSlice(int slot = melody) const { return slots[(size_t) slot].playingSlice.load(std::memory_order_relaxed); }
    Link link() const;
    bool haveBoth() const;                                  // both lanes hold a loop
    int leaderOf(int slot) const;                           // the lane whose pocket `slot` is locked to, or -1
    bool leads(int slot) const;                             // this lane keeps its own feel and the other follows it
    void setPreview(bool on) { preview.store(on); }
    bool isPreviewing() const { return preview.load(); }
    bool hostIsPlaying() const { return hostPlaying.load(); }
    juce::File renderToFile(int loops, int slot = melody);  // offline bounce for drag-out
    void prepareDragOut(int loops, int slot = melody);      // render it ahead, in the background (mouse over Drag out)
    bool isDragOutReady(int loops, int slot = melody) const { const auto k = dragKey(loops, slot); const juce::ScopedLock sl(loopLock); return slots[(size_t) slot].dragCacheKey == k; }
    int getLoopSerial(int slot = melody) const { return slots[(size_t) slot].serial.load(); }
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
    LearnInfo getPocketInfo(int slot) const { const juce::ScopedLock sl(loopLock); return slots[(size_t) slot].pocketInfo; }   // this lane's own feel
    int getLearnSerial() const { return learnSerial.load(); }

    juce::AudioProcessorValueTreeState state;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    // Loop hand-off: the audio thread reads `live` raw; old loops are retired here and freed only
    // once the audio thread has moved past them. One of these per lane.
    struct Slot
    {
        std::atomic<const bounce::LoopData*> live { nullptr };
        std::atomic<const bounce::LoopData*> inUse { nullptr };
        std::shared_ptr<const bounce::LoopData> current;
        std::vector<std::shared_ptr<const bounce::LoopData>> retired;
        std::vector<float> rawL, rawR;                        // the loaded audio (kept for re-analysis / state)
        double rawRate = 44100.0;
        juce::String name, lastSliceKey;                      // re-analyse when any slicing control changes
        std::atomic<bool> analysing { false };
        std::atomic<bool> cancelAnalysis { false };           // a slicing control moved again: drop the run in flight
        // The loop as the session stores it (FLAC), encoded once per loaded loop on the worker thread.
        // FL Studio asks for the state often (undo history, autosave); re-encoding 60 s of audio every time froze the UI.
        juce::MemoryBlock flacCache;                          // guarded by loopLock
        int flacFor = -1, rawSerial = 0;                      // guarded by loopLock: which raw audio the cache holds
        std::atomic<int> serial { 0 }, playingSlice { -1 };
        bool detectOnNextAnalysis = false;
        // The project tempo when this loop was loaded (0 = not known yet). The loop is always read on
        // this tempo, so changing the project tempo later never re-cuts it: it just plays faster or slower.
        std::atomic<double> madeAtBpm { 0.0 };
        juce::MemoryBlock dragCache;                          // guarded by loopLock
        juce::String dragCacheKey, dragPendingKey;            // guarded by loopLock
        // this loop's own pocket (microtiming + accents), readable from the audio thread without locks
        std::array<std::atomic<float>, bounce::stepsPerBar * 2> pocket {};
        std::atomic<bool> pocketValid { false };
        LearnInfo pocketInfo;                                 // guarded by loopLock
        // audio thread
        bounce::Player player;
        bounce::GrooveSettings lastSettings {};
        double lastBpm = -1.0;
        const bounce::LoopData* lastLoop = nullptr;
    };
    std::array<Slot, numSlots> slots;
    mutable juce::CriticalSection loopLock;                  // message/worker threads only

    void encodeFlacCache(int slot);
    static juce::MemoryBlock encodeFlac(const std::vector<float>& l, const std::vector<float>& r, double sr);
    juce::ThreadPool worker { 1 };
    juce::ThreadPool encoder { 1 };                          // session FLAC, in parallel with the analysis
    juce::String sliceKey(int slot) const;
    bounce::GrooveTemplate readPocket(int slot) const;
    int currentPreset = -1;
    juce::String loadNote;
    int noteSerial = 0;
    juce::String dragKey(int loops, int slot) const;
    static juce::MemoryBlock renderDragBytes(std::shared_ptr<const bounce::LoopData>, const bounce::GrooveSettings&, double bpm, double outRate, float gain, int loops);
    double analysisTempoHint() const;
    double tempoHintFor(int slot) const { return slots[(size_t) slot].madeAtBpm.load(std::memory_order_relaxed); }

    // learned groove, readable from the audio thread without locks
    std::array<std::atomic<float>, bounce::stepsPerBar * 2> learnedAtoms {};
    std::atomic<bool> learning { false };
    std::atomic<int> learnSerial { 0 };
    LearnInfo learnInfo;                                     // guarded by loopLock
    void setLearned(const bounce::GrooveTemplate& t, const LearnInfo& info);
    bounce::GrooveTemplate readLearned() const;

    double internalStep = 0.0, expectedStep = -1e9;
    std::atomic<bool> preview { false }, hostPlaying { false };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<float> cpu { 0.0f };
    double rate = 44100.0;

    std::atomic<float>* pGroove; std::atomic<float>* pAmount; std::atomic<float>* pSwing; std::atomic<float>* pTighten;
    std::atomic<float>* pHumanize; std::atomic<float>* pAccent; std::atomic<float>* pFeel; std::atomic<float>* pOutput;
    std::atomic<float>* pSync; std::atomic<float>* pTempo; std::atomic<float>* pSensitivity; std::atomic<float>* pBars;
    std::atomic<float>* pTime; std::atomic<float>* pMaterial; std::atomic<float>* pChop; std::atomic<float>* pHold;
    std::atomic<float>* pShift; std::atomic<float>* pAlign;
    std::atomic<float>* pLink; std::atomic<float>* pMelLevel; std::atomic<float>* pDrumLevel;

    void setRawAudio(int slot, std::vector<float> l, std::vector<float> r, double sr, const juce::String& name, double madeAtBpm = -1.0);
    void startAnalysis(int slot);
    void publish(int slot, std::shared_ptr<const bounce::LoopData> loop);
    void timerCallback() override;                           // frees retired loops, re-analyses on slicing changes
    bounce::GrooveSettings readGroove() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpsilonBounceProcessor)
};
