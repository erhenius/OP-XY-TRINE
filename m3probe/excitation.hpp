#pragma once
#include "setup.hpp"
#include <opxy/q31.hpp>

namespace m3_resonator {
struct ChannelState {
    opxy::i32 dc, envelope, error, pulse_level;
    unsigned cooldown, pulse;
    bool armed, positive;
};
struct Excitation {
    ChannelState channel[2];
    Setup current;
    opxy::i32 amount, route_gain;
    bool started;
};
namespace excitation {
using namespace opxy;
using namespace opxy::q31;
inline i32 attenuate(i32 a, i32 b) { return b == maximum ? a : multiply(a, b); }
// A convex blend cannot overflow. Scaling terms separately also handles opposite rails.
inline i32 blend(i32 dry, i32 wet, i32 amount) {
    if (!amount) return dry;
    if (amount == maximum) return wet;
    return add(multiply(dry, maximum - amount), multiply(wet, amount));
}
inline i32 dc_block(ChannelState& s, i32 x) {
    const i32 diff = add(x, s.dc == -maximum-1 ? maximum : -s.dc);
    s.dc = add(s.dc, multiply(diff, 4890000));
    return add(x, s.dc == -maximum-1 ? maximum : -s.dc);
}
inline i32 sample(ChannelState& s, Exciter type, i32 x) {
    const i32 mag = x < 0 ? (x == -maximum-1 ? maximum : -x) : x;
    if (type == Exciter::Double) {
        const i32 y = dc_block(s, mag);
        return add(y, multiply(y, 1932735283));
    }
    if (type == Exciter::Fold || type == Exciter::Wrap) {
        // Work at half scale. The input has one bit of headroom, so one wrap suffices.
        const i32 edge = 408021893, period = 2 * edge;
        i32 y = add(x, multiply(x, 214748365));
        if (y > edge) y = type == Exciter::Fold ? period - y : y - period;
        else if (y < -edge) y = type == Exciter::Fold ? -period - y : y + period;
        y = dc_block(s, shift_left(y, 1));
        return add(y, half(half(y)));
    }
    const i32 decayed = multiply(s.envelope, 2145050248);
    s.envelope = mag > decayed ? mag : decayed;
    if (type == Exciter::Sigma) {
        if (s.envelope < 64425) { s.error = 0; return 0; }
        const i32 step = add(s.envelope, multiply(s.envelope, 322122547));
        // Resolve the sign at half scale to retain headroom in the error accumulator.
        const i32 sum = add(s.error, half(x));
        const i32 bit = sum >= 0 ? step : -step;
        s.error = sum - half(bit);
        const i32 bound = half(step);
        if (s.error > bound) s.error = bound;
        if (s.error < -bound) s.error = -bound;
        return bit;
    }
    i32 threshold = multiply(s.envelope, 75161928);
    if (threshold < 6442451) threshold = 6442451;
    if (x < -threshold) s.armed = true;
    if (s.cooldown) --s.cooldown;
    if (x > threshold && s.armed) {
        s.armed = false;
        if (!s.cooldown) {
            s.cooldown = 16;
            s.positive = !s.positive;
            s.pulse = 6;
            s.pulse_level = add(shift_left(s.envelope, 2), s.envelope);
        }
    }
    if (type == Exciter::Sub) {
        const i32 y = multiply(s.envelope, 1825361101);
        return dc_block(s, s.positive ? y : -y);
    }
    const i32 weights[] = {214748365, 536870912, 1073741824, 1610612736, maximum, maximum};
    return dc_block(s, s.pulse ? attenuate(s.pulse_level, weights[--s.pulse]) : 0);
}
inline i32 apply(Excitation& e, unsigned channel, i32 x) {
    return blend(x, sample(e.channel[channel], e.current.exciter, x), e.amount);
}
inline i32 ramp(i32 now, i32 target) {
    const i32 step = maximum / 16;
    if (target - now > step) return now + step;
    if (now - target > step) return now - step;
    return target;
}
inline void advance(Excitation& e, Setup s) {
    const i32 target = s.exciter == Exciter::Clean ? 0 :
        s.blend == 100 ? maximum : static_cast<i32>(s.blend * 21474836u);
    if (!e.started) { e.current = s; e.route_gain = maximum; e.started = true; }
    bool changed = s.exciter != e.current.exciter || s.whole != e.current.whole;
    e.amount = ramp(e.amount, changed ? 0 : target);
    if (changed && !e.amount) {
        e.channel[0] = {}; e.channel[1] = {};
        e.current.exciter = s.exciter;
        e.current.whole = s.whole;
    }
    e.route_gain = ramp(e.route_gain, s.mode == e.current.mode ? maximum : 0);
    if (!e.route_gain) e.current.mode = s.mode;
}
}
}
