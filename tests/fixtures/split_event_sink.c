/* A separate translation unit models the original event handler's boundary.
 * This is an observable sink, not a copy of ZMK's implementation. */
#include "split_event_sink.h"

unsigned sink_calls;
int sink_result;
const struct zmk_split_transport_central *sink_transport;
uint8_t sink_source, sink_batteries[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];
struct zmk_split_transport_peripheral_event sink_event;

int zmk_split_transport_central_peripheral_event_handler(
    const struct zmk_split_transport_central *transport, uint8_t source,
    struct zmk_split_transport_peripheral_event event) {
    sink_calls++;
    sink_transport = transport;
    sink_source = source;
    sink_event = event;
    if (event.type == ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_BATTERY_EVENT) {
        /* Reaching this assertion means the wrapper failed to protect the
         * original array access, regardless of whether an overwrite crashes. */
        assert(source < ARRAY_SIZE(sink_batteries));
        sink_batteries[source] = event.data.battery_event.level;
    }
    return sink_result;
}
