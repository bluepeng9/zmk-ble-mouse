#pragma once
#include <test_runtime.h>
#include <zmk/split/central.h>

extern unsigned sink_calls;
extern int sink_result;
extern const struct zmk_split_transport_central *sink_transport;
extern uint8_t sink_source, sink_batteries[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];
extern struct zmk_split_transport_peripheral_event sink_event;
