#include "resonator.hpp"
#include <opxy/q31.hpp>

namespace m3_resonator {
namespace {
using opxy::q31::add;
using opxy::q31::half;
using opxy::q31::maximum;
using opxy::q31::multiply;
using opxy::q31::shift_left;

#include "coefficients.inc"

constexpr unsigned control_frames = 16;
constexpr i32 snap_threshold = 1 << 11;
constexpr i32 middle = 1 << 30;
constexpr unsigned decay_fraction_bits = 26;
constexpr u32 decay_unit = 1u << decay_fraction_bits;
constexpr i32 minimum = -maximum - 1;
constexpr i32 pan_width = 987842478;  // 0.46 in Q31
// Begin tapering at Q ~1.46 and reach zero makeup near Q ~3.91.
constexpr i32 makeup_taper_start = 3 << 28;
constexpr i32 lp_hp_makeup = 1457720529;  // +4.5 dB expressed as gain above unity in Q31
constexpr i32 bp_extra_makeup = maximum - lp_hp_makeup;  // raises BP to +6 dB

i32 negate(i32 value) { return value == minimum ? maximum : -value; }
i32 subtract(i32 a, i32 b) { return add(a, negate(b)); }

// Value ranges are non-negative Q31. Division by 32 gives approximately
// 11.4 ms of smoothing at the 16-frame control interval.
i32 approach(i32 current, i32 target) {
    const i32 difference = target - current;
    if (difference > -snap_threshold && difference < snap_threshold) return target;
    return current + difference / 32;
}

struct Location {
    unsigned index;
    i32 fraction;
};

Location locate(i32 value, unsigned bits) {
    const u32 position = static_cast<u32>(value);
    const unsigned fraction_bits = 31 - bits;
    return {position >> fraction_bits,
            static_cast<i32>((position & ((1u << fraction_bits) - 1)) << bits)};
}

i32 mix(i32 a, i32 b, i32 fraction) {
    return add(a, multiply(subtract(b, a), fraction));
}

SweepCoefficients sweep_mix(const SweepCoefficients& a, const SweepCoefficients& b, i32 fraction) {
    return {mix(a.a1, b.a1, fraction), mix(a.a2, b.a2, fraction), mix(a.a3, b.a3, fraction)};
}

bool same(const Parameters& a, const Parameters& b) {
    for (unsigned i = 0; i < Decay; ++i)
        if (a.value[i] != b.value[i]) return false;
    return true;
}

void prepare(Coefficients& c, const Parameters& parameters) {
    if (c.valid && same(c.key, parameters)) return;
    const i32* p = parameters.value;
    if (!c.valid || p[Cutoff] != c.key.value[Cutoff] || p[Resonance] != c.key.value[Resonance]) {
        static_assert(cutoff_steps == 64 && quality_steps == 16, "Control table grid changed");
        const auto cutoff = locate(p[Cutoff], 6);
        const auto q = locate(p[Resonance], 4);
        const auto& a = sweep_table[cutoff.index];
        const auto& b = sweep_table[cutoff.index + 1];
        const auto coeff = sweep_mix(sweep_mix(a[q.index], a[q.index + 1], q.fraction),
                                     sweep_mix(b[q.index], b[q.index + 1], q.fraction), cutoff.fraction);
        c.a1 = coeff.a1;
        c.a2 = coeff.a2;
        c.a3 = coeff.a3;
        c.half_damping = mix(half_damping[q.index], half_damping[q.index + 1], q.fraction);
    }
    if (!c.valid || p[Shape] != c.key.value[Shape]) {
        const i32 shape = p[Shape];
        c.low = shape < middle ? maximum - 2 * shape : 0;
        c.high = shape > middle ? 2 * (shape - middle) : 0;
        c.band = maximum - c.low - c.high;
    }
    if (!c.valid || p[Shape] != c.key.value[Shape] || p[Resonance] != c.key.value[Resonance]) {
        const i32 over = p[Resonance] - makeup_taper_start;
        const i32 taper = maximum - shift_left(over > 0 ? over : 0, 1);
        const i32 shape_makeup = add(lp_hp_makeup, multiply(bp_extra_makeup, c.band));
        c.output_gain = add(middle, half(multiply(shape_makeup, taper)));
    }
    c.key = parameters;
    c.valid = true;
}

void prepare_bank(Voice& voice, Setup setting, unsigned played) {
    const i32 decay = voice.current.value[Decay];
    const int last_note = int(sizeof(note_cosine) / sizeof(note_cosine[0])) - 1;
    const int root = bounded(int(played), 0, last_note);
    const int first = bounded(root + setting.interval1, 0, last_note);
    const int second = bounded(root + setting.interval2, 0, last_note);
    const unsigned pitches = unsigned(root) | unsigned(first) << 8 | unsigned(second) << 16;
    if (voice.bank_ready && voice.last_decay == decay && voice.pitch_key == pitches) return;
    const unsigned position = static_cast<unsigned>(decay);
    const unsigned bin = position >> decay_fraction_bits;
    const i32 fraction = i32((position & (decay_unit - 1u)) << (31 - decay_fraction_bits));
    const i32 radius = mix(radii[bin], radii[bin + 1], fraction);
    const i32 squared = multiply(radius, radius);
    voice.negative_radius_squared = -squared;
    voice.modal_feed = multiply(maximum - squared, 1928128284);  // -0.94 dB from unity at band center
    voice.feedback[0] = multiply(radius, note_cosine[root]);
    voice.feedback[1] = multiply(radius, note_cosine[first]);
    voice.feedback[2] = multiply(radius, note_cosine[second]);
    voice.last_decay = decay;
    voice.pitch_key = pitches;
    voice.bank_ready = true;
}

i32 render_band(Band& state, i32 coefficient, i32 radius_squared, i32 feed) {
    const i32 output = add(feed, state.first);
    state.first = add(state.second, shift_left(multiply(coefficient, output), 1));
    state.second = add(-feed, multiply(radius_squared, output));
    return output;
}

i32 filter(SweepState& sweep, const Coefficients& c, i32 input) {
    const i32 v3 = subtract(input, sweep.integrator2);
    const i32 bp = add(multiply(c.a1, sweep.integrator1), multiply(c.a2, v3));
    const i32 lp = add(sweep.integrator2, add(multiply(c.a2, sweep.integrator1), multiply(c.a3, v3)));
    sweep.integrator1 = add(bp, subtract(bp, sweep.integrator1));
    sweep.integrator2 = add(lp, subtract(lp, sweep.integrator2));
    if (c.high == 0) {
        if (c.band == 0) return multiply(c.low, lp);
        return add(multiply(c.low, lp), multiply(c.band, bp));
    }
    const i32 hp = subtract(subtract(input, shift_left(multiply(c.half_damping, bp), 1)), lp);
    return add(multiply(c.band, bp), multiply(c.high, hp));
}

i32 level(const Coefficients& c, i32 output) {
    if (c.output_gain == middle) return output;
    return shift_left(multiply(c.output_gain, output), 1);
}

i32 bank_level(i32 sample, i32 gain) {
    return gain == maximum ? sample : multiply(gain, sample);
}

template<Mode mode>
i32 route(SweepState& sweep, const Coefficients& c, i32 dry, i32 wet) {
    if (mode == Mode::Bypass) {
        const i32 post = half(half(wet));
        return level(c, add(filter(sweep, c, add(dry, subtract(wet, post))), post));
    }
    return level(c, filter(sweep, c, add(dry, wet)));
}

template<bool Stereo, Mode mode, bool Drive>
void render_span(Voice& voice, const Coefficients& c, i32* block, unsigned count, Excitation& exc) {
    const i32 gain = excitation::attenuate(voice.bank_gain, exc.route_gain);
    const bool ringing = voice.bank_gain != 0;
    constexpr unsigned stride = Stereo ? 2 : 1;
    for (unsigned frame = 0; frame < count; ++frame, block += stride) {
        i32 left = half(block[0]);
        i32 right = Stereo ? half(block[1]) : 0;
        if (Drive && exc.current.whole) {
            left = excitation::apply(exc, 0, left);
            if (Stereo) right = excitation::apply(exc, 1, right);
        }

        i32 wet = 0, side = 0;
        if (ringing) {
            i32 input = Stereo ? half(add(left, right)) : left;
            if (Drive && !exc.current.whole) input = excitation::apply(exc, 0, input);
            const i32 feed = multiply(voice.modal_feed, input);
            const i32 radius_squared = voice.negative_radius_squared;
            const i32 root = render_band(voice.band[0], voice.feedback[0], radius_squared, feed);
            const i32 first = multiply(1610612736, render_band(voice.band[1], voice.feedback[1], radius_squared, feed));
            const i32 second = multiply(1342177280, render_band(voice.band[2], voice.feedback[2], radius_squared, feed));
            wet = bank_level(add(root, add(first, second)), gain);
            if (Stereo) side = bank_level(multiply(pan_width, subtract(first, second)), gain);
        }
        block[0] = route<mode>(voice.left, c, left, add(wet, side));
        if (Stereo) block[1] = route<mode>(voice.right, c, right, subtract(wet, side));
    }
}

template<bool Stereo, bool Drive>
void span(Voice& voice, const Coefficients& c, i32* block, unsigned count, Excitation& exc) {
    if (voice.mode == Mode::Wide) render_span<Stereo, Mode::Wide, Drive>(voice, c, block, count, exc);
    else render_span<Stereo, Mode::Bypass, Drive>(voice, c, block, count, exc);
}

template<bool Stereo>
void process_block(Voice& voice, Coefficients& cache, const Parameters& targets, i32* block,
                   Excitation& exc, Setup setting, unsigned note) {
    Parameters target = targets;
    for (unsigned i = 0; i < ControlCount; ++i)
        if (target.value[i] < 0) target.value[i] = 0;
    const unsigned position = decay_position(target.value[Decay]);
    const i32 target_gain = position ? maximum : 0;
    target.value[Decay] = position ? i32((position - 1u) * decay_unit) : voice.current.value[Decay];
    if (!voice.initialized) {
        voice.current = target;
        voice.bank_gain = target_gain;
        voice.initialized = true;
    }
    if (!voice.bank_gain) voice.current.value[Decay] = target.value[Decay];
    const bool stable = exc.started && exc.amount == 0 && (!setting.blend || setting.exciter == Exciter::Clean)
        && exc.current.exciter == setting.exciter && exc.current.whole == setting.whole
        && exc.current.mode == setting.mode && exc.route_gain == maximum;
    if (stable && same(voice.current, target) && voice.current.value[Decay] == target.value[Decay]
            && voice.bank_gain == target_gain) {
        prepare(cache, target);
        if (voice.bank_gain) prepare_bank(voice, setting, note);
        span<Stereo, false>(voice, cache, block, frames, exc);
        return;
    }
    for (unsigned offset = 0; offset < frames; offset += control_frames) {
        const i32 previous_gain = voice.bank_gain;
        voice.bank_gain = approach(voice.bank_gain, target_gain);
        for (unsigned i = 0; i < ControlCount; ++i)
            voice.current.value[i] = approach(voice.current.value[i], target.value[i]);
        if (previous_gain && !voice.bank_gain)
            for (auto& band : voice.band) band = {};
        excitation::advance(exc, setting);
        voice.mode = exc.current.mode;
        prepare(cache, voice.current);
        if (voice.bank_gain) prepare_bank(voice, setting, note);
        i32* part = block + offset * (Stereo ? 2 : 1);
        if (exc.amount) span<Stereo, true>(voice, cache, part, control_frames, exc);
        else span<Stereo, false>(voice, cache, part, control_frames, exc);
    }
}
} // namespace

void reset(Voice& voice) { voice = {}; }

unsigned decay_ms(unsigned position) {
    return position && position <= 31 ? decay_times[position - 1] : 0;
}

void perform(Voice& voice, Coefficients& cache, const Parameters& params, i32* block,
             Excitation& exc, Setup setup, bool stereo, unsigned note) {
    if (stereo) process_block<true>(voice, cache, params, block, exc, setup, note);
    else process_block<false>(voice, cache, params, block, exc, setup, note);
}
}
