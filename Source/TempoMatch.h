#pragma once
// Epsilon Bounce: tempo match (v1.6). Step 1 of the flow: before any groove work, each loop's own
// tempo is found and the loop is brought to the project tempo, so the melody and the drums share
// one grid. No JUCE in here, so the engine tests build without it.
//
//   estimateTempo()  reads a loop's tempo from its rhythm (onset autocorrelation), not just its length,
//                    so a sample cut a little long or short, or a flip with a tail, still reads right.
//                    Of the equivalent readings (70 / 140 / 280) it takes the one nearest the project
//                    tempo, so a half-time loop is never stretched to double length.
//   stretchStereo()  changes a loop's speed without changing its pitch: a phase vocoder with identity
//                    phase locking (Laroche & Dolson), one shared phase rotation for both channels (the
//                    stereo image stays put) and a phase reset on each attack (transients stay sharp).
//
// Both run on the worker thread and allocate freely; nothing here is called from the audio thread.

#include "BounceEngine.h"

namespace bounce
{
struct TempoGuess
{
    double bpm = 0.0;          // 0 = could not tell
    double bars = 0.0;         // the loop's length in bars at that tempo
    float confidence = 0.0f;   // 0..1: how clearly the rhythm repeats at this tempo
    bool fromLength = false;   // the loop is a whole number of bars at this tempo (the exact length was used)
};

namespace detail
{
// A loop's length in whole bars: a tail (reverb, a few ms of silence) rounds down, a loop cut a hair
// short rounds up. Powers of two win close calls, since almost every loop is 1, 2, 4, 8 or 16 bars.
inline int snapBars(double bars)
{
    int best = 1; double bestScore = 1e9;
    for (int c = 1; c <= 32; ++c)
    {
        const bool pow2 = (c & (c - 1)) == 0;
        const double over = bars >= c ? (bars - c) / c * 0.5 : (c - bars) / c * 2.0;
        const double score = over + (pow2 ? 0.0 : 0.03);
        if (score < bestScore) { bestScore = score; best = c; }
    }
    return best;
}

inline double princarg(double p) { return p - 2.0 * pi * std::floor((p + pi) / (2.0 * pi)); }

// Onset strength per frame: log-magnitude spectral flux, with the slow trend removed.
inline std::vector<float> noveltyCurve(const std::vector<float>& mono, double sr, int hop, double& fps)
{
    const int frame = sr > 30000.0 ? 2048 : 512;
    fps = sr / hop;
    const int n = (int) mono.size();
    const int frames = std::max(0, 1 + (n - frame) / hop);
    std::vector<float> nov((size_t) frames, 0.0f), prev(frame / 2 + 1, 0.0f), win(frame);
    for (int i = 0; i < frame; ++i) win[(size_t) i] = 0.5f - 0.5f * (float) std::cos(2.0 * pi * i / frame);
    std::vector<std::complex<float>> buf(frame);
    const FFT f(frame);
    for (int m = 0; m < frames; ++m)
    {
        const int at = m * hop;
        for (int i = 0; i < frame; ++i) buf[(size_t) i] = { mono[(size_t) (at + i)] * win[(size_t) i], 0.0f };
        f(buf);
        float flux = 0.0f;
        for (int k = 1; k <= frame / 2; ++k)
        {
            const float v = std::log1p(20.0f * mag(buf[(size_t) k]));
            flux += std::max(0.0f, v - prev[(size_t) k]);
            prev[(size_t) k] = v;
        }
        nov[(size_t) m] = m == 0 ? 0.0f : flux;
    }
    // subtract a ~0.4 s moving average and half-wave rectify: only the bumps (the hits) remain
    const int w = std::max(1, (int) (0.2 * fps));
    std::vector<float> out((size_t) frames, 0.0f);
    double acc = 0.0; int cnt = 0;
    for (int i = -w; i < frames + w; ++i)
    {
        const int add = i + w, rem = i - w - 1;
        if (add >= 0 && add < frames) { acc += nov[(size_t) add]; ++cnt; }
        if (rem >= 0 && rem < frames) { acc -= nov[(size_t) rem]; --cnt; }
        if (i >= 0 && i < frames) out[(size_t) i] = std::max(0.0f, nov[(size_t) i] - (float) (acc / std::max(1, cnt)));
    }
    return out;
}

// Normalised autocorrelation of the novelty curve at a fractional lag (in frames).
struct Acf
{
    std::vector<double> a;
    double at(double lag) const
    {
        if (lag < 0.0) return 0.0;
        const int i = (int) lag;
        if (i + 1 >= (int) a.size()) return 0.0;
        const double fr = lag - i;
        return a[(size_t) i] * (1.0 - fr) + a[(size_t) i + 1] * fr;
    }
};

inline Acf autocorr(const std::vector<float>& x, int maxLag)
{
    Acf r;
    const int n = (int) x.size();
    maxLag = std::min(maxLag, n - 1);
    r.a.assign((size_t) std::max(0, maxLag + 2), 0.0);
    double e = 0.0;
    for (float v : x) e += (double) v * v;
    if (e <= 0.0) return r;
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i + lag < n; ++i) s += (double) x[(size_t) i] * x[(size_t) (i + lag)];
        r.a[(size_t) lag] = s / e * n / (double) std::max(1, n - lag);   // unbiased: long lags aren't penalised
    }
    return r;
}

// How strongly the rhythm repeats every beat at this tempo (beats, 2 beats, a bar, 2 bars), plus a
// little credit for the 8th-note level so a busy hat pattern doesn't read as double time on its own.
inline double beatScore(const Acf& acf, double fps, double bpm)
{
    const double beat = fps * 60.0 / bpm;
    return acf.at(beat) + 0.5 * acf.at(2.0 * beat) + 0.5 * acf.at(4.0 * beat) + 0.25 * acf.at(8.0 * beat)
         + 0.25 * acf.at(0.5 * beat);
}
} // namespace detail

// preferBpm: the project tempo (0 = unknown). Among readings that are the same rhythm at half or double
// speed, the one nearest the project tempo wins; without one, the 70-180 BPM window decides.
inline TempoGuess estimateTempo(const std::vector<float>& left, const std::vector<float>& right, double sr, double preferBpm = 0.0)
{
    TempoGuess g;
    const int n = (int) left.size();
    if (n < (int) (0.5 * sr) || sr <= 0.0) return g;
    std::vector<float> mono((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (left[(size_t) i] + (right.size() == left.size() ? right[(size_t) i] : left[(size_t) i]));

    // Onsets don't need the top octaves: average down to ~11 kHz first (4x less work at 44.1 / 48 kHz).
    const int dec = std::max(1, (int) std::lround(sr / 11025.0));
    std::vector<float> small((size_t) (n / dec));
    for (size_t i = 0; i < small.size(); ++i)
    {
        float a = 0.0f;
        for (int j = 0; j < dec; ++j) a += mono[i * (size_t) dec + (size_t) j];
        small[i] = a / (float) dec;
    }
    const double srd = sr / dec;
    const int hop = std::max(16, (int) std::lround(srd / 172.0));     // ~5.8 ms per frame
    double fps = 0.0;
    const auto nov = detail::noveltyCurve(small, srd, hop, fps);
    const double seconds = n / sr;
    const auto acf = detail::autocorr(nov, (int) (fps * 60.0 / 40.0 * 8.0) + 4);

    // 1. Coarse search, 50-220 BPM in quarter-BPM steps.
    double best = 0.0, bestScore = -1e9, sum = 0.0, sum2 = 0.0; int cnt = 0;
    for (double b = 50.0; b <= 220.0; b += 0.25)
    {
        const double s = detail::beatScore(acf, fps, b);
        sum += s; sum2 += s * s; ++cnt;
        if (s > bestScore) { bestScore = s; best = b; }
    }
    if (best <= 0.0) return g;
    const double mean = sum / cnt, sd = std::sqrt(std::max(1e-12, sum2 / cnt - mean * mean));
    g.confidence = (float) std::clamp((bestScore - mean) / sd / 6.0, 0.0, 1.0);

    // 2. The same rhythm can read as b/2, b or 2b. Keep the readings the rhythm supports nearly as well
    //    as the best one.
    struct Cand { double bpm, score; };
    std::vector<Cand> cands;
    for (double m : { 0.25, 0.5, 1.0, 2.0, 4.0 })
    {
        const double b = best * m;
        if (b < 55.0 || b > 210.0) continue;
        // refine around it: 0.02 BPM steps within +-3 %
        double rb = b, rs = -1e9;
        for (double x = b * 0.97; x <= b * 1.03; x += 0.02)
        {
            const double s = detail::beatScore(acf, fps, x);
            if (s > rs) { rs = s; rb = x; }
        }
        if (rs >= bestScore * 0.55) cands.push_back({ rb, rs });
    }
    if (cands.empty()) cands.push_back({ best, bestScore });

    // 3. Loops are almost always cut to whole bars: if the length says "exactly N bars" at a tempo within
    //    1.5 % of a candidate, that exact tempo is the answer (the rhythm alone is good to ~0.5 %).
    for (auto& c : cands)
    {
        const double bars = seconds * c.bpm / 240.0;
        const int nb = detail::snapBars(bars);
        const double exact = 240.0 * nb / seconds;
        if (std::abs(exact / c.bpm - 1.0) < 0.015 && std::abs(bars - nb) / nb < 0.02) c.bpm = exact;
    }

    // 4. Pick: nearest to the project tempo (on a log scale), else nearest the middle of 70-180.
    const double target = preferBpm > 0.0 ? preferBpm : 115.0;
    const Cand* pick = &cands.front();
    double pickCost = 1e9;
    for (const auto& c : cands)
    {
        double cost = std::abs(std::log2(c.bpm / target));
        if (preferBpm <= 0.0 && (c.bpm < 70.0 || c.bpm > 180.0)) cost += 1.0;
        cost -= 0.35 * (c.score / std::max(1e-9, bestScore));    // a clearly stronger reading still wins
        if (cost < pickCost) { pickCost = cost; pick = &c; }
    }
    g.bpm = pick->bpm;
    g.bars = seconds * g.bpm / 240.0;
    const int nb = detail::snapBars(g.bars);
    g.fromLength = std::abs(240.0 * nb / seconds / g.bpm - 1.0) < 1e-6;
    return g;
}

//==============================================================================
// Pitch-preserving time stretch. factor = output length / input length (2.0 = half speed).
// Returns false if cancelled.
inline bool stretchStereo(const std::vector<float>& inL, const std::vector<float>& inR, double factor, double sr,
                          std::vector<float>& outL, std::vector<float>& outR, const std::atomic<bool>* cancel = nullptr)
{
    const int n = (int) inL.size();
    const int outLen = std::max(1, (int) std::lround(n * factor));
    outL.assign((size_t) outLen, 0.0f);
    outR.assign((size_t) outLen, 0.0f);
    if (n == 0) return true;
    if (std::abs(factor - 1.0) < 1e-6)
    {
        std::copy(inL.begin(), inL.end(), outL.begin());
        std::copy(inR.begin(), inR.end(), outR.begin());
        return true;
    }
    const int N = sr > 60000.0 ? 8192 : 4096;     // ~90 ms window: resolves close notes in low chords (a 2048 window
                                                   // merged A3 and C4 into one smeared partial); attacks are handled by the cuts
    const int Hs = N / 4;                          // synthesis hop
    const double Ha = Hs / factor;                 // analysis hop (fractional; frames are taken at rounded positions)
    const int half = N / 2, bins = N / 2 + 1;
    const detail::FFT fft(N);

    std::vector<float> win(N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * (float) std::cos(2.0 * pi * i / N);
    const float olaGain = 1.0f / 1.5f;             // Hann analysis x Hann synthesis at 75 % overlap sums to 1.5

    auto sample = [&](const std::vector<float>& x, int i) { return i >= 0 && i < n ? x[(size_t) i] : 0.0f; };
    std::vector<std::complex<float>> XL(N), XR(N), YL(N), YR(N), Z(N);
    // per channel: last analysis phase and last output phase (the output state always matches what was written)
    std::array<std::vector<double>, 2> prevPhi { std::vector<double>(bins, 0.0), std::vector<double>(bins, 0.0) },
                                       prevOut { std::vector<double>(bins, 0.0), std::vector<double>(bins, 0.0) },
                                       phi { std::vector<double>(bins), std::vector<double>(bins) },
                                       outPhi { std::vector<double>(bins), std::vector<double>(bins) };
    std::array<std::vector<float>, 2> magC { std::vector<float>(bins), std::vector<float>(bins) };
    std::vector<float> magS(bins), prevMag(bins, 0.0f);
    std::vector<int> peakOf(bins), peaks;
    peaks.reserve((size_t) bins);
    std::vector<float> accL((size_t) outLen + N, 0.0f), accR((size_t) outLen + N, 0.0f);
    const int frames = (int) std::ceil((outLen + N) / (double) Hs) + 1;
    int prevPos = 0;
    double fluxAvg = 0.0;

    const int firstFrame = -(N / Hs);              // frames centred before 0 too, so the first samples get full overlap
    for (int m = firstFrame; m < frames; ++m)
    {
        if (cancel != nullptr && (m & 63) == 0 && cancel->load(std::memory_order_relaxed)) return false;
        const int posA = (int) std::lround(m * Ha) - half;     // frame start in the input (centred on m*Ha)
        const int posS = m * Hs - half;                        // frame start in the output
        // both channels in one complex FFT (L real, R imaginary), separated by symmetry
        for (int i = 0; i < N; ++i)
            Z[(size_t) i] = { sample(inL, posA + i) * win[(size_t) i], sample(inR, posA + i) * win[(size_t) i] };
        fft(Z);
        for (int k = 0; k < bins; ++k)
        {
            const auto a = Z[(size_t) k], b = std::conj(Z[(size_t) ((N - k) % N)]);
            XL[(size_t) k] = 0.5f * (a + b);
            XR[(size_t) k] = std::complex<float>(0.0f, -0.5f) * (a - b);
        }

        float flux = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            magC[0][(size_t) k] = detail::mag(XL[(size_t) k]); phi[0][(size_t) k] = std::arg(XL[(size_t) k]);
            magC[1][(size_t) k] = detail::mag(XR[(size_t) k]); phi[1][(size_t) k] = std::arg(XR[(size_t) k]);
            magS[(size_t) k] = magC[0][(size_t) k] + magC[1][(size_t) k];
            const float lm = std::log1p(10.0f * magS[(size_t) k]);
            flux += std::max(0.0f, lm - prevMag[(size_t) k]);
            prevMag[(size_t) k] = lm;
        }
        // an attack: this frame's flux jumps well above the recent average -> reset phases (keeps the hit sharp)
        const bool attack = m > firstFrame && flux > 2.5 * fluxAvg + 1.0 && flux > 4.0;
        fluxAvg = fluxAvg * 0.85 + flux * 0.15;

        const int dA = m == firstFrame ? 0 : (posA + half) - prevPos;
        if (m == firstFrame || attack || dA <= 0)
        {
            for (int c = 0; c < 2; ++c) for (int k = 0; k < bins; ++k) outPhi[(size_t) c][(size_t) k] = phi[(size_t) c][(size_t) k];
        }
        else
        {
            // peaks of the summed magnitude: every other bin follows the phase of the peak it belongs to
            peaks.clear();
            for (int k = 2; k < bins - 2; ++k)
            {
                const float v = magS[(size_t) k];
                if (v > magS[(size_t) k - 1] && v >= magS[(size_t) k + 1] && v > magS[(size_t) k - 2] && v >= magS[(size_t) k + 2] && v > 1e-6f)
                    peaks.push_back(k);
            }
            if (peaks.empty()) peaks.push_back(1);
            size_t p = 0;
            for (int k = 0; k < bins; ++k)
            {
                while (p + 1 < peaks.size() && std::abs(peaks[p + 1] - k) < std::abs(peaks[p] - k)) ++p;
                peakOf[(size_t) k] = peaks[p];
            }
            for (int c = 0; c < 2; ++c)
            {
                auto& op = outPhi[(size_t) c]; const auto& ph = phi[(size_t) c];
                for (int pk : peaks)
                {
                    const double omega = 2.0 * pi * pk / N;
                    const double dev = detail::princarg(ph[(size_t) pk] - prevPhi[(size_t) c][(size_t) pk] - omega * dA);
                    op[(size_t) pk] = prevOut[(size_t) c][(size_t) pk] + (omega + dev / dA) * Hs;
                }
                for (int k = 0; k < bins; ++k)
                {
                    const int pk = peakOf[(size_t) k];
                    if (pk != k) op[(size_t) k] = op[(size_t) pk] + (ph[(size_t) k] - ph[(size_t) pk]);
                }
            }
        }
        prevPos = posA + half;

        // One rotation per bin, taken from the louder channel and applied to both: the left/right phase
        // relation (the stereo image) is kept, and even fully out-of-phase content stays coherent.
        for (int k = 0; k < bins; ++k)
        {
            const int c = magC[0][(size_t) k] >= magC[1][(size_t) k] ? 0 : 1;
            const double r = outPhi[(size_t) c][(size_t) k] - phi[(size_t) c][(size_t) k];
            const std::complex<float> z((float) std::cos(r), (float) std::sin(r));
            YL[(size_t) k] = XL[(size_t) k] * z;
            YR[(size_t) k] = XR[(size_t) k] * z;
            for (int ch = 0; ch < 2; ++ch)
            {
                prevPhi[(size_t) ch][(size_t) k] = phi[(size_t) ch][(size_t) k];
                prevOut[(size_t) ch][(size_t) k] = detail::princarg(phi[(size_t) ch][(size_t) k] + r);
            }
        }
        // back to time: W = YL + i*YR over the full (Hermitian) spectrum; ifft(W) = conj(FFT(conj(W))) / N,
        // whose real part is the left channel and imaginary part the right
        for (int k = 0; k < N; ++k)
        {
            const int kk = k < bins ? k : N - k;
            const auto l = k < bins ? YL[(size_t) kk] : std::conj(YL[(size_t) kk]);
            const auto r = k < bins ? YR[(size_t) kk] : std::conj(YR[(size_t) kk]);
            Z[(size_t) k] = std::conj(l + std::complex<float>(0.0f, 1.0f) * r);
        }
        fft(Z);
        const float norm = olaGain / (float) N;
        for (int i = 0; i < N; ++i)
        {
            const int o = posS + i;
            if (o < 0 || o >= outLen) continue;
            accL[(size_t) o] += Z[(size_t) i].real() * norm * win[(size_t) i];
            accR[(size_t) o] -= Z[(size_t) i].imag() * norm * win[(size_t) i];
        }
    }
    std::copy(accL.begin(), accL.begin() + outLen, outL.begin());
    std::copy(accR.begin(), accR.begin() + outLen, outR.begin());
    return true;
}

//==============================================================================
// The stretch the plugin uses: the loop is cut at its strong attacks and each piece is stretched on its
// own, then laid back at its scaled start. Each attack starts exactly where it should (a stretch across
// an attack smears it into the 40 ms before), and nothing leaks across a cut. 2 ms fades join the pieces.
inline bool stretchLoop(const std::vector<float>& inL, const std::vector<float>& inR, double factor, double sr, Material material,
                        std::vector<float>& outL, std::vector<float>& outR, const std::atomic<bool>* cancel = nullptr,
                        const std::vector<int>* knownCuts = nullptr)   // piece starts, if the loop was already cut
{
    const int n = (int) inL.size();
    const int outLen = std::max(1, (int) std::lround(n * factor));
    if (std::abs(factor - 1.0) < 1e-6 || n < 4096) return stretchStereo(inL, inR, factor, sr, outL, outR, cancel);

    // Cut points: attacks (energy) always, plus note / chord changes for melodies (a new note on a held
    // chord has little energy jump). Strong ones only, at least 60 ms apart.
    // Known cuts (the loop's slices) come first, so every slice starts exactly on a piece; attacks the
    // slicer didn't split (a pluck inside a melodic slice) still get a piece of their own.
    std::vector<std::pair<int, float>> found;
    if (knownCuts != nullptr) for (int c : *knownCuts) found.push_back({ c, 2.0f });
    for (Material m : { Material::drums, Material::melodic })
    {
        if (m == Material::melodic && (material == Material::drums || knownCuts != nullptr)) continue;
        AnalysisSettings a;
        a.material = m;
        a.sensitivity = 0.8f;
        a.autoAlign = false;
        a.cancel = cancel;
        const auto d = analyse(inL, inR, sr, a);
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) return false;
        float top = 0.0f;
        for (const auto& sl : d->slices) top = std::max(top, sl.peak);
        // in a melody, an energy jump only counts when it's a real hit (a drum in a flip), not a swell
        const float keep = m == Material::drums && material != Material::drums ? (knownCuts != nullptr ? 0.3f : 0.6f) : 0.2f;
        for (const auto& sl : d->slices) if (sl.peak >= keep * top) found.push_back({ sl.onset, sl.peak });   // cut right on the attack
    }
    std::sort(found.begin(), found.end());
    std::vector<int> cuts { 0 };
    const int minGap = (int) (0.06 * sr);
    for (const auto& f : found)
    {
        if (n - f.first < minGap || f.first <= 0) continue;
        if (f.first - cuts.back() >= minGap) { cuts.push_back(f.first); continue; }
        // two candidates closer than 60 ms: a known cut (a slice) wins over a found attack
        if (f.second > 1.5f && cuts.size() > 1 && f.first - cuts[cuts.size() - 2] >= minGap) cuts.back() = f.first;
    }
    cuts.push_back(n);

    outL.assign((size_t) outLen, 0.0f);
    outR.assign((size_t) outLen, 0.0f);
    // Each piece is stretched on its own audio only: the next attack is never inside a piece's last window,
    // so it can't smear back into the tail before it. The joins are 3 ms fades at the cut (which sits in
    // the pre-roll just before an attack, where the old sound is ending anyway).
    const int xf = std::max(16, (int) (0.003 * sr));
    std::vector<float> segL, segR, oL, oR;
    for (size_t c = 0; c + 1 < cuts.size(); ++c)
    {
        const bool last = c + 2 == cuts.size();
        const int a0 = cuts[c], a1 = cuts[c + 1];
        const int o0 = (int) std::lround(a0 * factor), o1 = last ? outLen : (int) std::lround(a1 * factor);
        if (o1 <= o0) continue;
        segL.assign(inL.begin() + a0, inL.begin() + a1);
        segR.assign(inR.begin() + a0, inR.begin() + a1);
        if (! stretchStereo(segL, segR, (double) (o1 - o0) / (a1 - a0), sr, oL, oR, cancel)) return false;
        const int len = std::min((int) oL.size(), o1 - o0);
        // The attack itself is copied untouched from the original (12 ms), then crossfades (6 ms) into the
        // stretched sound: a phase vocoder spreads an attack over its whole window, so a hit would come in
        // soft and a few ms late. 12 ms isn't stretched, which nobody can hear; a soft attack everybody can.
        const int keep = std::min({ (int) (0.012 * sr), len / 3, a1 - a0 }), blend = std::min((int) (0.006 * sr), keep);
        const int in = std::max(4, (int) (0.001 * sr));
        for (int i = 0; i < len; ++i)
        {
            float l = oL[(size_t) i], r = oR[(size_t) i];
            if (i < keep)
            {
                const float w = i < keep - blend ? 1.0f : std::cos(0.5f * (float) pi * (float) (i - (keep - blend)) / blend);
                l = w * inL[(size_t) (a0 + i)] + (1.0f - w) * l;
                r = w * inR[(size_t) (a0 + i)] + (1.0f - w) * r;
            }
            float g = 1.0f;
            if (c > 0 && i < in) g = (float) i / in;                                                     // no click at the cut
            if (! last && len - i <= xf) g *= std::sin(0.5f * (float) pi * (float) (len - i) / xf);    // the old tail out
            outL[(size_t) (o0 + i)] += g * l;
            outR[(size_t) (o0 + i)] += g * r;
        }
    }
    return true;
}

//==============================================================================
// The loop after its stretch, already cut: every piece started on one of the cut loop's attacks, so each
// slice's attack is now exactly at onset x factor. Its slices are carried over (no second analysis, so the
// cuts can't differ from the ones you saw a moment ago, and a long sample is ready in half the time).
inline std::shared_ptr<LoopData> rescaleLoop(const LoopData& cut, std::vector<float> left, std::vector<float> right, double factor, double newBpm)
{
    auto d = std::make_shared<LoopData>();
    d->left = std::move(left); d->right = std::move(right);
    d->length = (int) d->left.size();
    d->sampleRate = cut.sampleRate;
    d->material = cut.material;
    d->bars = cut.bars;
    d->bpm = newBpm;
    d->stepSamples = cut.samplesPerStep() * factor;
    d->alignSteps = cut.alignSteps;
    const int n = d->length;
    d->overview.assign(512, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        auto& o = d->overview[(size_t) std::min(511, (int) ((int64_t) i * 512 / std::max(1, n)))];
        o = std::max(o, std::abs(0.5f * (d->left[(size_t) i] + d->right[(size_t) i])));
    }
    d->slices.reserve(cut.slices.size());
    for (size_t i = 0; i < cut.slices.size(); ++i)
    {
        Slice sl = cut.slices[i];
        const int pre = sl.onset - sl.start;                       // the pre-roll stays the same length in time
        sl.onset = std::clamp((int) std::lround(cut.slices[i].onset * factor), 0, std::max(0, n - 1));
        sl.start = std::max(0, sl.onset - pre);
        d->slices.push_back(sl);
    }
    for (size_t i = 0; i < d->slices.size(); ++i)
    {
        auto& sl = d->slices[i];
        const int next = i + 1 < d->slices.size() ? d->slices[i + 1].start : n;
        sl.end = std::max(sl.start + 1, std::min(n, next));
    }
    return d;
}
} // namespace bounce
