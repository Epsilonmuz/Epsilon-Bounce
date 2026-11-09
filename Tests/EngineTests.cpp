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
template <typename StepT>
static void makeLoop(double sr, double bpm, int bars, const std::vector<StepT>& steps, std::vector<float>& l, std::vector<float>& r, const std::vector<float>& levels = {})
{
    const int len = (int) std::lround(bars * 4 * 60.0 / bpm * sr);
    l.assign((size_t) len, 0.0f); r.assign((size_t) len, 0.0f);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    const double sps = len / (bars * 16.0);
    for (size_t h = 0; h < steps.size(); ++h)
    {
        const double st = (double) steps[h];
        const float lvl = h < levels.size() ? levels[h] : 0.7f;
        const int at = (int) std::lround(st * sps);
        const int dur = (int) (0.06 * sr);
        for (int i = 0; i < dur && at + i < len; ++i)
        {
            const float env = std::exp(-(float) i / (float) (0.012 * sr));
            l[(size_t) (at + i)] += lvl * env * u(rng);
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

    // ---- v1.1: every genre is distinct, bounded, and lands where its template says
    {
        std::vector<int> steps16; for (int i = 0; i < 32; ++i) steps16.push_back(i);   // a hit on every 16th, 2 bars
        std::vector<float> l, r;
        makeLoop(48000.0, 120.0, 2, steps16, l, r);
        auto d = analyse(l, r, 48000.0, {});
        check(d->slices.size() == 32, "16th-note loop: 32 slices", (double) d->slices.size());
        bool allFinite = true, allDistinct = true; float peak = 0.0f;
        std::vector<GrooveTemplate> seen;
        for (int gi = 0; gi < grooveCount; ++gi)
        {
            if ((Groove) gi == Groove::learned) continue;
            const auto t = grooveTemplate((Groove) gi);
            for (const auto& o : seen) allDistinct &= (o != t);
            seen.push_back(t);
            GrooveSettings g; g.groove = (Groove) gi; g.amount = 1.5f; g.swing = 1.0f; g.accent = 1.0f; g.humanizeMs = 20.0f;
            auto out = renderLoops(*d, g, 48000.0, 140.0, 2, 128);
            for (auto v : out) { allFinite &= std::isfinite(v); peak = std::max(peak, std::abs(v)); }
            // placements never jump past a neighbour's slot (no reordering beyond half a 16th)
            for (int st = 0; st < 16; ++st)
            {
                const double off = (st & 1 ? t.swing : 0.0f) + t.offset[(size_t) st];
                allFinite &= std::abs(off) < 0.5;
            }
        }
        check(allDistinct, "all 13 genre templates are different from each other");
        check(allFinite && peak < 1.6f, "every genre at max: finite, offsets under half a 16th, bounded level", 20.0 * std::log10(std::max(1e-9f, peak)));
        check(grooveCount == 14 && std::string(grooveName(Groove::learned)) == "LEARNED", "14 groove slots, LEARNED last");
        // spot-check two genres in rendered audio: boom bap's 58 % swing and lay-back snare
        GrooveSettings bb; bb.groove = Groove::boomBap; bb.tighten = 1.0f; bb.accent = 0.0f;
        Slice s1; s1.gridStep = 1.0; Slice s4; s4.gridStep = 4.0;
        const auto tb = grooveTemplate(Groove::boomBap);
        check(std::abs(place(s1, 1, bb, tb, 125.0).step - 1.16) < 1e-6, "boom bap: off-16th at MPC 58 %", place(s1, 1, bb, tb, 125.0).step);
        check(std::abs(place(s4, 4, bb, tb, 125.0).step - 4.07) < 1e-6, "boom bap: backbeat snare laid back 0.07", place(s4, 4, bb, tb, 125.0).step);
    }

    // ---- v1.1: Groove Learn recovers a reference loop's feel
    for (double sr : { 44100.0, 48000.0 })
    {
        const auto tag = std::string(sr == 44100.0 ? "[44.1k] " : "[48k] ");
        // reference: 2 bars of 16ths, off-16ths swung 0.24 late, snare on 4 / 12 another 0.08 late and louder
        std::vector<double> st; std::vector<float> lv;
        for (int b = 0; b < 2; ++b)
            for (int i = 0; i < 16; ++i)
            {
                double x = b * 16 + i + ((i & 1) ? 0.24 : 0.0) + ((i == 4 || i == 12) ? 0.08 : 0.0);
                st.push_back(x); lv.push_back((i == 4 || i == 12) ? 0.9f : (i & 1) ? 0.35f : 0.6f);
            }
        std::vector<float> l, r;
        makeLoop(sr, 95.0, 2, st, l, r, lv);
        auto ref = analyse(l, r, sr, {});
        auto learned = learnGroove(*ref);
        check(learned.stepsHeard == 16 && learned.hits >= 30, tag + "learn: heard all 16 steps", learned.hits);
        double worstOff = 0.0, worstSnare = 0.0;
        for (int i = 1; i < 16; i += 2) worstOff = std::max(worstOff, std::abs(learned.groove.offset[(size_t) i] - 0.24));
        for (int i : { 4, 12 }) worstSnare = std::max(worstSnare, std::abs(learned.groove.offset[(size_t) i] - 0.08));
        check(worstOff < 0.03, tag + "learn: recovers the 0.24 swing (worst error, 16ths)", worstOff);
        check(worstSnare < 0.03, tag + "learn: recovers the late snare (worst error, 16ths)", worstSnare);
        check(learned.groove.accentDb[4] > 1.0f && learned.groove.accentDb[1] < -1.0f, tag + "learn: loud snare accented, quiet off-16ths ducked", learned.groove.accentDb[4]);
        check(std::abs(learned.swingPercent - 62.0f) < 2.0f, tag + "learn: reports ~62 % swing", learned.swingPercent);

        // apply the learned feel to a dead-straight loop and measure where the hits land
        std::vector<int> straightSteps; for (int i = 0; i < 16; ++i) straightSteps.push_back(i);
        std::vector<float> l2, r2;
        makeLoop(sr, 120.0, 1, straightSteps, l2, r2);
        auto target = analyse(l2, r2, sr, {});
        GrooveSettings g; g.groove = Groove::learned; g.learned = learned.groove; g.tighten = 1.0f; g.accent = 0.0f;
        auto out = renderLoops(*target, g, sr, 120.0, 1, 256);
        auto on = findOnsets(out, sr);
        const double hostSps = 60.0 / 120.0 / 4.0 * sr;
        double worst = 0.0;
        for (size_t i = 0; i < on.size() && i < 16; ++i)
        {
            const double want = (double) i + learned.groove.offset[i];
            worst = std::max(worst, std::abs(on[i] / hostSps - want));
        }
        check(on.size() == 16 && worst < 0.03, tag + "learned feel applied to a straight loop (worst, 16ths)", worst);
        // empty / tiny reference: no crash, flat template
        auto empty = learnGroove(*analyse(std::vector<float>(48000, 0.0f), {}, sr, {}));
        check(empty.hits == 0 && empty.groove == GrooveTemplate {}, tag + "learn: silent reference gives a flat groove");
    }

    // ---- v1.2: melodies and flipped samples
    for (double sr : { 44100.0, 48000.0 })
    {
        const auto tag = std::string(sr == 44100.0 ? "[44.1k] " : "[48k] ");
        const double bpm = 100.0;
        const int len = (int) std::lround(8 * 60.0 / bpm * sr);          // 2 bars
        const double sps = len / 32.0;
        auto midiHz = [](double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); };
        // smooth chord changes, no attacks: Am - F - C - G on syncopated 16ths, 12 ms crossfades
        const std::vector<int> changeSteps { 0, 6, 8, 14, 16, 22, 24, 30 };
        const std::vector<std::vector<int>> chords { {57,60,64}, {53,57,60}, {48,52,55}, {55,59,62}, {57,60,64}, {53,57,60}, {48,52,55}, {55,59,62} };
        std::vector<float> l((size_t) len, 0.0f);
        const int xf = (int) (0.012 * sr);
        for (size_t c = 0; c < changeSteps.size(); ++c)
        {
            const int a = (int) std::lround(changeSteps[c] * sps), b = c + 1 < changeSteps.size() ? (int) std::lround(changeSteps[c + 1] * sps) : len;
            for (int i = std::max(0, a - xf); i < std::min(len, b + xf); ++i)
            {
                float w = 1.0f;
                if (i < a + xf && c > 0) w = std::clamp((float) (i - (a - xf)) / (2.0f * xf), 0.0f, 1.0f);
                if (i > b - xf && c + 1 < changeSteps.size()) w *= std::clamp((float) ((b + xf) - i) / (2.0f * xf), 0.0f, 1.0f);
                float v = 0.0f;
                for (int m : chords[c]) v += (float) std::sin(2.0 * pi * midiHz(m) * i / sr);
                l[(size_t) i] += 0.18f * w * v;
            }
        }
        AnalysisSettings mel; mel.material = Material::melodic;
        auto dm = analyse(l, l, sr, mel);
        // each change is a 24 ms crossfade centred on its 16th; a good cut sits between 10 ms before the
        // crossfade starts and the moment it ends (cutting at its start keeps the new chord's onset)
        double worst = 0.0; int found = 0;
        for (int st : changeSteps)
        {
            double best = 1e9;
            for (const auto& sl : dm->slices)
            {
                const double dms = (sl.onset - st * sps) / sr * 1000.0;     // + = after the centre
                // +-6 ms: one 5.8 ms analysis hop plus the snap to a zero crossing
                const double outside = dms < -28.0 ? -28.0 - dms : dms > 18.0 ? dms - 18.0 : 0.0;
                best = std::min(best, st == 0 ? std::abs(dms) : outside);
            }
            if (best == 0.0) ++found;
            worst = std::max(worst, best);
        }
        check(found == (int) changeSteps.size() && (int) dm->slices.size() == (int) changeSteps.size(),
              tag + "melodic: finds all 8 smooth chord changes, no extras (" + std::to_string(dm->slices.size()) + " slices)", (double) found);
        check(worst == 0.0, tag + "melodic: every cut inside its chord crossfade (ms outside, worst)", worst);
        AnalysisSettings drm;
        auto dd = analyse(l, l, sr, drm);
        check(dd->slices.size() < 4, tag + "drums mode finds no hits in the same smooth chords (" + std::to_string(dd->slices.size()) + ")");

        // one held note with vibrato: melodic must not split it
        std::vector<float> vib((size_t) len);
        double ph = 0.0;
        for (int i = 0; i < len; ++i) { const double f = midiHz(64) * std::pow(2.0, 0.3 / 12.0 * std::sin(2.0 * pi * 5.5 * i / sr)); ph += 2.0 * pi * f / sr; vib[(size_t) i] = 0.4f * (float) std::sin(ph); }
        auto dv = analyse(vib, vib, sr, mel);
        check(dv->slices.size() == 1, tag + "melodic: a note with vibrato stays one slice (" + std::to_string(dv->slices.size()) + ")");

        // grid: chops every 16th / 8th, each cut on a zero crossing
        AnalysisSettings g16; g16.material = Material::grid; g16.autoAlign = false;
        auto dg = analyse(l, l, sr, g16);
        bool zc = true;
        for (size_t k = 1; k < dg->slices.size(); ++k)
        {
            const int c = dg->slices[k].onset;
            zc &= (l[(size_t) c - 1] <= 0.0f) != (l[(size_t) c] <= 0.0f) || std::abs(l[(size_t) c]) < 0.02f;
        }
        check(dg->slices.size() == 32, tag + "grid 16ths: 32 chops in 2 bars", (double) dg->slices.size());
        check(zc, tag + "grid: every cut lands on a zero crossing");
        AnalysisSettings g8 = g16; g8.gridDiv = 2;
        check(analyse(l, l, sr, g8)->slices.size() == 16, tag + "grid 8ths: 16 chops");

        // auto-align: a drum loop whose every hit is 25 ms late
        std::vector<int> st; for (int i = 0; i < 32; i += 2) st.push_back(i);
        std::vector<float> dl, dr;
        std::vector<double> late; for (int x : st) late.push_back(x + 0.025 / (60.0 / bpm / 4.0));
        makeLoop(sr, bpm, 2, late, dl, dr);
        auto da = analyse(dl, dr, sr, {});
        const double msStep = 60000.0 / bpm / 4.0;
        check(std::abs(da->alignSteps * msStep - 25.0) < 1.5, tag + "auto-align: finds the 25 ms offset", da->alignSteps * msStep);
        double off = 0.0; for (size_t k = 1; k < da->slices.size(); ++k) off = std::max(off, std::abs(da->slices[k].gridStep - std::round(da->slices[k].gridStep)));
        check(off < 0.02, tag + "auto-align: every hit back on its 16th (worst, 16ths)", off);
        AnalysisSettings noAlign; noAlign.autoAlign = false;
        check(std::abs(analyse(dl, dr, sr, noAlign)->alignSteps) < 1e-9, tag + "auto-align off leaves the loop alone");

        // shift: whole 16ths
        {
            GrooveSettings sh; sh.groove = Groove::straight; sh.tighten = 1.0f; sh.accent = 0.0f; sh.shiftSteps = 2;
            Slice sl; sl.gridStep = 4.0;
            check(std::abs(place(sl, 3, sh, grooveTemplate(Groove::straight), 150.0).step - 6.0) < 1e-9, tag + "shift +2 moves a hit two 16ths later");
        }

        // hold: a sustained melody, note 2 moved late - with Hold the gap is filled, without it it is silent
        std::vector<float> mel2((size_t) len, 0.0f);
        const std::vector<int> noteSteps { 0, 8, 16, 24 };
        const std::vector<int> notes { 57, 60, 64, 62 };
        double phase = 0.0;      // continuous phase: the source itself has no clicks
        for (size_t k = 0; k < noteSteps.size(); ++k)
        {
            const int a = (int) std::lround(noteSteps[k] * sps), b = k + 1 < noteSteps.size() ? (int) std::lround(noteSteps[k + 1] * sps) : len;
            for (int i = a; i < b; ++i) { phase += 2.0 * pi * midiHz(notes[k]) / sr; mel2[(size_t) i] = 0.5f * (float) std::sin(phase); }
        }
        auto dh = analyse(mel2, mel2, sr, mel);
        check(dh->slices.size() == 4, tag + "melodic: 4 notes, 4 slices", (double) dh->slices.size());
        GrooveTemplate lateT; lateT.offset[8] = 0.45f;       // note 2 (16th 8) lands almost half a 16th late
        for (bool holdOn : { false, true })
        {
            GrooveSettings gs; gs.groove = Groove::learned; gs.learned = lateT; gs.tighten = 1.0f; gs.accent = 0.0f; gs.hold = holdOn; gs.softCuts = true;
            Player pl; pl.prepare(sr, 256); pl.update(*dh, gs, bpm);
            const double spsHost = bpm / 60.0 * 4.0 / sr;
            const int total = (int) std::floor(32 / spsHost) - 1;
            std::vector<float> L((size_t) total, 0.0f), R((size_t) total, 0.0f);
            for (int pos = 0; pos < total; pos += 256) pl.render(*dh, L.data() + pos, R.data() + pos, std::min(256, total - pos), pos * spsHost, spsHost, 1.0f);
            // the gap: from the end of note 1's audio to the late note 2
            const int g0 = (int) (8.0 / spsHost) + (int) (0.012 * sr), g1 = (int) (8.40 / spsHost);
            double e = 0.0; for (int i = g0; i < g1; ++i) e += (double) L[(size_t) i] * L[(size_t) i];
            const double rms = std::sqrt(e / std::max(1, g1 - g0));
            float jump = 0.0f; for (int i = 1; i < total; ++i) jump = std::max(jump, std::abs(L[(size_t) i] - L[(size_t) i - 1]));
            const float sineSlope = (float) (2.0 * pi * midiHz(64) / sr * 0.5);
            if (holdOn)
            {
                check(rms > 0.25, tag + "hold: the note sustains through the gap (RMS)", rms);
                check(jump < 2.5f * sineSlope, tag + "hold: no clicks anywhere (largest sample jump vs a clean sine)", jump / sineSlope);
            }
            else
                check(rms < 0.02, tag + "hold off: the gap is clean silence (RMS)", rms);
        }
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
