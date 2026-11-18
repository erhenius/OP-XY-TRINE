#pragma once
#include "excitation.hpp"

namespace m3_resonator {
using opxy::i32;
using opxy::u32;
constexpr unsigned band_count = 3;
constexpr unsigned frames = 64;
enum ControlIndex { Cutoff, Resonance, Shape, Decay, ControlCount };

struct Parameters { i32 value[ControlCount]; };
struct Band { i32 first, second; };
struct SweepState { i32 integrator1, integrator2; };

struct Voice {
    Band band[band_count];
    SweepState left, right;
    Parameters current;
    i32 bank_gain;
    i32 feedback[band_count];
    i32 negative_radius_squared, modal_feed;
    i32 last_decay;
    unsigned pitch_key;
    Mode mode;
    bool initialized;
    bool bank_ready;
};

struct Coefficients {
    Parameters key;
    i32 a1, a2, a3;
    i32 half_damping;
    i32 low, band, high;
    i32 output_gain;  // Q30
    bool valid;
};

static_assert(sizeof(Voice) + sizeof(u32) <= 0x90, "Voice and object header exceed stock filter slot");

void reset(Voice&);
unsigned decay_ms(unsigned position);
void perform(Voice&, Coefficients&, const Parameters&, i32*, Excitation&, Setup, bool stereo, unsigned note);
}
