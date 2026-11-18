#pragma once
#include <opxy/types.hpp>

namespace m3_resonator {
enum class Exciter : opxy::u8 { Clean, Double, Pulse, Sub, Fold, Wrap, Sigma };
enum class Mode : opxy::u8 { Wide, Bypass };

struct Setup {
    Exciter exciter;
    Mode mode;
    unsigned blend;
    bool whole;
    int interval1;
    int interval2;
};

constexpr unsigned setup_bits = 25;
constexpr unsigned setup_mask = (1u << setup_bits) - 1u;

inline bool valid_setup(unsigned word) {
    return word <= setup_mask && (word & 7) < 7 && ((word >> 3) & 3) <= 1
        && ((word >> 6) & 127) <= 100 && ((word >> 13) & 63) <= 49 && (word >> 19) <= 49;
}

// Zero fields retain the default major triad and keep zero-filled projects useful.
inline int decode_interval(unsigned field, int fallback) {
    return field ? int(field) - 25 : fallback;
}

inline unsigned encode_interval(int interval, int fallback) {
    return interval == fallback ? 0u : unsigned(interval + 25);
}

inline Setup unpack(unsigned word) {
    if (!valid_setup(word)) word = 0;
    return {Exciter(word & 7), Mode((word >> 3) & 3), (word >> 6) & 127, (word & 32) != 0,
            decode_interval((word >> 13) & 63, 4), decode_interval(word >> 19, 7)};
}

inline unsigned pack(Setup s) {
    return unsigned(s.exciter) | (unsigned(s.mode) << 3) | (unsigned(s.whole) << 5) | (s.blend << 6)
         | (encode_interval(s.interval1, 4) << 13) | (encode_interval(s.interval2, 7) << 19);
}

inline unsigned migrate_v1(unsigned word) {
    if (((word >> 3) & 3) == 2) word &= ~(3u << 3); // Old Focus becomes Wide.
    return word < 8192 && valid_setup(word) ? word : 0;
}

inline int bounded(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

inline unsigned edit(unsigned word, unsigned knob, int delta) {
    Setup s = unpack(word);
    delta = bounded(delta, -100, 100);
    switch (knob) {
    case 0:
        if (delta) {
            const int first = int(Exciter::Pulse);
            const int current = int(s.exciter);
            // The two legacy modes retain their saved sound until K1 turns.
            const int index = current < first ? (delta > 0 ? -1 : 0) : current - first;
            s.exciter = Exciter(first + (index + delta % 5 + 10) % 5);
        }
        break;
    case 2: s.interval1 = bounded(s.interval1 + delta, -24, 24); break;
    case 1: s.blend = bounded(int(s.blend) + delta, 0, 100); break;
    case 3: s.interval2 = bounded(s.interval2 + delta, -24, 24); break;
    }
    return pack(s);
}

inline unsigned click(unsigned word, unsigned knob) {
    Setup s = unpack(word);
    if (knob == 2) s.mode = s.mode == Mode::Wide ? Mode::Bypass : Mode::Wide;
    if (knob == 3) s.whole = !s.whole;
    return pack(s);
}

inline unsigned decay_position(opxy::i32 value) {
    return value > 0 ? static_cast<unsigned>(value) >> 26 : 0;
}

inline unsigned played_note(opxy::i32 stock_key) {
    return stock_key < 0 ? 0 : static_cast<unsigned>(stock_key) >> 24;
}
}
