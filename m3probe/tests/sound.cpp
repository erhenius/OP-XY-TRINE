#include "../fx.cpp"
int main() {
    m3probe::State state = {};
    opxy::i32 left[opxy::block_frames] = {123}, right[opxy::block_frames] = {-456};
    opxy::i32 params[8] = {};
    m3probe::initialize(state);
    m3probe::process(state, left, right, params);
    return left[0] == 123 && right[0] == -456 ? 0 : 1;
}
