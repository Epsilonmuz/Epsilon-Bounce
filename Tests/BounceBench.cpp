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

        if (sr == 48000.0 && snapDir != juce::File())
        {
            snapDir.createDirectory();
            for (int g = 0; g < 4; ++g) p.state.getParameter("groove")->setValueNotifyingHost(p.state.getParameter("groove")->convertTo0to1(1.0f));
            for (auto id : { "amount", "swing", "humanize", "accent", "feel" })
                p.state.getParameter(id)->setValueNotifyingHost(p.state.getParameter(id)->getDefaultValue());
            p.state.getParameter("swing")->setValueNotifyingHost(0.2f);
            std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
            ed->setSize(900, 560);
            for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(40);
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
            juce::PNGImageFormat png;
            auto file = snapDir.getChildFile("epsilon-bounce-editor.png");
            file.deleteFile();
            if (auto os = file.createOutputStream()) png.writeImageToStream(img, *os);
            check(file.existsAsFile(), "editor snapshot " + file.getFullPathName());

            EpsilonBounceProcessor empty;
            std::unique_ptr<juce::AudioProcessorEditor> ed2(empty.createEditor());
            ed2->setSize(900, 560);
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
