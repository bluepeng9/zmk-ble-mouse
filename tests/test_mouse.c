/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ble_mouse/hid_parser.h>
#include <ble_mouse/button_state.h>

#include "fixtures/hid_reports.h"

static void test_reports(void) {
    struct mouse_hid_map map;
    struct mouse_hid_frame frame;
    assert(mouse_hid_parse(descriptor, sizeof(descriptor), &map) == 0);
    assert(map.report_count == 3 && map.buttons == 31);
    assert(map.x && map.y && map.wheel && map.pan);
    uint8_t report[] = {0x19, 0x00, 0xf8, 0x7f, 0xff, 0x02};
    assert(mouse_hid_decode(&map, 7, report, sizeof(report), &frame) == 0);
    assert(frame.x == -2048 && frame.y == 2047);
    assert(frame.wheel == -1 && frame.pan == 2);
    assert(frame.buttons == 0x19 && frame.buttons_present == 31);
    memset(report, 0, sizeof(report));
    assert(mouse_hid_decode(&map, 7, report, sizeof(report), &frame) == 0);
    assert(frame.buttons == 0 && frame.x == 0 && frame.wheel == 0);
    assert(mouse_hid_decode(&map, 7, report, sizeof(report) - 1, &frame) == -EMSGSIZE);
    assert(mouse_hid_decode(&map, 7, report, sizeof(report) + 1, &frame) == -EMSGSIZE);
    assert(mouse_hid_decode(&map, 0x11, report, sizeof(report), &frame) == -ENOENT);
    assert(mouse_hid_decode(&map, 0x55, report, sizeof(report), &frame) == -ENOENT);
    uint8_t consumer = 1;
    assert(mouse_hid_decode(&map, 0x2a, &consumer, 1, &frame) == 0);
    assert(frame.buttons == 8 && frame.buttons_present == 24);
    consumer = 2;
    assert(mouse_hid_decode(&map, 0x2a, &consumer, 1, &frame) == 0);
    assert(frame.buttons == 16);
}

static void test_no_report_id(void) {
    uint8_t simple[] = {0x05,1, 0x09,2, 0xa1,1, 0x09,0x30, 0x09,0x31,
                        0x15,0x81, 0x25,0x7f, 0x75,8, 0x95,2, 0x81,6, 0xc0};
    struct mouse_hid_map map;
    struct mouse_hid_frame frame;
    uint8_t report[] = {0x80,0x7f};
    assert(mouse_hid_parse(simple, sizeof(simple), &map) == 0);
    assert(mouse_hid_decode(&map, 0, report, sizeof(report), &frame) == 0);
    assert(frame.x == -128 && frame.y == 127 && frame.buttons_present == 0);
    simple[sizeof(simple) - 2] = 2; /* Absolute coordinates are not a relative mouse. */
    assert(mouse_hid_parse(simple, sizeof(simple), &map) == -ENOTSUP);
}

static void test_bad_descriptors(void) {
    struct mouse_hid_map map;
    uint8_t truncated[] = {0x05};
    uint8_t long_item[] = {0xfe,8,1,0};
    uint8_t pop[] = {0xb4};
    uint8_t end[] = {0xc0};
    uint8_t zero_id[] = {0x85,0};
    assert(mouse_hid_parse(NULL, 1, &map) == -EINVAL);
    assert(mouse_hid_parse(descriptor, 0, &map) == -EINVAL);
    assert(mouse_hid_parse(truncated, sizeof(truncated), &map) == -EINVAL);
    assert(mouse_hid_parse(long_item, sizeof(long_item), &map) == -EINVAL);
    assert(mouse_hid_parse(pop, sizeof(pop), &map) == -EINVAL);
    assert(mouse_hid_parse(end, sizeof(end), &map) == -EINVAL);
    assert(mouse_hid_parse(zero_id, sizeof(zero_id), &map) == -EINVAL);
    uint8_t too_many[] = {0x75,32, 0x96,0xff,0xff, 0x81,2};
    assert(mouse_hid_parse(too_many, sizeof(too_many), &map) == -E2BIG);
}

static void test_button_lifecycle(void) {
    struct mouse_button_state state = {0};
    struct mouse_button_edges edges = mouse_buttons_update(&state, 9);
    assert(edges.press == 9 && edges.release == 0);
    edges = mouse_buttons_update(&state, 9);
    assert(edges.press == 0 && edges.release == 0); /* No repeated refcount increment. */
    assert(mouse_buttons_reset(&state, 9) == 9); /* Endpoint switch releases only our source. */
    edges = mouse_buttons_update(&state, 9);
    assert(edges.press == 0 && edges.release == 0); /* Held buttons suppressed on new host. */
    edges = mouse_buttons_update(&state, 25);
    assert(edges.press == 16 && edges.release == 0); /* New forward press still works. */
    edges = mouse_buttons_update(&state, 16);
    assert(edges.press == 0 && edges.release == 0);
    edges = mouse_buttons_update(&state, 25);
    assert(edges.press == 9 && edges.release == 0); /* Released old buttons can be pressed again. */
    assert(mouse_buttons_reset(&state, 0) == 25); /* Disconnect or overflow releases all ours. */
    edges = mouse_buttons_update(&state, 0);
    assert(!edges.press && !edges.release);
}

static void test_mutated_descriptors(void) {
    uint32_t seed = 12345;
    uint8_t copy[sizeof(descriptor)], report[MOUSE_HID_MAX_REPORT_BYTES];
    struct mouse_hid_map map;
    struct mouse_hid_frame frame;
    for (int n = 0; n < 20000; n++) {
        memcpy(copy, descriptor, sizeof(copy));
        seed = seed * 1664525u + 1013904223u;
        copy[seed % sizeof(copy)] ^= (seed >> 16) | 1;
        size_t length = n % 3 ? sizeof(copy) : seed % sizeof(copy);
        if (mouse_hid_parse(copy, length, &map)) {
            continue;
        }
        memset(report, seed >> 24, sizeof(report));
        for (uint8_t i = 0; i < map.report_count; i++) {
            size_t bytes = (map.reports[i].bits + 7u) / 8u;
            assert(bytes <= sizeof(report));
            int err = mouse_hid_decode(&map, map.reports[i].id, report, bytes, &frame);
            assert(err == 0 || err == -ENOENT);
        }
    }
}

int main(void) {
    test_reports();
    test_no_report_id();
    test_bad_descriptors();
    test_button_lifecycle();
    test_mutated_descriptors();
    puts("Mouse HID parser and button lifecycle: passed (20,000 descriptor mutations)");
    return 0;
}
