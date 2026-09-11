/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>

struct mouse_button_state { uint8_t forwarded, suppressed; };
struct mouse_button_edges { uint8_t press, release; };

struct mouse_button_edges mouse_buttons_update(struct mouse_button_state *state, uint8_t physical);
uint8_t mouse_buttons_reset(struct mouse_button_state *state, uint8_t physical);
