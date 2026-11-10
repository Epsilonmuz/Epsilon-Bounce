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

bool waitForLoop(EpsilonBounceProcessor& p, int serialBefore)
{
    for (int i = 0; i < 400; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        if (p.getLoopSerial() != serialBefore && ! p.isAnalysing()) return true;
    }
    return false;
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
            p.state.getParameter("tighten")->setValueNotifyingHost(1.0f);
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

        if (sr == 48000.0 && snapDir != juce::File())
        {
            snapDir.createDirectory();
            {
                auto* q = p.state.getParameter("material"); q->setValueNotifyingHost(q->convertTo0to1(1.0f));
                const int ser = p.getLoopSerial();
                p.loadFile(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bounce-bench-chords.wav"));
                waitForLoop(p, ser);
                p.applyPreset(3);
                auto* gq = p.state.getParameter("groove"); gq->setValueNotifyingHost(gq->convertTo0to1((float) bounce::Groove::learned));
                for (int i = 0; i < 20; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            }
            std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
            ed->setSize(1080, 680);
            for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(40);
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
            juce::PNGImageFormat png;
            auto file = snapDir.getChildFile("epsilon-bounce-editor.png");
            file.deleteFile();
            if (auto os = file.createOutputStream()) png.writeImageToStream(img, *os);
            check(file.existsAsFile(), "editor snapshot " + file.getFullPathName());

            EpsilonBounceProcessor empty;
            std::unique_ptr<juce::AudioProcessorEditor> ed2(empty.createEditor());
            ed2->setSize(1080, 680);
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
