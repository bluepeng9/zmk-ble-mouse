/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <zmk/split/central.h>
#include <zmk/split/transport/central.h>

/* GNU ld --wrap redirects the transport's external call through this check.
 * The original ZMK implementation remains linked and receives valid events.
 * Keep the real implementation in its upstream translation unit: defining or
 * copying it here would defeat that boundary. */
int __real_zmk_split_transport_central_peripheral_event_handler(
    const struct zmk_split_transport_central *transport, uint8_t source,
    struct zmk_split_transport_peripheral_event event);

int __wrap_zmk_split_transport_central_peripheral_event_handler(
    const struct zmk_split_transport_central *transport, uint8_t source,
    struct zmk_split_transport_peripheral_event event) {
    /* A non-split disconnect supplies (uint8_t)-EINVAL as its source. Match
     * the upstream battery array's bound, without reserving another slot. */
    if (event.type == ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_BATTERY_EVENT &&
        source >= ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT) {
        return -EINVAL;
    }
    return __real_zmk_split_transport_central_peripheral_event_handler(transport, source, event);
}
