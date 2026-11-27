// Epsilon Bounce 2.0 Pocket Engine tests: no JUCE. Build: cmake --build build --target BouncePocketTests && ctest
//
// The material is made the way the problem shows up in real sessions: a trap / drill drum loop with a
// late snare, swung hats and a triplet hat roll, and a melody made dead on the grid with plucks, a slow
// pad and a triplet figure. Every check compares against the drum loop's own timing.
#include "PocketEngine.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <string>

using namespace bounce;

static int failures = 0, checks = 0;
static void check(bool ok, const std::string& what, double value = 0.0)
{
    ++checks;
    std::printf("%s %-70s %10.3f\n", ok ? "PASS" : "FAIL", what.c_str(), value);
    if (! ok) ++failures;
}

#include "PocketMaterial.h"
using namespace pocket_material;
static std::shared_ptr<LoopData> cut(std::vector<float> l, std::vector<float> r, Material m)
{
    AnalysisSettings a;
    a.material = m;
    a.tempoHint = bpm;
    a.sensitivity = 0.6f;
    return analyse(std::move(l), std::move(r), sr, a);
}

int main()
{
    const double ms = sr / 1000.0;
    std::vector<float> dl, dr, ml, mr;
    makeDrums(2, dl, dr);
    std::vector<int> truthAttacks;
    makeMelody(2, ml, mr, &truthAttacks);
    const auto drums = cut(dl, dr, Material::drums);
    const auto mel = cut(ml, mr, Material::melodic);
    check(drums->bpm == bpm && mel->bpm == bpm, "both loops read on the project tempo (140)", drums->bpm);

    // ---------------------------------------------------------------- the leader's pocket map
    const auto dmono = pocket::monoOf(*drums);
    const auto dnotes = pocket::superFlux(dmono, sr, 0.5f, true);
    const auto map = pocket::buildMap(dnotes, pocket::gridOf(*drums));
    const double tk = tickS();
    auto devMs = [&](int t) { return map.dev[(size_t) t] * tk / ms; };
    check(map.valid, "drum pocket map built", map.notes);
    check(map.tripletBeats >= 2, "triplet hat roll read as triplets (beat 4, both bars)", map.tripletBeats);
    check(std::abs(devMs(12) - 14.0) < 2.5, "map: snare on 2 is 14 ms late", devMs(12));
    check(std::abs(devMs(36) - 14.0) < 2.5, "map: snare on 4 is 14 ms late", devMs(36));
    check(std::abs(devMs(3) - 18.0) < 2.5, "map: off-16th hats swung 18 ms", devMs(3));
    check(std::abs(devMs(0)) < 2.0, "map: the downbeat is on time", devMs(0));
    check(std::abs(devMs(40) - 10.0) < 2.5, "map: triplet hats laid back 10 ms", devMs(40));
    check(std::abs(devMs(28) - 10.0) < 3.0, "map: beat-3 triplet ticks borrow the triplet pocket", devMs(28));

    // ---------------------------------------------------------------- 1.x lock, for the record
    {
        const auto learned = learnGroove(*drums);
        GrooveSettings g; g.groove = Groove::learned; g.learned = learned.groove; g.amount = 1.0f; g.tighten = 1.0f;
        g.swing = 0.0f; g.accent = 0.0f;
        const double msPerStep = 60000.0 / bpm / 4.0;
        double worstTrip = 0.0;
        for (size_t i = 0; i < mel->slices.size(); ++i)
        {
            const auto& sl = mel->slices[i];
            const double tick = sl.onset / tk;
            const int t = ((int) std::lround(tick)) % 48;
            if (t == 28 || t == 32)
            {
                const auto p = place(sl, (int) i, g, learnGroove(*drums).groove, msPerStep);
                const double landTick = p.step * 3.0;              // 16th steps -> ticks
                worstTrip = std::max(worstTrip, std::abs(landTick - std::round(tick)) * tk / ms);
            }
        }
        check(worstTrip > 10.0, "1.x lock moved triplet notes off their triplet (the bug)", worstTrip);
    }

    // ---------------------------------------------------------------- 2.0 placement
    const auto mmono = pocket::monoOf(*mel);
    const auto mnotes = pocket::superFlux(mmono, sr, 0.5f, true);
    check((int) mnotes.size() == 2 * melodyNoteCount(), "SuperFlux finds every melody note (incl. pads, legato)", (double) mnotes.size());
    {
        double worst = 0.0, worstPad = 0.0;
        const auto pat = melodyPattern();
        for (size_t k = 0; k < truthAttacks.size(); ++k)
        {
            const int a = truthAttacks[k];
            double best = 1e9; for (const auto& n : mnotes) best = std::min(best, std::abs(n.at - a) / ms);
            (pat[k % pat.size()].kind == 1 ? worstPad : worst) = std::max(pat[k % pat.size()].kind == 1 ? worstPad : worst, best);
        }
        check(worst < 1.5, "SuperFlux: pluck attacks within 1.5 ms of the truth (worst, ms)", worst);
        check(worstPad < 9.0, "SuperFlux: pad (60 ms swell) starts within 9 ms (worst, ms)", worstPad);
    }
    pocket::Settings st;
    pocket::Report rep;
    const auto moves = pocket::placeNotes(mnotes, pocket::gridOf(*mel), map, st, &rep);
    double padPc = 0.0;
    for (const auto& n : mnotes) if (std::abs(n.at - truthAttacks[1]) < 8 * ms) padPc = n.pcSamples / ms;
    check(padPc > 18.0 && padPc < 34.0, "pad is heard ~27 ms after it starts (half its swell)", padPc);
    double worstPocket = 0.0;
    int tripCount = 0;
    for (const auto& mv : moves)
    {
        const int t = ((mv.tick % 48) + 48) % 48;
        tripCount += mv.triplet ? 1 : 0;
        // where the note is heard now vs where the drums are heard at that tick
        const auto it = std::find_if(mnotes.begin(), mnotes.end(), [&](const pocket::Note& n) { return n.at == mv.src; });
        const double heard = mv.dst + it->pcSamples;
        const double want = pocket::gridOf(*mel).sampleOf(mv.tick + map.dev[(size_t) t]);
        worstPocket = std::max(worstPocket, std::abs(heard - want) / ms);
    }
    check(worstPocket < 0.6, "2.0: every note is HEARD in the drums' pocket (worst, ms)", worstPocket);
    check(tripCount == 4, "2.0: the triplet figure stays triplets (2 per bar off the 16ths)", tripCount);
    {
        // the off-16th note picks up the swing, the pad starts early by its swell
        double swingMove = -1, padMove = 0;
        for (const auto& mv : moves)
        {
            const int t = ((mv.tick % 48) + 48) % 48;
            if (t == 21 && mv.src < 48 * tk) swingMove = (mv.dst - mv.src) / ms;
            if (t == 12 && mv.src < 48 * tk) padMove = (mv.dst - mv.src) / ms;
        }
        check(std::abs(swingMove - 18.0) < 2.0, "2.0: off-16th melody note takes the hats' 18 ms swing", swingMove);
        check(padMove < -8.0 && padMove > -22.0, "2.0: pad starts before the late snare so it is heard with it", padMove);
    }
    {
        pocket::Settings raw; raw.perceived = false;
        const auto rawNotes = pocket::superFlux(mmono, sr, 0.5f, false);
        const auto mv2 = pocket::placeNotes(rawNotes, pocket::gridOf(*mel), map, raw);
        double padMove = 0;
        for (const auto& mv : mv2) if (((mv.tick % 48) + 48) % 48 == 12 && mv.src < 48 * tk) padMove = (mv.dst - mv.src) / ms;
        check(padMove > 5.0, "perceived OFF: the pad lines up its onset (moves late, heard late)", padMove);
    }
    {
        pocket::Settings z; z.depth = 0.0f;
        const auto mv0 = pocket::placeNotes(mnotes, pocket::gridOf(*mel), map, z);
        double worst = 0.0;
        for (size_t i = 0; i < mv0.size(); ++i)
            worst = std::max(worst, std::abs(mv0[i].dst + mnotes[i].pcSamples - pocket::gridOf(*mel).sampleOf(mv0[i].tick)) / ms);
        check(worst < 0.6, "Depth 0: lattice-quantised by heard timing (worst, ms)", worst);
    }

    // ---------------------------------------------------------------- the whole lock: warped audio
    {
        pocket::Report r;
        const auto t0 = std::chrono::steady_clock::now();
        const auto out = pocket::lockToPocket(*mel, *drums, st, &r);
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(out != nullptr && out->length == mel->length, "locked melody keeps its exact length", out ? out->length : 0);
        check(el < 2.0, "2-bar lock time (s)", el);
        // the notes are really there in the audio now
        const auto outNotes = pocket::superFlux(pocket::monoOf(*out), sr, 0.5f, false);
        double worst = 0.0;
        for (const auto& mv : moves)
        {
            double best = 1e9; for (const auto& n : outNotes) best = std::min(best, std::abs(n.at - mv.dst) / ms);
            worst = std::max(worst, best);
        }
        check(worst < 2.5, "warped audio: every attack where the pocket put it (worst, ms)", worst);
        // level kept (no dips at the joins), no clicks: biggest sample-to-sample jump vs the source's
        double ein = 0, eout = 0;
        for (int i = 1; i < mel->length; ++i) { ein += mel->left[(size_t) i] * mel->left[(size_t) i]; eout += out->left[(size_t) i] * out->left[(size_t) i]; }
        const double db = 10.0 * std::log10(eout / ein);
        check(std::abs(db) < 0.7, "warped audio: level within 0.7 dB", db);
        // clicks: the melody has nothing above ~1 kHz, so anything loud above 8 kHz is a click at a join
        auto hfPeak = [&](const std::vector<float>& x)
        {
            const float c = (float) std::exp(-2.0 * pi * 8000.0 / sr);
            float a = 0, b = 0, pk = 0;
            for (float v : x) { a = c * a + (1 - c) * v; b = c * b + (1 - c) * a; pk = std::max(pk, std::abs(v - b)); }
            return pk;
        };
        const float hfIn = hfPeak(mel->left), hfOut = hfPeak(out->left);
        check(hfOut < 2.0f * hfIn + 0.004f, "warped audio: no clicks at the joins (peak above 8 kHz, out/in)", hfOut / std::max(1e-6f, hfIn));
        // pitch kept: the sustained pad on beat 2 still reads E4 (329.6 Hz) by zero crossings
        {
            const int a = (int) (truthAttacks[1] + 0.07 * sr), len = (int) (0.12 * sr);
            double bestR = -1; int bestLag = 1;
            for (int lag = (int) (sr / 400); lag <= (int) (sr / 250); ++lag)
            {
                double c = 0, e0 = 0, e1 = 0;
                for (int i = 0; i < len; ++i) { const float x = out->left[(size_t) (a + i)], y = out->left[(size_t) (a + i + lag)]; c += x * y; e0 += x * x; e1 += y * y; }
                const double rr = c / std::sqrt(e0 * e1 + 1e-12);
                if (rr > bestR) { bestR = rr; bestLag = lag; }
            }
            const double hz = sr / bestLag;
            check(std::abs(hz - 329.63) < 4.0, "warped audio: pitch kept (pad still E4, Hz)", hz);
        }
        check(r.barOne.shift16 == 0, "bar 1 finder: melody cut on bar 1 keeps Shift 0", r.barOne.shift16);
        // the slices follow: played at their places they reproduce the warp
        bool ordered = true;
        for (size_t i = 1; i < out->slices.size(); ++i) ordered &= out->slices[i].onset > out->slices[i - 1].onset && out->slices[i].start >= out->slices[i - 1].start;
        check(ordered && out->slices.size() == mel->slices.size(), "slices carried through the warp, in order", (double) out->slices.size());
    }

    // ---------------------------------------------------------------- bar 1 finder
    {
        std::vector<float> pl, pr;
        makeDrums(2, pl, pr, 4 * 3);      // a percussion loop cut a beat late
        const auto perc = cut(pl, pr, Material::drums);
        const auto pn = pocket::superFlux(pocket::monoOf(*perc), sr, 0.5f, true);
        const auto g = pocket::findBarOne(pn, pocket::gridOf(*perc), map);
        check(g.shift16 == -4 || g.shift16 == 12, "bar 1 finder: a loop cut one beat late -> Shift -4", g.shift16);
        const auto g0 = pocket::findBarOne(dnotes, pocket::gridOf(*drums), map);
        check(g0.shift16 == 0, "bar 1 finder: the same loop on bar 1 -> 0", g0.shift16);
    }

    // ---------------------------------------------------------------- drums follow a melody
    {
        std::vector<float> sl, sr2;
        makeDrums(2, sl, sr2);
        // straight, quantised drums following a melody: the triplet hats must stay triplets
        const auto d2 = cut(sl, sr2, Material::drums);
        pocket::Settings z; z.depth = 0.0f;
        const auto out = pocket::lockToPocket(*d2, *mel, z);
        int trip = 0;
        for (const auto& s : out->slices)
        {
            const double t = s.gridStep * 3.0;
            const int tt = ((int) std::lround(t) % 12 + 12) % 12;
            if ((tt == 4 || tt == 8 || tt == 2 || tt == 10) && std::abs(t - std::round(t)) < 0.3) ++trip;
        }
        check(trip >= 6, "drums following: triplet roll kept on triplets (hit by hit)", trip);
        check(out->left == d2->left, "drums following: audio untouched (hits just move)", 0);
    }

    // ---------------------------------------------------------------- a long melody: time
    {
        std::vector<float> l, r;
        makeMelody(32, l, r);
        std::vector<float> l2, r2;
        makeDrums(32, l2, r2);
        const auto longMel = cut(l, r, Material::melodic);
        const auto longDr = cut(l2, r2, Material::drums);
        const auto t0 = std::chrono::steady_clock::now();
        const auto out = pocket::lockToPocket(*longMel, *longDr, st);
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(out != nullptr, "32-bar melody (55 s) locked", 0);
        check(el < 12.0, "32-bar lock time on one core (s)", el);
    }

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
