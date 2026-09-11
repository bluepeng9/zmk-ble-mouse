/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_ble_mouse_receiver

#include <zmk/ble_mouse.h>
#include <ble_mouse/button_state.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>

LOG_MODULE_REGISTER(ble_mouse_output, CONFIG_ZMK_BLE_MOUSE_LOG_LEVEL);

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);
static const struct device *const receiver = DEVICE_DT_INST_GET(0);
static struct k_spinlock input_state_lock;
static atomic_t stream_epoch, physical_buttons, reset_pending, reset_buttons;
/* Only the module worker updates the forwarded button state. */
static struct mouse_button_state buttons;

struct queued_input {
    struct mouse_hid_frame frame;
    uint32_t epoch;
    uint8_t buttons;
};
K_MSGQ_DEFINE(mouse_inputs, sizeof(struct queued_input), CONFIG_ZMK_BLE_MOUSE_QUEUE_SIZE, 4);

static void send_packet(struct mouse_button_edges edges, int16_t x, int16_t y,
                        int8_t wheel, int8_t pan) {
    struct { uint16_t code; int32_t value; bool key; } events[9];
    size_t count = 0;
    for (uint8_t i = 0; i < 5; i++) {
        if ((edges.press | edges.release) & BIT(i)) {
            events[count].code = INPUT_BTN_0 + i;
            events[count].value = !!(edges.press & BIT(i));
            events[count++].key = true;
        }
    }
    const uint16_t codes[] = {INPUT_REL_X, INPUT_REL_Y, INPUT_REL_WHEEL, INPUT_REL_HWHEEL};
    const int32_t values[] = {x, y, wheel, pan};
    for (size_t i = 0; i < ARRAY_SIZE(codes); i++) {
        if (values[i]) {
            events[count].code = codes[i];
            events[count].value = values[i];
            events[count++].key = false;
        }
    }
    for (size_t i = 0; i < count; i++) {
        bool sync = i + 1 == count;
        /* No lock needed by input callbacks is held. Waiting avoids losing
         * a release when Zephyr's input queue fills up. */
        if (events[i].key) {
            input_report_key(receiver, events[i].code, events[i].value, sync, K_FOREVER);
        } else {
            input_report_rel(receiver, events[i].code, events[i].value, sync, K_FOREVER);
        }
    }
}

static void apply_pending_reset(void) {
    k_spinlock_key_t key = k_spin_lock(&input_state_lock);
    bool pending = atomic_clear(&reset_pending);
    uint8_t physical = atomic_get(&reset_buttons);
    k_spin_unlock(&input_state_lock, key);
    if (pending) {
        struct mouse_button_edges edges = {.release = mouse_buttons_reset(&buttons, physical)};
        send_packet(edges, 0, 0, 0, 0);
    }
}

static void send_frame(struct queued_input *input) {
    struct mouse_button_edges edges = mouse_buttons_update(&buttons, input->buttons);
    struct mouse_hid_frame *frame = &input->frame;
    /* Bound work for a device claiming enormous 32-bit deltas. */
    for (int i = 0; i < 32; i++) {
        if (i && input->epoch != (uint32_t)atomic_get(&stream_epoch)) {
            return;
        }
        int16_t x = CLAMP(frame->x, -32767, 32767);
        int16_t y = CLAMP(frame->y, -32767, 32767);
        int8_t wheel = CLAMP(frame->wheel, -127, 127);
        int8_t pan = CLAMP(frame->pan, -127, 127);
        send_packet(edges, x, y, wheel, pan);
        edges = (struct mouse_button_edges){0};
        frame->x -= x;
        frame->y -= y;
        frame->wheel -= wheel;
        frame->pan -= pan;
        if (!frame->x && !frame->y && !frame->wheel && !frame->pan) {
            return;
        }
    }
    LOG_WRN("Excessive mouse delta truncated");
}

static void output_work_cb(struct k_work *work) {
    struct queued_input input;
    apply_pending_reset();
    while (!k_msgq_get(&mouse_inputs, &input, K_NO_WAIT)) {
        apply_pending_reset();
        if (input.epoch == (uint32_t)atomic_get(&stream_epoch)) {
            send_frame(&input);
        }
    }
    apply_pending_reset();
}
K_WORK_DEFINE(output_work, output_work_cb);

void zmk_mouse_output_receive(const struct mouse_hid_frame *frame, uint8_t physical) {
    k_spinlock_key_t key = k_spin_lock(&input_state_lock);
    atomic_set(&physical_buttons, physical & 0x1f);
    struct queued_input input = {
        .frame = *frame, .buttons = physical, .epoch = atomic_get(&stream_epoch),
    };
    bool overflow = k_msgq_put(&mouse_inputs, &input, K_NO_WAIT) != 0;
    if (overflow) {
        atomic_inc(&stream_epoch);
        atomic_set(&reset_buttons, physical & 0x1f);
        atomic_set(&reset_pending, 1);
        k_msgq_purge(&mouse_inputs);
    }
    k_spin_unlock(&input_state_lock, key);
    if (overflow) {
        LOG_WRN("Mouse input queue overflow; resynchronizing");
    }
    zmk_ble_mouse_submit(&output_work);
}

static void reset_stream(bool disconnected) {
    k_spinlock_key_t key = k_spin_lock(&input_state_lock);
    if (disconnected) {
        atomic_clear(&physical_buttons);
    }
    atomic_inc(&stream_epoch);
    atomic_set(&reset_buttons, atomic_get(&physical_buttons));
    atomic_set(&reset_pending, 1);
    k_msgq_purge(&mouse_inputs);
    k_spin_unlock(&input_state_lock, key);
    zmk_ble_mouse_submit(&output_work);
}

void zmk_mouse_output_disconnect(void) { reset_stream(true); }

static int endpoint_changed(const zmk_event_t *event) {
    if (as_zmk_endpoint_changed(event)) {
        /* The public event arrives AFTER OUT changes. Only our own queued
         * frames can be discarded here; upstream input/HOG queues retain
         * their normal semantics. There is no old-host release hook. */
        reset_stream(false);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(ble_mouse_output, endpoint_changed);
ZMK_SUBSCRIPTION(ble_mouse_output, zmk_endpoint_changed);
