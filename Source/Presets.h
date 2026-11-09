#pragma once
// Epsilon Bounce factory presets: three or four flavours per genre, each a different pocket.
// A preset only moves the feel controls (groove, amount, swing, tighten, humanize, accent, feel,
// time) - never the loaded loop, slicing or output level.
#include "BounceEngine.h"

namespace bounce
{
enum class TimeMode { normal = 0, half, doubleTime };
inline double timeFactor(TimeMode m) { return m == TimeMode::half ? 0.5 : m == TimeMode::doubleTime ? 2.0 : 1.0; }

struct Preset
{
    const char* name;
    const char* family;     // menu section
    Groove groove;
    float amount, swing, tighten, humanizeMs, accent, feelMs;   // percent / ms, as the knobs show them
    TimeMode time;
    const char* note;       // one line for the manual and the tooltip
};

using T = TimeMode;
inline const Preset presets[] = {
    //  name                          family        groove               amt  swg  tight hum  acc  feel  time
    { "Quantize Clean",               "Straight",   Groove::straight,    100,  0, 100,  0,    0,   0, T::normal,     "Snaps every hit to the grid and follows your tempo - a slice-based time-stretch with no smearing." },
    { "Human Drummer",                "Straight",   Groove::straight,    100,  0,  60,  6,   50,   0, T::normal,     "Keeps 40 % of the original timing and adds a few ms of drift that is the same every playback." },
    { "Double-Time Roll",             "Straight",   Groove::straight,    100,  0, 100,  1,    0,   0, T::doubleTime, "Plays the loop twice as fast on the grid - instant rolls and fills from any drum loop." },

    { "Dembow Clasico",               "Dembow",     Groove::dembow,      100,  0,  60,  2,   75,   0, T::normal,     "The textbook pocket: the 'ch' on 3 and 11 leans late, 6 and 14 push." },
    { "Dembow Romantico",             "Dembow",     Groove::dembow,       80, 10,  50,  4,   50,   6, T::normal,     "Softer, laid back 6 ms, a little shuffle - for the slow, melodic side." },
    { "Dembow Callejero",             "Dembow",     Groove::dembow,      130, 15,  80,  3,  100,  -4, T::normal,     "Exaggerated lean, hard accents, pushed ahead of the beat - street energy." },
    { "Dembow Half-Time Perreo",      "Dembow",     Groove::dembow,      100,  0,  70,  2,   80,   3, T::half,       "Half-time: the loop stretches over twice the bars for a heavy, slow perreo." },

    { "Drill UK Classic",             "Drill",      Groove::drill,       100,  0,  80,  2,   75,   0, T::normal,     "The ~62 % shuffle with the dragging half-time snare." },
    { "Drill NY Bounce",              "Drill",      Groove::drill,       120, 10,  70,  3,   90,  -3, T::normal,     "More swing and pushed forward - the Brooklyn bounce." },
    { "Drill Bronx Slide",            "Drill",      Groove::drill,       110, 20,  60,  5,   80,   5, T::normal,     "Heaviest swing, laid back - hats slide into the snare." },
    { "Drill Half-Time Slide",        "Drill",      Groove::drill,       100, 10,  70,  3,   75,   2, T::half,       "Half-time drill: a fast loop becomes the slow, sliding drill frame." },

    { "Trap Atlanta Tight",           "Trap",       Groove::trap,        100,  0, 100,  0,   75,  -2, T::normal,     "Dead tight, downbeats a hair early - classic Atlanta." },
    { "Trap Melodic Float",           "Trap",       Groove::trap,         80,  8,  70,  4,   50,   4, T::normal,     "Loose and floating for melodic trap and R&B." },
    { "Trap Rage Rush",               "Trap",       Groove::trap,        120,  0, 100,  0,  100,  -8, T::normal,     "Everything pushed 8 ms early, accents slammed - rage urgency." },
    { "Trap Hi-Hat Double",           "Trap",       Groove::trap,        100,  0, 100,  1,   60,   0, T::doubleTime, "Double-time: a hat or perc loop becomes 32nd-note rolls." },

    { "Reggaeton Comercial",          "Reggaeton",  Groove::reggaeton,   100,  0,  90,  1,   80,   0, T::normal,     "Clean radio dembow: almost on the grid, accents doing the work." },
    { "Reggaeton Old School",         "Reggaeton",  Groove::reggaeton,   120,  6,  60,  4,   90,   3, T::normal,     "2000s feel: more lean, more swing, laid back." },
    { "Moombahton Half-Time",         "Reggaeton",  Groove::reggaeton,   100,  8,  70,  3,   75,   2, T::half,       "Half-time reggaeton - the moombah tempo from a reggaeton loop." },

    { "Afrobeats Lagos Roll",         "Afrobeats",  Groove::afrobeats,   100,  0,  60,  3,   75,   0, T::normal,     "The rolling 3-3-2 with light swing." },
    { "Afro-Swing",                   "Afrobeats",  Groove::afrobeats,   110, 15,  50,  4,   70,   4, T::normal,     "London afro-swing: more shuffle, laid back." },
    { "Afro-Drill",                   "Afrobeats",  Groove::afrobeats,   100, 25,  70,  2,   85,  -3, T::normal,     "Afro accents with drill-level swing, pushed - the crossover." },

    { "Amapiano Log Drum Lazy",       "Amapiano",   Groove::amapiano,    100,  0,  60,  3,   75,   5, T::normal,     "Shakers shuffled, log-drum 8ths late - the yanos pocket." },
    { "Amapiano Private School",      "Amapiano",   Groove::amapiano,     80,  0,  50,  5,   40,   8, T::normal,     "Soft, jazzy and very late - private-school piano." },
    { "Amapiano Bacardi",             "Amapiano",   Groove::amapiano,    120,  0,  90,  1,  100,  -2, T::normal,     "Tighter and punchier for the Bacardi / gqom edge." },

    { "Jersey Bed-Squeak Bounce",     "Jersey Club", Groove::jerseyClub, 100,  0, 100,  0,  100,  -3, T::normal,     "The five-kick bounce, pushed and ducked around the kicks." },
    { "Philly Club",                  "Jersey Club", Groove::jerseyClub, 110,  6,  90,  2,   90,   0, T::normal,     "A little swing on top - the Philly cousin." },
    { "Jersey Double-Time Run",       "Jersey Club", Groove::jerseyClub, 100,  0, 100,  0,  100,   0, T::doubleTime, "Double-time: half-speed loops become 140+ club runs." },

    { "Boom Bap Golden Era",          "Boom Bap",   Groove::boomBap,     100,  0,  80,  3,   75,   2, T::normal,     "MPC 58 % with the snare sitting back." },
    { "Boom Bap SP-1200 Dusty",       "Boom Bap",   Groove::boomBap,     100,  6,  60,  5,   60,   4, T::normal,     "Looser, dustier, a touch more swing." },
    { "Boom Bap Dilla Drunk",         "Boom Bap",   Groove::boomBap,     130, 15,  20,  9,   50,   8, T::normal,     "Barely quantized, exaggerated swing and drift - the drunk feel." },

    { "UKG 2-Step Skip",              "UK Garage",  Groove::ukGarage,    100,  0,  70,  3,   75,   0, T::normal,     "The 66 % shuffle with the skipping kick." },
    { "Speed Garage",                 "UK Garage",  Groove::ukGarage,    110,  0,  90,  1,   90,  -3, T::normal,     "Tighter and pushed for the speed garage drive." },
    { "UKG Bassline Swing",           "UK Garage",  Groove::ukGarage,    120, 10,  70,  2,   80,   0, T::normal,     "Extra swing for bassline / niche." },

    { "Lo-Fi Study Session",          "Lo-Fi",      Groove::lofi,        100,  0,  50,  6,   40,   6, T::normal,     "Lazy and late, accents flattened." },
    { "Lo-Fi Tape Wobble",            "Lo-Fi",      Groove::lofi,        120,  8,  30, 12,   30,  10, T::normal,     "Loose timing and 12 ms of drift - a worn tape machine." },
    { "Lo-Fi Half-Time Rain",         "Lo-Fi",      Groove::lofi,        100,  5,  40,  8,   30,   8, T::half,       "Half-time and late - for slowed, rainy beats." },

    { "Baile Funk Tamborzao",         "Baile Funk", Groove::bailefunk,   100,  0,  80,  2,   90,   0, T::normal,     "The tamborzao: the 3 and 10 rush, the 6 sits back." },
    { "Funk Mandelao",                "Baile Funk", Groove::bailefunk,   130,  0, 100,  0,  100,  -5, T::normal,     "Exaggerated and slammed - mandelao aggression." },
    { "Funk BH",                      "Baile Funk", Groove::bailefunk,    90,  8,  60,  4,   70,   3, T::normal,     "Looser, swung, laid back - Belo Horizonte style." },

    { "Phonk Memphis Cowbell",        "Phonk",      Groove::phonk,       100,  0,  70,  3,   80,   3, T::normal,     "Medium swing, the half-time snare heavy and late." },
    { "Drift Phonk",                  "Phonk",      Groove::phonk,       120,  6,  90,  1,  100,  -4, T::normal,     "Tighter, pushed, accents slammed for drift phonk." },
    { "Phonk Half-Time Haunt",        "Phonk",      Groove::phonk,       100, 10,  60,  4,   75,   6, T::half,       "Half-time and late - the haunted Memphis crawl." },
};
constexpr int presetCount = (int) (sizeof(presets) / sizeof(presets[0]));
} // namespace bounce
