/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MOUSE_HID_MAX_REPORTS 8
#define MOUSE_HID_MAX_FIELDS 48
#define MOUSE_HID_MAX_REPORT_BYTES 64
#define MOUSE_HID_MAX_MAP_BYTES 1024

enum mouse_hid_usage { MOUSE_X, MOUSE_Y, MOUSE_WHEEL, MOUSE_PAN, MOUSE_BUTTON };

struct mouse_hid_field {
    uint16_t bit;
    uint8_t size;
    uint8_t report;
    uint8_t usage;
    uint8_t button;
    bool is_signed;
};

struct mouse_hid_report {
    uint16_t bits;
    uint8_t id;
    uint8_t buttons;
    bool useful;
};

struct mouse_hid_map {
    struct mouse_hid_report reports[MOUSE_HID_MAX_REPORTS];
    struct mouse_hid_field fields[MOUSE_HID_MAX_FIELDS];
    uint8_t report_count;
    uint8_t field_count;
    uint8_t buttons;
    bool x, y, wheel, pan;
};

struct mouse_hid_frame {
    int32_t x, y, wheel, pan;
    uint8_t buttons;
    uint8_t buttons_present;
};

int mouse_hid_parse(const uint8_t *data, size_t length, struct mouse_hid_map *map);
int mouse_hid_report_index(const struct mouse_hid_map *map, uint8_t id);
/* HOGP carries the report ID in the Report Reference, not in the payload. */
int mouse_hid_decode(const struct mouse_hid_map *map, uint8_t id, const uint8_t *data,
                     size_t length, struct mouse_hid_frame *frame);
