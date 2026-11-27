#pragma once
// Test material for the v2.0 Pocket Engine (engine tests and host bench): a trap / drill drum loop with a late
// snare, swung hats and a triplet hat roll, and a melody made dead on the grid with plucks, slow pads and a
// triplet figure. 140 BPM. No JUCE.
#include <cmath>
#include <random>
#include <vector>

namespace pocket_material
{
constexpr double kPi = 3.14159265358979323846;
inline double sr = 44100.0;
constexpr double bpm = 140.0;
inline double beatS() { return 60.0 / bpm * sr; }
inline double tickS() { return beatS() / 12.0; }

// ---- drums: ticks (12 per beat) + delay in ms
struct Hit { int tick; double lateMs; int kind; };   // kind 0 kick, 1 snare, 2 hat
inline std::vector<Hit> drumPattern()
{
    std::vector<Hit> h;
    for (int beat = 0; beat < 4; ++beat)
    {
        const int b = beat * 12;
        if (beat < 3)
        {
            for (int s = 0; s < 4; ++s) h.push_back({ b + s * 3, (s & 1) ? 18.0 : 0.0, 2 });   // swung off-16ths
        }
        else
        {
            for (int t = 0; t < 6; ++t) h.push_back({ b + t * 2, (t == 0) ? 0.0 : 10.0, 2 });    // triplet roll, laid back
        }
    }
    h.push_back({ 0, 0.0, 0 }); h.push_back({ 30, 0.0, 0 });                   // kick on 1 and "3 and"
    h.push_back({ 12, 14.0, 1 }); h.push_back({ 36, 14.0, 1 });                // snare on 2 and 4, late
    return h;
}

inline void makeDrums(int bars, std::vector<float>& l, std::vector<float>& r, int rotateTicks = 0)
{
    const int len = (int) std::lround(bars * 4 * beatS());
    l.assign((size_t) len, 0.0f); r.assign((size_t) len, 0.0f);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (int bar = 0; bar < bars; ++bar)
        for (const auto& h : drumPattern())
        {
            const int tk = ((bar * 48 + h.tick + rotateTicks) % (bars * 48) + bars * 48) % (bars * 48);
            const int at = (int) std::lround(tk * tickS() + h.lateMs * 0.001 * sr);
            const int dur = (int) ((h.kind == 0 ? 0.25 : h.kind == 1 ? 0.15 : 0.035) * sr);
            for (int i = 0; i < dur && at + i < len; ++i)
            {
                const double t = i / sr;
                float v;
                if (h.kind == 0) v = 0.8f * (float) (std::exp(-t * 12) * std::sin(2 * kPi * (50 + 80 * std::exp(-t * 30)) * t));
                else if (h.kind == 1) v = 0.5f * std::exp(-(float) i / (float) (0.03 * sr)) * u(rng);
                else v = 0.16f * std::exp(-(float) i / (float) (0.007 * sr)) * u(rng);
                l[(size_t) (at + i)] += v; r[(size_t) (at + i)] += v;
            }
        }
}

// ---- melody: made dead on the grid. kind 0 pluck, 1 pad (60 ms swell)
struct MNote { int tick; int midi; int kind; };
inline std::vector<MNote> melodyPattern()
{
    return {
        { 0, 57, 0 },             // beat 1: pluck (kick there, on time)
        { 12, 64, 1 },            // beat 2: pad (snare 14 ms late; the pad is heard ~25 ms after it starts)
        { 18, 60, 0 },            // the "2 and": on-8th, on time
        { 21, 62, 0 },            // the "2 and a": off-16th, hats 18 ms late
        { 24, 65, 0 }, { 28, 64, 0 }, { 32, 62, 0 },   // beat 3: 8th-note triplets (drill)
        { 36, 60, 1 },            // beat 4: pad on the late snare
        { 42, 59, 0 },            // the "4 and"
    };
}
inline int melodyNoteCount() { return (int) melodyPattern().size(); }

inline void makeMelody(int bars, std::vector<float>& l, std::vector<float>& r, std::vector<int>* attacks = nullptr)
{
    const int len = (int) std::lround(bars * 4 * beatS());
    l.assign((size_t) len, 0.0f); r.assign((size_t) len, 0.0f);
    const auto pat = melodyPattern();
    for (int bar = 0; bar < bars; ++bar)
        for (size_t k = 0; k < pat.size(); ++k)
        {
            const auto& nt = pat[k];
            const int at = (int) std::lround((bar * 48 + nt.tick) * tickS());
            const int nextTick = k + 1 < pat.size() ? bar * 48 + pat[k + 1].tick : (bar + 1) * 48;
            const int end = std::min(len, (int) std::lround(nextTick * tickS()));
            const double f = 440.0 * std::pow(2.0, (nt.midi - 69) / 12.0);
            if (attacks) attacks->push_back(at);
            const int rel = (int) (0.004 * sr);
            for (int i = 0; at + i < end; ++i)
            {
                const double t = i / sr;
                double env = nt.kind == 0 ? std::exp(-t * 5) * std::min(1.0, t / 0.001) : std::min(1.0, t / 0.060) * std::exp(-t * 0.8);
                const int left = end - (at + i);
                if (left < rel) env *= (double) left / rel;
                const float v = (float) (0.25 * env * (std::sin(2 * kPi * f * t) + 0.5 * std::sin(4 * kPi * f * t) + 0.25 * std::sin(6 * kPi * f * t)));
                l[(size_t) (at + i)] += v; r[(size_t) (at + i)] += 0.9f * v;
            }
        }
}

} // namespace pocket_material
