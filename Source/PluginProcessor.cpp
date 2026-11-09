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
    for (auto& a : learnedAtoms) a.store(0.0f);
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
    player.prepare(rate, samplesPerBlock);
    lastLoop = nullptr; lastBpm = -1.0; expectedStep = -1e9; internalStep = 0.0;
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
bounce::GrooveSettings EpsilonBounceProcessor::currentGroove() const { return readGroove(); }

double EpsilonBounceProcessor::playbackBpm() const
{
    const auto h = hostBpm.load();
    return (pSync->load() >= 0.5f && h > 0.0) ? h : (double) pTempo->load();
}

static bool sameSettings(const bounce::GrooveSettings& a, const bounce::GrooveSettings& b)
{
    return a.groove == b.groove && a.amount == b.amount && a.swing == b.swing && a.tighten == b.tighten
        && a.humanizeMs == b.humanizeMs && a.accent == b.accent && a.feelMs == b.feelMs && a.learned == b.learned;
}

void EpsilonBounceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int frames = buffer.getNumSamples();
    buffer.clear();
    midi.clear();

    const auto* loop = live.load(std::memory_order_acquire);
    inUse.store(loop, std::memory_order_release);
    if (live.load(std::memory_order_acquire) != loop) { inUse.store(nullptr); return; }   // swapped mid-check: skip a block
    if (loop == nullptr || loop->slices.empty() || frames == 0) { playingSlice.store(-1); return; }

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
        const double loopSteps = loop->bars * bounce::stepsPerBar;
        if (internalStep > loopSteps * 1024.0) internalStep = std::fmod(internalStep, loopSteps);
    }
    else if (! preview.load(std::memory_order_relaxed))
        internalStep = 0.0;

    if (! playing)
    {
        if (expectedStep > -1e8) player.reset();
        expectedStep = -1e9;
        playingSlice.store(-1);
        inUse.store(nullptr, std::memory_order_release);
        return;
    }

    const auto g = readGroove();
    if (loop != lastLoop || std::abs(bpm - lastBpm) > 1e-6 || ! sameSettings(g, lastSettings))
    {
        if (loop != lastLoop) player.reset();
        player.update(*loop, g, bpm);
        lastLoop = loop; lastBpm = bpm; lastSettings = g;
    }
    if (std::abs(stepStart - expectedStep) > 0.5) player.reset();    // jumped (host loop / locate)
    expectedStep = stepStart + frames * stepsPerSample;

    const float gain = juce::Decibels::decibelsToGain(pOutput->load(std::memory_order_relaxed));
    player.render(*loop, buffer.getWritePointer(0), buffer.getWritePointer(1), frames, stepStart, stepsPerSample, gain);
    playingSlice.store(player.playingSlice(), std::memory_order_relaxed);
    inUse.store(nullptr, std::memory_order_release);

    const auto elapsed = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
    const float load = (float) (elapsed / (frames / rate) * 100.0);
    cpu.store(cpu.load(std::memory_order_relaxed) * 0.95f + load * 0.05f, std::memory_order_relaxed);
}

//==============================================================================
juce::String EpsilonBounceProcessor::loadFile(const juce::File& file)
{
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
    setRawAudio(std::move(l), std::move(r), reader->sampleRate, file.getFileNameWithoutExtension());
    return {};
}

void EpsilonBounceProcessor::setRawAudio(std::vector<float> l, std::vector<float> r, double sr, const juce::String& name)
{
    {
        const juce::ScopedLock sl(loopLock);
        rawL = std::move(l); rawR = std::move(r); rawRate = sr; loopName = name;
    }
    startAnalysis();
}

void EpsilonBounceProcessor::startAnalysis()
{
    std::vector<float> l, r; double sr;
    {
        const juce::ScopedLock sl(loopLock);
        if (rawL.empty()) return;
        l = rawL; r = rawR; sr = rawRate;
    }
    bounce::AnalysisSettings a;
    a.sensitivity = pSensitivity->load() * 0.01f;
    a.barsHint = barsChoices[juce::jlimit(0, 4, juce::roundToInt(pBars->load()))];
    lastSensitivity = pSensitivity->load(); lastBars = juce::roundToInt(pBars->load());
    analysing = true;
    worker.addJob([this, l = std::move(l), r = std::move(r), sr, a]() mutable
    {
        auto data = bounce::analyse(std::move(l), std::move(r), sr, a);
        juce::MessageManager::callAsync([this, data] { publish(data); analysing = false; });
    });
}

void EpsilonBounceProcessor::publish(std::shared_ptr<const bounce::LoopData> loop)
{
    const juce::ScopedLock sl(loopLock);
    if (current) retired.push_back(current);
    current = std::move(loop);
    live.store(current.get(), std::memory_order_release);
    ++loopSerial;
}

std::shared_ptr<const bounce::LoopData> EpsilonBounceProcessor::getLoop() const
{
    const juce::ScopedLock sl(loopLock);
    return current;
}

void EpsilonBounceProcessor::timerCallback()
{
    {
        const juce::ScopedLock sl(loopLock);
        const auto* busy = inUse.load(std::memory_order_acquire);
        retired.erase(std::remove_if(retired.begin(), retired.end(), [busy](const auto& p) { return p.get() != busy; }), retired.end());
    }
    // Slicing controls changed: re-analyse (debounced by the 10 Hz timer)
    if (! analysing.load() && (std::abs(pSensitivity->load() - lastSensitivity) > 0.5f || juce::roundToInt(pBars->load()) != lastBars))
    {
        bool have;
        { const juce::ScopedLock sl(loopLock); have = ! rawL.empty(); }
        if (have) startAnalysis();
        else { lastSensitivity = pSensitivity->load(); lastBars = juce::roundToInt(pBars->load()); }
    }
}

//==============================================================================
juce::File EpsilonBounceProcessor::renderToFile(int loops)
{
    auto loop = getLoop();
    if (! loop || loop->slices.empty()) return {};
    const double bpm = gridBpm();
    const double hostTempo = playbackBpm();
    const double outRate = rate > 0 ? rate : 44100.0;
    bounce::Player p;
    p.prepare(outRate, 512);
    p.update(*loop, readGroove(), bpm);
    const double sps = bpm / 60.0 * 4.0 / outRate;
    const int total = (int) std::floor(loops * loop->bars * bounce::stepsPerBar / sps);   // whole loops at the grid speed
    juce::AudioBuffer<float> out(2, total);
    out.clear();
    const float gain = juce::Decibels::decibelsToGain(pOutput->load());
    for (int pos = 0; pos < total; pos += 512)
        p.render(*loop, out.getWritePointer(0) + pos, out.getWritePointer(1) + pos, juce::jmin(512, total - pos), pos * sps, sps, gain);

    const auto grooveName = juce::String(bounce::grooveName(readGroove().groove)).toLowerCase();
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Epsilon Bounce");
    dir.createDirectory();
    auto file = dir.getChildFile(juce::File::createLegalFileName(loopName + " - " + grooveName + " " + juce::String(juce::roundToInt(hostTempo)) + " BPM"
                                                                   + (timeFactor() < 1.0 ? " half-time" : timeFactor() > 1.0 ? " double-time" : "") + ".wav"));
    file.deleteFile();
    juce::WavAudioFormat wav;
    if (auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream()))
        if (auto writer = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(stream.get(), outRate, 2, 24, {}, 0)))
        {
            stream.release();
            writer->writeFromAudioSampleBuffer(out, 0, total);
            return file;
        }
    return {};
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
            LearnInfo info { name, res.hits, res.stepsHeard, res.swingPercent, res.hits >= 4 };
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
            tree.appendChild(lt, nullptr);
        }
    }
    const juce::ScopedLock sl(loopLock);
    if (! rawL.empty())
    {
        // the loop travels with the session, FLAC-compressed
        juce::MemoryBlock flacData;
        {
            auto* mos = new juce::MemoryOutputStream(flacData, false);
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatWriter> w(flac.createWriterFor(mos, rawRate, 2, 24, {}, 0));
            if (w != nullptr)
            {
                const float* chans[] { rawL.data(), rawR.data() };
                w->writeFromFloatArrays(chans, 2, (int) rawL.size());
            }
            else delete mos;
        }
        juce::ValueTree audio("Loop");
        audio.setProperty("name", loopName, nullptr);
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
    auto audio = tree.getChildWithName("Loop");
    if (audio.isValid()) tree.removeChild(audio, nullptr);
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
            setLearned(t, { lt.getProperty("name").toString(), (int) lt.getProperty("hits"), (int) lt.getProperty("steps"), (float) lt.getProperty("swing"), true });
        }
    }
    state.replaceState(tree);
    if (! audio.isValid()) return;
    if (auto* mb = audio.getProperty("flac").getBinaryData())
    {
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> r(flac.createReaderFor(new juce::MemoryInputStream(*mb, true), true));
        if (r != nullptr && r->lengthInSamples > 0)
        {
            const int n = (int) r->lengthInSamples;
            juce::AudioBuffer<float> buf(2, n);
            r->read(&buf, 0, n, 0, true, true);
            setRawAudio({ buf.getReadPointer(0), buf.getReadPointer(0) + n }, { buf.getReadPointer(1), buf.getReadPointer(1) + n },
                        r->sampleRate, audio.getProperty("name").toString());
        }
    }
}

juce::AudioProcessorEditor* EpsilonBounceProcessor::createEditor() { return new EpsilonBounceEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EpsilonBounceProcessor(); }
