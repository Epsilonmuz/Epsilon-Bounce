// Epsilon Bounce v1.6 tempo-match tests: no JUCE. Build: cmake --build build --target BounceTempoTests && ctest
#include "TempoMatch.h"
#include <cstdio>
#include <random>
#include <string>
#include <chrono>

using namespace bounce;
static int failures = 0, checks = 0;
static void check(bool ok, const std::string& what, double value = 0.0)
{
    ++checks;
    std::printf("%s %-66s %10.3f\n", ok ? "PASS" : "FAIL", what.c_str(), value);
    if (!ok) ++failures;
}

// Drums: kick (pitched thump), snare (noise), hats (short noise) on a 16th pattern.
static void drums(double sr, double bpm, int bars, double tailBars, std::vector<float>& l, std::vector<float>& r,
                  const char* kick = "X-----x---X-----", const char* snare = "----X-------X---", const char* hat = "x-x-x-x-x-x-x-x-")
{
    const double sps = 60.0 / bpm / 4.0 * sr;
    const int len = (int) std::lround((bars + tailBars) * 16 * sps);
    l.assign((size_t) len, 0.0f); r.assign((size_t) len, 0.0f);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (int b = 0; b < bars; ++b)
        for (int s = 0; s < 16; ++s)
        {
            const int at = (int) std::lround((b * 16 + s) * sps);
            auto add = [&](int dur, auto gen) { for (int i = 0; i < dur && at + i < len; ++i) { const float v = gen(i); l[(size_t) (at + i)] += v; r[(size_t) (at + i)] += v; } };
            if (kick[s] != '-') add((int) (0.25 * sr), [&](int i) { const double t = i / sr; return 0.8f * (float) (std::exp(-t * 12) * std::sin(2 * pi * (50 + 80 * std::exp(-t * 30)) * t)); });
            if (snare[s] != '-') add((int) (0.15 * sr), [&](int i) { return 0.5f * std::exp(-(float) i / (float) (0.03 * sr)) * u(rng); });
            if (hat[s] != '-') add((int) (0.04 * sr), [&](int i) { return (hat[s] == 'X' ? 0.3f : 0.18f) * std::exp(-(float) i / (float) (0.008 * sr)) * u(rng); });
        }
}

// A melody: plucked tones with harmonics, a note on given 16th steps, cycling through a minor phrase.
static void melody(double sr, double bpm, int bars, double tailBars, std::vector<float>& l, std::vector<float>& r, const char* rhythm = "X--x--x-X-x-x---")
{
    const double sps = 60.0 / bpm / 4.0 * sr;
    const int len = (int) std::lround((bars + tailBars) * 16 * sps);
    l.assign((size_t) len, 0.0f); r.assign((size_t) len, 0.0f);
    const int notes[] = { 57, 60, 64, 62, 60, 55, 57, 64 };
    int ni = 0;
    for (int b = 0; b < bars; ++b)
        for (int s = 0; s < 16; ++s)
        {
            if (rhythm[s] == '-') continue;
            const double f = 440.0 * std::pow(2.0, (notes[ni++ % 8] - 69) / 12.0);
            const int at = (int) std::lround((b * 16 + s) * sps);
            const int dur = (int) (0.6 * sr);
            for (int i = 0; i < dur && at + i < len; ++i)
            {
                const double t = i / sr;
                const float v = (float) (0.3 * std::exp(-t * 4) * (std::sin(2 * pi * f * t) + 0.4 * std::sin(4 * pi * f * t) + 0.2 * std::sin(6 * pi * f * t)));
                l[(size_t) (at + i)] += v; r[(size_t) (at + i)] += v * 0.8f;
            }
        }
}

static double relErr(double a, double b) { return std::abs(a / b - 1.0); }
static bool sameFamily(double got, double truth)   // got is truth, x2 or x0.5 within 0.6 %
{
    for (double m : { 0.5, 1.0, 2.0 }) if (relErr(got, truth * m) < 0.006) return true;
    return false;
}

// Zero-crossing frequency of a steady region
static double zcFreq(const std::vector<float>& x, double sr, int from, int to)
{
    int zc = 0, first = -1, last = -1;
    for (int i = from + 1; i < to; ++i)
        if (x[(size_t) i - 1] <= 0.0f && x[(size_t) i] > 0.0f) { if (first < 0) first = i; last = i; ++zc; }
    return zc > 1 ? (zc - 1) * sr / (last - first) : 0.0;
}

// The plugin's two phases on one loop: cut on its own grid, stretch on those cuts, carry the slices over.
// Then, for every true hit (given in source samples) that the cutter split: where its slice landed (vs
// where it belongs) and where the stretched audio really has its attack (re-analysed).
// Where a hit's attack really is: the first sample near `at` that reaches half the local peak
static double attackAt(const std::vector<float>& x, double sr, double at)
{
    const int a = std::max(0, (int) (at - 0.015 * sr)), b = std::min((int) x.size(), (int) (at + 0.03 * sr));
    float pk = 0.0f;
    for (int i = a; i < b; ++i) pk = std::max(pk, std::abs(x[(size_t) i]));
    for (int i = a; i < b; ++i) if (std::abs(x[(size_t) i]) >= 0.5f * pk) return i;
    return at;
}

struct TwoPhase { double sliceWorstMs = 0.0, audioWorstMs = 0.0; int hits = 0; std::shared_ptr<LoopData> d; };
static TwoPhase twoPhase(const std::vector<float>& l, const std::vector<float>& r, double sr, double srcBpm, double target,
                         Material m, const std::vector<double>& truth)
{
    TwoPhase out;
    AnalysisSettings a1; a1.exactBpm = srcBpm; a1.material = m; a1.autoAlign = false; a1.sensitivity = 0.8f;
    auto cut = analyse(l, r, sr, a1);
    std::vector<int> cuts; for (const auto& s : cut->slices) cuts.push_back(s.onset);
    std::vector<float> sl, sr2;
    const double factor = srcBpm / target;
    stretchLoop(l, r, factor, sr, m, sl, sr2, nullptr, &cuts);
    out.d = rescaleLoop(*cut, sl, sr2, factor, target);
    for (double was : truth)
    {
        bool cutThere = false;     // (a hit the cutter didn't split isn't a tempo question)
        for (const auto& s : cut->slices) cutThere |= std::abs(s.onset - was) < 0.005 * sr;
        if (! cutThere) continue;
        const double want = was * factor;
        double bs = 1e9;
        for (const auto& s : out.d->slices) bs = std::min(bs, std::abs(s.onset - want));
        const double ba = std::abs(attackAt(sl, sr, want) - attackAt(l, sr, was) * factor);   // the attack moved with it
        out.sliceWorstMs = std::max(out.sliceWorstMs, bs / sr * 1000.0);
        out.audioWorstMs = std::max(out.audioWorstMs, ba / sr * 1000.0);
        ++out.hits;
    }
    return out;
}

int main()
{
    for (double sr : { 44100.0, 48000.0 })
    {
        const std::string at = " @" + std::to_string((int) sr);

        // 1. Drum loops cut to whole bars: exact tempo (the length decides once the rhythm is close)
        for (double bpm : { 72.0, 85.0, 92.0, 100.0, 118.0, 128.0, 140.0, 150.0, 174.0 })
        {
            std::vector<float> l, r; drums(sr, bpm, 4, 0.0, l, r);
            const auto g = estimateTempo(l, r, sr, 0.0);
            check(sameFamily(g.bpm, bpm), "drums " + std::to_string((int) bpm) + " BPM, 4 bars: tempo found" + at, g.bpm);
        }
        // 2. With a project tempo, the reading nearest to it is taken (half-time loop into a fast project)
        {
            std::vector<float> l, r; drums(sr, 70.0, 2, 0.0, l, r);
            const auto g = estimateTempo(l, r, sr, 140.0);
            check(relErr(g.bpm, 140.0) < 0.006 || relErr(g.bpm, 70.0) < 0.006, "70 BPM loop in a 140 project reads 70 or 140" + at, g.bpm);
        }
        // 3. A reverb tail / trailing silence: still the right tempo
        for (double bpm : { 90.0, 140.0 })
        {
            std::vector<float> l, r; drums(sr, bpm, 4, 0.3, l, r);
            const auto g = estimateTempo(l, r, sr, 0.0);
            check(sameFamily(g.bpm, bpm), "drums " + std::to_string((int) bpm) + " + 0.3 bar tail: tempo found" + at, g.bpm);
        }
        // 4. Not cut to bars at all (a flip that starts mid-bar, odd length): the rhythm alone, within 1 %
        {
            std::vector<float> l, r; drums(sr, 96.0, 5, 0.0, l, r);
            const int cut0 = (int) (0.37 * l.size() / 5.0), cut1 = (int) (4.1 * l.size() / 5.0);
            std::vector<float> l2(l.begin() + cut0, l.begin() + cut1), r2(r.begin() + cut0, r.begin() + cut1);
            const auto g = estimateTempo(l2, r2, sr, 0.0);
            bool ok = false;
            for (double m : { 0.5, 1.0, 2.0 }) ok |= relErr(g.bpm, 96.0 * m) < 0.01;
            check(ok, "drums 96 BPM cut at an odd point: tempo within 1 %" + at, g.bpm);
        }
        // 5. Melodies (no drums at all)
        for (double bpm : { 80.0, 92.0, 110.0, 140.0 })
        {
            std::vector<float> l, r; melody(sr, bpm, 4, 0.0, l, r);
            const auto g = estimateTempo(l, r, sr, 0.0);
            check(sameFamily(g.bpm, bpm), "melody " + std::to_string((int) bpm) + " BPM: tempo found" + at, g.bpm);
        }

        // 6. Stretch: length exact, pitch unchanged, stereo phase relation kept
        {
            const int n = (int) (2.0 * sr);
            std::vector<float> l((size_t) n), r((size_t) n), ol, or_;
            for (int i = 0; i < n; ++i) { l[(size_t) i] = 0.5f * (float) std::sin(2 * pi * 440.0 * i / sr); r[(size_t) i] = -l[(size_t) i]; }
            for (double f : { 0.64, 1.5, 1.25 })
            {
                stretchStereo(l, r, f, sr, ol, or_);
                check((int) ol.size() == (int) std::lround(n * f), "stretch x" + std::to_string(f).substr(0, 4) + ": exact length" + at, (double) ol.size());
                const double hz = zcFreq(ol, sr, (int) (0.3 * ol.size()), (int) (0.7 * ol.size()));
                check(std::abs(hz - 440.0) < 2.0, "stretch x" + std::to_string(f).substr(0, 4) + ": pitch stays 440 Hz" + at, hz);
                double c = 0, el = 0, er = 0;
                for (size_t i = ol.size() / 4; i < ol.size() * 3 / 4; ++i) { c += ol[i] * or_[i]; el += ol[i] * ol[i]; er += or_[i] * or_[i]; }
                const double corr = c / std::sqrt(el * er + 1e-12);
                check(corr < -0.99, "stretch x" + std::to_string(f).substr(0, 4) + ": stereo image kept (L/R correlation)" + at, corr);
                double rmsIn = 0, rmsOut = 0;
                for (int i = n / 4; i < 3 * n / 4; ++i) rmsIn += l[(size_t) i] * l[(size_t) i];
                for (size_t i = ol.size() / 4; i < ol.size() * 3 / 4; ++i) rmsOut += ol[i] * ol[i];
                const double lvl = 10 * std::log10((rmsOut / (ol.size() / 2)) / (rmsIn / (n / 2)));
                check(std::abs(lvl) < 1.0, "stretch x" + std::to_string(f).substr(0, 4) + ": level kept (dB)" + at, lvl);
            }
        }

        // 7. Drums stretched 90 -> 140 (Fit: stretch, or a flip with drums in it): every hit on the 140
        //    grid, and the stretched audio's attacks really there (kept sharp: within 3 ms).
        {
            std::vector<float> l, r; drums(sr, 90.0, 2, 0.0, l, r, "X-------X-------", "----X-------X---", "----------------");
            std::vector<double> truth;
            for (int st : { 0, 4, 8, 12, 16, 20, 24, 28 }) truth.push_back(st * 60.0 / 90.0 / 4.0 * sr);
            const auto t0 = std::chrono::steady_clock::now();
            const auto tp = twoPhase(l, r, sr, 90.0, 140.0, Material::drums, truth);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            check(tp.d->bars == 2 && tp.hits >= 6, "90 -> 140 drums: " + std::to_string(tp.hits) + " hits cut, 2 bars on the 140 grid" + at, tp.hits);
            check(tp.sliceWorstMs < 1.0, "90 -> 140 drums: slices on the 16th grid (worst ms)" + at, tp.sliceWorstMs);
            check(tp.audioWorstMs < 3.0, "90 -> 140 drums: attacks in the stretched audio (worst ms)" + at, tp.audioWorstMs);
            check(ms < 1500.0, "stretch speed: 2 bars cut + stretched in (ms)" + at, ms);
        }

        // 8. Two lanes, one grid: a 92 BPM melody and 140 BPM drums in a 140 project. The melody's
        //    tempo is found, it is stretched to 140 (or its half-time reading), and its note starts land on 16ths.
        {
            std::vector<float> ml, mr, dl, dr; melody(sr, 92.0, 4, 0.0, ml, mr); drums(sr, 140.0, 4, 0.0, dl, dr);
            const auto gm = estimateTempo(ml, mr, sr, 140.0), gd = estimateTempo(dl, dr, sr, 140.0);
            check(relErr(gd.bpm, 140.0) < 0.002, "two lanes: drums read 140" + at, gd.bpm);
            check(sameFamily(gm.bpm, 92.0), "two lanes: melody read 92 (or 46 / 184)" + at, gm.bpm);
            std::vector<double> truth;
            const char* rhythm = "X--x--x-X-x-x---";
            for (int b = 0; b < 4; ++b) for (int st = 0; st < 16; ++st) if (rhythm[st] != '-') truth.push_back((b * 16 + st) * (60.0 / 92.0 / 4.0 * sr));
            const auto tp = twoPhase(ml, mr, sr, gm.bpm, 140.0, Material::melodic, truth);
            const auto& d = tp.d;
            check(d->bpm == 140.0 && tp.hits >= 18 && tp.sliceWorstMs < 1.0, "two lanes: " + std::to_string(tp.hits) + " cut melody notes on the 140 grid (worst ms)" + at, tp.sliceWorstMs);
            check(tp.audioWorstMs < 6.0, "two lanes: the stretched notes start where the slices say (worst ms)" + at, tp.audioWorstMs);
            const double expectBars = 4.0 * gm.bpm / 92.0;
            check(std::abs(d->bars - expectBars) < 0.6, "two lanes: melody length in 140 bars", d->bars);
        }
    }
    // 9. Speed: a 60 s melody (the longest allowed) estimates + stretches in reasonable time
    {
        std::vector<float> l, r; melody(48000.0, 100.0, 25, 0.0, l, r);
        const auto t0 = std::chrono::steady_clock::now();
        const auto g = estimateTempo(l, r, 48000.0, 140.0);
        const double est = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::vector<float> ol, orr;
        stretchLoop(l, r, g.bpm / 140.0, 48000.0, Material::melodic, ol, orr);
        const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        check(est < 1500.0, "60 s melody: tempo estimate (ms)", est);
        check(total < 8000.0, "60 s melody: estimate + stretch (ms)", total);
    }
    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
