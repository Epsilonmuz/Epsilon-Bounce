#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
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
    // v1.4: melody + drums slots
    const auto level = [](float v) { return v <= -59.9f ? juce::String("off") : (v > 0.05f ? "+" : "") + juce::String(v, 1) + " dB"; };
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { "link", 4 }, "Pocket", StringArray { "DRUMS LEAD", "MELODY LEADS", "GROOVE" }, 0));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "melLevel", 4 }, "Melody Level", NormalisableRange<float>(-60.0f, 6.0f, 0.1f, 2.5f), 0.0f, attr(level)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "drumLevel", 4 }, "Drums Level", NormalisableRange<float>(-60.0f, 6.0f, 0.1f, 2.5f), 0.0f, attr(level)));
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
    for (auto& a : learnedAtoms) a.store(0.0f);
    for (auto& sl : slots) for (auto& a : sl.pocket) a.store(0.0f);
    startTimerHz(10);
}

EpsilonBounceProcessor::~EpsilonBounceProcessor()
{
    stopTimer();
    worker.removeAllJobs(true, 4000);
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
    if (slot == drums) return juce::String(pAlign->load() >= 0.5f ? 1 : 0);      // drums are always cut on their hits
    return juce::String(juce::roundToInt(pSensitivity->load())) + "|" + juce::String(juce::roundToInt(pBars->load())) + "|"
         + juce::String((int) material()) + "|" + juce::String(juce::roundToInt(pChop->load())) + "|" + juce::String(pAlign->load() >= 0.5f ? 1 : 0);
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

// What a slot plays with. One loop loaded, or Pocket = GROOVE: the chosen groove, as always.
// Both loaded and one leads: the leader plays with its own timing untouched, the other is placed
// with the leader's learned pocket (Amount, Tighten, Accent still say how hard it is pulled in).
bounce::GrooveSettings EpsilonBounceProcessor::grooveFor(int slot) const
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
    if (leader >= 0)
    {
        g.groove = bounce::Groove::learned;
        g.learned = readPocket(leader);
    }
    return g;
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
    bool any = false;
    for (int i = 0; i < numSlots; ++i)
    {
        auto& sl = slots[(size_t) i];
        const auto* l = sl.live.load(std::memory_order_acquire);
        sl.inUse.store(l, std::memory_order_release);
        if (sl.live.load(std::memory_order_acquire) != l) { sl.inUse.store(nullptr, std::memory_order_release); l = nullptr; }
        if (l != nullptr && l->slices.empty()) l = nullptr;
        loops[i] = l;
        any |= l != nullptr;
    }
    const auto release = [this] { for (auto& sl : slots) sl.inUse.store(nullptr, std::memory_order_release); };
    if (! any || frames == 0) { for (auto& sl : slots) sl.playingSlice.store(-1); release(); return; }
    const auto* first = loops[melody] != nullptr ? loops[melody] : loops[drums];

    // Transport
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
        const double loopSteps = first->bars * bounce::stepsPerBar * (loops[melody] && loops[drums] ? (double) loops[drums]->bars : 1.0);
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
        const auto g = grooveFor(i);
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
    setRawAudio(slot, std::move(l), std::move(r), reader->sampleRate, file.getFileNameWithoutExtension());
    return {};
}

void EpsilonBounceProcessor::setRawAudio(int slot, std::vector<float> l, std::vector<float> r, double sr, const juce::String& name)
{
    {
        const juce::ScopedLock sl(loopLock);
        auto& s = slots[(size_t) slot];
        s.rawL = std::move(l); s.rawR = std::move(r); s.rawRate = sr; s.name = name;
    }
    startAnalysis(slot);
}

void EpsilonBounceProcessor::clearSlot(int slot)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    {
        const juce::ScopedLock sl(loopLock);
        auto& s = slots[(size_t) slot];
        s.rawL.clear(); s.rawR.clear(); s.name.clear();
    }
    publish(slot, nullptr);
}

void EpsilonBounceProcessor::startAnalysis(int slot)
{
    auto& s = slots[(size_t) slot];
    std::vector<float> l, r; double sr;
    {
        const juce::ScopedLock sl(loopLock);
        if (s.rawL.empty()) return;
        l = s.rawL; r = s.rawR; sr = s.rawRate;
    }
    bounce::AnalysisSettings a;
    if (slot == drums)
    {
        a.sensitivity = 0.6f;       // catch ghost notes: they carry most of a groove's feel
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
    s.lastSliceKey = sliceKey(slot);
    s.analysing = true;
    worker.addJob([this, slot, l = std::move(l), r = std::move(r), sr, a]() mutable
    {
        auto data = bounce::analyse(std::move(l), std::move(r), sr, a);
        juce::MessageManager::callAsync([this, slot, data]
        {
            bool still;
            { const juce::ScopedLock sl(loopLock); still = ! slots[(size_t) slot].rawL.empty(); }
            if (still) publish(slot, data);          // (cleared while analysing: drop the result)
            slots[(size_t) slot].analysing = false;
        });
    });
}

void EpsilonBounceProcessor::publish(int slot, std::shared_ptr<const bounce::LoopData> loop)
{
    auto& s = slots[(size_t) slot];
    // This loop's own pocket: what the other slot locks to when this one leads.
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
        // Slicing controls changed: re-analyse (debounced by the 10 Hz timer)
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
juce::File EpsilonBounceProcessor::renderToFile(int loops, int slot)
{
    slot = juce::jlimit(0, numSlots - 1, slot);
    auto loop = getLoop(slot);
    if (! loop || loop->slices.empty()) return {};
    const auto groove = grooveFor(slot);
    const auto loopName = getLoopName(slot);
    const double bpm = gridBpm();
    const double hostTempo = playbackBpm();
    const double outRate = rate > 0 ? rate : 44100.0;
    bounce::Player p;
    p.prepare(outRate, 512);
    p.update(*loop, groove, bpm);
    const double sps = bpm / 60.0 * 4.0 / outRate;
    const int total = (int) std::floor(loops * loop->bars * bounce::stepsPerBar / sps);   // whole loops at the grid speed
    juce::AudioBuffer<float> out(2, total);
    out.clear();
    const float gain = juce::Decibels::decibelsToGain(pOutput->load());
    for (int pos = 0; pos < total; pos += 512)
        p.render(*loop, out.getWritePointer(0) + pos, out.getWritePointer(1) + pos, juce::jmin(512, total - pos), pos * sps, sps, gain);

    // Named for what was done to it: its groove, or the loop it was locked to, or 'original feel' when it leads.
    const int leader = leaderOf(slot);
    const auto grooveName = leads(slot) ? juce::String("original feel")
                          : leader >= 0 ? "locked to " + getLoopName(leader)
                          : juce::String(bounce::grooveName(groove.groove)).toLowerCase();
    // Kept in Music/Epsilon Sounds/Epsilon Bounce/, not temp: FL Studio links dragged-in audio by its
    // path, so a temp file would later go missing from the project. Same audio reuses its file;
    // different audio gets a new name, so a file a project already uses is never overwritten.
    juce::MemoryBlock bytes;
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(bytes, false);
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(outRate)
                                                      .withNumChannels(2).withBitsPerSample(24));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer(out, 0, total)) return {};
    }
    auto dir = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Epsilon Sounds").getChildFile("Epsilon Bounce");
    if (! dir.createDirectory()) return {};
    const auto base = juce::File::createLegalFileName(loopName + " - " + grooveName + " " + juce::String(juce::roundToInt(hostTempo)) + " BPM"
                                                      + (timeFactor() < 1.0 ? " half-time" : timeFactor() > 1.0 ? " double-time" : ""));
    auto file = dir.getChildFile(base + ".wav");
    for (int n = 2; file.existsAsFile(); ++n)
    {
        juce::MemoryBlock existing;
        if (file.getSize() == (juce::int64) bytes.getSize() && file.loadFileAsData(existing) && existing == bytes) return file;
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
    learning = true;
    worker.addJob([this, l = std::move(l), r = std::move(r), sr, a, name]() mutable
    {
        auto ref = bounce::analyse(std::move(l), std::move(r), sr, a);
        const auto res = bounce::learnGroove(*ref);
        juce::MessageManager::callAsync([this, res, name, bpm = ref->bpm]
        {
            LearnInfo info { name, res.hits, res.stepsHeard, res.swingPercent, res.hits >= 4, res.heard };
            if (info.valid)
            {
                setLearned(res.groove, info);
                if (auto* p = state.getParameter("groove"))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->convertTo0to1((float) bounce::Groove::learned));
                    p->endChangeGesture();
                }
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
        const auto& s = slots[(size_t) i];
        if (s.rawL.empty()) continue;
        // the loops travel with the session, FLAC-compressed
        juce::MemoryBlock flacData;
        {
            auto* mos = new juce::MemoryOutputStream(flacData, false);
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatWriter> w(flac.createWriterFor(mos, s.rawRate, 2, 24, {}, 0));
            if (w != nullptr)
            {
                const float* chans[] { s.rawL.data(), s.rawR.data() };
                w->writeFromFloatArrays(chans, 2, (int) s.rawL.size());
            }
            else delete mos;
        }
        juce::ValueTree audio(i == melody ? "Loop" : "Drums");
        audio.setProperty("name", s.name, nullptr);
        audio.setProperty("flac", juce::var(flacData), nullptr);
        tree.appendChild(audio, nullptr);
    }
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, dest);
}

void EpsilonBounceProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    if (! tree.isValid()) return;
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
                setRawAudio(i, { buf.getReadPointer(0), buf.getReadPointer(0) + n }, { buf.getReadPointer(1), buf.getReadPointer(1) + n },
                            r->sampleRate, audio.getProperty("name").toString());
            }
        }
    }
}

juce::AudioProcessorEditor* EpsilonBounceProcessor::createEditor() { return new EpsilonBounceEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EpsilonBounceProcessor(); }
