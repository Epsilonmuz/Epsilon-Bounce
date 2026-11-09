#pragma once
// Epsilon Bounce engine: slices an audio loop at its hits and plays the slices back on the host
// grid with a dembow / drill / trap pocket. No JUCE in here, so the tests build without it.
//
// Threads: analyse() runs on a worker thread and produces an immutable LoopData. The audio thread
// only reads a LoopData it was handed (Processor publishes it with an atomic pointer) and never
// allocates: Player::prepare() sizes everything up front.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace bounce
{
constexpr int stepsPerBar = 16;          // 16th-note grid
constexpr int maxSlices = 1024;
constexpr double pi = 3.14159265358979323846;

//==============================================================================
// Grooves. Offsets are in fractions of a 16th step (+ = later), accents in dB.
// Order is the parameter's choice order: only ever append, so saved sessions keep their groove.
enum class Groove
{
    straight = 0, dembow, drill, trap,                         // v1.0
    reggaeton, afrobeats, amapiano, jerseyClub, boomBap,       // v1.1
    ukGarage, lofi, bailefunk, phonk,
    learned,                                                   // copied from a reference loop
    count
};
constexpr int grooveCount = static_cast<int>(Groove::count);
inline const char* grooveName(Groove g)
{
    switch (g)
    {
        case Groove::straight:   return "STRAIGHT";
        case Groove::dembow:     return "DEMBOW";
        case Groove::drill:      return "DRILL";
        case Groove::trap:       return "TRAP";
        case Groove::reggaeton:  return "REGGAETON";
        case Groove::afrobeats:  return "AFROBEATS";
        case Groove::amapiano:   return "AMAPIANO";
        case Groove::jerseyClub: return "JERSEY";
        case Groove::boomBap:    return "BOOM BAP";
        case Groove::ukGarage:   return "UK GARAGE";
        case Groove::lofi:       return "LO-FI";
        case Groove::bailefunk:  return "BAILE FUNK";
        case Groove::phonk:      return "PHONK";
        case Groove::learned:    return "LEARNED";
        default:                 return "";
    }
}

struct GrooveTemplate
{
    std::array<float, stepsPerBar> offset {};   // fraction of a 16th
    std::array<float, stepsPerBar> accentDb {};
    float swing = 0.0f;                         // extra delay on off-16ths (fraction of a 16th)
    bool operator==(const GrooveTemplate& o) const { return offset == o.offset && accentDb == o.accentDb && swing == o.swing; }
    bool operator!=(const GrooveTemplate& o) const { return ! (*this == o); }
};

// MPC-style swing percentage (50 = straight, 66.7 = triplet) as a delay on off-16ths.
constexpr float mpcSwing(float percent) { return (percent - 50.0f) / 50.0f; }

inline GrooveTemplate grooveTemplate(Groove g)
{
    GrooveTemplate t;
    auto accents = [&t](std::initializer_list<int> steps, float db) { for (auto s : steps) t.accentDb[(size_t) s] = db; };
    switch (g)
    {
        case Groove::straight:
            break;
        case Groove::dembow:
            // Kick on every beat, the "ch" on 16ths 3, 6, 11, 14: those lean late / early for the
            // bounce; off-16ths swing lightly.
            t.swing = 0.10f;
            t.offset[3] = 0.10f;  t.offset[11] = 0.10f;
            t.offset[6] = -0.05f; t.offset[14] = -0.05f;
            accents({ 3, 6, 11, 14 }, 1.5f);
            accents({ 1, 5, 9, 13 }, -1.5f);
            break;
        case Groove::drill:
            // UK / NY drill: heavy 16th shuffle (~62 %), half-time snare dragging a hair, the 16th
            // before it pulled in.
            t.swing = 0.24f;
            t.offset[8] = 0.04f;
            t.offset[7] = -0.04f; t.offset[15] = -0.04f;
            for (int s = 1; s < stepsPerBar; s += 2) t.accentDb[(size_t) s] = -2.0f;
            t.accentDb[8] = 1.0f;
            break;
        case Groove::trap:
            // Straight and tight; downbeats pushed a touch forward, off-8th hats just quieter.
            t.swing = 0.04f;
            for (int s = 0; s < stepsPerBar; s += 4) t.offset[(size_t) s] = -0.02f;
            accents({ 2, 6, 10, 14 }, -1.0f);
            t.accentDb[8] = 1.0f;
            break;
        case Groove::reggaeton:
            // Dembow's cleaner commercial cousin: same 3 / 6 / 11 / 14 snare pattern, almost on the
            // grid, accented harder, no 16th swing.
            t.offset[3] = 0.04f;  t.offset[11] = 0.04f;
            t.offset[6] = -0.02f; t.offset[14] = -0.02f;
            accents({ 3, 6, 11, 14 }, 2.0f);
            accents({ 1, 5, 9, 13 }, -2.5f);
            break;
        case Groove::afrobeats:
            // Rolling 3-3-2 feel: 16ths 0, 3, 6 and 8, 11, 14 carry it; light swing, the 3s lay back,
            // the 6s and 14s push.
            t.swing = 0.16f;
            t.offset[3] = 0.06f;  t.offset[11] = 0.06f;
            t.offset[6] = -0.04f; t.offset[14] = -0.04f;
            accents({ 0, 3, 6, 8, 11, 14 }, 1.5f);
            accents({ 1, 9 }, -2.0f);
            break;
        case Groove::amapiano:
            // Lazy shuffled shakers (~60 %), log-drum off-8ths (2, 6, 10, 14) laid back, the kick
            // dead on the beat.
            t.swing = mpcSwing(60.0f);
            for (auto s : { 2, 6, 10, 14 }) t.offset[(size_t) s] = 0.08f;
            accents({ 2, 6, 10, 14 }, 1.5f);
            accents({ 1, 5, 9, 13 }, -1.5f);
            break;
        case Groove::jerseyClub:
            // Straight, urgent: the 5-kick bounce (0, 4, 8, 10, 12) pushed early, everything else
            // ducked so the kicks jump out.
            t.swing = 0.03f;
            for (auto s : { 4, 10, 12 }) t.offset[(size_t) s] = -0.04f;
            accents({ 0, 4, 8, 10, 12 }, 2.0f);
            accents({ 1, 3, 5, 7, 9, 11, 13, 15 }, -2.5f);
            break;
        case Groove::boomBap:
            // MPC 58 % swing with the backbeat snare (4, 12) laid back like a live drummer.
            t.swing = mpcSwing(58.0f);
            t.offset[4] = 0.07f; t.offset[12] = 0.07f;
            accents({ 4, 12 }, 1.5f);
            accents({ 1, 5, 9, 13 }, -1.0f);
            break;
        case Groove::ukGarage:
            // 2-step: heavy shuffle (~66 %), the skippy kick on 10 pulled early, the snares on 4 / 12
            // a hair late.
            t.swing = mpcSwing(66.0f);
            t.offset[10] = -0.05f;
            t.offset[4] = 0.03f; t.offset[12] = 0.03f;
            accents({ 4, 12 }, 1.0f);
            accents({ 3, 7, 11, 15 }, -2.0f);
            break;
        case Groove::lofi:
            // Drunk and late: soft swing, everything except the downbeat drags, accents flattened.
            t.swing = mpcSwing(57.0f);
            for (int s = 1; s < stepsPerBar; ++s) t.offset[(size_t) s] = s % 4 == 0 ? 0.08f : 0.05f;
            for (int s = 0; s < stepsPerBar; ++s) t.accentDb[(size_t) s] = s % 4 == 0 ? 0.0f : -1.5f;
            break;
        case Groove::bailefunk:
            // Tamborzao: hits on 0, 3, 6, 10, 12 - the 3 and 10 rush, the 6 sits back.
            t.swing = 0.05f;
            t.offset[3] = -0.04f; t.offset[10] = -0.04f;
            t.offset[6] = 0.05f;
            accents({ 0, 3, 6, 10, 12 }, 2.0f);
            accents({ 1, 5, 9, 13 }, -2.0f);
            break;
        case Groove::phonk:
            // Memphis cowbell bounce: medium swing, half-time snare on 8 heavy and late.
            t.swing = mpcSwing(56.0f);
            t.offset[8] = 0.06f;
            accents({ 8 }, 2.0f);
            accents({ 2, 6, 10, 14 }, 1.0f);
            accents({ 1, 3, 5, 7, 9, 11, 13, 15 }, -1.5f);
            break;
        default: break;
    }
    return t;
}

//==============================================================================
// Analysis

struct Slice
{
    int start = 0;          // first sample to play (onset minus pre-roll)
    int onset = 0;          // where the hit actually starts
    int end = 0;            // last sample (exclusive) before the next slice's start
    double gridStep = 0.0;  // where the onset sits on the loop's own 16th grid (fractional)
    int step = 0;           // nearest 16th (0 .. bars*16-1)
    float peak = 0.0f;
};

struct LoopData
{
    std::vector<float> left, right;
    double sampleRate = 44100.0;
    int length = 0;
    int bars = 1;
    double bpm = 120.0;                 // the loop's own tempo, from its length and bar count
    std::vector<Slice> slices;
    std::vector<float> overview;        // 512 peak values for the waveform display
    double samplesPerStep() const { return (double) length / (bars * stepsPerBar); }
};

namespace detail
{
inline void fft(std::vector<std::complex<float>>& a)
{
    const auto n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto ang = -2.0 * pi / (double) len;
        const std::complex<float> wl((float) std::cos(ang), (float) std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
} // namespace detail

// Picks the bar count that puts the loop's tempo in a 85-170 BPM window (trap, drill and dembow all
// live there). barsHint > 0 forces it.
inline int chooseBars(int length, double sampleRate, int barsHint)
{
    if (barsHint > 0) return barsHint;
    const double seconds = length / sampleRate;
    int best = 1; double bestScore = 1e9;
    for (int b : { 1, 2, 4, 8 })
    {
        const double bpm = 240.0 * b / seconds;
        const double score = bpm < 85.0 ? (85.0 - bpm) * 2.0 : bpm > 170.0 ? (bpm - 170.0) * 2.0 : std::abs(bpm - 128.0) * 0.01;
        if (score < bestScore) { bestScore = score; best = b; }
    }
    return best;
}

struct AnalysisSettings
{
    int barsHint = 0;            // 0 = auto
    float sensitivity = 0.5f;    // 0..1, more = more slices
    double prerollMs = 3.0;      // play this much before each detected onset so attacks stay intact
};

// Spectral-flux onset detection with an adaptive threshold, then each onset is refined to the
// sample where the attack starts.
inline std::shared_ptr<LoopData> analyse(std::vector<float> left, std::vector<float> right, double sampleRate,
                                         const AnalysisSettings& settings)
{
    auto data = std::make_shared<LoopData>();
    if (right.size() != left.size()) right = left;
    data->length = (int) left.size();
    data->sampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    data->left = std::move(left);
    data->right = std::move(right);
    const int n = data->length;
    if (n < 1024) return data;

    data->bars = chooseBars(n, data->sampleRate, settings.barsHint);
    data->bpm = 240.0 * data->bars / (n / data->sampleRate);

    std::vector<float> mono((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (data->left[(size_t) i] + data->right[(size_t) i]);

    // Overview
    data->overview.assign(512, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        auto& o = data->overview[(size_t) std::min(511, (int) ((int64_t) i * 512 / n))];
        o = std::max(o, std::abs(mono[(size_t) i]));
    }

    // Spectral flux
    constexpr int frame = 1024, hop = 256;
    std::vector<float> window(frame);
    for (int i = 0; i < frame; ++i) window[(size_t) i] = 0.5f - 0.5f * (float) std::cos(2.0 * pi * i / frame);
    const int frames = 1 + (n - 1) / hop;
    std::vector<float> flux((size_t) frames, 0.0f), prev(frame / 2 + 1, 0.0f), mag(frame / 2 + 1);
    std::vector<std::complex<float>> buf(frame);
    for (int f = 0; f < frames; ++f)
    {
        const int centre = f * hop;
        for (int i = 0; i < frame; ++i)
        {
            const int idx = centre - frame / 2 + i;
            buf[(size_t) i] = { (idx >= 0 && idx < n ? mono[(size_t) idx] : 0.0f) * window[(size_t) i], 0.0f };
        }
        detail::fft(buf);
        float sum = 0.0f;
        for (int k = 1; k <= frame / 2; ++k)
        {
            mag[(size_t) k] = std::log1p(100.0f * std::abs(buf[(size_t) k]));
            const auto d = mag[(size_t) k] - prev[(size_t) k];
            if (d > 0) sum += d;
        }
        flux[(size_t) f] = sum;
        std::swap(prev, mag);
    }
    float maxFlux = 0.0f;
    for (auto v : flux) maxFlux = std::max(maxFlux, v);
    if (maxFlux <= 0.0f) { data->slices.push_back({ 0, 0, n, 0.0, 0, 1.0f }); return data; }
    for (auto& v : flux) v /= maxFlux;

    // Adaptive threshold: local mean over ~120 ms plus a floor set by sensitivity
    const float floorThresh = 0.30f - 0.24f * std::clamp(settings.sensitivity, 0.0f, 1.0f);
    const int wing = std::max(2, (int) (0.06 * data->sampleRate / hop));
    const int minGap = std::max(1, (int) (0.045 * data->sampleRate / hop));   // 45 ms between hits
    std::vector<int> onsetFrames;
    for (int f = 1; f < frames - 1; ++f)
    {
        if (flux[(size_t) f] < flux[(size_t) f - 1] || flux[(size_t) f] < flux[(size_t) f + 1]) continue;
        float mean = 0.0f; int cnt = 0;
        for (int k = std::max(0, f - wing); k <= std::min(frames - 1, f + wing); ++k) { mean += flux[(size_t) k]; ++cnt; }
        mean /= (float) cnt;
        if (flux[(size_t) f] < floorThresh + 1.2f * mean * 0.5f) continue;
        if (!onsetFrames.empty() && f - onsetFrames.back() < minGap)
        {
            if (flux[(size_t) f] > flux[(size_t) onsetFrames.back()]) onsetFrames.back() = f;
            continue;
        }
        onsetFrames.push_back(f);
    }

    // Refine to the attack start: walk back from the frame centre to where the envelope drops
    // under 15 % of the hit's local peak.
    const int preroll = (int) std::round(settings.prerollMs * 0.001 * data->sampleRate);
    std::vector<int> onsets;
    onsets.push_back(0);
    for (auto f : onsetFrames)
    {
        const int c = f * hop;
        const int lo = std::max(0, c - frame / 2), hi = std::min(n, c + hop * 2);
        float pk = 0.0f; int pkAt = c;
        for (int i = lo; i < hi; ++i) if (std::abs(mono[(size_t) i]) > pk) { pk = std::abs(mono[(size_t) i]); pkAt = i; }
        int s = pkAt;
        // smoothed envelope walk-back
        float env = pk;
        for (int i = pkAt; i > lo; --i)
        {
            env = std::max(std::abs(mono[(size_t) i]), env * 0.995f);
            if (std::abs(mono[(size_t) i]) < 0.15f * pk)
            {
                // need ~1 ms quiet to accept
                bool quiet = true;
                const int q = std::max(lo, i - (int) (0.001 * data->sampleRate));
                for (int k = q; k < i; ++k) if (std::abs(mono[(size_t) k]) > 0.15f * pk) { quiet = false; break; }
                if (quiet) { s = i; break; }
            }
            s = i;
        }
        if (s - onsets.back() > (int) (0.03 * data->sampleRate)) onsets.push_back(s);
    }

    if (onsets.size() > (size_t) maxSlices) onsets.resize((size_t) maxSlices);
    const double sps = data->samplesPerStep();
    const int totalSteps = data->bars * stepsPerBar;
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        Slice s;
        s.onset = onsets[i];
        s.start = std::max(0, s.onset - (i == 0 ? 0 : preroll));
        const int nextOnset = i + 1 < onsets.size() ? onsets[i + 1] : n;
        s.end = std::max(s.start + 1, i + 1 < onsets.size() ? std::max(s.start + 1, nextOnset - preroll) : n);
        s.gridStep = s.onset / sps;
        s.step = ((int) std::lround(s.gridStep)) % totalSteps;
        for (int k = s.onset; k < std::min(s.end, s.onset + (int) (0.02 * data->sampleRate)); ++k)
            s.peak = std::max(s.peak, std::abs(mono[(size_t) k]));
        data->slices.push_back(s);
    }
    return data;
}

//==============================================================================
// Groove placement: where each slice lands, in 16th steps from the loop start.

struct GrooveSettings
{
    Groove groove = Groove::dembow;
    float amount = 1.0f;       // 0..1.5 scales the template
    float swing = 0.0f;        // 0..1 extra swing on off-16ths: 0 = template only, 1 = +0.33 step (75 %)
    float tighten = 0.5f;      // 0..1 pulls the original timing onto the grid before the groove applies
    float humanizeMs = 0.0f;   // 0..20 random timing, stable per slice
    float accent = 0.75f;      // 0..1 applies the template's accents
    float feelMs = 0.0f;       // -20..+20 whole-loop push / lay back
    std::uint32_t seed = 0x5eed;
    GrooveTemplate learned {}; // used when groove == learned
};

inline GrooveTemplate templateFor(const GrooveSettings& g)
{
    return g.groove == Groove::learned ? g.learned : grooveTemplate(g.groove);
}

//==============================================================================
// Groove Learn: read a reference loop's microtiming and accents into a template.
// For every 16th of the bar, the average distance of the reference's hits from the grid becomes
// that step's offset; their average level (relative to the loop) becomes its accent. Steps the
// reference never hits borrow the average of steps with the same role (downbeat / 8th / 16th),
// so its swing still carries over.

struct LearnResult
{
    GrooveTemplate groove;
    int hits = 0;              // reference hits that were used
    int stepsHeard = 0;        // how many of the 16 steps the reference actually played
    float swingPercent = 50.0f;// the reference's off-16th swing as an MPC-style percentage
};

inline LearnResult learnGroove(const LoopData& ref)
{
    LearnResult r;
    if (ref.slices.size() < 2) return r;
    std::array<double, stepsPerBar> sumDev {}, sumLevel {};
    std::array<int, stepsPerBar> count {};
    double allLevel = 0.0;
    int used = 0;
    for (const auto& s : ref.slices)
    {
        const double q = std::round(s.gridStep);
        const double dev = s.gridStep - q;
        if (std::abs(dev) > 0.45) continue;            // ambiguous: halfway between two 16ths
        const int step = (((int) q % stepsPerBar) + stepsPerBar) % stepsPerBar;
        sumDev[(size_t) step] += dev;
        sumLevel[(size_t) step] += std::max(1e-4f, s.peak);
        allLevel += std::max(1e-4f, s.peak);
        ++count[(size_t) step];
        ++used;
    }
    if (used < 2) return r;
    allLevel /= used;
    // role averages: 0 = quarter, 1 = off-8th, 2 = off-16th
    auto role = [](int s) { return s % 4 == 0 ? 0 : s % 2 == 0 ? 1 : 2; };
    std::array<double, 3> roleDev {}; std::array<int, 3> roleN {};
    for (int s = 0; s < stepsPerBar; ++s)
        if (count[(size_t) s] > 0) { roleDev[(size_t) role(s)] += sumDev[(size_t) s] / count[(size_t) s]; ++roleN[(size_t) role(s)]; }
    for (int s = 0; s < stepsPerBar; ++s)
    {
        if (count[(size_t) s] > 0)
        {
            r.groove.offset[(size_t) s] = (float) (sumDev[(size_t) s] / count[(size_t) s]);
            const double lvl = sumLevel[(size_t) s] / count[(size_t) s];
            r.groove.accentDb[(size_t) s] = (float) std::clamp(20.0 * std::log10(lvl / allLevel) * 0.5, -6.0, 6.0);
            ++r.stepsHeard;
        }
        else
        {
            const int k = role(s);
            r.groove.offset[(size_t) s] = roleN[(size_t) k] > 0 ? (float) (roleDev[(size_t) k] / roleN[(size_t) k]) : 0.0f;
        }
    }
    // Express the off-16th lean as a swing figure for the display (the offsets already carry it).
    const double off = roleN[2] > 0 ? roleDev[2] / roleN[2] : 0.0;
    const double on = (roleN[0] + roleN[1]) > 0 ? (roleDev[0] + roleDev[1]) / (roleN[0] + roleN[1]) : 0.0;
    r.swingPercent = (float) std::clamp(50.0 + (off - on) * 50.0, 0.0, 100.0);
    r.hits = used;
    return r;
}

struct Placement
{
    double step = 0.0;     // target position of the onset in 16th steps (can be fractional / negative)
    float gain = 1.0f;
};

inline float hashUnit(std::uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (float) (x & 0xffffff) / (float) 0xffffff * 2.0f - 1.0f;   // -1..1
}

// msPerStep converts the millisecond controls at the playback tempo.
inline Placement place(const Slice& s, int index, const GrooveSettings& g, const GrooveTemplate& t, double msPerStep)
{
    Placement p;
    const double quant = std::round(s.gridStep);
    const double base = s.gridStep + (quant - s.gridStep) * std::clamp(g.tighten, 0.0f, 1.0f);
    const int stepInBar = (((int) quant % stepsPerBar) + stepsPerBar) % stepsPerBar;
    const bool offSixteenth = (stepInBar & 1) == 1;
    double off = t.offset[(size_t) stepInBar] + (offSixteenth ? t.swing : 0.0f);
    off *= std::clamp(g.amount, 0.0f, 1.5f);
    if (offSixteenth) off += std::clamp(g.swing, 0.0f, 1.0f) * (1.0 / 3.0);
    const double ms = g.feelMs + g.humanizeMs * hashUnit(g.seed ^ (std::uint32_t) (index * 2654435761u));
    if (msPerStep > 0) off += ms / msPerStep;
    // index 0 owns the downbeat: never let it drift before the loop start
    p.step = index == 0 ? std::max(0.0, base + off) : base + off;
    p.gain = std::pow(10.0f, t.accentDb[(size_t) stepInBar] * std::clamp(g.accent, 0.0f, 1.0f) / 20.0f);
    return p;
}

//==============================================================================
// Player: two slice voices (the playing slice plus one fading out) triggered on the host grid.

class Player
{
public:
    void prepare(double hostRate, int /*maxBlock*/)
    {
        rate = hostRate > 0 ? hostRate : 44100.0;
        fadeLen = std::max(16, (int) (0.004 * rate));
        placements.assign(maxSlices, {});
        order.assign(maxSlices, 0);
        reset();
    }
    void reset() { for (auto& v : voices) v = {}; }

    // Recompute placements for this loop at this tempo. Allocation-free: vectors were sized in prepare().
    void update(const LoopData& d, const GrooveSettings& g, double bpm)
    {
        const auto t = templateFor(g);
        const double msPerStep = 60000.0 / std::max(20.0, bpm) / 4.0;
        count = std::min((int) d.slices.size(), maxSlices);
        loopSteps = d.bars * stepsPerBar;
        for (int i = 0; i < count; ++i)
        {
            placements[(size_t) i] = place(d.slices[(size_t) i], i, g, t, msPerStep);
            order[(size_t) i] = i;
        }
        // keep trigger order sorted by target time (insertion sort, n is small)
        for (int i = 1; i < count; ++i)
        {
            const int key = order[(size_t) i]; int j = i - 1;
            while (j >= 0 && placements[(size_t) order[(size_t) j]].step > placements[(size_t) key].step) { order[(size_t) (j + 1)] = order[(size_t) j]; --j; }
            order[(size_t) (j + 1)] = key;
        }
        prerollSteps = 0.0;
        if (count > 1)
        {
            const auto& s = d.slices[1];
            prerollSteps = (s.onset - s.start) / d.sampleRate * 1000.0 / msPerStep;
        }
    }

    // Render one block. stepStart = the playhead position in 16th steps at sample 0 of the block,
    // stepsPerSample = tempo; both from the host (or the internal clock). Adds into out.
    void render(const LoopData& d, float* outL, float* outR, int frames, double stepStart, double stepsPerSample, float gain)
    {
        if (count == 0 || loopSteps <= 0) return;
        const double ratio = d.sampleRate / rate;
        int pos = 0;
        while (pos < frames)
        {
            // find the next trigger inside [pos, frames)
            const double s0 = stepStart + pos * stepsPerSample;
            const double s1 = stepStart + frames * stepsPerSample;
            int hitSample = frames, hitSlice = -1;
            for (int k = 0; k < count; ++k)
            {
                const int i = order[(size_t) k];
                // trigger early by the pre-roll so the onset itself lands on the target
                const double target = placements[(size_t) i].step - (i == 0 ? 0.0 : prerollSteps);
                const double t = target + std::ceil((s0 - target) / loopSteps - 1e-9) * loopSteps;   // first hit at or after s0
                if (t < s1)
                {
                    const int at = std::clamp((int) std::ceil((t - stepStart) / stepsPerSample - 1e-9), pos, frames - 1);
                    if (at < hitSample) { hitSample = at; hitSlice = i; }
                }
            }
            renderVoices(d, outL, outR, pos, hitSample, ratio, gain);
            if (hitSlice < 0) break;
            trigger(d, hitSlice);
            pos = hitSample;
            renderVoices(d, outL, outR, pos, std::min(frames, pos + 1), ratio, gain);
            pos += 1;
        }
    }

    int sliceCount() const { return count; }
    const Placement& placement(int i) const { return placements[(size_t) i]; }
    int playingSlice() const { return voices[0].active ? voices[0].slice : -1; }

private:
    struct Voice
    {
        bool active = false;
        int slice = -1;
        double readPos = 0.0;
        int end = 0;
        float gain = 1.0f;
        int fadeOut = -1;     // samples left in the choke fade, -1 = not fading
        int fadeIn = 0;       // samples into the fade-in
    };
    std::array<Voice, 2> voices {};
    std::vector<Placement> placements;
    std::vector<int> order;
    int count = 0, loopSteps = 16, fadeLen = 176;
    double rate = 44100.0, prerollSteps = 0.0;

    void trigger(const LoopData& d, int i)
    {
        // choke the playing slice into the fade voice
        if (voices[0].active)
        {
            voices[1] = voices[0];
            voices[1].fadeOut = fadeLen;
        }
        const auto& s = d.slices[(size_t) i];
        voices[0] = {};
        voices[0].active = true;
        voices[0].slice = i;
        voices[0].readPos = s.start;
        voices[0].end = s.end;
        voices[0].gain = placements[(size_t) i].gain;
        voices[0].fadeIn = i == 0 ? fadeLen : 0;   // the pre-roll already starts in silence
    }

    void renderVoices(const LoopData& d, float* outL, float* outR, int from, int to, double ratio, float gain)
    {
        for (auto& v : voices)
        {
            if (!v.active) continue;
            for (int n = from; n < to; ++n)
            {
                const int i0 = (int) v.readPos;
                if (i0 + 1 >= v.end || i0 + 1 >= d.length)
                {
                    v.active = false; break;
                }
                // fade the last 4 ms of the slice so a moved-late hit leaves clean silence, not a click
                float g = v.gain * gain;
                const int left = v.end - i0;
                if (left < fadeLen) g *= (float) left / (float) fadeLen;
                if (v.fadeOut >= 0)
                {
                    g *= (float) v.fadeOut / (float) fadeLen;
                    if (--v.fadeOut <= 0) { v.active = false; break; }
                }
                if (v.fadeIn < fadeLen) { g *= (float) v.fadeIn / (float) fadeLen; ++v.fadeIn; }
                const float fr = (float) (v.readPos - i0);
                outL[n] += g * (d.left[(size_t) i0] + fr * (d.left[(size_t) i0 + 1] - d.left[(size_t) i0]));
                outR[n] += g * (d.right[(size_t) i0] + fr * (d.right[(size_t) i0 + 1] - d.right[(size_t) i0]));
                v.readPos += ratio;
            }
        }
    }
};

} // namespace bounce
