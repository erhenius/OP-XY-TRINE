#pragma once
#include <opxy/types.hpp>

namespace m3_display {
using opxy::i32;
using opxy::u32;

#include "display_tables.inc"

constexpr u32 curve_points = 33;
constexpr i32 graph_top = 60, graph_bottom = 156;

inline u32 normalized(i32 value) { return value > 0 ? u32(value) >> 16 : 0; }

inline unsigned interpolate(const unsigned* table, u32 value, unsigned bits) {
    const unsigned shift = 15u - bits;
    const unsigned bin = value >> shift;
    const unsigned scale = 1u << shift;
    const unsigned fraction = value & (scale - 1u);
    // Both tables and weights are non-negative and their sum fits 32 bits.
    return (table[bin] * (scale - fraction) + table[bin + 1] * fraction + scale / 2) >> shift;
}

inline unsigned cutoff_hz(i32 value) {
    return interpolate(hz_table, normalized(value), 6);
}

inline unsigned resonance_hundredths(i32 value) {
    const unsigned damping = interpolate(damping_table, normalized(value), 4);
    return (102400u + damping / 2) / damping;
}

inline unsigned gain_height(u32 power) {
    unsigned low = 0, high = sizeof(db_table) / sizeof(db_table[0]) - 1;
    if (power >= db_table[high]) return high;
    while (high - low > 1) {
        const unsigned center = (low + high) / 2;
        if (db_table[center] <= power) low = center;
        else high = center;
    }
    return low;
}

// Bilinear frequency warp of the sweep's LP/BP/HP mix. Plot the sweep's
// normalized response before level makeup, ringing or any input excitation.
inline void sweep_pixels(i32 cutoff, i32 resonance, i32 shape, i32 (&out)[curve_points]) {
    constexpr i32 unity = 1024;
    const unsigned center = interpolate(frequency_table, normalized(cutoff), 6);
    const i32 damping = interpolate(damping_table, normalized(resonance), 4);
    const i32 mix = i32(normalized(shape));
    const i32 low = mix < 16384 ? (32768 - 2 * mix) >> 5 : 0;
    const i32 high = mix > 16384 ? (2 * (mix - 16384) + 16) >> 5 : 0;
    const i32 band = unity - low - high;
    for (unsigned i = 0; i < curve_points; ++i) {
        const unsigned f = plot_frequency[i];
        const bool lower = f <= center;
        const i32 ratio = i32(lower ? (f << 10) / center : (center << 10) / f);
        const i32 square = ratio * ratio >> 10;
        const i32 lp = lower ? unity : square;
        const i32 hp = lower ? square : unity;
        const i32 real = (low * lp - high * hp) / unity;
        const i32 imag = band * ratio >> 10;
        const i32 dr = lp - hp;
        const i32 di = damping * ratio >> 10;
        const u32 power = (u32(real * real + imag * imag) << 11) / u32(dr * dr + di * di);
        out[i] = graph_bottom - i32(2 * gain_height(power));
    }
}
}
