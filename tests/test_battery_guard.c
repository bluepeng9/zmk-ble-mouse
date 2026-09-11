/* Call the PUBLIC symbol from a different translation unit. This suite must
 * link with --wrap; directly calling __wrap_* would not test its installation. */
#include <stdio.h>
#include "fixtures/split_event_sink.h"

static const struct zmk_split_transport_central transport = {0};

static int battery(uint8_t source, uint8_t level) {
    struct zmk_split_transport_peripheral_event event = {
        .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_BATTERY_EVENT,
        .data = {.battery_event = {.level = level}},
    };
    return zmk_split_transport_central_peripheral_event_handler(&transport, source, event);
}

int main(void) {
    sink_result = 19; /* The original handler's result must survive forwarding. */
    for (unsigned i = 0; i < ARRAY_SIZE(sink_batteries); i++) {
        assert(battery(i, 71 + i) == 19);
        assert(sink_batteries[i] == 71 + i && sink_source == i);
        assert(sink_transport == &transport);
    }
    unsigned accepted = sink_calls;
    for (unsigned source = ARRAY_SIZE(sink_batteries); source <= UINT8_MAX; source++) {
        assert(battery(source, 0) == -EINVAL);
        assert(sink_calls == accepted);
    }
    /* This is the actual non-split disconnect conversion in the upstream code. */
    assert(battery((uint8_t)-EINVAL, 0) == -EINVAL);
    assert(sink_calls == accepted);
    for (unsigned i = 0; i < ARRAY_SIZE(sink_batteries); i++) {
        assert(sink_batteries[i] == 71 + i);
    }
    /* Normal keyboard disconnects still clear that keyboard's battery value. */
    assert(battery(0, 0) == 19 && sink_batteries[0] == 0);
    for (unsigned type = 0; type <= ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_RELAY_EVENT; type++) {
        if (type == ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_BATTERY_EVENT) continue;
        struct zmk_split_transport_peripheral_event event = {0};
        event.type = type;
        memset(event.data.payload, 0xa5, sizeof(event.data.payload));
        sink_result = -ENOTSUP;
        assert(zmk_split_transport_central_peripheral_event_handler(&transport, 0, event) == -ENOTSUP);
        assert(!memcmp(&event, &sink_event, sizeof(event)));
        assert(sink_source == 0 && sink_transport == &transport);
    }
    puts("Split battery guard: passed through the real linker wrapper; keyboard events preserved");
    return 0;
}
