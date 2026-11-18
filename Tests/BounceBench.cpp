// Epsilon Bounce host bench: drives the real processor like a DAW would.
//  - loads a synthetic 2-bar dembow loop from a WAV file, waits for analysis
//  - plays it with a 140 BPM host playhead at 44.1 and 48 kHz, checks hits land on the grid
//  - every groove at full settings: finite, bounded
//  - state round-trip carries the loop inside the session
//  - drag-out render writes a WAV
//  - editor snapshot to docs/epsilon-bounce-editor.png
//   BounceBench [snapshot-dir]
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <ctime>

namespace
{
int fails = 0;
void check(bool ok, const juce::String& what) { std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); if (! ok) ++fails; }

struct Head : juce::AudioPlayHead
{
    double bpm = 140.0, ppq = 0.0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm(bpm); p.setPpqPosition(ppq); p.setIsPlaying(playing); p.setTimeSignature(TimeSignature { 4, 4 });
        return p;
    }
};

juce::File writeLoop(double sr)
{
    const std::vector<int> steps { 0, 3, 4, 6, 8, 11, 12, 14, 16, 19, 20, 22, 24, 27, 28, 30 };
    const double bpm = 100.0;
    const int len = (int) std::lround(8 * 60.0 / bpm * sr);
    juce::AudioBuffer<float> b(2, len); b.clear();
    juce::Random rng(3);
    const double sps = len / 32.0;
    for (auto st : steps)
    {
        const int at = (int) std::lround(st * sps);
        for (int i = 0; i < (int) (0.06 * sr) && at + i < len; ++i)
        {
            const float env = std::exp(-(float) i / (float) (0.012 * sr)) * 0.7f;
            const float v = env * (rng.nextFloat() * 2.0f - 1.0f);
            b.setSample(0, at + i, v); b.setSample(1, at + i, v);
        }
    }
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bounce-bench-loop.wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(f.createOutputStream().release(), sr, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer(b, 0, len);
    return f;
}

// A loop bounced from the project at `bpm`: hits exactly on the given 16ths, then `tailBars` of
// ringing tail (a reverb / delay tail or a little silence), as FL Studio exports it.
juce::File writeGridLoop(double sr, double bpm, int bars, double tailBars, const std::vector<int>& steps, const char* name)
{
    const double sps = 60.0 / bpm / 4.0 * sr;
    const int len = (int) std::lround((bars + tailBars) * 16.0 * sps);
    juce::AudioBuffer<float> b(2, len); b.clear();
    juce::Random rng(7);
    for (auto st : steps)
    {
        const int at = (int) std::lround(st * sps);
        for (int i = 0; i < (int) (0.05 * sr) && at + i < len; ++i)
        {
            const float v = std::exp(-(float) i / (float) (0.010 * sr)) * 0.7f * (rng.nextFloat() * 2.0f - 1.0f);
            b.setSample(0, at + i, v); b.setSample(1, at + i, v);
        }
    }
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(f.createOutputStream().release(), sr, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer(b, 0, len);
    return f;
}

bool waitForLoop(EpsilonBounceProcessor& p, int serialBefore)
{
    for (int i = 0; i < 400; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        if (p.getLoopSerial() != serialBefore && ! p.isAnalysing()) return true;
    }
    return false;
}

// A long, dense melodic loop: chords changing every 8th with plucked attacks, 8 bars at 90 BPM (~21 s),
// or 60 s for the worst case. The kind of sample that used to make Bounce lag.
// A tonal loop of short plucks (silence between them), cycling through a phrase: a melody at `bpm`.
juce::File writePluckLoop(double sr, double bpm, int bars, int everySteps, const char* name)
{
    const double sps = 60.0 / bpm / 4.0 * sr;
    const int len = (int) std::lround(bars * 16 * sps);
    juce::AudioBuffer<float> b(2, len); b.clear();
    const int notes[] { 57, 60, 64, 62, 60, 55, 57, 64 };
    int k = 0;
    for (int st = 0; st < bars * 16; st += everySteps, ++k)
    {
        const int at = (int) std::lround(st * sps);
        const double hz = 440.0 * std::pow(2.0, (notes[k % 8] - 69) / 12.0);
        for (int i = 0; i < (int) (0.09 * sr) && at + i < len; ++i)
        {
            const double t = i / sr;
            const float v = (float) (0.5 * std::exp(-t * 45.0) * (std::sin(2.0 * juce::MathConstants<double>::pi * hz * t) + 0.3 * std::sin(4.0 * juce::MathConstants<double>::pi * hz * t)));
            b.setSample(0, at + i, v); b.setSample(1, at + i, v);
        }
    }
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(f.createOutputStream().release(), sr, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer(b, 0, len);
    return f;
}

juce::File writeLongMelody(double sr, double seconds, const char* name)
{
    const int len = (int) (seconds * sr);
    juce::AudioBuffer<float> b(2, len); b.clear();
    const double eighth = 60.0 / 90.0 / 2.0 * sr;
    const int roots[] { 57, 53, 48, 55, 52, 50 };
    for (int k = 0; k * eighth < len; ++k)
    {
        const int a = (int) (k * eighth), e = juce::jmin(len, (int) ((k + 1) * eighth));
        const int root = roots[(k / 2) % 6] + ((k & 1) ? 12 : 0);
        for (int i = a; i < e; ++i)
        {
            const double t = (i - a) / sr;
            const float env = (float) std::exp(-t * 6.0) * 0.25f;
            float v = 0.0f;
            for (int m : { root, root + 4, root + 7 }) v += (float) std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * std::pow(2.0, (m - 69) / 12.0) * t);
            b.setSample(0, i, env * v); b.setSample(1, i, env * v * 0.9f);
        }
    }
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(f.createOutputStream().release(), sr, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer(b, 0, len);
    return f;
}

double msSince(double t0) { return juce::Time::getMillisecondCounterHiRes() - t0; }

// CPU time of this thread: what the audio callback really costs, without the OS preempting the bench
// (wall-clock spikes on a shared CI machine are the scheduler, not Bounce).
double threadCpuMs()
{
#if JUCE_MAC || JUCE_LINUX
    timespec ts {};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
#else
    return juce::Time::getMillisecondCounterHiRes();
#endif
}

// ---- Lag profile: everything that can make Bounce (or the DAW around it) stutter, timed.
void lagProfile()
{
    std::printf("\n== lag profile (48 kHz, 128-sample blocks = 2.67 ms budget)\n");
    const double sr = 48000.0;
    const int block = 128;
    EpsilonBounceProcessor p;
    Head head;
    p.setPlayHead(&head);
    p.setPlayConfigDetails(0, 2, sr, block);
    p.prepareToPlay(sr, block);
    auto setChoice = [&p](const char* id, float v) { auto* q = p.state.getParameter(id); q->setValueNotifyingHost(q->convertTo0to1(v)); };

    // 1. load + analyse a 60 s melodic sample
    setChoice("material", 1.0f);
    auto big = writeLongMelody(sr, 59.0, "bounce-bench-long-melody.wav");
    int ser = p.getLoopSerial();
    double t0 = juce::Time::getMillisecondCounterHiRes();
    p.loadFile(big);
    const double loadCall = msSince(t0);
    // v1.6: the loop is playable as soon as it is cut (first publish); the tempo match (a 90 BPM sample in a
    // 140 project: stretched, pitch kept) follows in the background and replaces it when it's ready.
    double analyse = -1.0;
    for (int i = 0; i < 2000; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        if (analyse < 0.0 && p.getLoopSerial() != ser) analyse = msSince(t0);
        if (analyse >= 0.0 && ! p.isAnalysing()) break;
    }
    const double matched = msSince(t0);
    std::printf("   load call (message thread): %.1f ms, 59 s melodic playable after %.0f ms, tempo-matched after %.0f ms (%s), %d slices\n", loadCall, analyse, matched,
                p.isStretched(EpsilonBounceProcessor::melody) ? "stretched" : "not stretched", p.getLoop() ? (int) p.getLoop()->slices.size() : 0);
    // 250 ms: GitHub's shared macOS runners read and decode the 30 MB test file about 4x slower than a
    // real machine (167 ms there, 37 ms on the Linux bench). The load path itself is unchanged since 1.4.
    check(loadCall < 250.0, "lag: loading a 59 s file blocks the UI for " + juce::String(loadCall, 1) + " ms (< 250)");
    check(analyse >= 0.0 && analyse < 4000.0, "lag: 59 s melodic sample playable in " + juce::String(analyse, 0) + " ms (< 4 s, off the UI thread; was 3.5 s here)");
    check(matched < 9000.0, "lag: ...and tempo-matched to the project in " + juce::String(matched, 0) + " ms (< 9 s, in the background)");

    // 2. the host asking for the session (FL Studio does this a lot: undo history, autosave)
    juce::MemoryBlock st;
    juce::MessageManager::getInstance()->runDispatchLoopUntil(400);   // a DAW's next save is never instant
    double worstState = 0.0;
    for (int i = 0; i < 6; ++i)
    {
        t0 = juce::Time::getMillisecondCounterHiRes();
        p.getStateInformation(st);
        worstState = juce::jmax(worstState, msSince(t0));
    }
    std::printf("   getStateInformation with a 59 s loop: worst %.1f ms (%d KB)\n", worstState, (int) (st.getSize() / 1024));
    check(worstState < 20.0, "lag: the host saving the session takes " + juce::String(worstState, 1) + " ms (< 20, was 265 ms: a full FLAC encode every time)");
    {
        EpsilonBounceProcessor q;
        Head qh;                                   // the host tells the reopened plugin the same tempo
        q.setPlayHead(&qh);
        q.setPlayConfigDetails(0, 2, sr, block);
        q.prepareToPlay(sr, block);
        { juce::AudioBuffer<float> wb(2, block); juce::MidiBuffer wm; for (int i = 0; i < 2; ++i) { wb.clear(); q.processBlock(wb, wm); } }
        const int qs = q.getLoopSerial();
        t0 = juce::Time::getMillisecondCounterHiRes();
        q.setStateInformation(st.getData(), (int) st.getSize());
        const double restore = msSince(t0);
        bool back = false;
        for (int i = 0; i < 2000 && ! back; ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil(5); back = q.getLoopSerial() != qs && ! q.isAnalysing(); }
        check(back && q.getLoop() && q.getLoop()->slices.size() == p.getLoop()->slices.size(),
              "lag: the session reopens with the 59 s loop (" + juce::String(restore, 0) + " ms on the host's thread)");
        juce::MemoryBlock again;
        t0 = juce::Time::getMillisecondCounterHiRes();
        q.getStateInformation(again);
        check(msSince(t0) < 20.0, "lag: a reopened session saves instantly too (" + juce::String(msSince(t0), 1) + " ms)");
    }

    // 3. the audio thread while a knob is being turned (automation every block)
    double worstBlock = 0.0, total = 0.0; int blocks = 0;
    std::vector<double> blockMs; blockMs.reserve(12000);
    juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
    auto* amt = p.state.getParameter("amount");
    for (int material = 0; material < 3; ++material)
    {
        setChoice("material", (float) material);
        setChoice("chop", 0.0f);
        ser = p.getLoopSerial();
        for (int i = 0; i < 600 && (p.getLoopSerial() == ser || p.isAnalysing()); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        head.playing = true;
        for (int b = 0; b < 4000; ++b)
        {
            head.ppq = b * block / sr * head.bpm / 60.0;
            amt->setValueNotifyingHost((float) ((b % 200) / 200.0));
            const double t = threadCpuMs();
            p.processBlock(buf, midi);
            const double ms = threadCpuMs() - t;
            if (b > 10) { worstBlock = juce::jmax(worstBlock, ms); total += ms; ++blocks; blockMs.push_back(ms); }
        }
    }
    // A single spike on a shared CI Mac (the OS paging, another job) swung the raw worst block from 0.08 to
    // 1.7 ms on identical code, so the gate is the 99.9th percentile; and no block may ever miss its deadline.
    std::sort(blockMs.begin(), blockMs.end());
    const double p999 = blockMs.empty() ? 0.0 : blockMs[(size_t) std::min<double>((double) blockMs.size() - 1, std::floor(blockMs.size() * 0.999))];
    std::printf("   audio thread, knob automated every block, all 3 cut modes: mean %.4f ms, 99.9%% %.3f ms, worst %.3f ms of 2.67\n", total / blocks, p999, worstBlock);
    check(p999 < 0.6, "lag: 99.9 % of audio blocks take under " + juce::String(p999, 3) + " ms with a knob moving (< 0.6 ms of 2.67)");
    check(worstBlock < 2.67, "lag: no audio block misses its deadline (worst " + juce::String(worstBlock, 3) + " ms of 2.67)");

    // 4. drag-out of a long loop (rendered when the drag starts, on the UI thread)
    {
        t0 = juce::Time::getMillisecondCounterHiRes();
        auto f = p.renderToFile(2);
        const double cold = msSince(t0);
        f.deleteFile();
        p.state.getParameter("swing")->setValueNotifyingHost(0.3f);   // new settings, then the mouse reaches the button
        p.prepareDragOut(2);
        for (int i = 0; i < 1000 && ! p.isDragOutReady(2); ++i) juce::Thread::sleep(10);   // the mouse rests on the button
        check(p.isDragOutReady(2), "lag: the drag-out is ready in the background");
        t0 = juce::Time::getMillisecondCounterHiRes();
        f = p.renderToFile(2);
        const double warm = msSince(t0);
        std::printf("   drag-out of the 59 s loop: %.0f ms cold, %.0f ms once the mouse is over the button\n", cold, warm);
        check(f.existsAsFile() && warm < cold * 0.6, "lag: drag-out pre-rendered on hover starts in " + juce::String(warm, 0) + " ms (cold " + juce::String(cold, 0) + " ms)");
        f.deleteFile();
        p.state.getParameter("swing")->setValueNotifyingHost(0.0f);
    }

    // 5. the UI: one playing-frame repaint of the grid, and the whole editor
    setChoice("material", 1.0f);
    ser = p.getLoopSerial();
    for (int i = 0; i < 600 && (p.getLoopSerial() == ser || p.isAnalysing()); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    ed->setSize(1160, 840);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    juce::Component* grid = nullptr;
    for (auto* c : ed->getChildren()) if (grid == nullptr && dynamic_cast<GrooveGrid*>(c) != nullptr) grid = c;   // the melody lane
    double gridMs = 0.0, edMs = 0.0;
    for (int i = 0; i < 10; ++i)
    {
        juce::Image img(juce::Image::ARGB, grid->getWidth(), grid->getHeight(), true);
        juce::Graphics g(img);
        t0 = juce::Time::getMillisecondCounterHiRes();
        grid->paintEntireComponent(g, false);
        gridMs = i == 0 ? 0.0 : juce::jmax(gridMs, msSince(t0));
    }
    for (int i = 0; i < 3; ++i)
    {
        t0 = juce::Time::getMillisecondCounterHiRes();
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        edMs = juce::jmax(edMs, msSince(t0));
    }
    std::printf("   UI: grid frame %.2f ms (opaque: %s), whole editor %.1f ms\n", gridMs, grid->isOpaque() ? "yes" : "no - the panel behind it repaints too", edMs);
    check(grid->isOpaque(), "lag: the grid is opaque, so playback frames don't repaint the panel behind it");
    check(gridMs < 4.0, "lag: a playback frame of the grid draws in " + juce::String(gridMs, 2) + " ms (< 4)");
    ed.reset();
    big.deleteFile();
}

std::vector<int> onsets(const juce::AudioBuffer<float>& b)
{
    std::vector<int> out; int silent = 100000;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        if (std::abs(b.getSample(0, i)) > 0.05f) { if (silent > 800) out.push_back(i); silent = 0; }
        else ++silent;
    }
    return out;
}

juce::AudioBuffer<float> play(EpsilonBounceProcessor& p, Head& head, double sr, double quarters, int block = 256)
{
    const int total = (int) std::floor(quarters * 60.0 / head.bpm * sr) - 1;
    juce::AudioBuffer<float> out(2, total), buf(2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min(block, total - pos);
        head.ppq = pos / sr * head.bpm / 60.0;
        juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), 2, n);
        p.processBlock(view, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, view, c, 0, n);
    }
    return out;
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File snapDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]) : juce::File();
    lagProfile();

    for (double sr : { 44100.0, 48000.0 })
    {
        const juce::String tag = sr == 44100.0 ? "[44.1k] " : "[48k] ";
        EpsilonBounceProcessor p;
        Head head;
        p.setPlayHead(&head);
        p.setPlayConfigDetails(0, 2, sr, 256);
        p.prepareToPlay(sr, 256);
        const auto serial = p.getLoopSerial();
        check(p.loadFile(writeLoop(sr)).isEmpty(), tag + "loads a WAV loop");
        check(waitForLoop(p, serial), tag + "analysis finishes");
        auto loop = p.getLoop();
        check(loop && loop->slices.size() == 16 && loop->bars == 2, tag + "16 hits, 2 bars (" + juce::String(loop ? (int) loop->slices.size() : 0) + ")");

        // straight + tighten 100 at 140 BPM: hits on the 140 BPM 16th grid
        p.state.getParameter("groove")->setValueNotifyingHost(0.0f);
        p.state.getParameter("tighten")->setValueNotifyingHost(1.0f);
        p.state.getParameter("accent")->setValueNotifyingHost(0.0f);
        auto out = play(p, head, sr, 16.0);
        auto on = onsets(out);
        const double sps = 60.0 / 140.0 / 4.0 * sr;
        const int steps[] { 0, 3, 4, 6, 8, 11, 12, 14, 16, 19, 20, 22, 24, 27, 28, 30 };
        double worst = 0.0;
        for (size_t i = 0; i < on.size(); ++i) worst = std::max(worst, std::abs(on[i] - (steps[i % 16] + 32.0 * (double) (i / 16)) * sps) / sr * 1000.0);
        check(on.size() == 32, tag + "host 140 BPM: 32 hits over 2 loops (" + juce::String((int) on.size()) + ")");
        check(worst < 1.0, tag + "hits on the grid, worst " + juce::String(worst, 3) + " ms");

        // dembow pushes step 3
        p.state.getParameter("groove")->setValueNotifyingHost(p.state.getParameter("groove")->convertTo0to1(1.0f));
        p.prepareToPlay(sr, 256);
        out = play(p, head, sr, 8.0);
        on = onsets(out);
        const double late = on.size() > 1 ? (on[1] - 3 * sps) / sps : -1.0;
        check(std::abs(late - 0.20) < 0.03, tag + "dembow pushes the a-of-1 by " + juce::String(late, 3) + " of a 16th");

        // all grooves at max
        bool finite = true; float peak = 0.0f;
        for (int g = 0; g < 4; ++g)
        {
            p.state.getParameter("groove")->setValueNotifyingHost(p.state.getParameter("groove")->convertTo0to1((float) g));
            for (auto id : { "amount", "swing", "humanize", "accent", "feel" }) p.state.getParameter(id)->setValueNotifyingHost(1.0f);
            out = play(p, head, sr, 8.0, 97);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < out.getNumSamples(); ++i) { const auto v = out.getSample(c, i); finite &= std::isfinite(v); peak = std::max(peak, std::abs(v)); }
        }
        check(finite && peak < 1.6f, tag + "every groove at max: finite, peak " + juce::String(juce::Decibels::gainToDecibels(peak), 1) + " dBFS");

        // stopped host + preview off = silence; preview on = sound
        head.playing = false;
        out = play(p, head, sr, 4.0);
        check(out.getMagnitude(0, out.getNumSamples()) == 0.0f, tag + "host stopped: silent");
        p.setPreview(true);
        out = play(p, head, sr, 4.0);
        check(out.getMagnitude(0, out.getNumSamples()) > 0.1f, tag + "preview plays with the host stopped");
        p.setPreview(false); head.playing = true;

        // state round-trip with the loop inside
        juce::MemoryBlock st;
        p.getStateInformation(st);
        EpsilonBounceProcessor q;
        q.setPlayConfigDetails(0, 2, sr, 256);
        q.prepareToPlay(sr, 256);
        const auto qs = q.getLoopSerial();
        q.setStateInformation(st.getData(), (int) st.getSize());
        check(waitForLoop(q, qs) && q.getLoop() && q.getLoop()->slices.size() == 16, tag + "session reload brings the loop back (" + juce::String((int) (st.getSize() / 1024)) + " KB state)");
        check(q.state.getParameter("groove")->getValue() == p.state.getParameter("groove")->getValue(), tag + "session reload keeps the settings");

        // drag-out render
        const auto f = p.renderToFile(2);
        check(f.existsAsFile() && f.getSize() > 100000, tag + "drag-out renders " + f.getFileName());
        check(! f.isAChildOf(juce::File::getSpecialLocation(juce::File::tempDirectory)), tag + "drag-out file is kept out of temp (FL links by path)");
        check(p.renderToFile(2) == f, tag + "the same drag-out reuses its file");
        {
            auto* amt = p.state.getParameter("amount");
            const auto before = amt->getValue();
            amt->setValueNotifyingHost(before > 0.5f ? 0.2f : 0.9f);
            const auto g = p.renderToFile(2);
            check(g.existsAsFile() && g != f && f.existsAsFile(), tag + "a changed drag-out gets a new file; the old one stays");
            g.deleteFile();
            amt->setValueNotifyingHost(before);
        }
        f.deleteFile();


        // ---- v1.1: presets, time modality, Groove Learn
        {
            check(bounce::presetCount >= 39, tag + juce::String(bounce::presetCount) + " factory presets");
            bool allOk = true;
            for (int i = 0; i < bounce::presetCount; ++i)
            {
                p.applyPreset(i);
                const auto& pr = bounce::presets[i];
                allOk &= juce::roundToInt(p.state.getParameter("groove")->convertFrom0to1(p.state.getParameter("groove")->getValue())) == (int) pr.groove;
                allOk &= std::abs(p.state.getParameter("swing")->convertFrom0to1(p.state.getParameter("swing")->getValue()) - pr.swing) < 0.2f;
            }
            check(allOk, tag + "every preset sets its groove and swing");

            // half-time: over 8 quarter notes at 140 BPM a 2-bar loop plays half of itself
            p.applyPreset(0);                      // Quantize Clean: straight, tighten 100
            p.prepareToPlay(sr, 256);
            auto normal = onsets(play(p, head, sr, 8.0));
            p.state.getParameter("time")->setValueNotifyingHost(p.state.getParameter("time")->convertTo0to1(1.0f));
            p.prepareToPlay(sr, 256);
            auto half = onsets(play(p, head, sr, 8.0));
            p.state.getParameter("time")->setValueNotifyingHost(p.state.getParameter("time")->convertTo0to1(2.0f));
            p.prepareToPlay(sr, 256);
            auto dbl = onsets(play(p, head, sr, 8.0));
            check(normal.size() == 16 && half.size() == 8 && dbl.size() == 32,
                  tag + "time modality: normal / half / double = " + juce::String((int) normal.size()) + " / " + juce::String((int) half.size()) + " / " + juce::String((int) dbl.size()) + " hits");
            const double sps = 60.0 / 140.0 / 4.0 * sr;
            check(half.size() > 1 && std::abs((half[1] - half[0]) - 3 * 2 * sps) < sr * 0.001, tag + "half-time: 16th 3 lands on host 16th 6");
            p.state.getParameter("time")->setValueNotifyingHost(0.0f);

            // learn: a swung reference file
            juce::AudioBuffer<float> ref(2, (int) std::lround(8 * 60.0 / 100.0 * sr)); ref.clear();
            juce::Random rng(9);
            const double rsps = ref.getNumSamples() / 32.0;
            for (int b = 0; b < 32; ++b)
            {
                const double pos = b + ((b & 1) ? 0.26 : 0.0);
                const int at = (int) std::lround(pos * rsps);
                for (int i = 0; i < (int) (0.04 * sr) && at + i < ref.getNumSamples(); ++i)
                {
                    const float v = std::exp(-(float) i / (float) (0.01 * sr)) * ((b & 1) ? 0.4f : 0.7f) * (rng.nextFloat() * 2.0f - 1.0f);
                    ref.setSample(0, at + i, v); ref.setSample(1, at + i, v);
                }
            }
            auto rf = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bounce-bench-reference.wav");
            rf.deleteFile();
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(rf.createOutputStream().release(), sr, 2, 24, {}, 0));
                w->writeFromAudioSampleBuffer(ref, 0, ref.getNumSamples());
            }
            const int ls = p.getLearnSerial();
            check(p.learnFromFile(rf).isEmpty(), tag + "learn: accepts a reference WAV");
            for (int i = 0; i < 400 && (p.getLearnSerial() == ls || p.isLearning()); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            const auto info = p.getLearnInfo();
            check(info.valid && std::abs(info.swingPercent - 63.0f) < 2.5f, tag + "learn: reads " + juce::String(info.swingPercent, 1) + " % swing from the reference (63 % in)");
            check(juce::roundToInt(p.state.getParameter("groove")->convertFrom0to1(p.state.getParameter("groove")->getValue())) == (int) bounce::Groove::learned,
                  tag + "learn: switches the groove to LEARNED");
            auto plain = [&p](const char* id) { auto* q = p.state.getParameter(id); return q->convertFrom0to1(q->getValue()); };
            check(plain("tighten") > 99.0f && std::abs(plain("amount") - 100.0f) < 0.5f && plain("swing") < 0.5f && std::abs(plain("feel")) < 0.05f,
                  tag + "match drums locks: tighten 100 %, amount 100 %, no extra swing / feel");
            p.prepareToPlay(sr, 256);
            auto lo = onsets(play(p, head, sr, 8.0));
            const double off = lo.size() > 1 ? (lo[1] - 3 * sps) / sps : -1.0;   // the loop's 2nd hit is 16th 3 (an off-16th)
            check(std::abs(off - 0.26) < 0.04, tag + "learn: off-16ths of your loop now land " + juce::String(off, 3) + " late (0.26 in the reference)");

            juce::MemoryBlock st2;
            p.getStateInformation(st2);
            EpsilonBounceProcessor q2;
            q2.setStateInformation(st2.getData(), (int) st2.getSize());
            const auto i2 = q2.getLearnInfo();
            check(i2.valid && i2.name == info.name && std::abs(q2.currentGroove().learned.offset[1] - p.currentGroove().learned.offset[1]) < 1e-3f,
                  tag + "learn: the learned feel is saved in the session");
            for (int i = 0; i < 200 && q2.isAnalysing(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        }


        // ---- v1.2: melodies, grid chops, hold, shift, align
        {
            const double bpm = 100.0;
            const int len = (int) std::lround(8 * 60.0 / bpm * sr);
            const double lsps = len / 32.0;
            auto hz = [](double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); };
            const int chg[] { 0, 6, 8, 14, 16, 22, 24, 30 };
            const int ch[8][3] { {57,60,64}, {53,57,60}, {48,52,55}, {55,59,62}, {57,60,64}, {53,57,60}, {48,52,55}, {55,59,62} };
            juce::AudioBuffer<float> mb(2, len); mb.clear();
            const int xfl = (int) (0.012 * sr);
            for (int c = 0; c < 8; ++c)
            {
                const int a = (int) std::lround(chg[c] * lsps), b = c < 7 ? (int) std::lround(chg[c + 1] * lsps) : len;
                for (int i = juce::jmax(0, a - xfl); i < juce::jmin(len, b + xfl); ++i)
                {
                    float w = 1.0f;
                    if (i < a + xfl && c > 0) w = juce::jlimit(0.0f, 1.0f, (float) (i - (a - xfl)) / (2.0f * xfl));
                    if (i > b - xfl && c < 7) w *= juce::jlimit(0.0f, 1.0f, (float) ((b + xfl) - i) / (2.0f * xfl));
                    float v = 0.0f; for (int m : ch[c]) v += (float) std::sin(2.0 * juce::MathConstants<double>::pi * hz(m) * i / sr);
                    for (int k = 0; k < 2; ++k) mb.addSample(k, i, 0.18f * w * v);
                }
            }
            auto mf = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bounce-bench-chords.wav");
            mf.deleteFile();
            { juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(mf.createOutputStream().release(), sr, 2, 24, {}, 0)); w->writeFromAudioSampleBuffer(mb, 0, len); }

            auto setChoice = [&p](const char* id, float v) { auto* q = p.state.getParameter(id); q->setValueNotifyingHost(q->convertTo0to1(v)); };
            setChoice("material", 1.0f);                       // MELODIC
            int ser = p.getLoopSerial();
            check(p.loadFile(mf).isEmpty() && waitForLoop(p, ser), tag + "melodic: loads a chord loop");
            check(p.getLoop() && p.getLoop()->slices.size() == 8, tag + "melodic: 8 chord changes -> 8 slices (" + juce::String(p.getLoop() ? (int) p.getLoop()->slices.size() : 0) + ")");
            check(p.holdActive(), tag + "hold AUTO is on for melodic material");
            setChoice("material", 2.0f);                       // GRID
            ser = p.getLoopSerial();
            for (int i = 0; i < 400 && (p.getLoopSerial() == ser || p.isAnalysing()); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            check(p.getLoop() && p.getLoop()->slices.size() == 32, tag + "grid: re-cuts into 32 16th chops when switched (" + juce::String(p.getLoop() ? (int) p.getLoop()->slices.size() : 0) + ")");
            setChoice("chop", 1.0f);                           // 8ths
            ser = p.getLoopSerial();
            for (int i = 0; i < 400 && (p.getLoopSerial() == ser || p.isAnalysing()); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            check(p.getLoop() && p.getLoop()->slices.size() == 16, tag + "grid: 8th chops -> 16 slices");
            setChoice("hold", 2.0f);
            check(! p.holdActive(), tag + "hold OFF overrides AUTO");
            setChoice("hold", 0.0f);
            setChoice("material", 0.0f);
            check(! p.holdActive(), tag + "hold AUTO is off for drums");

            // melodic + hold through the real plugin, half-time: no gaps, no clicks
            setChoice("material", 1.0f);
            ser = p.getLoopSerial();
            for (int i = 0; i < 400 && (p.getLoopSerial() == ser || p.isAnalysing()); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            p.applyPreset(0);
            setChoice("time", 1.0f);
            p.prepareToPlay(sr, 256);
            auto held = play(p, head, sr, 16.0);
            float minRms = 1.0f, jump = 0.0f;
            const int win = (int) (0.02 * sr);
            for (int i = win; i + win < held.getNumSamples(); i += win) minRms = juce::jmin(minRms, held.getRMSLevel(0, i, win));
            for (int i = 1; i < held.getNumSamples(); ++i) jump = juce::jmax(jump, std::abs(held.getSample(0, i) - held.getSample(0, i - 1)));
            check(minRms > 0.05f, tag + "half-time chords with Hold: no gaps (quietest 20 ms RMS " + juce::String(minRms, 3) + ")");
            check(jump < 0.08f, tag + "half-time chords with Hold: no clicks (largest jump " + juce::String(jump, 3) + ")");
            setChoice("time", 0.0f);
            setChoice("material", 0.0f);
            ser = p.getLoopSerial();
            for (int i = 0; i < 400 && (p.getLoopSerial() == ser || p.isAnalysing()); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        }

        // ---- v1.5: two lanes - melody + drums, each draggable in and out, locked into one pocket
        {
            using P = EpsilonBounceProcessor;
            auto setChoice = [&p](const char* id, float v) { auto* q = p.state.getParameter(id); q->setValueNotifyingHost(q->convertTo0to1(v)); };
            auto waitSlot = [&p](int slot, int before)
            {
                for (int i = 0; i < 400 && (p.getLoopSerial(slot) == before || p.isAnalysing(slot)); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
                return p.getLoopSerial(slot) != before;
            };
            const double sps = 60.0 / 140.0 / 4.0 * sr;
            setChoice("material", 0.0f);
            int ms = p.getLoopSerial(P::melody);
            check(p.loadFile(writeLoop(sr), P::melody).isEmpty() && waitSlot(P::melody, ms), tag + "slots: melody lane loads");
            for (int i = 0; i < 30; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            p.applyPreset(0);                                  // straight, tighten 100
            check(! p.haveBoth() && p.leaderOf(P::melody) < 0, tag + "slots: one loop alone rides the chosen groove");

            const auto rf = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bounce-bench-reference.wav");   // 63 % swing drums
            const int ds = p.getLoopSerial(P::drums);
            check(p.loadFile(rf, P::drums).isEmpty() && waitSlot(P::drums, ds), tag + "slots: drums lane loads");
            const auto dInfo = p.getPocketInfo(P::drums);
            check(p.getLoop(P::drums) && p.getLoop(P::drums)->slices.size() == 32, tag + "slots: drums cut on their 32 hits (" + juce::String(p.getLoop(P::drums) ? (int) p.getLoop(P::drums)->slices.size() : 0) + ")");
            check(dInfo.valid && std::abs(dInfo.swingPercent - 63.0f) < 2.5f, tag + "slots: drums' pocket read as " + juce::String(dInfo.swingPercent, 1) + " % swing");
            check(p.haveBoth() && p.link() == P::Link::drumsLead && p.leads(P::drums) && p.leaderOf(P::melody) == P::drums, tag + "slots: DRUMS LEAD by default, melody follows");

            // melody alone in the output: its off-16th now lands where the drums' off-16ths land
            setChoice("drumLevel", -60.0f);
            p.prepareToPlay(sr, 256);
            auto on = onsets(play(p, head, sr, 8.0));
            double off = on.size() > 1 ? (on[1] - 3 * sps) / sps : -9.0;
            check(std::abs(off - 0.26) < 0.04, tag + "DRUMS LEAD: melody's off-16th lands " + juce::String(off, 3) + " late (drums: 0.26)");
            // drums alone: untouched feel
            setChoice("drumLevel", 0.0f); setChoice("melLevel", -60.0f);
            p.prepareToPlay(sr, 256);
            on = onsets(play(p, head, sr, 8.0));
            off = on.size() > 1 ? (on[1] - 1 * sps) / sps : -9.0;
            check(on.size() == 32 && std::abs(off - 0.26) < 0.04, tag + "DRUMS LEAD: drums keep their own feel (" + juce::String((int) on.size()) + " hits, off-16th " + juce::String(off, 3) + ")");
            // melody leads: the drums are pulled onto the (straight) melody's pocket
            setChoice("link", 1.0f);
            check(p.leads(P::melody) && p.leaderOf(P::drums) == P::melody, tag + "MELODY LEADS: drums follow");
            p.prepareToPlay(sr, 256);
            on = onsets(play(p, head, sr, 8.0));
            off = on.size() > 1 ? (on[1] - 1 * sps) / sps : -9.0;
            check(std::abs(off) < 0.04, tag + "MELODY LEADS: drums' off-16th pulled to the melody's straight pocket (" + juce::String(off, 3) + ")");
            // groove: both ride the chosen groove (dembow pushes the a-of-1 by 0.20)
            setChoice("link", 2.0f); setChoice("groove", 1.0f); setChoice("melLevel", 0.0f); setChoice("drumLevel", -60.0f);
            check(p.leaderOf(P::melody) < 0 && ! p.leads(P::drums), tag + "GROOVE: nobody leads");
            p.prepareToPlay(sr, 256);
            on = onsets(play(p, head, sr, 8.0));
            off = on.size() > 1 ? (on[1] - 3 * sps) / sps : -9.0;
            check(std::abs(off - 0.20) < 0.03, tag + "GROOVE: melody back on dembow (" + juce::String(off, 3) + ")");
            setChoice("drumLevel", 0.0f); setChoice("link", 0.0f);

            // both lanes together: finite, sane level
            p.prepareToPlay(sr, 256);
            auto both = play(p, head, sr, 8.0, 97);
            bool fin = true; for (int i = 0; i < both.getNumSamples(); ++i) fin &= std::isfinite(both.getSample(0, i));
            check(fin && both.getMagnitude(0, both.getNumSamples()) < 1.6f && both.getMagnitude(0, both.getNumSamples()) > 0.3f, tag + "slots: both lanes play together");

            // drag out, one file per lane
            const auto fm = p.renderToFile(2, P::melody), fd = p.renderToFile(2, P::drums);
            check(fm.existsAsFile() && fd.existsAsFile() && fm != fd, tag + "drag-out per lane: \"" + fm.getFileName() + "\" and \"" + fd.getFileName() + "\"");
            check(fm.getFileName().containsIgnoreCase("locked to") && fd.getFileName().containsIgnoreCase("original feel"), tag + "drag-out files say what was done");
            fm.deleteFile(); fd.deleteFile();

            // session carries both loops and the link
            juce::MemoryBlock st3;
            p.getStateInformation(st3);
            EpsilonBounceProcessor q3;
            q3.setPlayConfigDetails(0, 2, sr, 256);
            q3.prepareToPlay(sr, 256);
            q3.setStateInformation(st3.getData(), (int) st3.getSize());
            for (int i = 0; i < 600 && (! q3.getLoop(P::melody) || ! q3.getLoop(P::drums) || q3.isAnalysing(P::melody) || q3.isAnalysing(P::drums)); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            check(q3.haveBoth() && q3.getLoopName(P::drums) == p.getLoopName(P::drums) && q3.leaderOf(P::melody) == P::drums,
                  tag + "session reload brings both lanes back (" + juce::String((int) (st3.getSize() / 1024)) + " KB)");

            // clearing the drums: the melody is on its own groove again
            p.clearSlot(P::drums);
            for (int i = 0; i < 30; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            check(! p.haveBoth() && p.getLoop(P::drums) == nullptr && p.leaderOf(P::melody) < 0, tag + "slots: Clear empties a lane");
            p.prepareToPlay(sr, 256);
            auto after = play(p, head, sr, 4.0);
            check(after.getMagnitude(0, after.getNumSamples()) > 0.1f, tag + "slots: melody still plays after the drums are cleared");
            setChoice("groove", 0.0f);
        }

        // ---- v1.5.1: changing the project tempo keeps melody and drums lined up.
        // Both loops were bounced at 140 with tails of different lengths. Before the fix, moving the
        // project to 150 re-read them on the NEW tempo: the drums (4.15 bars at 140 = 4.45 bars at 150)
        // were mistaken for a 4-bar loop at 150 with a long tail, the melody fell back to its raw length,
        // and the two lanes landed on different grids.
        {
            using P = EpsilonBounceProcessor;
            EpsilonBounceProcessor t;
            Head th; th.bpm = 140.0;
            t.setPlayHead(&th);
            t.setPlayConfigDetails(0, 2, sr, 256);
            t.prepareToPlay(sr, 256);
            auto setT = [&t](const char* id, float v) { auto* q = t.state.getParameter(id); q->setValueNotifyingHost(q->convertTo0to1(v)); };
            auto settle = [&t]
            {
                for (int i = 0; i < 600; ++i)
                {
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
                    if (i > 30 && ! t.isAnalysing(P::melody) && ! t.isAnalysing(P::drums)) break;
                }
            };
            juce::AudioBuffer<float> warm(2, 256);
            juce::MidiBuffer noMidi;
            auto runBlocks = [&] (int n) { for (int i = 0; i < n; ++i) { warm.clear(); t.processBlock(warm, noMidi); } };

            runBlocks(4);                          // the host has told Bounce its tempo before the loops arrive
            setT("material", 0.0f);
            setT("link", 2.0f);                    // GROOVE: both lanes ride the same straight grid
            t.applyPreset(0);
            setT("accent", 0.0f);

            const std::vector<int> drumHits { 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60 };
            std::vector<int> melHits; for (int s = 0; s < 128; s += 8) melHits.push_back(s + 2);
            const int ms = t.getLoopSerial(P::melody), ds = t.getLoopSerial(P::drums);
            check(t.loadFile(writeGridLoop(sr, 140.0, 8, 0.30, melHits, "bounce-bench-mel140.wav"), P::melody).isEmpty(), tag + "tempo: 8-bar melody made at 140 loads");
            check(t.loadFile(writeGridLoop(sr, 140.0, 4, 0.15, drumHits, "bounce-bench-drm140.wav"), P::drums).isEmpty(), tag + "tempo: 4-bar drums made at 140 load");
            settle();
            check(t.getLoopSerial(P::melody) != ms && t.getLoopSerial(P::drums) != ds, tag + "tempo: both lanes analysed");

            auto gridError = [&] (int keepSlot, double bpm, const std::vector<int>& hits, int loopSteps)
            {
                setT("melLevel", keepSlot == P::melody ? 0.0f : -60.0f);
                setT("drumLevel", keepSlot == P::drums ? 0.0f : -60.0f);
                th.bpm = bpm;
                t.prepareToPlay(sr, 256);
                const auto on = onsets(play(t, th, sr, loopSteps / 4.0));
                const double sps = 60.0 / bpm / 4.0 * sr;
                double worst = on.size() == hits.size() ? 0.0 : 1e9;
                for (size_t i = 0; i < on.size() && i < hits.size(); ++i) worst = std::max(worst, std::abs(on[i] - hits[i] * sps) / sr * 1000.0);
                return worst;
            };

            const double m140 = gridError(P::melody, 140.0, melHits, 128), d140 = gridError(P::drums, 140.0, drumHits, 64);
            check(m140 < 1.0 && d140 < 1.0, tag + "tempo: at 140 both lanes sit on the grid (melody " + juce::String(m140, 2) + " ms, drums " + juce::String(d140, 2) + " ms)");

            th.bpm = 150.0;                       // the producer speeds the project up
            runBlocks(4);
            settle();
            const auto mel = t.getLoop(P::melody), drm = t.getLoop(P::drums);
            check(mel && drm && mel->bars == 8 && drm->bars == 4 && std::abs(mel->bpm - 140.0) < 0.01 && std::abs(drm->bpm - 140.0) < 0.01,
                  tag + "tempo: after the project goes to 150, both loops are still read as made at 140 ("
                      + juce::String(mel ? mel->bars : 0) + " bars at " + juce::String(mel ? mel->bpm : 0.0, 1) + ", "
                      + juce::String(drm ? drm->bars : 0) + " bars at " + juce::String(drm ? drm->bpm : 0.0, 1) + ")");
            const double m150 = gridError(P::melody, 150.0, melHits, 128), d150 = gridError(P::drums, 150.0, drumHits, 64);
            check(m150 < 1.0 && d150 < 1.0, tag + "tempo: at 150 both lanes land on the 150 grid together (melody " + juce::String(m150, 2) + " ms, drums " + juce::String(d150, 2) + " ms)");
            const double m120 = gridError(P::melody, 120.0, melHits, 128), d120 = gridError(P::drums, 120.0, drumHits, 64);
            check(m120 < 1.0 && d120 < 1.0, tag + "tempo: slowed to 120 they still line up (melody " + juce::String(m120, 2) + " ms, drums " + juce::String(d120, 2) + " ms)");

            // The tempo each loop was made at travels with the session: reopening at 150 doesn't re-read them.
            juce::MemoryBlock st4;
            t.getStateInformation(st4);
            EpsilonBounceProcessor u;
            Head uh; uh.bpm = 150.0;
            u.setPlayHead(&uh);
            u.setPlayConfigDetails(0, 2, sr, 256);
            u.prepareToPlay(sr, 256);
            u.setStateInformation(st4.getData(), (int) st4.getSize());
            for (int i = 0; i < 4; ++i) { warm.clear(); u.processBlock(warm, noMidi); }
            for (int i = 0; i < 600 && (! u.getLoop(P::melody) || ! u.getLoop(P::drums) || u.isAnalysing(P::melody) || u.isAnalysing(P::drums) || i < 40); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            const auto um = u.getLoop(P::melody), ud = u.getLoop(P::drums);
            check(um && ud && std::abs(um->bpm - 140.0) < 0.01 && std::abs(ud->bpm - 140.0) < 0.01 && um->bars == 8 && ud->bars == 4,
                  tag + "tempo: a session reopened at 150 still knows both loops were made at 140");
        }


        // ---- v1.6: tempo match first. A 92 BPM melody and 140 BPM drums in a 140 project: the melody's own
        // tempo is found, it is stretched to 140 (pitch kept) and its notes land on the 140 grid; the drums,
        // already at 140, are left alone. Then the project moves to 150 and the melody follows.
        {
            using P = EpsilonBounceProcessor;
            EpsilonBounceProcessor t;
            Head th; th.bpm = 140.0;
            t.setPlayHead(&th);
            t.setPlayConfigDetails(0, 2, sr, 256);
            t.prepareToPlay(sr, 256);
            auto setT = [&t](const char* id, float v) { auto* q = t.state.getParameter(id); q->setValueNotifyingHost(q->convertTo0to1(v)); };
            juce::AudioBuffer<float> warm(2, 256);
            juce::MidiBuffer noMidi;
            auto runBlocks = [&] (int n) { for (int i = 0; i < n; ++i) { warm.clear(); t.processBlock(warm, noMidi); } };
            auto settle = [&] (int minMs)
            {
                for (int i = 0; i < 1500; ++i)
                {
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
                    if (i * 10 > minMs && ! t.isAnalysing(P::melody) && ! t.isAnalysing(P::drums)) break;
                }
            };
            runBlocks(4);
            setT("material", 1.0f);                // MELODIC
            setT("link", 2.0f);                    // GROOVE, straight, no accents: placement is only the tempo match
            t.applyPreset(0);
            setT("accent", 0.0f);
            setT("hold", 2.0f);                    // off: the plucks keep their silence between them

            const std::vector<int> drumHits { 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60 };
            check(t.loadFile(writePluckLoop(sr, 92.0, 4, 2, "bounce-bench-pluck92.wav"), P::melody).isEmpty(), tag + "match: a 92 BPM melody loads into a 140 project");
            check(t.loadFile(writeGridLoop(sr, 140.0, 4, 0.0, drumHits, "bounce-bench-drm140b.wav"), P::drums).isEmpty(), tag + "match: 140 BPM drums load");
            settle(600);
            const double ms = t.getSourceBpm(P::melody), ds = t.getSourceBpm(P::drums);
            bool family = false; for (double m : { 0.5, 1.0, 2.0 }) family |= std::abs(ms / (92.0 * m) - 1.0) < 0.006;
            check(family, tag + "match: the melody's own tempo is found (" + juce::String(ms, 2) + " BPM)");
            check(std::abs(ds - 140.0) < 0.01 && ! t.isStretched(P::drums), tag + "match: the drums read 140 and are left alone (" + juce::String(ds, 2) + ")");
            const auto mel = t.getLoop(P::melody);
            check(t.isStretched(P::melody) && std::abs(t.getMatchedBpm(P::melody) - 140.0) < 0.01 && mel && std::abs(mel->bpm - 140.0) < 0.01,
                  tag + "match: the melody is stretched to 140 (its grid: " + juce::String(mel ? mel->bpm : 0.0, 2) + " BPM, " + juce::String(mel ? mel->bars : 0) + " bars)");

            // its notes land on the 140 grid: every pluck within 3 ms of a 16th, all 32 of them
            auto melodyOnGrid = [&] (double bpm, int& count)
            {
                setT("melLevel", 0.0f); setT("drumLevel", -60.0f);
                th.bpm = bpm;
                t.prepareToPlay(sr, 256);
                const auto loop = t.getLoop(P::melody);
                const auto on = onsets(play(t, th, sr, loop ? loop->bars * 4.0 : 16.0));
                const double sps = 60.0 / bpm / 4.0 * sr;
                double worst = 0.0;
                for (int o : on) worst = std::max(worst, std::abs(o - std::round(o / sps) * sps) / sr * 1000.0);
                count = (int) on.size();
                return worst;
            };
            int n140 = 0;
            const double w140 = melodyOnGrid(140.0, n140);
            // the yardstick: the same melody made at 140 (no stretch). Melodic cuts sit a few ms before a note
            // on its quietest point, so neither is at 0 ms; the stretched one must be as tight as the real one.
            double control = 0.0;
            {
                EpsilonBounceProcessor c;
                Head ch; ch.bpm = 140.0;
                c.setPlayHead(&ch); c.setPlayConfigDetails(0, 2, sr, 256); c.prepareToPlay(sr, 256);
                auto setC = [&c](const char* id, float v) { auto* q = c.state.getParameter(id); q->setValueNotifyingHost(q->convertTo0to1(v)); };
                for (int i = 0; i < 4; ++i) { warm.clear(); c.processBlock(warm, noMidi); }
                setC("material", 1.0f); setC("link", 2.0f); c.applyPreset(0); setC("accent", 0.0f); setC("hold", 2.0f);
                c.loadFile(writePluckLoop(sr, 140.0, 8, 4, "bounce-bench-pluck140.wav"), P::melody);
                for (int i = 0; i < 1500 && (c.getLoop(P::melody) == nullptr || c.isAnalysing(P::melody) || i < 30); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
                c.prepareToPlay(sr, 256);
                const auto on = onsets(play(c, ch, sr, 32.0));
                const double sps = 60.0 / 140.0 / 4.0 * sr;
                for (int o : on) control = std::max(control, std::abs(o - std::round(o / sps) * sps) / sr * 1000.0);
            }
            check(n140 == 32 && w140 <= control + 1.0, tag + "match: all " + juce::String(n140) + " stretched melody notes on the 140 grid (worst " + juce::String(w140, 2)
                  + " ms; the same melody made at 140: " + juce::String(control, 2) + " ms)");

            th.bpm = 150.0;                       // the project speeds up: the melody is matched again
            runBlocks(4);
            settle(900);
            check(std::abs(t.getMatchedBpm(P::melody) - 150.0) < 0.01 && std::abs(t.getSourceBpm(P::melody) - ms) < 1e-6,
                  tag + "match: project at 150 -> the melody is re-stretched to 150, its own tempo unchanged (" + juce::String(t.getMatchedBpm(P::melody), 2) + ")");
            int n150 = 0;
            const double w150 = melodyOnGrid(150.0, n150);
            check(n150 == 32 && w150 <= control + 1.0, tag + "match: at 150 all " + juce::String(n150) + " notes on the 150 grid (worst " + juce::String(w150, 2) + " ms)");

            // 1/2: the same rhythm read at half speed; Fit: slices; Auto
            t.scaleSourceTempo(P::melody, 0.5);
            settle(600);
            check(std::abs(t.getSourceBpm(P::melody) - ms * 0.5) < 0.01 && t.isTempoTyped(P::melody), tag + "match: 1/2 reads the melody at " + juce::String(t.getSourceBpm(P::melody), 2));
            t.setFit(P::melody, P::Fit::slices);
            settle(600);
            check(! t.isStretched(P::melody) && t.getLoop(P::melody) && std::abs(t.getLoop(P::melody)->bpm - ms * 0.5) < 0.01, tag + "match: Fit SLICES plays it on its own grid, re-timed hit by hit");
            t.setFit(P::melody, P::Fit::automatic);
            t.setSourceTempo(P::melody, 0.0);
            settle(600);
            check(std::abs(t.getSourceBpm(P::melody) - ms) < 0.01 && ! t.isTempoTyped(P::melody) && t.isStretched(P::melody), tag + "match: Auto reads it again (" + juce::String(t.getSourceBpm(P::melody), 2) + ")");

            // the tempo read travels with the session: no re-read, no flip
            t.setFit(P::drums, P::Fit::stretch);
            settle(600);
            juce::MemoryBlock st5;
            t.getStateInformation(st5);
            EpsilonBounceProcessor u;
            Head uh; uh.bpm = 150.0;
            u.setPlayHead(&uh);
            u.setPlayConfigDetails(0, 2, sr, 256);
            u.prepareToPlay(sr, 256);
            u.setStateInformation(st5.getData(), (int) st5.getSize());
            for (int i = 0; i < 4; ++i) { warm.clear(); u.processBlock(warm, noMidi); }
            for (int i = 0; i < 1500 && (! u.getLoop(P::melody) || u.isAnalysing(P::melody) || u.isAnalysing(P::drums) || i < 40); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            check(std::abs(u.getSourceBpm(P::melody) - ms) < 1e-6 && u.getFit(P::drums) == P::Fit::stretch && u.isStretched(P::melody),
                  tag + "match: a reopened session keeps each loop's tempo and fit");
        }

        if (sr == 48000.0 && snapDir != juce::File())
        {
            snapDir.createDirectory();
            {
                auto* q = p.state.getParameter("material"); q->setValueNotifyingHost(q->convertTo0to1(1.0f));
                const int ser = p.getLoopSerial();
                p.loadFile(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bounce-bench-chords.wav"));
                waitForLoop(p, ser);
                p.applyPreset(3);
                const int dser = p.getLoopSerial(EpsilonBounceProcessor::drums);
                p.loadFile(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bounce-bench-reference.wav"), EpsilonBounceProcessor::drums);
                for (int i = 0; i < 400 && (p.getLoopSerial(EpsilonBounceProcessor::drums) == dser || p.isAnalysing(EpsilonBounceProcessor::drums)); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
                for (int i = 0; i < 20; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            }
            std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
            ed->setSize(1160, 840);
            for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(40);
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
            juce::PNGImageFormat png;
            auto file = snapDir.getChildFile("epsilon-bounce-editor.png");
            file.deleteFile();
            if (auto os = file.createOutputStream()) png.writeImageToStream(img, *os);
            check(file.existsAsFile(), "editor snapshot " + file.getFullPathName());

            EpsilonBounceProcessor empty;
            std::unique_ptr<juce::AudioProcessorEditor> ed2(empty.createEditor());
            ed2->setSize(1160, 840);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
            auto img2 = ed2->createComponentSnapshot(ed2->getLocalBounds(), true, 2.0f);
            auto file2 = snapDir.getChildFile("epsilon-bounce-empty.png");
            file2.deleteFile();
            if (auto os = file2.createOutputStream()) png.writeImageToStream(img2, *os);
            ed.reset(); ed2.reset();
        }
    }
    std::printf("\n%d failure(s)\n", fails);
    return fails == 0 ? 0 : 1;
}
