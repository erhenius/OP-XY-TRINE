#include "../resonator.cpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <climits>

using namespace m3_resonator;

namespace {
constexpr double sample_rate = 44100.0;
constexpr double pi = 3.14159265358979323846;
constexpr double scale = 2147483648.0;

double frequency(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

void check_tuning() {
    Voice voice{};
    Setup setup = unpack(0);
    for (unsigned note = 0; note < 128; ++note) {
        assert(played_note(i32(note * 0xffffffu + 0x7fffffu)) == note);
        for (unsigned position = 1; position <= 31; ++position) {
            voice.current.value[Decay] = i32((position - 1) << 26);
            prepare_bank(voice, setup, note);
            const double radius = std::sqrt(-voice.negative_radius_squared / scale);
            const double ms = -1000.0 * std::log(1000.0) / (sample_rate * std::log(radius));
            assert(std::fabs(ms - decay_ms(position)) < 0.01);
            const int expected[] = {int(note), int(note) + 4, int(note) + 7};
            for (unsigned b = 0; b < band_count; ++b) {
                const double measured = sample_rate / (2.0 * pi) *
                    std::acos(voice.feedback[b] / scale / radius);
                assert(std::fabs(measured - frequency(expected[b])) < 0.01);
            }
        }
    }
    setup.interval1 = -24;
    setup.interval2 = 24;
    prepare_bank(voice, setup, 0);
    assert(voice.pitch_key == (24u << 16));
    prepare_bank(voice, setup, 127);
    assert(voice.pitch_key == (134u << 16 | 103u << 8 | 127u));
    assert(decay_ms(0) == 0 && decay_ms(1) == 150 && decay_ms(31) == 1300);
}

void check_setup() {
    assert(unpack(0).interval1 == 4 && unpack(0).interval2 == 7 && pack(unpack(0)) == 0);
    for (int a = -24; a <= 24; ++a) for (int b = -24; b <= 24; ++b) {
        unsigned word = edit(edit(0,2,a-4),3,b-7);
        assert(valid_setup(word) && unpack(word).interval1 == a && unpack(word).interval2 == b);
        word = edit(edit(word,0,5),1,100);
        assert(unpack(word).blend == 100 && unpack(word).exciter == Exciter::Sigma);
        const unsigned toggled = click(click(word,2),3);
        const auto s = unpack(toggled);
        assert(s.mode == Mode::Bypass && s.whole && s.interval1 == a && s.interval2 == b);
        assert(click(click(toggled,3),2) == word);
    }
    assert(unpack(edit(0,2,INT_MAX)).interval1 == 24);
    assert(unpack(edit(0,3,INT_MIN)).interval2 == -24);
    for (unsigned i = 2; i <= 6; ++i) {
        assert(unpack(edit(i,0,1)).exciter == Exciter(i==6 ? 2 : i+1));
        assert(edit(edit(i,0,1),0,-1)==i);
    }
    assert(migrate_v1(16u | 32u | 50u << 6) == (32u | 50u << 6));
}

void check_audio(bool stereo) {
    Voice voice{}, dry{};
    Excitation wet_exc{}, dry_exc{};
    Coefficients a{}, b{};
    Parameters p{{1530000000, 450000000, 0, 20 << 26}};
    Parameters disabled = p;
    disabled.value[Decay] = 0;
    Setup setup = unpack(0);
    const unsigned words = frames * (stereo ? 2 : 1);
    i32 buffer[128]{}, reference[128]{};
    bool different = false;
    for (unsigned block = 0; block < 1200; ++block) {
        for (unsigned i = 0; i < words; ++i) {
            buffer[i] = block % 16 == 0 && i < unsigned(stereo ? 2 : 1) ? 900000000 : 0;
        }
        std::memcpy(reference,buffer,words*sizeof(i32));
        if (block < 300 || block >= 900) p.value[Decay] = disabled.value[Decay];
        else p.value[Decay] = i32((12 + block % 19) << 26);
        if (block >= 500 && block < 900) {
            setup.exciter = Exciter(block / 17 % 7);
            setup.blend = block % 101;
            setup.mode = Mode(block / 11 % 2);
            setup.whole = block / 13 % 2;
            setup.interval1 = int(block % 49) - 24;
        } else setup = unpack(0);
        perform(voice,a,p,buffer,wet_exc,setup,stereo,48 + block % 23);
        perform(dry,b,disabled,reference,dry_exc,unpack(0),stereo,48);
        const bool equal = std::memcmp(buffer,reference,words*sizeof(i32)) == 0;
        if (block < 300) assert(equal);
        if (block >= 300 && block < 900 && !equal) different = true;
        if (block >= 1150) {
            assert(voice.bank_gain == 0);
            for (const auto& band : voice.band) assert(band.first == 0 && band.second == 0);
            for (unsigned i = 0; i < words; ++i) assert(std::abs(buffer[i] / 2048 - reference[i] / 2048) < 8);
        }
    }
    assert(different);
}
}

int main() {
    check_tuning();
    check_setup();
    check_audio(false);
    check_audio(true);
    std::puts("PASS: note tracking, all interval pairs and decay positions, mono/stereo processing and Off");
}
