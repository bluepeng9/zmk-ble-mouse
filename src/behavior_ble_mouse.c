/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT zmk_behavior_ble_mouse

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <dt-bindings/zmk/ble_mouse.h>
#include <zmk/behavior.h>
#if IS_ENABLED(CONFIG_ZMK_BLE_MOUSE_CENTRAL)
#include <zmk/ble_mouse.h>
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_ZMK_BLE_MOUSE_CENTRAL)
    switch (binding->param1) {
    case MOUSE_PAIR: return zmk_ble_mouse_pair();
    case MOUSE_CLEAR: zmk_ble_mouse_clear_hold(true); return ZMK_BEHAVIOR_OPAQUE;
    default: return -EINVAL;
    }
#else
    return -ENOTSUP;
#endif
}

static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_ZMK_BLE_MOUSE_CENTRAL)
    if (binding->param1 == MOUSE_CLEAR) {
        zmk_ble_mouse_clear_hold(false);
    }
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata values[] = {
    {.display_name = "Pair mouse (60 seconds)", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
     .value = MOUSE_PAIR},
    {.display_name = "Clear mouse (hold 2 seconds)", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
     .value = MOUSE_CLEAR},
};
static const struct behavior_parameter_metadata_set sets[] = {
    {.param1_values = values, .param1_values_len = ARRAY_SIZE(values)},
};
static const struct behavior_parameter_metadata metadata = {
    .sets = sets, .sets_len = ARRAY_SIZE(sets),
};
#endif

static const struct behavior_driver_api api = {
    .binding_pressed = pressed,
    .binding_released = released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
#endif
