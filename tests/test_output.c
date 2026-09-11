/* Production output worker through a fake standard input listener.
 * Upstream input/HOG queue timing and old-host release are NOT simulated. */
#include <stdio.h>
#include <test_runtime.h>
#include "../src/mouse_output.c"

static unsigned counts[5], packets;
static int destination;
static int32_t move_x, move_y, scroll_x, scroll_y;
static void (*on_sync)(void);
static struct {
    int destination;
    uint8_t buttons;
    int32_t x, y, wheel, pan;
} sent[128];

static int synchronize(bool sync) {
    assert(!input_state_lock.held);
    if (!sync) return 0;
    assert(packets < ARRAY_SIZE(sent));
    sent[packets].destination = destination;
    sent[packets].x = move_x;
    sent[packets].y = move_y;
    sent[packets].wheel = scroll_y;
    sent[packets].pan = scroll_x;
    sent[packets].buttons = 0;
    for (unsigned i = 0; i < 5; i++) sent[packets].buttons |= (counts[i] != 0) << i;
    packets++;
    move_x = move_y = scroll_x = scroll_y = 0;
    if (on_sync) {
        void (*callback)(void) = on_sync;
        on_sync = NULL;
        callback();
    }
    return 0;
}
int input_report_key(const struct device *dev, uint16_t code, int32_t value, bool sync, int timeout) {
    assert(dev == receiver && timeout == K_FOREVER && code >= INPUT_BTN_0 && code < INPUT_BTN_0 + 5);
    unsigned button = code - INPUT_BTN_0;
    if (value) counts[button]++;
    else { assert(counts[button]); counts[button]--; }
    return synchronize(sync);
}
int input_report_rel(const struct device *dev, uint16_t code, int32_t value, bool sync, int timeout) {
    assert(dev == receiver && timeout == K_FOREVER);
    switch (code) {
    case INPUT_REL_X: move_x += value; break;
    case INPUT_REL_Y: move_y += value; break;
    case INPUT_REL_WHEEL: scroll_y += value; break;
    case INPUT_REL_HWHEEL: scroll_x += value; break;
    default: assert(false);
    }
    return synchronize(sync);
}
static void reset_output(void) {
    fake_runtime_reset();
    stream_epoch = physical_buttons = reset_pending = reset_buttons = 0;
    memset(&buttons, 0, sizeof(buttons));
    memset(counts, 0, sizeof(counts));
    k_msgq_purge(&mouse_inputs);
    move_x = move_y = scroll_x = scroll_y = 0;
    packets = 0;
    destination = 0;
    on_sync = NULL;
}
static void receive(uint8_t physical, int32_t x, int32_t y, int32_t wheel, int32_t pan) {
    struct mouse_hid_frame frame = {.x = x, .y = y, .wheel = wheel, .pan = pan};
    zmk_mouse_output_receive(&frame, physical);
}
static void switch_endpoint(void) {
    destination++;
    const zmk_event_t event = {.endpoint = true};
    assert(endpoint_changed(&event) == ZMK_EV_EVENT_BUBBLE);
}
static void test_disconnect_preserves_keyboard(void) {
    reset_output();
    counts[0]++; /* Existing keyboard mouse key. */
    receive(9, 1, 2, 0, 0);
    fake_drain();
    assert(counts[0] == 2 && counts[3] == 1 && sent[0].buttons == 9);
    receive(9, 0, 0, 0, 0);
    fake_drain();
    assert(counts[0] == 2 && counts[3] == 1 && packets == 1);
    receive(9, 100, 100, 0, 0);
    zmk_mouse_output_disconnect();
    fake_drain();
    assert(counts[0] == 1 && counts[3] == 0 && packets == 2);
    assert(sent[1].buttons == 1 && !sent[1].x && !sent[1].y);
    receive(0, 0, 0, 0, 0);
    fake_drain();
    assert(counts[0] == 1);
}
static void test_endpoint_epoch_and_held_buttons(void) {
    reset_output();
    receive(9, 1, 2, 0, 0);
    fake_drain();
    assert(sent[0].destination == 0 && sent[0].buttons == 9);
    receive(9, 900, 800, 0, 0);
    switch_endpoint();
    fake_drain();
    /* Public endpoint events arrive AFTER the change, so this release goes
     * through the new endpoint. No assertion of old-host cleanup is made. */
    assert(packets == 2 && sent[1].destination == 1 && !sent[1].buttons);
    receive(9, 5, -5, 0, 0);
    fake_drain();
    assert(sent[2].destination == 1 && sent[2].x == 5 && !sent[2].buttons);
    receive(25, 0, 0, 0, 0);
    fake_drain();
    assert(sent[3].buttons == 16);
    receive(0, 0, 0, 0, 0);
    fake_drain();
    receive(9, 0, 0, 0, 0);
    fake_drain();
    assert(sent[5].buttons == 9);
}
static void test_overflow_and_large_wheel(void) {
    reset_output();
    counts[0]++;
    receive(25, 0, 0, 0, 0);
    fake_drain();
    for (unsigned i = 0; i < CONFIG_ZMK_BLE_MOUSE_QUEUE_SIZE; i++) receive(25, 1, 1, 0, 0);
    receive(0, 0, 0, 0, 0);
    fake_drain();
    assert(packets == 2 && sent[1].buttons == 1);
    assert(counts[0] == 1 && !counts[3] && !counts[4]);
    reset_output();
    receive(0, 40000, -40000, -300, 280);
    fake_drain();
    assert(packets == 3);
    int x = 0, y = 0, wheel = 0, pan = 0;
    for (unsigned i = 0; i < packets; i++) {
        x += sent[i].x; y += sent[i].y; wheel += sent[i].wheel; pan += sent[i].pan;
    }
    assert(x == 40000 && y == -40000 && wheel == -300 && pan == 280);
}
static void test_reentrant_endpoint_change(void) {
    reset_output();
    on_sync = switch_endpoint; /* Input callback must not deadlock the worker. */
    receive(9, 40000, 0, 0, 0);
    fake_drain();
    assert(packets == 2 && sent[0].x == 32767 && !sent[1].x);
    assert(!counts[0] && !counts[3] && !sent[1].buttons);
}
static void test_new_press_after_pending_reset(void) {
    reset_output();
    receive(1, 0, 0, 0, 0);
    fake_drain();
    switch_endpoint();
    receive(17, 0, 0, 0, 0); /* Forward was pressed AFTER the output change. */
    fake_drain();
    assert(packets == 3 && sent[2].buttons == 16);
    assert(!counts[0] && counts[4] == 1);
}
int main(void) {
    test_disconnect_preserves_keyboard();
    test_endpoint_epoch_and_held_buttons();
    test_overflow_and_large_wheel();
    test_reentrant_endpoint_change();
    test_new_press_after_pending_reset();
    puts("Mouse output lifecycle: passed (standard input fake, production worker)");
    return 0;
}
