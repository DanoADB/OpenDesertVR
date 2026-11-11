// SPDX-License-Identifier: MIT
#include <cstdint>

struct OwnedState {
    std::uint32_t token;
    float values[16];
};

extern "C" __declspec(noinline) std::uint32_t OwnedMix(std::uint32_t value) { return value * 17u + 3u; }

extern "C" __declspec(dllexport) __declspec(noinline) void OwnedPublish(volatile OwnedState *state) {
    const auto next = OwnedMix(state->token);
    for (unsigned index = 0; index < 16; ++index) {
        state->values[index] = static_cast<float>(next + index);
    }
    state->token = next;
}

int main() {
    OwnedState state{};
    OwnedPublish(&state);
    return state.token == 3u ? 0 : 1;
}
