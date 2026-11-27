#pragma once
// Epsilon Bounce 2.0 - Pocket Engine. No JUCE in here, so the tests build without it.
//
// What 1.x did: the leader's feel was averaged into one offset per 16th, the follower was snapped to
// the nearest 16th (Tighten 100 %) and its slices were re-triggered there. Three things went wrong
// with real material, and each one is fixed here with a technique from the research:
//
//  1. Triplets were destroyed. A drill / trap melody note on a triplet was snapped to the nearest 16th.
//     -> A 12-ticks-per-beat lattice: every beat is read as straight 16ths or as 16th-note triplets,
//        whichever its notes fit (for the leader and the follower separately), and the pocket is kept
//        per tick. Straight notes get the straight pocket, triplet notes the triplet pocket.
//
//  2. Notes were lined up by their acoustic onset, not by where you hear them. A pad or a bowed sample
//     with a 60 ms swell is heard well after it starts, so it sounded late even when it was "on time".
//     -> Perceptual centres (P-centres; Danielsen et al. 2019, London et al. 2019): the moment a sound
//        is heard to land moves later with a slower attack. Each note's P-centre is estimated from its
//        rise time and the P-centres are lined up, follower to leader.
//
//  3. Melodies were cut into slices and re-triggered, so a moved note left a gap or a looped tail.
//     -> Elastic warp: the audio between two notes is time-stretched (the phase vocoder with identity
//        phase locking from TempoMatch.h) so every note lands on its target and the sound between them
//        never stops. Each note's attack is copied untouched. Moves are a few ms, so the stretch is
//        within +-10 % and inaudible. Drums keep moving hit by hit (their slices are clean).
//
// Note onsets for melodies come from SuperFlux (Boeck & Widmer, DAFx 2013): spectral flux on a log-
// frequency filterbank with a maximum filter over frequency, which ignores vibrato and slides and
// still catches a new note on a held chord.
//
// Research also shows exaggerated microtiming lowers groove ratings while a performance's own microtiming
// and a fully quantised one rate the same (Senn et al. 2016, Frontiers in Psychology). So Depth is capped
// at 100 % of the leader's pocket, and with Depth at 0 the follower is simply quantised (lattice-aware).

#include "BounceEngine.h"
#include "TempoMatch.h"

namespace bounce
{
namespace pocket
{
constexpr int ticksPerBeat = 12;                 // 16th = 3 ticks, 16th-triplet = 2 ticks
constexpr int ticksPerBar = ticksPerBeat * 4;

struct Note
{
    int at = 0;              // attack start (samples)
    float strength = 0.0f;   // peak level in the first 30 ms
    float pcSamples = 0.0f;  // perceptual centre after the attack start (samples)
    float low = 0.0f;        // share of energy under ~150 Hz in the first 40 ms (kicks, 808s)
    float novelty = 1.0f;    // how strongly the spectrum changed there (0..1, the onset curve's peak)
    double pc() const { return at + (double) pcSamples; }
};

//==============================================================================
namespace detail
{
// |x| with a trailing max over w samples, so a sounding note's zero crossings don't read as silence.
// A view of samples [base, base + count) of a signal: x(i) for absolute i.
struct View
{
    const float* p = nullptr; int base = 0, count = 0;
    float operator()(int i) const { return p[i - base]; }
    int begin() const { return base; }
    int end() const { return base + count; }
};

inline void envelope(const View& x, int from, int to, int w, std::vector<float>& env)
{
    from = std::clamp(from, x.begin(), x.end()); to = std::clamp(to, from, x.end());
    env.assign((size_t) (to - from), 0.0f);
    std::vector<int> dq; dq.reserve((size_t) (to - from + w));
    size_t head = 0;
    for (int i = std::max(x.begin(), from - w); i < to; ++i)
    {
        const float v = std::abs(x(i));
        while (dq.size() > head && std::abs(x(dq.back())) <= v) dq.pop_back();
        if (dq.size() < head) head = dq.size();
        dq.push_back(i);
        while (dq[head] <= i - w) ++head;
        if (i >= from) env[(size_t) (i - from)] = std::abs(x(dq[head]));
    }
}

// A note's shape around a rough onset time: where its attack really starts and where it is heard.
// lo / hi bound the search (the neighbouring notes). The envelope is |x| with a 2 ms trailing max.
//  - peak:   the loudest point within 120 ms after the rough onset (a pluck peaks at once, a pad later)
//  - before: the quietest point between lo and the peak (the level the note rises from)
//  - attack: walking back from the peak, the last moment under before + 5 % of the rise
//  - heard:  the first moment at or over before + 50 % of the rise. P-centre research (Danielsen et al.
//            2019) finds attack time the main cue: a fast hit is heard at its onset, a swell well into
//            its rise. Capped 40 ms after the attack.
struct Shape { int at = 0; float peak = 0.0f; float pc = 0.0f; bool rises = false; };

inline Shape shapeAt1(const View& mono, int approx, int lo, int hi, double sr)
{
    Shape sh;
    approx = std::clamp(approx, lo, hi - 1);
    // two envelopes: a 2 ms hold finds the attack sharply (the previous note's last moment doesn't hide
    // this one's start); a 6 ms hold is smooth through a low note's own cycles and gives the level curve
    std::vector<float> sharp, env;
    envelope(mono, lo, hi, std::max(8, (int) (0.002 * sr)), sharp);
    envelope(mono, lo, hi, std::max(8, (int) (0.006 * sr)), env);
    const int p0 = std::max(lo, approx - (int) (0.030 * sr)), p1 = std::min(hi, approx + (int) (0.120 * sr));
    int pkAt = approx; float pk = 0.0f;
    for (int i = p0; i < p1; ++i) if (env[(size_t) (i - lo)] > pk) { pk = env[(size_t) (i - lo)]; pkAt = i; }
    float before = pk, beforeS = pk;
    for (int i = lo; i <= pkAt; ++i) { before = std::min(before, env[(size_t) (i - lo)]); beforeS = std::min(beforeS, sharp[(size_t) (i - lo)]); }
    sh.peak = pk;
    sh.at = approx;
    if (pk <= 1e-6f || pk - before < 0.1f * pk) return sh;                // no real rise (a legato change)
    sh.rises = true;
    const float t5 = beforeS + 0.05f * (pk - beforeS), t50 = before + 0.5f * (pk - before);
    int at = lo;
    for (int i = pkAt; i > lo; --i) if (sharp[(size_t) (i - 1 - lo)] < t5) { at = i; break; }
    sh.at = at;
    int heard = pkAt;
    for (int i = at; i <= pkAt; ++i) if (env[(size_t) (i - lo)] >= t50) { heard = i; break; }
    sh.pc = (float) std::clamp((double) (heard - at), 0.0, 0.040 * sr);
    return sh;
}

// A hit under a louder ringing sound (a hat over an 808 tail) has no rise in the full-band envelope, but a
// clear one above 2 kHz. Both are measured; the one where the note rises more clearly decides.
inline Shape shapeAt(const std::vector<float>& mono, int approx, int lo, int hi, double sr)
{
    const int n = (int) mono.size();
    lo = std::clamp(lo, 0, std::max(0, n - 1)); hi = std::clamp(hi, lo + 1, n);
    const View fv { mono.data(), 0, n };
    const auto full = shapeAt1(fv, approx, lo, hi, sr);
    // 2nd-order high-pass (a signal minus two one-pole low-pass stages), over the window only
    const int a0 = std::max(0, lo - (int) (0.005 * sr));
    std::vector<float> hp((size_t) (hi - a0), 0.0f);
    const float c = (float) std::exp(-2.0 * pi * 2000.0 / sr);
    float l1 = 0.0f, l2 = 0.0f;
    for (int i = a0; i < hi; ++i)
    {
        l1 = c * l1 + (1.0f - c) * mono[(size_t) i];
        l2 = c * l2 + (1.0f - c) * l1;
        hp[(size_t) (i - a0)] = mono[(size_t) i] - l2;
    }
    const View hv { hp.data(), a0, hi - a0 };
    const auto high = shapeAt1(hv, approx, lo, hi, sr);
    auto rise = [&](const View& x, const Shape& sh)
    {
        // how clearly it rises: level just after the attack vs just before it
        const int w = std::max(8, (int) (0.003 * sr));
        double b = 0, a = 0;
        for (int i = std::max(lo, sh.at - 2 * w); i < sh.at; ++i) b = std::max(b, (double) std::abs(x(i)));
        for (int i = sh.at; i < std::min(hi, sh.at + 4 * w); ++i) a = std::max(a, (double) std::abs(x(i)));
        return a / (b + 1e-9);
    };
    Shape out = rise(hv, high) > 2.0 * rise(fv, full) ? high : full;
    out.peak = full.peak;                       // level is always the full-band one
    out.rises = full.rises || high.rises;
    return out;
}

// Energy under ~150 Hz over the first 40 ms vs all of it (one-pole low-pass, twice).
inline float lowShare(const std::vector<float>& mono, int at, double sr)
{
    const int n = (int) mono.size(), to = std::min(n, at + (int) (0.04 * sr));
    const float a = (float) std::exp(-2.0 * pi * 150.0 / sr);
    float s1 = 0.0f, s2 = 0.0f; double lowE = 0.0, allE = 0.0;
    for (int i = at; i < to; ++i)
    {
        const float x = mono[(size_t) i];
        s1 = a * s1 + (1.0f - a) * x; s2 = a * s2 + (1.0f - a) * s1;
        lowE += (double) s2 * s2; allE += (double) x * x;
    }
    return allE > 1e-12 ? (float) std::min(1.0, lowE / allE) : 0.0f;
}
} // namespace detail

inline std::vector<float> monoOf(const LoopData& d)
{
    std::vector<float> m((size_t) d.length);
    for (int i = 0; i < d.length; ++i) m[(size_t) i] = 0.5f * (d.left[(size_t) i] + d.right[(size_t) i]);
    return m;
}

// Rough onset times (sorted) -> notes with their real attack, level and heard time.
inline std::vector<Note> shapeNotes(const std::vector<float>& mono, const std::vector<int>& approx, double sr, bool perceived,
                                    const std::vector<float>* novelty = nullptr)
{
    std::vector<Note> v;
    const int n = (int) mono.size();
    for (size_t i = 0; i < approx.size(); ++i)
    {
        const int lo = i == 0 ? 0 : std::max(approx[i - 1] + (int) (0.015 * sr), approx[i] - (int) (0.080 * sr));
        const int hi = i + 1 < approx.size() ? std::max(approx[i] + 8, approx[i + 1] - (int) (0.002 * sr)) : n;
        const auto sh = detail::shapeAt(mono, approx[i], std::min(lo, approx[i]), hi, sr);
        Note nt;
        nt.at = sh.at; nt.strength = sh.peak; nt.pcSamples = perceived ? sh.pc : 0.0f;
        nt.low = detail::lowShare(mono, nt.at, sr);
        nt.novelty = novelty != nullptr ? (*novelty)[i] : 1.0f;
        // no rise in level: a legato note change counts only when the spectrum changed clearly
        if (! sh.rises && nt.novelty < 0.3f) continue;
        if (! v.empty() && nt.at - v.back().at < (int) (0.025 * sr)) { if (nt.strength > v.back().strength) v.back() = nt; continue; }
        v.push_back(nt);
    }
    return v;
}

//==============================================================================
// SuperFlux onsets (Boeck & Widmer 2013). 200 frames/s, 24 bands per octave from 30 Hz to 16 kHz,
// log magnitude, maximum filter of 3 bands on the frame 2 hops back, positive differences summed.
// Peak picking: local max in +-30 ms, above the mean of -100..+70 ms by delta, 30 ms apart.
inline std::vector<Note> superFlux(const std::vector<float>& mono, double sr, float sensitivity = 0.5f,
                                   bool perceived = true, const std::atomic<bool>* cancel = nullptr)
{
    std::vector<Note> notes;
    const int n = (int) mono.size();
    if (n < 2048) return notes;
    const int N = sr > 60000.0 ? 4096 : 2048;
    const int hop = std::max(1, (int) std::lround(sr / 200.0));
    const int frames = 1 + (n - 1) / hop;
    const int bins = N / 2 + 1;
    // band of each FFT bin (-1 = outside 30 Hz - 16 kHz); bands too narrow for a bin merge with the next
    std::vector<int> bandOf((size_t) bins, -1);
    int bands = 0;
    {
        int lastBand = -1;
        for (int k = 1; k < bins; ++k)
        {
            const double hz = k * sr / N;
            if (hz < 30.0 || hz > 16000.0) continue;
            const int b = (int) std::floor(24.0 * std::log2(hz / 30.0));
            if (b != lastBand) { ++bands; lastBand = b; }
            bandOf[(size_t) k] = bands - 1;
        }
    }
    if (bands < 4) return notes;
    std::vector<float> win((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * (float) std::cos(2.0 * pi * i / N);
    const bounce::detail::FFT fft((size_t) N);
    std::vector<std::complex<float>> buf((size_t) N);
    // two lags: 2 frames (10 ms, the SuperFlux setting: sharp attacks) and 8 frames (40 ms: swells, pads and
    // bowed notes, whose rise is spread too thin for a 10 ms difference to see)
    constexpr int lag = 2, slowLag = 8, keepN = slowLag + 1;
    std::vector<std::vector<float>> hist(keepN, std::vector<float>((size_t) bands, 0.0f));
    std::vector<float> flux((size_t) frames, 0.0f), slow((size_t) frames, 0.0f), maxf((size_t) bands), zeros((size_t) bands, 0.0f);
    for (int f = 0; f < frames; ++f)
    {
        if ((f & 127) == 0 && cancel != nullptr && cancel->load(std::memory_order_relaxed)) return {};
        const int c = f * hop;
        for (int i = 0; i < N; ++i)
        {
            const int idx = c - N / 2 + i;
            buf[(size_t) i] = { (idx >= 0 && idx < n ? mono[(size_t) idx] : 0.0f) * win[(size_t) i], 0.0f };
        }
        fft(buf);
        auto& cur = hist[(size_t) (f % keepN)];
        std::fill(cur.begin(), cur.end(), 0.0f);
        for (int k = 1; k < bins; ++k)
            if (bandOf[(size_t) k] >= 0) cur[(size_t) bandOf[(size_t) k]] += bounce::detail::mag(buf[(size_t) k]);
        for (auto& v : cur) v = std::log10(1.0f + v);
        for (int pass = 0; pass < 2; ++pass)
        {
            // frames before the start compare against silence, so a note right at sample 0 is found
            const int L = pass == 0 ? lag : slowLag;
            const auto& old = f >= L ? hist[(size_t) ((f - L) % keepN)] : zeros;
            for (int b = 0; b < bands; ++b)
                maxf[(size_t) b] = std::max({ old[(size_t) b], old[(size_t) std::max(0, b - 1)], old[(size_t) std::min(bands - 1, b + 1)] });
            float s = 0.0f;
            for (int b = 0; b < bands; ++b) s += std::max(0.0f, cur[(size_t) b] - maxf[(size_t) b]);
            (pass == 0 ? flux : slow)[(size_t) f] = s;
        }
    }
    auto pick = [&](std::vector<float>& x, double win, double avgPre, double avgPost, float d)
    {
        std::vector<int> pk;
        float mx = 0.0f; for (auto v : x) mx = std::max(mx, v);
        if (mx <= 0.0f) return pk;
        for (auto& v : x) v /= mx;
        const int wMax = (int) std::lround(win * sr / hop);
        const int preAvg = (int) std::lround(avgPre * sr / hop), postAvg = (int) std::lround(avgPost * sr / hop);
        for (int f = 0; f < frames; ++f)
        {
            const float v = x[(size_t) f];
            if (v <= 0.0f) continue;
            bool isMax = true;
            for (int k = std::max(0, f - wMax); k <= std::min(frames - 1, f + wMax) && isMax; ++k) if (x[(size_t) k] > v) isMax = false;
            if (! isMax) continue;
            double mean = 0.0; int cnt = 0;
            for (int k = std::max(0, f - preAvg); k <= std::min(frames - 1, f + postAvg); ++k) { mean += x[(size_t) k]; ++cnt; }
            if (v < mean / std::max(1, cnt) + d) continue;
            if (! pk.empty() && f - pk.back() <= wMax) continue;
            pk.push_back(f);
        }
        return pk;
    };
    const float delta = 0.10f - 0.08f * std::clamp(sensitivity, 0.0f, 1.0f);
    // peak picking as in the paper: max over +-30 ms, mean over -100..+70 ms, 30 ms apart
    auto peaks = pick(flux, 0.030, 0.100, 0.070, delta);
    // swells the fast curve missed: their own peaks, wider windows, at least 60 ms from any found note
    for (int f : pick(slow, 0.060, 0.200, 0.150, delta * 1.5f))
    {
        bool near = false;
        for (int p : peaks) if (std::abs(p - f) * hop < (int) (0.060 * sr)) { near = true; break; }
        if (! near) peaks.push_back(-(f - slowLag / 2) - 1);   // the slow difference peaks mid-swell (negative = from the slow curve)
    }
    std::sort(peaks.begin(), peaks.end(), [](int a, int b) { return (a < 0 ? -a - 1 : a) < (b < 0 ? -b - 1 : b); });
    std::vector<int> approx;
    std::vector<float> nov;
    for (int f : peaks)
    {
        const bool fromSlow = f < 0;
        const int fr = std::max(0, fromSlow ? -f - 1 : f);
        approx.push_back(fr * hop);                      // the flux peaks when the new energy reaches the frame centre
        nov.push_back(fromSlow ? slow[(size_t) std::min(frames - 1, fr + slowLag / 2)] : flux[(size_t) fr]);
    }
    notes = shapeNotes(mono, approx, sr, perceived, &nov);
    float top = 0.0f; for (const auto& nt : notes) top = std::max(top, nt.strength);
    // drop the faintest flickers (reverb wash, noise): under 4 % of the loudest note
    notes.erase(std::remove_if(notes.begin(), notes.end(), [top](const Note& x) { return x.strength < 0.04f * top; }), notes.end());
    return notes;
}

// Drums are already cut on their hits: one note per slice.
inline std::vector<Note> notesFromSlices(const LoopData& d, const std::vector<float>& mono, bool perceived)
{
    // one note per slice, in slice order (a merged flam keeps its slice: shaped on its loudest hit)
    std::vector<Note> v;
    const int n = (int) mono.size();
    for (size_t i = 0; i < d.slices.size(); ++i)
    {
        const auto& s = d.slices[i];
        const int hi = i + 1 < d.slices.size() ? d.slices[i + 1].onset : n;
        const auto sh = detail::shapeAt(mono, s.onset, s.start, std::max(s.onset + 8, hi), d.sampleRate);
        Note nt; nt.at = s.onset; nt.strength = std::max(s.peak, sh.peak);
        // the slice keeps its own start; it is heard where its loudest hit reaches half its level
        nt.pcSamples = perceived ? (float) std::max(0, sh.at - s.onset) + sh.pc : 0.0f;
        nt.low = detail::lowShare(mono, nt.at, d.sampleRate);
        v.push_back(nt);
    }
    return v;
}

//==============================================================================
// The lattice. Each beat is read either as straight 16ths (ticks 0, 3, 6, 9) or as 16th triplets
// (ticks 0, 2, 4, 6, 8, 10), whichever its notes fit better. Straight wins ties: a beat goes triplet
// only when at least one note sits on a triplet-only tick (2, 4, 8, 10) and the fit is clearly better.
struct Grid
{
    double tick = 1.0;       // samples per tick
    double origin = 0.0;     // sample of tick 0 (the loop's own auto-align)
    int bars = 1;
    double ticksOf(double sample) const { return (sample - origin) / tick; }
    double sampleOf(double ticks) const { return origin + ticks * tick; }
};

inline Grid gridOf(const LoopData& d)
{
    Grid g;
    g.tick = d.samplesPerStep() * 4.0 / ticksPerBeat;
    g.origin = d.alignSteps * d.samplesPerStep();
    g.bars = std::max(1, d.bars);
    return g;
}

// For every note: the lattice tick it belongs to (absolute, from the loop start). Returns per-beat flags too.
inline std::vector<int> quantise(const std::vector<double>& posTicks, std::vector<bool>* tripletBeat = nullptr)
{
    std::vector<int> q(posTicks.size(), 0);
    if (posTicks.empty()) return q;
    double maxPos = 0.0; for (auto p : posTicks) maxPos = std::max(maxPos, p);
    const int beats = (int) std::floor(maxPos / ticksPerBeat + 0.5) + 2;
    std::vector<double> errS((size_t) beats, 0.0), errT((size_t) beats, 0.0);
    std::vector<int> tripletOnly((size_t) beats, 0), count((size_t) beats, 0);
    auto nearest = [](double p, int step) { return std::round(p / step) * step; };
    for (auto p : posTicks)
    {
        const int b = std::clamp((int) std::floor((p + 1.0) / ticksPerBeat), 0, beats - 1);   // a note 1 tick early still counts as this beat
        const double s = nearest(p, 3), t = nearest(p, 2);
        errS[(size_t) b] += std::abs(p - s);
        errT[(size_t) b] += std::abs(p - t);
        const int tt = (((int) t % ticksPerBeat) + ticksPerBeat) % ticksPerBeat;
        if ((tt == 4 || tt == 8) && std::abs(p - t) < 0.6) ++tripletOnly[(size_t) b];
        ++count[(size_t) b];
    }
    std::vector<bool> trip((size_t) beats, false);
    for (int b = 0; b < beats; ++b)
        trip[(size_t) b] = count[(size_t) b] > 0 && tripletOnly[(size_t) b] > 0 && errT[(size_t) b] < 0.6 * errS[(size_t) b];
    for (size_t i = 0; i < posTicks.size(); ++i)
    {
        const double p = posTicks[i];
        const int b = std::clamp((int) std::floor((p + 1.0) / ticksPerBeat), 0, beats - 1);
        q[i] = (int) (trip[(size_t) b] ? nearest(p, 2) : nearest(p, 3));
    }
    if (tripletBeat != nullptr) *tripletBeat = trip;
    return q;
}

//==============================================================================
// The leader's pocket: for each tick of the bar, how far (in ticks, + = late) its notes' P-centres sit
// from the lattice, averaged over its bars and weighted by level. Ticks it never plays borrow from the
// same role (beat / 8th / 16th / triplet) in the bar, then from their neighbours.
struct Map
{
    std::array<float, ticksPerBar> dev {};
    std::array<float, ticksPerBar> accentDb {};
    std::array<bool, ticksPerBar> heard {};
    std::array<float, stepsPerBar> stepProfile {};   // level per 16th, kicks weighted up (bar-1 finder)
    int notes = 0, tripletBeats = 0;
    bool valid = false;
};

inline int roleOf(int tick)   // 0 beat, 1 8th, 2 16th, 3 triplet
{
    const int t = ((tick % ticksPerBeat) + ticksPerBeat) % ticksPerBeat;
    return t == 0 ? 0 : t == 6 ? 1 : (t == 3 || t == 9) ? 2 : 3;
}

inline Map buildMap(const std::vector<Note>& notes, const Grid& g)
{
    Map m;
    std::vector<double> pos; pos.reserve(notes.size());
    for (const auto& nt : notes) pos.push_back(g.ticksOf(nt.pc()));
    std::vector<bool> trip;
    const auto q = quantise(pos, &trip);
    for (bool t : trip) m.tripletBeats += t ? 1 : 0;
    std::array<double, ticksPerBar> sum {}, w {}, lvl {};
    std::array<int, ticksPerBar> cnt {};
    double allLvl = 0.0; int used = 0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const double d = pos[i] - q[i];
        if (std::abs(d) > 1.4) continue;                     // too far from any lattice point: not a pocket
        const int t = ((q[i] % ticksPerBar) + ticksPerBar) % ticksPerBar;
        // level squared: where two hits share a tick (hat + snare), the loud one is the pocket
        const double wt = std::max(1e-6, (double) notes[i].strength * notes[i].strength);
        sum[(size_t) t] += d * wt; w[(size_t) t] += wt; lvl[(size_t) t] += wt; ++cnt[(size_t) t];
        allLvl += wt; ++used;
    }
    m.notes = used;
    if (used < 2) return m;
    // bar-1 profile: the strong hits only (kicks, snares, 808s), kicks weigh double
    float top = 0.0f; for (const auto& nt : notes) top = std::max(top, nt.strength);
    for (size_t i = 0; i < notes.size(); ++i)
        if (notes[i].strength >= 0.45f * top && q[i] % 3 == 0 && std::abs(pos[i] - q[i]) < 1.4)
            m.stepProfile[(size_t) ((((q[i] / 3) % stepsPerBar) + stepsPerBar) % stepsPerBar)] += notes[i].strength * (1.0f + notes[i].low);
    allLvl /= used;
    std::array<double, 4> roleSum {}, roleW {};
    for (int t = 0; t < ticksPerBar; ++t)
        if (w[(size_t) t] > 0.0)
        {
            m.dev[(size_t) t] = (float) (sum[(size_t) t] / w[(size_t) t]);
            m.heard[(size_t) t] = true;
            const double l = lvl[(size_t) t] / cnt[(size_t) t];
            m.accentDb[(size_t) t] = (float) std::clamp(20.0 * std::log10(l / allLvl) * 0.5, -6.0, 6.0);
            roleSum[(size_t) roleOf(t)] += m.dev[(size_t) t]; roleW[(size_t) roleOf(t)] += 1.0;
        }
    for (int t = 0; t < ticksPerBar; ++t)
    {
        if (m.heard[(size_t) t]) continue;
        const int r = roleOf(t);
        if (roleW[(size_t) r] > 0.0) { m.dev[(size_t) t] = (float) (roleSum[(size_t) r] / roleW[(size_t) r]); continue; }
        // nothing in this role: lean with the nearest heard ticks on either side (circular)
        int a = -1, b = -1;
        for (int k = 1; k < ticksPerBar && a < 0; ++k) if (m.heard[(size_t) ((t - k + ticksPerBar) % ticksPerBar)]) a = k;
        for (int k = 1; k < ticksPerBar && b < 0; ++k) if (m.heard[(size_t) ((t + k) % ticksPerBar)]) b = k;
        if (a > 0 && b > 0)
        {
            const float da = m.dev[(size_t) ((t - a + ticksPerBar) % ticksPerBar)], db = m.dev[(size_t) ((t + b) % ticksPerBar)];
            m.dev[(size_t) t] = (da * b + db * a) / (float) (a + b);
        }
    }
    m.valid = true;
    return m;
}

//==============================================================================
// Bar 1 finder: the follower's note profile against the leader's (kicks weigh more), over whole-16th
// shifts of -8..+8. Only a clear winner is suggested; ties keep 0 (most loops are cut on bar 1).
struct PhaseGuess { int shift16 = 0; float confidence = 0.0f; };

inline PhaseGuess findBarOne(const std::vector<Note>& follower, const Grid& gf, const Map& leader)
{
    PhaseGuess r;
    if (! leader.valid || follower.size() < 4) return r;
    std::array<float, stepsPerBar> fp {};
    for (const auto& nt : follower)
    {
        const double s = gf.ticksOf(nt.pc()) / 3.0;
        const int st = ((int) std::lround(s) % stepsPerBar + stepsPerBar) % stepsPerBar;
        if (std::abs(s - std::round(s)) < 0.35) fp[(size_t) st] += nt.strength * nt.strength;
    }
    auto score = [&](int k)
    {
        double a = 0.0, nf = 0.0, nl = 0.0;
        for (int s = 0; s < stepsPerBar; ++s)
        {
            const float lv = leader.stepProfile[(size_t) (((s + k) % stepsPerBar + stepsPerBar) % stepsPerBar)];
            a += fp[(size_t) s] * lv; nf += fp[(size_t) s] * fp[(size_t) s]; nl += lv * lv;
        }
        return nf > 0 && nl > 0 ? a / std::sqrt(nf * nl) : 0.0;
    };
    const double s0 = score(0);
    int best = 0; double bestS = s0;
    for (int k = -8; k <= 8; k += 2) { const double s = score(k); if (s > bestS + 1e-9) { bestS = s; best = k; } }   // 8ths and beats
    r.confidence = (float) (s0 > 1e-6 ? bestS / s0 : (bestS > 0 ? 10.0 : 1.0));
    if (best != 0 && r.confidence > 1.5f) r.shift16 = best;
    return r;
}

//==============================================================================
struct Settings
{
    float depth = 1.0f;          // 0..1 how much of the leader's pocket (0 = lattice-quantised)
    bool perceived = true;       // line up P-centres (heard timing) instead of raw onsets
    float sensitivity = 0.5f;    // follower note detection (melodies)
    int shift16 = 0;             // whole-16th move applied to the follower (Shift)
};

struct Move
{
    int src = 0;                 // the note's attack in the follower (samples)
    double dst = 0.0;            // where its attack goes
    int tick = 0;                // its lattice tick (absolute)
    bool triplet = false;
};

struct Report
{
    int notes = 0, moved = 0, tripletNotes = 0, leaderTripletBeats = 0;
    double meanMoveMs = 0.0, maxMoveMs = 0.0, perceivedMeanMs = 0.0;
    PhaseGuess barOne;
};

// Where each follower note goes: its P-centre onto the lattice, plus Depth x the leader's pocket there.
// The attack moves by the same amount (the P-centre is measured from it). Moves are capped at half a
// 16th: past that the note is in a different place in the bar, not a different pocket.
inline std::vector<Move> placeNotes(const std::vector<Note>& notes, const Grid& gf, const Map& leader, const Settings& s, Report* rep = nullptr)
{
    std::vector<Move> mv;
    std::vector<double> pos; pos.reserve(notes.size());
    for (const auto& nt : notes) pos.push_back(gf.ticksOf(nt.pc()));
    std::vector<bool> trip;
    const auto q = quantise(pos, &trip);
    const double cap = 1.5 * gf.tick;                          // half a 16th
    double sumMs = 0.0, maxMs = 0.0, pcMs = 0.0; int moved = 0, tripN = 0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        Move m;
        m.src = notes[i].at;
        m.tick = q[i];
        const int beat = std::clamp((int) std::floor((pos[i] + 1.0) / ticksPerBeat), 0, (int) trip.size() - 1);
        m.triplet = ! trip.empty() && trip[(size_t) beat] && (q[i] % 3 != 0);
        const int t = ((q[i] % ticksPerBar) + ticksPerBar) % ticksPerBar;
        const double dev = leader.valid ? std::clamp(s.depth, 0.0f, 1.0f) * leader.dev[(size_t) t] : 0.0;
        const double targetPc = gf.sampleOf(q[i] + dev);
        double shift = targetPc - notes[i].pc();
        shift = std::clamp(shift, -cap, cap);
        m.dst = notes[i].at + shift;
        if (std::abs(shift) > 0.5) ++moved;
        sumMs += std::abs(shift); maxMs = std::max(maxMs, std::abs(shift)); pcMs += notes[i].pcSamples;
        tripN += m.triplet ? 1 : 0;
        mv.push_back(m);
    }
    if (rep != nullptr)
    {
        rep->notes = (int) notes.size(); rep->moved = moved; rep->tripletNotes = tripN;
        rep->leaderTripletBeats = leader.tripletBeats;
        rep->meanMoveMs = notes.empty() ? 0.0 : sumMs / notes.size();   // in samples here; caller converts
        rep->maxMoveMs = maxMs;
        rep->perceivedMeanMs = notes.empty() ? 0.0 : pcMs / notes.size();
    }
    return mv;
}

//==============================================================================
// Elastic warp: piecewise time stretch so that src[i] lands on dst[i]. Every piece starts on a note
// attack; its first 12 ms are copied untouched (a phase vocoder would soften the hit), the rest is
// stretched by (dst[i+1]-dst[i]) / (src[i+1]-src[i]). Joins are 3 ms crossfades just before the next
// attack. Output length = input length.
inline bool warpAudio(const std::vector<float>& inL, const std::vector<float>& inR, double sr,
                      std::vector<std::pair<int, int>> marks, std::vector<float>& outL, std::vector<float>& outR,
                      const std::atomic<bool>* cancel = nullptr)
{
    const int n = (int) inL.size();
    outL.assign((size_t) n, 0.0f); outR.assign((size_t) n, 0.0f);
    std::sort(marks.begin(), marks.end());
    // keep pieces >= 25 ms on both sides, monotonic, and stretch factors within 0.6..1.6
    std::vector<std::pair<int, int>> m { { 0, 0 } };
    const int minLen = (int) (0.025 * sr);
    for (const auto& p : marks)
    {
        if (p.first <= 0 || p.first >= n - minLen) continue;
        const int ds = std::clamp(p.second, 0, n - 1);
        const int a = p.first - m.back().first, b = ds - m.back().second;
        if (a < minLen || b < minLen) continue;
        const double f = (double) b / a;
        if (f < 0.6 || f > 1.6) continue;
        m.push_back({ p.first, ds });
    }
    // the last piece runs to the end: make sure it is not squeezed too hard either
    while (m.size() > 1 && (n - m.back().second < minLen / 2 || (double) (n - m.back().second) / std::max(1, n - m.back().first) < 0.6)) m.pop_back();
    m.push_back({ n, n });

    // Joins: each piece (after the first) starts xf before its attack, copied untouched, and fades in over
    // those xf samples while the previous piece fades out over the same xf (raised cosine, sums to 1). Both carry the
    // end of the previous note there, so the join is seamless and the attack itself is never doubled.
    const int xf = std::max(16, (int) (0.003 * sr));
    std::vector<float> segL, segR, oL, oR;
    for (size_t c = 0; c + 1 < m.size(); ++c)
    {
        const bool last = c + 2 == m.size();
        const int a0 = m[c].first, a1 = m[c + 1].first, o0 = m[c].second, o1 = m[c + 1].second;
        if (a1 <= a0 || o1 <= o0) continue;
        const int pre = c == 0 ? 0 : std::min({ xf, a0, o0 });          // lead-in before this attack
        segL.assign(inL.begin() + a0, inL.begin() + a1);
        segR.assign(inR.begin() + a0, inR.begin() + a1);
        const double factor = (double) (o1 - o0) / (a1 - a0);
        if (std::abs(factor - 1.0) < 2e-4) { oL = segL; oR = segR; oL.resize((size_t) (o1 - o0), 0.0f); oR.resize((size_t) (o1 - o0), 0.0f); }
        else if (! stretchStereo(segL, segR, factor, sr, oL, oR, cancel)) return false;
        const int len = std::min((int) oL.size(), o1 - o0);
        const int keep = std::min({ (int) (0.012 * sr), (o1 - o0) / 3, a1 - a0 }), blend = std::max(1, std::min((int) (0.006 * sr), keep));
        // lead-in: the untouched source just before the attack, fading in
        for (int i = -pre; i < 0; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos((float) pi * ((float) (i + pre) + 0.5f) / (float) std::max(1, pre));   // + the fade below = 1
            outL[(size_t) (o0 + i)] += w * inL[(size_t) (a0 + i)];
            outR[(size_t) (o0 + i)] += w * inR[(size_t) (a0 + i)];
        }
        const int nextPre = last ? 0 : std::min({ xf, a1, o1 });
        for (int i = 0; i < len && o0 + i < n; ++i)
        {
            float l = oL[(size_t) i], r = oR[(size_t) i];
            if (i < keep && std::abs(factor - 1.0) >= 2e-4)
            {
                const float w = i < keep - blend ? 1.0f : std::cos(0.5f * (float) pi * (float) (i - (keep - blend)) / blend);
                l = w * inL[(size_t) (a0 + i)] + (1.0f - w) * l;
                r = w * inR[(size_t) (a0 + i)] + (1.0f - w) * r;
            }
            float g = 1.0f;
            const int toEnd = (o1 - o0) - i;                                 // samples until the next attack
            if (! last && toEnd <= nextPre) g = 0.5f + 0.5f * std::cos((float) pi * ((float) (nextPre - toEnd) + 0.5f) / (float) std::max(1, nextPre));
            outL[(size_t) (o0 + i)] += g * l;
            outR[(size_t) (o0 + i)] += g * r;
        }
    }
    return true;
}

//==============================================================================
// The whole step: follower + leader in, the follower in the leader's pocket out (same length, same
// tempo, its slices moved with it). Melodies are warped; drums are moved hit by hit. nullptr = cancelled.
inline std::shared_ptr<LoopData> lockToPocket(const LoopData& follower, const LoopData& leader, const Settings& s,
                                              Report* rep = nullptr, const std::atomic<bool>* cancel = nullptr)
{
    if (follower.length < 2048 || leader.length < 2048) return nullptr;
    const double sr = follower.sampleRate;
    const auto monoL = monoOf(leader), monoF = monoOf(follower);
    // drums: one note per hit (the slicer's cuts), each heard where its loudest hit lands, so a flam (a hat
    // on the beat and the late snare 14 ms after it) counts as the snare. Melodies: SuperFlux.
    const auto leaderNotes = leader.material == Material::drums ? notesFromSlices(leader, monoL, s.perceived)
                                                                : superFlux(monoL, leader.sampleRate, 0.5f, s.perceived, cancel);
    if (cancel != nullptr && cancel->load()) return nullptr;
    const auto map = buildMap(leaderNotes, gridOf(leader));
    const bool followerDrums = follower.material == Material::drums;
    const auto notes = followerDrums ? notesFromSlices(follower, monoF, s.perceived)
                                     : superFlux(monoF, sr, s.sensitivity, s.perceived, cancel);
    if (cancel != nullptr && cancel->load()) return nullptr;
    const auto gf = gridOf(follower);
    Report r;
    auto moves = placeNotes(notes, gf, map, s, &r);
    if (followerDrums && leader.material == Material::drums) r.barOne = findBarOne(notes, gf, map);   // drums vs drums only:
                                                                         // a melody's accents don't say where bar 1 is
    const double toMs = 1000.0 / sr;
    r.meanMoveMs *= toMs; r.maxMoveMs *= toMs; r.perceivedMeanMs *= toMs;
    if (rep != nullptr) *rep = r;

    auto out = std::make_shared<LoopData>();
    out->sampleRate = sr; out->material = follower.material; out->bars = follower.bars; out->bpm = follower.bpm;
    out->stepSamples = follower.samplesPerStep(); out->alignSteps = follower.alignSteps; out->length = follower.length;
    // where a source sample ends up: piecewise linear through the moves (identity at the ends)
    std::vector<std::pair<int, int>> marks;
    for (const auto& m : moves) marks.push_back({ m.src, (int) std::lround(m.dst) });
    std::sort(marks.begin(), marks.end());
    auto mapPos = [&](int x)
    {
        int a0 = 0, b0 = 0, a1 = follower.length, b1 = follower.length;
        for (const auto& p : marks) { if (p.first <= x) { a0 = p.first; b0 = p.second; } else { a1 = p.first; b1 = p.second; break; } }
        return a1 > a0 ? (int) std::lround(b0 + (double) (x - a0) * (b1 - b0) / (a1 - a0)) : b0;
    };
    const double sps = follower.samplesPerStep();
    const double align = follower.alignSteps * sps;
    if (followerDrums)
    {
        // hit by hit: the audio is untouched, each slice plays from its own place
        out->left = follower.left; out->right = follower.right;
        out->overview = follower.overview;
        out->slices = follower.slices;
        for (size_t i = 0; i < out->slices.size() && i < moves.size(); ++i)
        {
            auto& sl = out->slices[i];
            const double move = moves[i].dst - moves[i].src;
            sl.gridStep = (sl.onset + move - align) / sps + s.shift16;
            if (i == 0 && s.shift16 == 0) sl.gridStep = std::max(0.0, sl.gridStep);
        }
        return out;
    }
    if (! warpAudio(follower.left, follower.right, sr, marks, out->left, out->right, cancel)) return nullptr;
    out->overview.assign(512, 0.0f);
    for (int i = 0; i < out->length; ++i)
    {
        auto& o = out->overview[(size_t) std::min(511, (int) ((int64_t) i * 512 / out->length))];
        o = std::max(o, std::abs(0.5f * (out->left[(size_t) i] + out->right[(size_t) i])));
    }
    // the follower's slices, carried through the warp: each plays at its new place, so playback is the
    // warped audio, seamless (and Shift still moves bar 1 in whole 16ths)
    out->slices = follower.slices;
    for (size_t i = 0; i < out->slices.size(); ++i)
    {
        auto& sl = out->slices[i];
        const int pre = sl.onset - sl.start;
        sl.onset = i == 0 ? sl.onset : std::clamp(mapPos(sl.onset), 0, out->length - 1);
        sl.start = std::max(0, sl.onset - pre);
        sl.gridStep = (sl.onset - align) / sps + s.shift16;
        if (i == 0 && s.shift16 == 0) sl.gridStep = std::max(0.0, sl.gridStep);
    }
    for (size_t i = 0; i < out->slices.size(); ++i)
    {
        auto& sl = out->slices[i];
        const int next = i + 1 < out->slices.size() ? out->slices[i + 1].start : out->length;
        sl.end = std::max(sl.start + 1, std::min(out->length, next));
    }
    return out;
}

} // namespace pocket
} // namespace bounce
