#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr char stateMagic[4] { 'E', 'B', 'v', '2' };
juce::String pct(float v) { return juce::String(juce::roundToInt(v)) + " %"; }
juce::String ms(float v) { return (v > 0.05f ? "+" : "") + juce::String(v, 1) + " ms"; }
constexpr int barsChoices[] { 0, 1, 2, 4, 8 };
constexpr double maxLoopSeconds = 60.0;
}

juce::AudioProcessorValueTreeState::ParameterLayout EpsilonBounceProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const auto attr = [](std::function<String(float)> f) { return AudioParameterFloatAttributes().withStringFromValueFunction([f](float v, int) { return f(v); }); };
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "groove", 1 }, "Groove", [] { StringArray names; for (int i = 0; i < bounce::grooveCount; ++i) names.add(bounce::grooveName((bounce::Groove) i)); return names; }(), 1));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "amount", 1 }, "Amount", NormalisableRange<float>(0.0f, 150.0f, 0.1f), 100.0f, attr(pct)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "swing", 1 }, "Swing", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f, attr(pct)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "tighten", 1 }, "Tighten", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f, attr(pct)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "humanize", 1 }, "Humanize", NormalisableRange<float>(0.0f, 20.0f, 0.1f), 0.0f,
                                                attr([](float v) { return juce::String(v, 1) + " ms"; })));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "accent", 1 }, "Accent", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 75.0f, attr(pct)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "feel", 1 }, "Feel", NormalisableRange<float>(-20.0f, 20.0f, 0.1f), 0.0f, attr(ms)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "output", 1 }, "Output", NormalisableRange<float>(-24.0f, 6.0f, 0.1f), 0.0f,
                                                attr([](float v) { return (v > 0.05f ? "+" : "") + juce::String(v, 1) + " dB"; })));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "sync", 1 }, "Host Sync", true));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "tempo", 1 }, "Tempo", NormalisableRange<float>(60.0f, 200.0f, 0.1f), 120.0f,
                                                attr([](float v) { return juce::String(v, 1) + " BPM"; })));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "sensitivity", 1 }, "Sensitivity", NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f, attr(pct)));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "bars", 1 }, "Bars", StringArray { "Auto", "1 bar", "2 bars", "4 bars", "8 bars" }, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "time", 2 }, "Time", StringArray { "NORMAL", "HALF-TIME", "DOUBLE-TIME" }, 0));
    // v1.2: melodies and flipped samples
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "material", 3 }, "Cut On", StringArray { "DRUMS", "MELODIC", "GRID" }, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "chop", 3 }, "Grid Chop", StringArray { "16TH", "8TH", "BEAT" }, 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "hold", 3 }, "Hold", StringArray { "AUTO", "ON", "OFF" }, 0));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "shift", 3 }, "Shift", -8, 8, 0,
                                              AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return v == 0 ? juce::String("0") : (v > 0 ? "+" : "") + juce::String(v) + " /16"; })));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "align", 3 }, "Auto-Align", true));
    // v1.5: melody + drums lanes
    const auto level = [](float v) { return v <= -59.9f ? juce::String("off") : (v > 0.05f ? "+" : "") + juce::String(v, 1) + " dB"; };
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "link", 5 }, "Pocket", StringArray { "DRUMS LEAD", "MELODY LEADS", "GROOVE" }, 0));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "melLevel", 5 }, "Melody Level", NormalisableRange<float>(-60.0f, 6.0f, 0.1f, 2.5f), 0.0f, attr(level)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "drumLevel", 5 }, "Drums Level", NormalisableRange<float>(-60.0f, 6.0f, 0.1f, 2.5f), 0.0f, attr(level)));
    // v2.0: Pocket Engine
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "depth", 7 }, "Pocket Depth", NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f, attr(pct)));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "heard", 7 }, "Heard Timing", true));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "engine", 7 }, "Lock Engine", StringArray { "WARP", "SLICES" }, 0));
    return l;
}

EpsilonBounceProcessor::EpsilonBounceProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "EpsilonBounce", createLayout())
{
    pGroove = state.getRawParameterValue("groove");     pAmount = state.getRawParameterValue("amount");
    pSwing = state.getRawParameterValue("swing");       pTighten = state.getRawParameterValue("tighten");
    pHumanize = state.getRawParameterValue("humanize"); pAccent = state.getRawParameterValue("accent");
    pFeel = state.getRawParameterValue("feel");         pOutput = state.getRawParameterValue("output");
    pSync = state.getRawParameterValue("sync");         pTempo = state.getRawParameterValue("tempo");
    pSensitivity = state.getRawParameterValue("sensitivity"); pBars = state.getRawParameterValue("bars");
    pTime = state.getRawParameterValue("time");
    pMaterial = state.getRawParameterValue("material"); pChop = state.getRawParameterValue("chop"); pHold = state.getRawParameterValue("hold");
    pShift = state.getRawParameterValue("shift"); pAlign = state.getRawParameterValue("align");
    pLink = state.getRawParameterValue("link"); pMelLevel = state.getRawParameterValue("melLevel"); pDrumLevel = state.getRawParameterValue("drumLevel");
    pDepth = state.getRawParameterValue("depth"); pHeard = state.getRawParameterValue("heard"); pEngine = state.getRawParameterValue("engine");
    for (auto& a : learnedAtoms) a.store(0.0f);
    for (auto& sl : slots) for (auto& a : sl.pocket) a.store(0.0f);
    startTimerHz(10);
}

EpsilonBounceProcessor::~EpsilonBounceProcessor()
{
    stopTimer();
    alive->store(false);
    for (auto& s : slots) { s.cancelAnalysis = true; s.cancelPocket = true; }   // runs in flight stop at their next check
    pocketWorker.removeAllJobs(true, 4000);
    worker.removeAllJobs(true, 4000);
    encoder.removeAllJobs(true, 4000);
}

bool EpsilonBounceProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void EpsilonBounceProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    rate = sampleRate > 0 ? sampleRate : 44100.0;
    for (auto& sl : slots) { sl.player.prepare(rate, samplesPerBlock); sl.lastLoop = nullptr; sl.lastBpm = -1.0; }
    expectedStep = -1e9; internalStep = 0.0;
}

bounce::GrooveSettings EpsilonBounceProcessor::readGroove() const
{
    bounce::GrooveSettings g;
    g.groove = (bounce::Groove) juce::jlimit(0, bounce::grooveCount - 1, juce::roundToInt(pGroove->load(std::memory_order_relaxed)));
    g.amount = pAmount->load(std::memory_order_relaxed) * 0.01f;
    g.swing = pSwing->load(std::memory_order_relaxed) * 0.01f;
    g.tighten = pTighten->load(std::memory_order_relaxed) * 0.01f;
    g.humanizeMs = pHumanize->load(std::memory_order_relaxed);
    g.accent = pAccent->load(std::memory_order_relaxed) * 0.01f;
    g.feelMs = pFeel->load(std::memory_order_relaxed);
    if (g.groove == bounce::Groove::learned) g.learned = readLearned();
    g.shiftSteps = juce::roundToInt(pShift->load(std::memory_order_relaxed));
    g.hold = holdActive();
    g.softCuts = material() != bounce::Material::drums;
    return g;
}

bounce::Material EpsilonBounceProcessor::material() const
{
    return (bounce::Material) juce::jlimit(0, 2, juce::roundToInt(pMaterial->load(std::memory_order_relaxed)));
}

bool EpsilonBounceProcessor::holdActive() const
{
    const int h = juce::roundToInt(pHold->load(std::memory_order_relaxed));
    return h == 1 || (h == 0 && material() != bounce::Material::drums);
}

juce::String EpsilonBounceProcessor::sliceKey(int slot) const
{
    if (slot == drums)                                       // drums are always cut on their hits
        return juce::String(pAlign->load() >= 0.5f ? 1 : 0) + "|" + juce::String(tempoHintFor(slot), 1);
    return juce::String(juce::roundToInt(pSensitivity->load())) + "|" + juce::String(juce::roundToInt(pBars->load())) + "|"
         + juce::String((int) material()) + "|" + juce::String(juce::roundToInt(pChop->load())) + "|" + juce::String(pAlign->load() >= 0.5f ? 1 : 0)
         + "|" + juce::String(tempoHintFor(slot), 1);
}

bounce::GrooveTemplate EpsilonBounceProcessor::readPocket(int slot) const
{
    bounce::GrooveTemplate t;
    for (int i = 0; i < bounce::stepsPerBar; ++i)
    {
        t.offset[(size_t) i] = slots[(size_t) slot].pocket[(size_t) i].load(std::memory_order_relaxed);
        t.accentDb[(size_t) i] = slots[(size_t) slot].pocket[(size_t) (i + bounce::stepsPerBar)].load(std::memory_order_relaxed);
    }
    return t;
}

EpsilonBounceProcessor::Link EpsilonBounceProcessor::link() const
{
    return (Link) juce::jlimit(0, 2, juce::roundToInt(pLink->load(std::memory_order_relaxed)));
}
bool EpsilonBounceProcessor::haveBoth() const
{
    return slots[melody].live.load(std::memory_order_relaxed) != nullptr && slots[drums].live.load(std::memory_order_relaxed) != nullptr;
}
bool EpsilonBounceProcessor::leads(int slot) const
{
    if (! haveBoth()) return false;
    const auto l = link();
    return (l == Link::drumsLead && slot == drums) || (l == Link::melodyLeads && slot == melody);
}
int EpsilonBounceProcessor::leaderOf(int slot) const
{
    if (! haveBoth() || link() == Link::groove) return -1;
    const int leader = link() == Link::drumsLead ? drums : melody;
    return leader != slot && slots[(size_t) leader].pocketValid.load(std::memory_order_relaxed) ? leader : -1;
}

// What a lane plays with. One loop loaded, or Pocket = GROOVE: the chosen groove, as always.
// Both loaded and one leads: the leader plays with its own timing untouched; the other is LOCKED to
// the leader's pocket, the same lock Match drums applies (Tighten 100 % strips its old feel, the
// leader's microtiming goes on at 100 %, nothing is added on top). Accent stays on its knob.
bounce::GrooveSettings EpsilonBounceProcessor::grooveFor(int slot) const { return grooveFor(slot, pocketActive(slot)); }

bool EpsilonBounceProcessor::warpEngine() const { return juce::roundToInt(pEngine->load(std::memory_order_relaxed)) == 0; }

// The v2.0 lock plays when this lane follows the other and its lock was made from the loops loaded now.
// (While a new lock is being made, the 1.x lock plays: never silence, never the wrong audio.)
bool EpsilonBounceProcessor::pocketActive(int slot) const
{
    const int leader = leaderOf(slot);
    if (leader < 0 || ! warpEngine()) return false;
    const auto& s = slots[(size_t) slot];
    return s.pocketLive.load(std::memory_order_acquire) != nullptr
        && s.pocketFollow.load(std::memory_order_relaxed) == s.serial.load(std::memory_order_relaxed)
        && s.pocketLead.load(std::memory_order_relaxed) == slots[(size_t) leader].serial.load(std::memory_order_relaxed);
}

bounce::GrooveSettings EpsilonBounceProcessor::grooveFor(int slot, bool pocket) const
{
    auto g = readGroove();
    if (slot == drums)
    {
        g.hold = juce::roundToInt(pHold->load(std::memory_order_relaxed)) == 1;
        g.softCuts = false;
        g.shiftSteps = 0;
    }
    if (leads(slot))
    {
        g.amount = 0.0f; g.swing = 0.0f; g.tighten = 0.0f; g.humanizeMs = 0.0f; g.accent = 0.0f; g.feelMs = 0.0f;
        return g;
    }
    const int leader = leaderOf(slot);
    if (leader >= 0 && pocket)
    {
        // v2.0: the lock is already in the audio (and in its slices' places). Play it as it is; the leader's
        // accents stay on the Accent knob, Shift still moves bar 1.
        g.groove = bounce::Groove::learned;
        g.learned = readPocket(leader);
        for (auto& o : g.learned.offset) o = 0.0f;
        g.amount = 0.0f; g.tighten = 0.0f; g.swing = 0.0f; g.humanizeMs = 0.0f; g.feelMs = 0.0f;
        g.hold = false;                                  // the warp leaves no gaps to hold through
    }
    else if (leader >= 0)
    {
        g.groove = bounce::Groove::learned;
        g.learned = readPocket(leader);
        g.amount = 1.0f; g.tighten = 1.0f; g.swing = 0.0f; g.humanizeMs = 0.0f; g.feelMs = 0.0f;
    }
    return g;
}

// The project tempo, when the host gives one: a loop bounced from the same project then gets an exact grid.
double EpsilonBounceProcessor::analysisTempoHint() const
{
    const double h = hostBpm.load();
    return pSync->load() >= 0.5f && h >= 40.0 && h <= 300.0 ? h : 0.0;
}

double EpsilonBounceProcessor::matchTarget() const
{
    const double b = playbackBpm();
    return b >= 40.0 && b <= 300.0 ? b : 0.0;
}

void EpsilonBounceProcessor::setSourceTempo(int slot, double bpm)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    auto& s = slots[(size_t) slot];
    {
        const juce::ScopedLock sl(loopLock);
        if (s.rawL.empty()) return;
        s.srcOverride = bpm > 0.0 ? juce::jlimit(30.0, 300.0, bpm) : 0.0;
        if (bpm <= 0.0) { s.srcFor = -1; s.srcBpm.store(0.0, std::memory_order_relaxed); }   // detect again
    }
    restartAnalysis(slot);
}

void EpsilonBounceProcessor::restartAnalysis(int slot)
{
    auto& s = slots[(size_t) slot];
    if (! s.analysing.load()) { s.restartPending = false; startAnalysis(slot); return; }
    s.cancelAnalysis = true;          // the run in flight stops at its next check; the timer starts the new one
    s.restartPending = true;
}

void EpsilonBounceProcessor::scaleSourceTempo(int slot, double factor)
{
    const double cur = getSourceBpm(slot);
    if (cur > 0.0) setSourceTempo(slot, cur * factor);
}

void EpsilonBounceProcessor::setFit(int slot, Fit f)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    auto& s = slots[(size_t) slot];
    {
        const juce::ScopedLock sl(loopLock);
        if (s.fitMode == (int) f) return;
        s.fitMode = (int) f;
        if (s.rawL.empty()) return;
    }
    restartAnalysis(slot);
}

bool EpsilonBounceProcessor::fitUsesStretch(int slot) const
{
    int f;
    { const juce::ScopedLock sl(loopLock); f = slots[(size_t) slot].fitMode; }
    if (f == (int) Fit::stretch) return true;
    if (f == (int) Fit::slices) return false;
    return slot == melody && material() != bounce::Material::drums;
}

bounce::GrooveTemplate EpsilonBounceProcessor::readLearned() const
{
    bounce::GrooveTemplate t;
    for (int i = 0; i < bounce::stepsPerBar; ++i)
    {
        t.offset[(size_t) i] = learnedAtoms[(size_t) i].load(std::memory_order_relaxed);
        t.accentDb[(size_t) i] = learnedAtoms[(size_t) (i + bounce::stepsPerBar)].load(std::memory_order_relaxed);
    }
    return t;
}

double EpsilonBounceProcessor::timeFactor() const
{
    return bounce::timeFactor((bounce::TimeMode) juce::jlimit(0, 2, juce::roundToInt(pTime->load(std::memory_order_relaxed))));
}
bounce::GrooveSettings EpsilonBounceProcessor::currentGroove() const { return grooveFor(melody); }

double EpsilonBounceProcessor::playbackBpm() const
{
    const auto h = hostBpm.load();
    return (pSync->load() >= 0.5f && h > 0.0) ? h : (double) pTempo->load();
}

static bool sameSettings(const bounce::GrooveSettings& a, const bounce::GrooveSettings& b)
{
    return a.shiftSteps == b.shiftSteps && a.hold == b.hold && a.softCuts == b.softCuts && a.groove == b.groove && a.amount == b.amount && a.swing == b.swing && a.tighten == b.tighten
        && a.humanizeMs == b.humanizeMs && a.accent == b.accent && a.feelMs == b.feelMs && a.learned == b.learned;
}

void EpsilonBounceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int frames = buffer.getNumSamples();
    buffer.clear();
    midi.clear();

    // Take both loops for this block (a loop swapped mid-check sits the block out).
    const bounce::LoopData* loops[numSlots] {};
    bool usePocket[numSlots] {};
    bool any = false;
    for (int i = 0; i < numSlots; ++i)
    {
        auto& sl = slots[(size_t) i];
        // the lane's v2.0 pocket lock when it follows, else its loop
        usePocket[i] = pocketActive(i);
        auto& src = usePocket[i] ? sl.pocketLive : sl.live;
        const auto* l = src.load(std::memory_order_acquire);
        sl.inUse.store(l, std::memory_order_release);
        if (src.load(std::memory_order_acquire) != l) { sl.inUse.store(nullptr, std::memory_order_release); l = nullptr; }
        if (l != nullptr && l->slices.empty()) l = nullptr;
        loops[i] = l;
        any |= l != nullptr;
    }
    const auto release = [this] { for (auto& sl : slots) sl.inUse.store(nullptr, std::memory_order_release); };

    // Transport. Read before the empty-plugin early return: the project tempo must be known before
    // the first loop arrives, so that loop is read on it straight away (not re-cut at the first play).
    const bool sync = pSync->load(std::memory_order_relaxed) >= 0.5f;
    bool playing = false; double bpm = pTempo->load(std::memory_order_relaxed), stepStart = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) { hostBpm.store(*b); if (sync) bpm = *b; }
            hostPlaying.store(pos->getIsPlaying());
            if (sync && pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition()) { playing = true; stepStart = *ppq * 4.0; }
        }

    if (! any || frames == 0) { for (auto& sl : slots) sl.playingSlice.store(-1); release(); return; }
    const auto* first = loops[melody] != nullptr ? loops[melody] : loops[drums];
    // Time modality: half-time plays the loop's grid at half speed, double-time at twice.
    const double k = timeFactor();
    stepStart *= k;
    bpm *= k;
    const double stepsPerSample = bpm / 60.0 * 4.0 / rate;
    if (! playing && preview.load(std::memory_order_relaxed))
    {
        playing = true;
        stepStart = internalStep;   // already in grid steps
        internalStep += frames * stepsPerSample;
        // wrap on a length both loops divide (bar counts are powers of two)
        const double loopSteps = std::max(first->bars, loops[drums] != nullptr ? loops[drums]->bars : 1) * (double) bounce::stepsPerBar;
        if (internalStep > loopSteps * 1024.0) internalStep = std::fmod(internalStep, loopSteps);
    }
    else if (! preview.load(std::memory_order_relaxed))
        internalStep = 0.0;

    if (! playing)
    {
        if (expectedStep > -1e8) for (auto& sl : slots) sl.player.reset();
        expectedStep = -1e9;
        for (auto& sl : slots) sl.playingSlice.store(-1);
        release();
        return;
    }

    const bool jumped = std::abs(stepStart - expectedStep) > 0.5;            // host loop / locate
    expectedStep = stepStart + frames * stepsPerSample;
    const float outGain = juce::Decibels::decibelsToGain(pOutput->load(std::memory_order_relaxed));
    for (int i = 0; i < numSlots; ++i)
    {
        auto& sl = slots[(size_t) i];
        const auto* loop = loops[i];
        if (loop == nullptr) { sl.playingSlice.store(-1, std::memory_order_relaxed); sl.lastLoop = nullptr; continue; }
        const auto g = grooveFor(i, usePocket[i]);
        if (loop != sl.lastLoop || std::abs(bpm - sl.lastBpm) > 1e-6 || ! sameSettings(g, sl.lastSettings))
        {
            if (loop != sl.lastLoop) sl.player.reset();
            sl.player.update(*loop, g, bpm);
            sl.lastLoop = loop; sl.lastBpm = bpm; sl.lastSettings = g;
        }
        if (jumped) sl.player.reset();
        const float lv = (i == melody ? pMelLevel : pDrumLevel)->load(std::memory_order_relaxed);
        const float gain = lv <= -59.9f ? 0.0f : outGain * juce::Decibels::decibelsToGain(lv);
        sl.player.render(*loop, buffer.getWritePointer(0), buffer.getWritePointer(1), frames, stepStart, stepsPerSample, gain);
        sl.playingSlice.store(sl.player.playingSlice(), std::memory_order_relaxed);
    }
    release();

    const auto elapsed = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
    const float load = (float) (elapsed / (frames / rate) * 100.0);
    cpu.store(cpu.load(std::memory_order_relaxed) * 0.95f + load * 0.05f, std::memory_order_relaxed);
}

//==============================================================================
juce::String EpsilonBounceProcessor::loadFile(const juce::File& file, int slot)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader == nullptr) return "Can't read that file - try WAV, AIFF or FLAC.";
    if (reader->sampleRate <= 0 || reader->lengthInSamples <= 0) return "That file is empty.";
    if (reader->lengthInSamples > (juce::int64) (maxLoopSeconds * reader->sampleRate)) return "Loops up to 60 seconds, please.";
    const int n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buf((int) juce::jmax(1u, juce::jmin(2u, reader->numChannels)), n);
    reader->read(&buf, 0, n, 0, true, buf.getNumChannels() > 1);
    std::vector<float> l(buf.getReadPointer(0), buf.getReadPointer(0) + n);
    std::vector<float> r(buf.getReadPointer(buf.getNumChannels() - 1), buf.getReadPointer(buf.getNumChannels() - 1) + n);

    // Melody lane: drums or a melody? The analysis job decides (off this thread) and picks the cut mode
    // for it. GRID is a deliberate choice, so it is left alone. The drums lane is always cut on its hits.
    loadNote.clear();
    slots[(size_t) slot].detectOnNextAnalysis = slot == melody && material() != bounce::Material::grid;
    setRawAudio(slot, std::move(l), std::move(r), reader->sampleRate, file.getFileNameWithoutExtension());
    return {};
}

void EpsilonBounceProcessor::setRawAudio(int slot, std::vector<float> l, std::vector<float> r, double sr, const juce::String& name, double madeAtBpm,
                                         double knownSrcBpm, double srcOverride, int fitMode)
{
    {
        const juce::ScopedLock sl(loopLock);
        auto& s = slots[(size_t) slot];
        s.rawL = std::move(l); s.rawR = std::move(r); s.rawRate = sr; s.name = name;
        s.madeAtBpm.store(madeAtBpm >= 0.0 ? madeAtBpm : analysisTempoHint(), std::memory_order_relaxed);
        ++s.rawSerial;
        // a new loop: its tempo is read afresh (a session brings back the one read before, so it never flips)
        s.srcOverride = srcOverride;
        s.fitMode = juce::jlimit(0, 2, fitMode);
        s.srcBpm.store(knownSrcBpm > 0.0 ? knownSrcBpm : 0.0, std::memory_order_relaxed);
        s.srcFor = knownSrcBpm > 0.0 ? s.rawSerial : -1;
        s.fitL.clear(); s.fitR.clear(); s.fitKey.clear();
        s.rematchSince = 0;
    }
    startAnalysis(slot);
    encoder.addJob([this, slot] { encodeFlacCache(slot); });   // ready long before the host's next save
}

void EpsilonBounceProcessor::clearSlot(int slot)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    {
        const juce::ScopedLock sl(loopLock);
        auto& s = slots[(size_t) slot];
        s.rawL.clear(); s.rawR.clear(); s.name.clear();
        s.madeAtBpm.store(0.0, std::memory_order_relaxed);
        s.srcBpm.store(0.0, std::memory_order_relaxed); s.matchedBpm.store(0.0, std::memory_order_relaxed);
        s.stretched.store(false, std::memory_order_relaxed);
        s.srcOverride = 0.0; s.fitMode = 0; s.srcFor = -1; s.fitL.clear(); s.fitR.clear(); s.fitKey.clear();
        s.flacCache.reset(); s.flacFor = -1; ++s.rawSerial;
        s.dragCache.reset(); s.dragCacheKey.clear(); s.dragPendingKey.clear();
    }
    slots[(size_t) slot].cancelAnalysis = true;
    slots[(size_t) slot].cancelPocket = true;
    publish(slot, nullptr);
}

juce::MemoryBlock EpsilonBounceProcessor::encodeFlac(const std::vector<float>& l, const std::vector<float>& r, double sr)
{
    juce::MemoryBlock flacData;
    auto* mos = new juce::MemoryOutputStream(flacData, false);
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatWriter> w(flac.createWriterFor(mos, sr, 2, 24, {}, 0));
    if (w == nullptr) { delete mos; return {}; }
    const float* chans[] { l.data(), r.data() };
    w->writeFromFloatArrays(chans, 2, (int) l.size());
    w.reset();
    return flacData;
}

void EpsilonBounceProcessor::encodeFlacCache(int slot)
{
    auto& s = slots[(size_t) slot];
    std::vector<float> l, r; double sr; int serial;
    {
        const juce::ScopedLock sl(loopLock);
        if (s.rawL.empty() || s.flacFor == s.rawSerial) return;
        l = s.rawL; r = s.rawR; sr = s.rawRate; serial = s.rawSerial;
    }
    auto data = encodeFlac(l, r, sr);
    const juce::ScopedLock sl(loopLock);
    if (serial == s.rawSerial) { s.flacCache = std::move(data); s.flacFor = serial; }
}

void EpsilonBounceProcessor::startAnalysis(int slot)
{
    auto& s = slots[(size_t) slot];
    std::vector<float> l, r; double sr; int rawSerial;
    double override_, knownSrc; int fitMode;
    {
        const juce::ScopedLock sl(loopLock);
        if (s.rawL.empty()) return;
        l = s.rawL; r = s.rawR; sr = s.rawRate; rawSerial = s.rawSerial;
        override_ = s.srcOverride; fitMode = s.fitMode;
        knownSrc = s.srcFor == s.rawSerial ? s.srcBpm.load(std::memory_order_relaxed) : 0.0;
    }
    const double target = matchTarget();
    const double madeAt = tempoHintFor(slot);
    bounce::AnalysisSettings a;
    if (slot == drums)
    {
        a.sensitivity = 0.6f;     // catch ghost notes: they carry most of a groove's feel
        a.barsHint = 0;
        a.material = bounce::Material::drums;
    }
    else
    {
        a.sensitivity = pSensitivity->load() * 0.01f;
        a.barsHint = barsChoices[juce::jlimit(0, 4, juce::roundToInt(pBars->load()))];
        a.material = material();
        a.gridDiv = juce::roundToInt(pChop->load()) == 2 ? 4 : juce::roundToInt(pChop->load()) == 1 ? 2 : 1;
    }
    a.autoAlign = pAlign->load() >= 0.5f;
    a.tempoHint = tempoHintFor(slot);   // the tempo it was loaded at: a later project tempo change never re-cuts it
    a.cancel = &s.cancelAnalysis;
    s.lastSliceKey = sliceKey(slot);
    s.analysing = true;
    s.cancelAnalysis = false;
    const bool detect = std::exchange(s.detectOnNextAnalysis, false);
    worker.addJob([this, slot, rawSerial, l = std::move(l), r = std::move(r), sr, a, detect, target, madeAt, override_, knownSrc, fitMode]() mutable
    {
        bool switched = false;
        if (detect)
        {
            const auto guess = bounce::detectMaterial(l, r, sr);
            switched = guess.material != a.material;
            a.material = guess.material;
        }

        // ---- Step 1: tempo match ----
        const int n = (int) l.size();
        double src = override_ > 0.0 ? override_ : knownSrc;
        float conf = override_ > 0.0 ? 1.0f : slots[(size_t) slot].tempoConf.load(std::memory_order_relaxed);
        if (src <= 0.0 && a.barsHint > 0 && n > 0)
            src = 240.0 * a.barsHint * sr / n;                     // the Bars control says how long the loop is
        if (src <= 0.0 && n > 0)
        {
            const auto g = bounce::estimateTempo(l, r, sr, target > 0.0 ? target : madeAt);
            src = g.bpm; conf = g.confidence;
            // Made in this project (1.5.1)? A whole number of bars at the project tempo, and the rhythm agrees
            // within 2 %: then the project tempo is exact (a tail or a few ms of silence can't pull it off).
            if (madeAt > 0.0 && src > 0.0)
            {
                const double bars = n / (240.0 / madeAt * sr);
                for (int b : { 1, 2, 4, 8, 16, 32 })
                    if (bars >= b * 0.985 && bars <= b + std::min(0.5, 0.25 * b))
                        for (double m : { 0.5, 1.0, 2.0 })
                            if (std::abs(src / (madeAt * m) - 1.0) < 0.02) { src = madeAt * m; conf = std::max(conf, 0.9f); }
                if (std::abs(src / madeAt - 2.0) < 1e-9 || std::abs(src / madeAt - 0.5) < 1e-9) src = madeAt;
            }
        }
        const bool stretchFit = fitMode == (int) Fit::stretch
                             || (fitMode == (int) Fit::automatic && slot == melody && a.material != bounce::Material::drums);
        const bool needStretch = src > 0.0 && target > 0.0 && stretchFit && std::abs(src / target - 1.0) > 0.002;
        const juce::String key = juce::String(rawSerial) + "|" + juce::String(src, 4) + "|" + juce::String(target, 4) + "|" + juce::String((int) a.material);
        std::vector<float> fl, fr;
        if (needStretch)
        {
            const juce::ScopedLock lock(loopLock);
            auto& sl = slots[(size_t) slot];
            if (sl.fitKey == key && ! sl.fitL.empty()) { fl = sl.fitL; fr = sl.fitR; }
        }
        // Phase 1: the loop cut on its own grid, playable at once (the player re-triggers its pieces on the
        // project grid while the stretch is made).
        auto a1 = a;
        if (src > 0.0) a1.exactBpm = src;
        auto keepL = needStretch ? l : std::vector<float>(), keepR = needStretch ? r : std::vector<float>();
        auto data1 = bounce::analyse(std::move(l), std::move(r), sr, a1);
        const bool cachedStretch = ! fl.empty();
        if (! (needStretch && cachedStretch) || data1 == nullptr)   // (with the stretch cached, go straight to the stretched loop)
            postAnalysis(slot, rawSerial, data1, detect, switched, a.material, src, conf, false, src > 0.0 ? src : 0.0, target, ! needStretch || data1 == nullptr);
        if (! needStretch || data1 == nullptr) return;

        // Phase 2: stretch to the project tempo (pitch kept), cut on the same attacks, so the slices carry over.
        const double factor = src / target;
        if (fl.empty())
        {
            std::vector<int> cuts;
            for (const auto& sc : data1->slices) cuts.push_back(sc.onset);   // stretch pieces start on the attacks
            if (! bounce::stretchLoop(keepL, keepR, factor, sr, a.material, fl, fr, a.cancel, &cuts))
            {
                juce::MessageManager::callAsync([this, slot, token = alive] { if (token->load()) slots[(size_t) slot].analysing = false; });
                return;                                         // out of date: the timer starts the next run
            }
            const juce::ScopedLock lock(loopLock);
            auto& sl = slots[(size_t) slot];
            if (sl.rawSerial == rawSerial) { sl.fitL = fl; sl.fitR = fr; sl.fitKey = key; }
        }
        auto data = bounce::rescaleLoop(*data1, std::move(fl), std::move(fr), factor, target);
        postAnalysis(slot, rawSerial, data, cachedStretch && detect, switched, a.material, src, conf, true, target, target, true);
    });
}

void EpsilonBounceProcessor::postAnalysis(int slot, int rawSerial, std::shared_ptr<bounce::LoopData> data, bool detect, bool switched, bounce::Material m,
                                          double src, float conf, bool stretched, double matched, double target, bool final)
{
        juce::MessageManager::callAsync([this, slot, rawSerial, data, detect, switched, m, src, conf, stretched, matched, target, final, token = alive]
        {
            if (! token->load()) return;
            auto& sl = slots[(size_t) slot];
            bool current;
            { const juce::ScopedLock lock(loopLock); current = sl.rawSerial == rawSerial && ! sl.rawL.empty(); }
            if (! current) { sl.analysing = false; return; }    // the lane was cleared or reloaded meanwhile
            if (data != nullptr)
            {
                {
                    const juce::ScopedLock lock(loopLock);
                    if (sl.srcFor != rawSerial && src > 0.0) { sl.srcFor = rawSerial; }
                }
                sl.srcBpm.store(src, std::memory_order_relaxed);
                sl.tempoConf.store(conf, std::memory_order_relaxed);
                sl.stretched.store(stretched, std::memory_order_relaxed);
                sl.matchedBpm.store(matched, std::memory_order_relaxed);
            }
            const juce::String lane = slot == melody ? "Melody" : "Drums";
            const juce::String tempoNote = src <= 0.0 ? juce::String()
                : ! final ? lane + ": " + juce::String(src, 1) + " BPM, stretching to " + juce::String(target, 1) + " (pitch kept)..."
                : std::abs(src / juce::jmax(1.0, target) - 1.0) <= 0.002 || target <= 0.0 ? lane + ": " + juce::String(src, 1) + " BPM, already on your tempo"
                : stretched ? lane + ": " + juce::String(src, 1) + " BPM, stretched to " + juce::String(target, 1) + " (pitch kept)"
                : lane + ": " + juce::String(src, 1) + " BPM, its hits re-timed to " + juce::String(target, 1);
            if (detect)
            {
                if (switched)
                    if (auto* p = state.getParameter("material"))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost(p->convertTo0to1((float) m));
                        p->endChangeGesture();
                    }
                sl.lastSliceKey = sliceKey(slot);   // this result already uses the detected mode: no second run
                loadNote = m == bounce::Material::melodic ? "Melody detected: cutting on note and chord changes" : "Drums detected: cutting on hits";
                if (tempoNote.isNotEmpty()) loadNote = tempoNote + " - " + (m == bounce::Material::melodic ? "cut on note changes" : "cut on hits");
                ++noteSerial;
            }
            if (data != nullptr) publish(slot, data);   // nullptr: abandoned for newer settings; the timer starts the next run
            if (data != nullptr && slot == drums)
            {
                const auto info = getPocketInfo(drums);
                loadNote = ! info.valid ? "Not enough hits in \"" + info.name + "\" to read a pocket"
                         : (tempoNote.isNotEmpty() ? tempoNote + " - " : juce::String("Drums in: ")) + juce::String(info.hits) + " hits, " + juce::String(juce::roundToInt(info.swingPercent)) + " % swing"
                           + (leaderOf(melody) == drums ? " - melody locked to them" : juce::String());
                ++noteSerial;
            }
            else if (data != nullptr && ! detect && tempoNote.isNotEmpty() && slot == melody)
            {
                loadNote = tempoNote;
                ++noteSerial;
            }
            if (final) sl.analysing = false;
        });
}

void EpsilonBounceProcessor::publish(int slot, std::shared_ptr<const bounce::LoopData> loop)
{
    auto& s = slots[(size_t) slot];
    // This loop's own pocket: what the other lane locks to when this one leads.
    bounce::LearnResult res;
    if (loop) res = bounce::learnGroove(*loop);
    const bool valid = loop != nullptr && res.hits >= 4;
    for (int i = 0; i < bounce::stepsPerBar; ++i)
    {
        s.pocket[(size_t) i].store(valid ? res.groove.offset[(size_t) i] : 0.0f, std::memory_order_relaxed);
        s.pocket[(size_t) (i + bounce::stepsPerBar)].store(valid ? res.groove.accentDb[(size_t) i] : 0.0f, std::memory_order_relaxed);
    }
    s.pocketValid.store(valid, std::memory_order_relaxed);
    {
        const juce::ScopedLock sl(loopLock);
        s.pocketInfo = LearnInfo { s.name, res.hits, res.stepsHeard, res.swingPercent, valid, res.heard };
        if (s.current) s.retired.push_back(s.current);
        s.current = std::move(loop);
        s.live.store(s.current.get(), std::memory_order_release);
    }
    ++s.serial;
    ++learnSerial;                                   // displays that show a pocket redraw
}

//==============================================================================
// v2.0 Pocket Engine: the follower's lock is made on its own worker whenever what it depends on changes
// (either loop, Depth, Heard timing, Sensitivity), 150 ms after the last change so a turning knob doesn't
// queue up work. A run made out of date is stopped, not finished.
juce::String EpsilonBounceProcessor::pocketKeyFor(int slot) const
{
    const int leader = leaderOf(slot);
    if (leader < 0 || ! warpEngine()) return {};
    juce::String k;
    k << slots[(size_t) slot].serial.load() << "|" << slots[(size_t) leader].serial.load() << "|" << leader
      << "|" << juce::roundToInt(pDepth->load()) << "|" << (pHeard->load() >= 0.5f ? 1 : 0) << "|" << juce::roundToInt(pSensitivity->load());
    return k;
}

void EpsilonBounceProcessor::updatePocket(int slot)
{
    auto& s = slots[(size_t) slot];
    const auto key = pocketKeyFor(slot);
    if (key.isEmpty() || key == s.pocketKey) { s.wantKey.clear(); return; }
    const auto now = juce::Time::getMillisecondCounter();
    if (key != s.wantKey) { s.wantKey = key; s.wantSince = now; if (s.pocketing.load()) s.cancelPocket = true; return; }
    if (now - s.wantSince < 150) return;
    if (s.pocketing.load()) { s.cancelPocket = true; return; }       // starts at the next tick, once that run has stopped
    const int leader = leaderOf(slot);
    std::shared_ptr<const bounce::LoopData> f, l;
    {
        const juce::ScopedLock sl(loopLock);
        f = s.current; l = slots[(size_t) leader].current;
    }
    if (f == nullptr || l == nullptr || f->slices.empty() || l->slices.empty()) return;
    bounce::pocket::Settings st;
    st.depth = juce::jlimit(0.0f, 1.0f, pDepth->load() * 0.01f);
    st.perceived = pHeard->load() >= 0.5f;
    st.sensitivity = juce::jlimit(0.0f, 1.0f, pSensitivity->load() * 0.01f);
    const int fSer = s.serial.load(), lSer = slots[(size_t) leader].serial.load();
    s.pocketKey = key;
    s.wantKey.clear();
    s.cancelPocket = false;
    s.pocketing = true;
    pocketWorker.addJob([this, slot, f, l, st, key, fSer, lSer, leader, token = alive]
    {
        bounce::pocket::Report rep;
        std::shared_ptr<const bounce::LoopData> out = bounce::pocket::lockToPocket(*f, *l, st, &rep, &slots[(size_t) slot].cancelPocket);
        juce::MessageManager::callAsync([this, slot, out, rep, key, fSer, lSer, leader, token]
        {
            if (! token->load()) return;                               // the plugin closed meanwhile
            auto& s2 = slots[(size_t) slot];
            s2.pocketing = false;
            if (key != s2.pocketKey) return;                          // out of date: the timer starts the next one
            if (out == nullptr) { s2.pocketKey.clear(); return; }      // stopped: try again
            {
                const juce::ScopedLock sl(loopLock);
                if (s2.pocketCurrent) s2.retired.push_back(s2.pocketCurrent);
                s2.pocketCurrent = out;
                s2.pocketReport = rep;
            }
            s2.pocketLive.store(out.get(), std::memory_order_release);  // the pointer first: until the serials
            s2.pocketFollow.store(fSer, std::memory_order_relaxed);    // match, the 1.x lock keeps playing
            s2.pocketLead.store(lSer, std::memory_order_relaxed);
            ++s2.pocketSerial;
            const juce::String who = slot == melody ? "Melody" : "Drums", to = leader == drums ? "the drums" : "the melody";
            loadNote = who + " locked to " + to + ": " + juce::String(rep.notes) + (slot == melody ? " notes" : " hits")
                     + (pHeard->load() >= 0.5f ? " by heard timing" : " by onset")
                     + (rep.tripletNotes > 0 ? ", " + juce::String(rep.tripletNotes) + " on triplets" : juce::String())
                     + ", moved " + juce::String(rep.meanMoveMs, 1) + " ms on average";
            if (rep.barOne.shift16 != 0)
                loadNote << " - bar 1 looks " << std::abs(rep.barOne.shift16) << "/16 " << (rep.barOne.shift16 > 0 ? "early" : "late")
                         << ": try Shift " << (rep.barOne.shift16 > 0 ? "+" : "") << rep.barOne.shift16;
            ++noteSerial;
        });
    });
}

EpsilonBounceProcessor::PocketStatus EpsilonBounceProcessor::getPocketStatus(int slot) const
{
    PocketStatus st;
    st.active = pocketActive(slot);
    st.busy = slots[(size_t) slot].pocketing.load() || (! st.active && leaderOf(slot) >= 0 && warpEngine());
    const juce::ScopedLock sl(loopLock);
    st.report = slots[(size_t) slot].pocketReport;
    return st;
}

std::shared_ptr<const bounce::LoopData> EpsilonBounceProcessor::getPlayedLoop(int slot) const
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    const bool p = pocketActive(slot);
    const juce::ScopedLock sl(loopLock);
    return p && slots[(size_t) slot].pocketCurrent ? slots[(size_t) slot].pocketCurrent : slots[(size_t) slot].current;
}

std::shared_ptr<const bounce::LoopData> EpsilonBounceProcessor::getLoop(int slot) const
{
    const juce::ScopedLock sl(loopLock);
    return slots[(size_t) juce::jlimit(0, numSlots - 1, slot)].current;
}

void EpsilonBounceProcessor::timerCallback()
{
    for (int i = 0; i < numSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        {
            const juce::ScopedLock sl(loopLock);
            const auto* busy = s.inUse.load(std::memory_order_acquire);
            s.retired.erase(std::remove_if(s.retired.begin(), s.retired.end(), [busy](const auto& p) { return p.get() != busy; }), s.retired.end());
        }
        updatePocket(i);
        // A loop that arrived before the host said its tempo (sync off, or an old session) takes the
        // project tempo once, the first time it is known. After that it never changes.
        if (s.madeAtBpm.load(std::memory_order_relaxed) <= 0.0 && analysisTempoHint() > 0.0)
        {
            bool have;
            { const juce::ScopedLock sl(loopLock); have = ! s.rawL.empty(); }
            if (have) s.madeAtBpm.store(analysisTempoHint(), std::memory_order_relaxed);
        }
        if (s.restartPending.load() && ! s.analysing.load()) { s.restartPending = false; startAnalysis(i); continue; }
        // Tempo match: a stretched lane follows the project tempo. When the tempo moves (and stays put for
        // half a second, so a tempo automation ramp doesn't queue up work), the lane is stretched again from
        // its original audio. Until then the old one keeps playing, re-triggered on the new grid.
        if (s.stretched.load(std::memory_order_relaxed) || (fitUsesStretch(i) && s.srcBpm.load(std::memory_order_relaxed) > 0.0))
        {
            const double t = matchTarget(), m = s.matchedBpm.load(std::memory_order_relaxed);
            const bool off = t > 0.0 && m > 0.0 && std::abs(t / m - 1.0) > 0.003;
            if (! off) s.rematchSince = 0;
            else if (s.rematchSince == 0) s.rematchSince = juce::Time::getMillisecondCounter();
            else if (juce::Time::getMillisecondCounter() - s.rematchSince > 500 && ! s.analysing.load())
            {
                s.rematchSince = 0;
                startAnalysis(i);
            }
        }
        // Slicing controls changed: re-analyse (debounced by the 10 Hz timer). A run that is already out of
        // date is abandoned instead of finished, so a knob that is still moving never queues up seconds of work.
        if (s.analysing.load() && sliceKey(i) != s.lastSliceKey) s.cancelAnalysis = true;
        if (! s.analysing.load() && sliceKey(i) != s.lastSliceKey)
        {
            bool have;
            { const juce::ScopedLock sl(loopLock); have = ! s.rawL.empty(); }
            if (have) startAnalysis(i);
            else s.lastSliceKey = sliceKey(i);
        }
    }
}

//==============================================================================
juce::String EpsilonBounceProcessor::dragKey(int loops, int slot) const
{
    const auto g = grooveFor(slot);
    juce::String k;
    k << slot << "|" << getPlaySerial(slot) << "|" << (int) pocketActive(slot) << "|" << loops << "|" << (int) g.groove << "|" << g.amount << "|" << g.swing << "|" << g.tighten << "|" << g.humanizeMs
      << "|" << g.accent << "|" << g.feelMs << "|" << g.shiftSteps << "|" << (int) g.hold << "|" << (int) g.softCuts << "|" << gridBpm()
      << "|" << pOutput->load() << "|" << rate << "|" << learnSerial.load() << "|" << (int) link() << "|" << leaderOf(slot) << "|" << (int) leads(slot);
    return k;
}

// Renders the drag-out WAV into memory. Thread-safe: it works on a snapshot of the loop and the settings.
juce::MemoryBlock EpsilonBounceProcessor::renderDragBytes(std::shared_ptr<const bounce::LoopData> loop, const bounce::GrooveSettings& g,
                                                         double bpm, double outRate, float gain, int loops)
{
    juce::MemoryBlock bytes;
    if (! loop || loop->slices.empty()) return bytes;
    bounce::Player p;
    p.prepare(outRate, 512);
    p.update(*loop, g, bpm);
    const double sps = bpm / 60.0 * 4.0 / outRate;
    const int total = (int) std::floor(loops * loop->bars * bounce::stepsPerBar / sps);   // whole loops at the grid speed
    if (total <= 0) return bytes;
    juce::AudioBuffer<float> out(2, total);
    out.clear();
    for (int pos = 0; pos < total; pos += 512)
        p.render(*loop, out.getWritePointer(0) + pos, out.getWritePointer(1) + pos, juce::jmin(512, total - pos), pos * sps, sps, gain);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(bytes, false);
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(outRate).withNumChannels(2).withBitsPerSample(24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer(out, 0, total)) bytes.reset();
    return bytes;
}

// Called when the mouse reaches the Drag out button: by the time the drag starts, the audio is ready.
void EpsilonBounceProcessor::prepareDragOut(int loops, int slot)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    auto& s = slots[(size_t) slot];
    const auto key = dragKey(loops, slot);
    {
        const juce::ScopedLock sl(loopLock);
        if (s.dragCacheKey == key || s.dragPendingKey == key) return;
        s.dragPendingKey = key;
    }
    auto loop = getPlayedLoop(slot);
    const auto g = grooveFor(slot);
    const double bpm = gridBpm(), outRate = rate > 0 ? rate : 44100.0;
    const float gain = juce::Decibels::decibelsToGain(pOutput->load());
    encoder.addJob([this, slot, loop, g, bpm, outRate, gain, loops, key]
    {
        auto bytes = renderDragBytes(loop, g, bpm, outRate, gain, loops);
        auto& sl2 = slots[(size_t) slot];
        const juce::ScopedLock sl(loopLock);
        if (sl2.dragPendingKey == key) { sl2.dragCache = std::move(bytes); sl2.dragCacheKey = key; sl2.dragPendingKey.clear(); }
    });
}

juce::File EpsilonBounceProcessor::renderToFile(int loops, int slot)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    auto& s = slots[(size_t) slot];
    auto loop = getPlayedLoop(slot);
    if (! loop || loop->slices.empty()) return {};
    const auto key = dragKey(loops, slot);
    const auto groove = grooveFor(slot);
    const auto loopName = getLoopName(slot);
    juce::MemoryBlock bytes;
    {
        const juce::ScopedLock sl(loopLock);
        if (s.dragCacheKey == key) bytes = s.dragCache;
    }
    if (bytes.isEmpty())
        bytes = renderDragBytes(loop, groove, gridBpm(), rate > 0 ? rate : 44100.0, juce::Decibels::decibelsToGain(pOutput->load()), loops);
    if (bytes.isEmpty()) return {};
    {
        const juce::ScopedLock sl(loopLock);
        s.dragCache = bytes; s.dragCacheKey = key;
    }

    const double hostTempo = playbackBpm();
    // Named for what was done to it: its groove, the loop it was locked to, or 'original feel' when it leads.
    const int leader = leaderOf(slot);
    const auto grooveName = leads(slot) ? juce::String("original feel")
                          : leader >= 0 ? "locked to " + getLoopName(leader)
                          : juce::String(bounce::grooveName(groove.groove)).toLowerCase();
    // Kept in Music/Epsilon Sounds/Epsilon Bounce/, not temp: FL Studio links dragged-in audio by its
    // path, so a temp file would later go missing from the project. Same audio reuses its file;
    // different audio gets a new name, so a file a project already uses is never overwritten.
    auto dir = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Epsilon Sounds").getChildFile("Epsilon Bounce");
    if (! dir.createDirectory()) return {};
    const auto base = juce::File::createLegalFileName(loopName + " - " + grooveName + " " + juce::String(juce::roundToInt(hostTempo)) + " BPM"
                                                      + (timeFactor() < 1.0 ? " half-time" : timeFactor() > 1.0 ? " double-time" : ""));
    auto file = dir.getChildFile(base + ".wav");
    for (int n = 2; file.existsAsFile(); ++n)
    {
        if (file.getSize() == (juce::int64) bytes.getSize())
        {
            juce::MemoryBlock existing;
            if (file.loadFileAsData(existing) && existing == bytes) return file;
        }
        file = dir.getChildFile(base + " (" + juce::String(n) + ").wav");
    }
    return file.replaceWithData(bytes.getData(), bytes.getSize()) ? file : juce::File();
}

//==============================================================================
void EpsilonBounceProcessor::applyPreset(int index)
{
    if (index < 0 || index >= bounce::presetCount) return;
    const auto& pr = bounce::presets[index];
    auto set = [this](const char* id, float plain)
    {
        if (auto* p = state.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(plain));
            p->endChangeGesture();
        }
    };
    set("groove", (float) pr.groove);
    set("amount", pr.amount);
    set("swing", pr.swing);
    set("tighten", pr.tighten);
    set("humanize", pr.humanizeMs);
    set("accent", pr.accent);
    set("feel", pr.feelMs);
    set("time", (float) pr.time);
    currentPreset = index;
}

void EpsilonBounceProcessor::setLearned(const bounce::GrooveTemplate& t, const LearnInfo& info)
{
    for (int i = 0; i < bounce::stepsPerBar; ++i)
    {
        learnedAtoms[(size_t) i].store(t.offset[(size_t) i], std::memory_order_relaxed);
        learnedAtoms[(size_t) (i + bounce::stepsPerBar)].store(t.accentDb[(size_t) i], std::memory_order_relaxed);
    }
    { const juce::ScopedLock sl(loopLock); learnInfo = info; }
    ++learnSerial;
}

juce::String EpsilonBounceProcessor::learnFromFile(const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader == nullptr) return "Can't read that file - try WAV, AIFF or FLAC.";
    if (reader->sampleRate <= 0 || reader->lengthInSamples <= 0) return "That file is empty.";
    if (reader->lengthInSamples > (juce::int64) (maxLoopSeconds * reader->sampleRate)) return "Reference loops up to 60 seconds, please.";
    const int n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buf((int) juce::jmax(1u, juce::jmin(2u, reader->numChannels)), n);
    reader->read(&buf, 0, n, 0, true, buf.getNumChannels() > 1);
    std::vector<float> l(buf.getReadPointer(0), buf.getReadPointer(0) + n);
    std::vector<float> r(buf.getReadPointer(buf.getNumChannels() - 1), buf.getReadPointer(buf.getNumChannels() - 1) + n);
    const double sr = reader->sampleRate;
    const auto name = file.getFileNameWithoutExtension();
    bounce::AnalysisSettings a;
    a.sensitivity = 0.6f;     // catch ghost notes: they carry most of a groove's feel
    a.barsHint = barsChoices[juce::jlimit(0, 4, juce::roundToInt(pBars->load()))];
    a.tempoHint = analysisTempoHint();   // your drum loop was most likely bounced at the project tempo
    learning = true;
    const double target = matchTarget();
    worker.addJob([this, l = std::move(l), r = std::move(r), sr, a, name, target]() mutable
    {
        // v1.6: read the reference at its own tempo (a pocket is only right on the right grid)
        if (a.barsHint <= 0)
        {
            const auto g = bounce::estimateTempo(l, r, sr, target);
            if (g.bpm > 0.0 && (a.tempoHint <= 0.0 || std::abs(g.bpm / a.tempoHint - 1.0) > 0.02)) a.exactBpm = g.bpm;
        }
        auto ref = bounce::analyse(std::move(l), std::move(r), sr, a);
        const auto res = bounce::learnGroove(*ref);
        juce::MessageManager::callAsync([this, res, name, bpm = ref->bpm, token = alive]
        {
            if (! token->load()) return;
            LearnInfo info { name, res.hits, res.stepsHeard, res.swingPercent, res.hits >= 4, res.heard };
            if (info.valid)
            {
                setLearned(res.groove, info);
                // Lock: the loop's own timing is replaced by the drums' pocket, exactly as the drums play it.
                // Tighten 100 % strips the loop's old feel first; nothing is added on top.
                auto set = [this](const char* id, float plain)
                {
                    if (auto* p = state.getParameter(id))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost(p->convertTo0to1(plain));
                        p->endChangeGesture();
                    }
                };
                set("groove", (float) bounce::Groove::learned);
                set("amount", 100.0f);
                set("tighten", 100.0f);
                set("swing", 0.0f);
                set("humanize", 0.0f);
                set("feel", 0.0f);
                currentPreset = -1;
            }
            else
            {
                const juce::ScopedLock sl(loopLock);
                learnInfo = info;
                ++learnSerial;
            }
            juce::ignoreUnused(bpm);
            learning = false;
        });
    });
    return {};
}

//==============================================================================
void EpsilonBounceProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto tree = state.copyState();
    {
        const auto t = readLearned();
        const auto info = getLearnInfo();
        if (info.valid)
        {
            juce::ValueTree lt("Learned");
            juce::StringArray offs, accs;
            for (int i = 0; i < bounce::stepsPerBar; ++i) { offs.add(juce::String(t.offset[(size_t) i], 5)); accs.add(juce::String(t.accentDb[(size_t) i], 3)); }
            lt.setProperty("offsets", offs.joinIntoString(" "), nullptr);
            lt.setProperty("accents", accs.joinIntoString(" "), nullptr);
            lt.setProperty("name", info.name, nullptr);
            lt.setProperty("hits", info.hits, nullptr);
            lt.setProperty("steps", info.steps, nullptr);
            lt.setProperty("swing", info.swingPercent, nullptr);
            juce::String heard; for (auto h : info.heard) heard << (h ? "1" : "0");
            lt.setProperty("heard", heard, nullptr);
            tree.appendChild(lt, nullptr);
        }
    }
    const juce::ScopedLock sl(loopLock);
    for (int i = 0; i < numSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        if (s.rawL.empty()) continue;
        // the loops travel with the session, FLAC-compressed: encoded once per loop, not on every save
        if (s.flacFor != s.rawSerial) { s.flacCache = encodeFlac(s.rawL, s.rawR, s.rawRate); s.flacFor = s.rawSerial; }
        juce::ValueTree audio(i == melody ? "Loop" : "Drums");
        audio.setProperty("name", s.name, nullptr);
        audio.setProperty("madeAtBpm", s.madeAtBpm.load(std::memory_order_relaxed), nullptr);
        audio.setProperty("srcBpm", s.srcFor == s.rawSerial ? s.srcBpm.load(std::memory_order_relaxed) : 0.0, nullptr);   // v1.6
        audio.setProperty("srcTyped", s.srcOverride, nullptr);
        audio.setProperty("fit", s.fitMode, nullptr);
        audio.setProperty("flac", juce::var(s.flacCache), nullptr);
        tree.appendChild(audio, nullptr);
    }
    // Binary, not XML: the loop is megabytes, and base64-in-XML made every host save slow.
    // Reserve the whole size once: growing a multi-megabyte block step by step cost up to ~20 ms per save.
    size_t expected = 64 * 1024;
    for (const auto& s : slots) expected += s.flacCache.getSize();
    dest.ensureSize(expected, false);
    juce::MemoryOutputStream out(dest, false);
    out.write(stateMagic, 4);
    tree.writeToStream(out);
}

void EpsilonBounceProcessor::setStateInformation(const void* data, int size)
{
    if (data == nullptr || size <= 0) return;
    juce::ValueTree tree;
    if (size > 4 && std::memcmp(data, stateMagic, 4) == 0)
        tree = juce::ValueTree::readFromData(static_cast<const char*>(data) + 4, (size_t) size - 4);
    else if (auto xml = getXmlFromBinary(data, size))   // sessions saved by 1.3 and earlier
        tree = juce::ValueTree::fromXml(*xml);
    if (! tree.isValid() || ! tree.hasType(state.state.getType())) return;
    juce::ValueTree audios[numSlots] { tree.getChildWithName("Loop"), tree.getChildWithName("Drums") };
    for (auto& a : audios) if (a.isValid()) tree.removeChild(a, nullptr);
    auto lt = tree.getChildWithName("Learned");
    if (lt.isValid())
    {
        tree.removeChild(lt, nullptr);
        juce::StringArray offs, accs;
        offs.addTokens(lt.getProperty("offsets").toString(), " ", {});
        accs.addTokens(lt.getProperty("accents").toString(), " ", {});
        if (offs.size() == bounce::stepsPerBar && accs.size() == bounce::stepsPerBar)
        {
            bounce::GrooveTemplate t;
            for (int i = 0; i < bounce::stepsPerBar; ++i)
            {
                t.offset[(size_t) i] = juce::jlimit(-0.5f, 0.5f, offs[i].getFloatValue());
                t.accentDb[(size_t) i] = juce::jlimit(-6.0f, 6.0f, accs[i].getFloatValue());
            }
            LearnInfo li { lt.getProperty("name").toString(), (int) lt.getProperty("hits"), (int) lt.getProperty("steps"), (float) lt.getProperty("swing"), true, {} };
            const auto heard = lt.getProperty("heard").toString();
            for (int i = 0; i < bounce::stepsPerBar && i < heard.length(); ++i) li.heard[(size_t) i] = heard[i] == '1';
            setLearned(t, li);
        }
    }
    state.replaceState(tree);
    for (int i = 0; i < numSlots; ++i)
    {
        const auto& audio = audios[i];
        if (! audio.isValid()) continue;
        if (auto* mb = audio.getProperty("flac").getBinaryData())
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatReader> r(flac.createReaderFor(new juce::MemoryInputStream(*mb, true), true));
            if (r != nullptr && r->lengthInSamples > 0)
            {
                const int n = (int) r->lengthInSamples;
                juce::AudioBuffer<float> buf(2, n);
                r->read(&buf, 0, n, 0, true, true);
                // 1.5.0 and older sessions don't know the made-at tempo: 0 = take the project tempo once it is known.
                const double madeAt = audio.hasProperty("madeAtBpm") ? (double) audio.getProperty("madeAtBpm") : 0.0;
                // 1.6: the tempo read when it was loaded (so it never flips), a typed tempo, and the fit mode.
                // Older sessions: read again on load.
                setRawAudio(i, { buf.getReadPointer(0), buf.getReadPointer(0) + n }, { buf.getReadPointer(1), buf.getReadPointer(1) + n },
                            r->sampleRate, audio.getProperty("name").toString(), madeAt,
                            (double) audio.getProperty("srcBpm", 0.0), (double) audio.getProperty("srcTyped", 0.0), (int) audio.getProperty("fit", 0));
                const juce::ScopedLock sl(loopLock);
                slots[(size_t) i].flacCache = *mb;              // the session already holds it encoded
                slots[(size_t) i].flacFor = slots[(size_t) i].rawSerial;
            }
        }
    }
}

juce::AudioProcessorEditor* EpsilonBounceProcessor::createEditor() { return new EpsilonBounceEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EpsilonBounceProcessor(); }
