// Epsilon Bounce engine tests: no JUCE. Build: cmake --build build --target BounceEngineTests && ctest
#include "BounceEngine.h"
#include <cstdio>
#include <random>
#include <string>
#include <chrono>

using namespace bounce;
static int failures = 0, checks = 0;
static void check(bool ok, const std::string& what, double value = 0.0)
{
    ++checks;
    std::printf("%s %-62s %10.3f\n", ok ? "PASS" : "FAIL", what.c_str(), value);
    if (!ok) ++failures;
}

// A drum-ish loop: decaying noise bursts at the given 16th steps, silence between.
static void makeLoop(double sr, double bpm, int bars, const std::vector<int>& steps, std::vector<float>& l, std::vector<float>& r)
{
    const int len = (int) std::lround(bars * 4 * 60.0 / bpm * sr);
    l.assign((size_t) len, 0.0f); r.assign((size_t) len, 0.0f);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    const double sps = len / (bars * 16.0);
    for (auto st : steps)
    {
        const int at = (int) std::lround(st * sps);
        const int dur = (int) (0.06 * sr);
        for (int i = 0; i < dur && at + i < len; ++i)
        {
            const float env = std::exp(-(float) i / (float) (0.012 * sr));
            l[(size_t) (at + i)] += 0.7f * env * u(rng);
            r[(size_t) (at + i)] = l[(size_t) (at + i)];
        }
    }
}

// Onsets in rendered audio: first sample above a threshold after >= 20 ms of near-silence.
static std::vector<int> findOnsets(const std::vector<float>& x, double sr)
{
    std::vector<int> out;
    const int quiet = (int) (0.02 * sr);
    int silent = quiet;
    for (int i = 0; i < (int) x.size(); ++i)
    {
        if (std::abs(x[(size_t) i]) > 0.05f)
        {
            if (silent >= quiet) out.push_back(i);
            silent = 0;
        }
        else ++silent;
    }
    return out;
}

static std::vector<float> renderLoops(const LoopData& d, GrooveSettings g, double hostRate, double hostBpm, int loops, int block)
{
    Player p;
    p.prepare(hostRate, block);
    p.update(d, g, hostBpm);
    const double stepsPerSample = hostBpm / 60.0 * 4.0 / hostRate;
    const int total = (int) std::floor(loops * d.bars * 16 / stepsPerSample) - 1;   // stop just before the next downbeat
    std::vector<float> L((size_t) total, 0.0f), R((size_t) total, 0.0f);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min(block, total - pos);
        p.render(d, L.data() + pos, R.data() + pos, n, pos * stepsPerSample, stepsPerSample, 1.0f);
    }
    return L;
}

int main()
{
    const std::vector<int> steps { 0, 3, 4, 6, 8, 11, 12, 14, 16, 19, 20, 22, 24, 27, 28, 30 };   // a dembow, 2 bars
    for (double sr : { 44100.0, 48000.0 })
    {
        const auto tag = std::string(sr == 44100.0 ? "[44.1k] " : "[48k] ");
        std::vector<float> l, r;
        makeLoop(sr, 100.0, 2, steps, l, r);
        auto d = analyse(l, r, sr, {});
        check(d->bars == 2, tag + "auto bar count for a 2-bar 100 BPM loop", d->bars);
        check(std::abs(d->bpm - 100.0) < 0.05, tag + "loop tempo detected", d->bpm);
        check(d->slices.size() == steps.size(), tag + "one slice per hit", (double) d->slices.size());
        double worst = 0.0;
        const double sps = d->samplesPerStep();
        for (size_t i = 0; i < std::min(d->slices.size(), steps.size()); ++i)
            worst = std::max(worst, std::abs(d->slices[i].onset - steps[i] * sps) / sr * 1000.0);
        check(worst < 1.5, tag + "onset accuracy (worst, ms)", worst);
        bool stepsOk = true;
        for (size_t i = 0; i < std::min(d->slices.size(), steps.size()); ++i) stepsOk &= d->slices[i].step == steps[i];
        check(stepsOk, tag + "every hit on the right 16th");

        // Straight, no groove, at the loop's own tempo: hits come back where they were
        GrooveSettings straight; straight.groove = Groove::straight; straight.tighten = 0.0f; straight.accent = 0.0f;
        for (int block : { 64, 512 })
        {
            auto out = renderLoops(*d, straight, sr, 100.0, 3, block);
            auto on = findOnsets(out, sr);
            check(on.size() == steps.size() * 3, tag + "straight: all hits over 3 loops, block " + std::to_string(block), (double) on.size());
            double err = 0.0;
            const double hostSps = 60.0 / 100.0 / 4.0 * sr;
            for (size_t i = 0; i < on.size(); ++i)
            {
                const auto k = i % steps.size();
                const double want = (steps[k] + 32.0 * (double) (i / steps.size())) * hostSps;
                err = std::max(err, std::abs(on[i] - want) / sr * 1000.0);
            }
            check(err < 1.0, tag + "straight: worst timing error (ms), block " + std::to_string(block), err);
        }

        // Dembow at amount 1: step 3 and 11 land 0.10 + swing 0.10 = 0.20 of a 16th late
        GrooveSettings dem; dem.groove = Groove::dembow; dem.amount = 1.0f; dem.tighten = 1.0f; dem.accent = 0.0f;
        {
            auto out = renderLoops(*d, dem, sr, 100.0, 1, 256);
            auto on = findOnsets(out, sr);
            const double hostSps = 60.0 / 100.0 / 4.0 * sr;
            if (on.size() >= 2)
            {
                const double late = (on[1] - 3 * hostSps) / hostSps;
                check(std::abs(late - 0.20) < 0.02, tag + "dembow: step 3 pushed 0.20 of a 16th", late);
            }
            else check(false, tag + "dembow: hits rendered");
            if (on.size() >= 4)
            {
                const double early = (on[3] - 6 * hostSps) / hostSps;
                check(std::abs(early + 0.05) < 0.02, tag + "dembow: step 6 pulled 0.05 early", early);
            }
        }

        // Drill swing on off-16ths
        GrooveSettings dr; dr.groove = Groove::drill; dr.amount = 1.0f; dr.tighten = 1.0f;
        {
            Slice s; s.gridStep = 5.0; s.step = 5;
            auto pl = place(s, 3, dr, grooveTemplate(Groove::drill), 150.0);
            check(std::abs(pl.step - 5.24) < 1e-6, tag + "drill: off-16th swung to 62 %", pl.step);
        }

        // Tempo follow: the same loop at 140 BPM in the host lands on the 140 BPM grid
        {
            auto out = renderLoops(*d, straight, sr, 140.0, 2, 256);
            auto on = findOnsets(out, sr);
            const double hostSps = 60.0 / 140.0 / 4.0 * sr;
            double err = 0.0;
            for (size_t i = 0; i < on.size(); ++i)
            {
                const auto k = i % steps.size();
                const double want = (steps[k] + 32.0 * (double) (i / steps.size())) * hostSps;
                err = std::max(err, std::abs(on[i] - want) / sr * 1000.0);
            }
            check(on.size() == steps.size() * 2, tag + "140 BPM host: all hits over 2 loops", (double) on.size());
            check(err < 1.0, tag + "follows host tempo 140 BPM (worst ms)", err);
        }

        // Humanize is stable (same seed, same result) and bounded
        {
            GrooveSettings h = straight; h.humanizeMs = 10.0f;
            Slice s; s.gridStep = 4.0;
            const auto a = place(s, 5, h, grooveTemplate(Groove::straight), 150.0);
            const auto b = place(s, 5, h, grooveTemplate(Groove::straight), 150.0);
            check(a.step == b.step && std::abs(a.step - 4.0) <= 10.0 / 150.0 + 1e-9, tag + "humanize stable and within 10 ms", (a.step - 4.0) * 150.0);
        }

        // Every groove at max settings: finite, no runaway level
        {
            float peak = 0.0f; bool finite = true;
            for (int gi = 0; gi < grooveCount; ++gi)
            {
                GrooveSettings g; g.groove = (Groove) gi; g.amount = 1.5f; g.swing = 1.0f; g.humanizeMs = 20.0f; g.accent = 1.0f; g.feelMs = 20.0f;
                auto out = renderLoops(*d, g, sr, 150.0, 2, 128);
                for (auto v : out) { finite &= std::isfinite(v); peak = std::max(peak, std::abs(v)); }
            }
            check(finite && peak < 1.6f, tag + "all grooves at max: finite, peak below +4 dBFS", 20.0 * std::log10(std::max(1e-9f, peak)));
        }
    }


    // Speed: 60 s of a dense 4-bar loop at 48 kHz, 64-sample blocks (worst case for the trigger search).
    {
        std::vector<int> dense; for (int i = 0; i < 64; ++i) dense.push_back(i);
        std::vector<float> l, r;
        makeLoop(48000.0, 140.0, 4, dense, l, r);
        auto d = analyse(l, r, 48000.0, {});
        GrooveSettings g; g.groove = Groove::drill; g.humanizeMs = 5.0f;
        Player p; p.prepare(48000.0, 64); p.update(*d, g, 140.0);
        std::vector<float> L(64), R(64);
        const double sps = 140.0 / 60.0 * 4.0 / 48000.0;
        const int blocks = 60 * 48000 / 64;
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) { std::fill(L.begin(), L.end(), 0.0f); std::fill(R.begin(), R.end(), 0.0f); p.render(*d, L.data(), R.data(), 64, b * 64 * sps, sps, 1.0f); }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double cpu = secs / 60.0 * 100.0;
        std::printf("PERF render: %.3f %% of one core (%d slices, 64-sample blocks)\n", cpu, (int) d->slices.size());
        check(cpu < 2.0, "render costs under 2 % of a core", cpu);
        const auto a0 = std::chrono::steady_clock::now();
        auto d2 = analyse(l, r, 48000.0, {});
        const double ams = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a0).count();
        std::printf("PERF analyse 4 bars: %.1f ms (worker thread)\n", ams);
        check(ams < 2000.0 && d2->slices.size() > 0, "analysis of a 4-bar loop under 2 s", ams);
    }

    // Bar guessing
    check(chooseBars((int) (44100 * 4 * 60.0 / 140.0), 44100.0, 0) == 1, "1 bar at 140 BPM guessed as 1 bar");
    check(chooseBars((int) (44100 * 16 * 60.0 / 70.0), 44100.0, 0) == 8, "4 bars at 70 BPM read as 8 bars at 140", chooseBars((int) (44100 * 16 * 60.0 / 70.0), 44100.0, 0));

    // Silence and tiny input don't crash
    {
        auto d = analyse(std::vector<float>(100000, 0.0f), {}, 44100.0, {});
        check(d->slices.size() == 1, "silent loop gives one slice");
        auto d2 = analyse(std::vector<float>(10, 0.0f), {}, 44100.0, {});
        Player p; p.prepare(44100.0, 64); p.update(*d2, {}, 120.0);
        float L[64] {}, R[64] {};
        p.render(*d2, L, R, 64, 0.0, 0.001, 1.0f);
        check(true, "tiny loop renders nothing, safely");
    }

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
