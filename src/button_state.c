/* SPDX-License-Identifier: MIT */
#include <ble_mouse/button_state.h>

struct mouse_button_edges mouse_buttons_update(struct mouse_button_state *state, uint8_t physical) {
    physical &= 0x1f;
    state->suppressed &= physical;
    uint8_t next = physical & ~state->suppressed;
    struct mouse_button_edges edges = {
        .press = next & ~state->forwarded,
        .release = state->forwarded & ~next,
    };
    state->forwarded = next;
    return edges;
}

uint8_t mouse_buttons_reset(struct mouse_button_state *state, uint8_t physical) {
    uint8_t release = state->forwarded;
    state->forwarded = 0;
    state->suppressed = physical & 0x1f;
    return release;
}
