#include "../sweep_display.hpp"
#include <algorithm>
#include <cassert>
#include <climits>

using namespace m3_display;

int main() {
    assert(cutoff_hz(0) == 45);
    assert(cutoff_hz(INT_MAX) >= 14497 && cutoff_hz(INT_MAX) <= 14500);
    assert(resonance_hundredths(0) == 70);
    assert(resonance_hundredths(INT_MAX) >= 499 && resonance_hundredths(INT_MAX) <= 500);

    i32 low[curve_points], band[curve_points], high[curve_points], high_q[curve_points];
    const i32 center = 1 << 30;
    sweep_pixels(center, 0, 0, low);
    sweep_pixels(center, 0, center, band);
    sweep_pixels(center, 0, INT_MAX, high);
    sweep_pixels(center, INT_MAX, center, high_q);
    assert(low[0] < low[32] && high[0] > high[32]);
    assert(band[7] < band[0] && band[7] < band[32]);
    assert(*std::min_element(band,band + curve_points) == band[7]);
    assert(high_q[7] < band[7]);
    for (unsigned cutoff = 0; cutoff < 65; ++cutoff)
        for (unsigned q = 0; q < 17; ++q)
            for (unsigned shape = 0; shape < 65; ++shape) {
                sweep_pixels(cutoff == 64 ? INT_MAX : i32(cutoff << 25),
                             q == 16 ? INT_MAX : i32(q << 27),
                             shape == 64 ? INT_MAX : i32(shape << 25), low);
                for (i32 y : low) assert(y >= graph_top && y <= graph_bottom);
            }
}
