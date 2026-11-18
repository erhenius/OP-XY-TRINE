// Compatibility object for general FX lookups of the resonator filter IDs.
// Instrument voices use the separate filter factories.
#include "core.h"
namespace m3probe {
void initialize(State& state) { state.reserved = 0; }
void process(State&, opxy::Channel, opxy::Channel, opxy::Parameters) {}
}
