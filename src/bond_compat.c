/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include "keys.h"

LOG_MODULE_REGISTER(ble_mouse_bonds, CONFIG_ZMK_BLE_MOUSE_LOG_LEVEL);

#if !defined(ZMK_BLE_MOUSE_LEGACY_CONFIG_APPLIED) || defined(CONFIG_BT_SMP_SC_PAIR_ONLY)
#error "Bond adapter and host must both use the Legacy-compatible key layout"
#endif
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST), "Aging changes the saved key layout");

/* With no aging counter, enabling Legacy adds periph_ltk after the complete
 * old record's fields (possibly reusing its trailing alignment padding).
 * Derive both sizes from the selected host, including optional signing keys. */
#define STORAGE_OFFSET offsetof(struct bt_keys, storage_start)
#define OLD_FIELDS_LEN (offsetof(struct bt_keys, periph_ltk) - STORAGE_OFFSET)
#define OLD_STORAGE_LEN (ROUND_UP(offsetof(struct bt_keys, periph_ltk), \
                                 __alignof__(struct bt_keys)) - STORAGE_OFFSET)
#define FLAGS_OFFSET (offsetof(struct bt_keys, flags) - STORAGE_OFFSET)
#define TYPES_OFFSET (offsetof(struct bt_keys, keys) - STORAGE_OFFSET)
#define KEY_SIZE_OFFSET (offsetof(struct bt_keys, enc_size) - STORAGE_OFFSET)

BUILD_ASSERT(offsetof(struct bt_keys, enc_size) == STORAGE_OFFSET,
             "Unreviewed Bluetooth storage prefix");
BUILD_ASSERT(offsetof(struct bt_keys, flags) == STORAGE_OFFSET + 1 &&
             offsetof(struct bt_keys, keys) == STORAGE_OFFSET + 2 &&
             offsetof(struct bt_keys, ltk) == STORAGE_OFFSET + 4,
             "Unreviewed Bluetooth key metadata layout");
BUILD_ASSERT(sizeof(struct bt_ltk) == 26 && sizeof(struct bt_irk) == 22 &&
             offsetof(struct bt_keys, irk) == offsetof(struct bt_keys, ltk) + 26,
             "Unreviewed LTK/IRK storage layout");
#if defined(CONFIG_BT_SIGNING)
BUILD_ASSERT(offsetof(struct bt_keys, periph_ltk) == offsetof(struct bt_keys, remote_csrk) +
                                                  sizeof(struct bt_csrk),
             "Unreviewed signed bond suffix");
#else
BUILD_ASSERT(offsetof(struct bt_keys, periph_ltk) == offsetof(struct bt_keys, irk) + 22,
             "Unreviewed unsigned bond suffix");
#endif
BUILD_ASSERT(sizeof(struct bt_keys) == ROUND_UP(offsetof(struct bt_keys, periph_ltk) +
                                              sizeof(struct bt_ltk), __alignof__(struct bt_keys)),
             "Fields after periph_ltk require a new bond adapter");
BUILD_ASSERT(OLD_FIELDS_LEN <= OLD_STORAGE_LEN && OLD_STORAGE_LEN < BT_KEYS_STORAGE_LEN,
             "Unexpected SC/Legacy storage lengths");

int __real_settings_call_set_handler(const char *name, size_t len, settings_read_cb read_cb,
                                    void *cb_arg, const struct settings_load_arg *load_arg);
int __real_settings_save_one(const char *name, const void *value, size_t len);

static bool is_bond_key(const char *name) {
    return name && !strncmp(name, "bt/keys/", 8) && name[8];
}

static bool sc_record(const uint8_t *data) {
    uint16_t types;
    memcpy(&types, data + TYPES_OFFSET, sizeof(types));
    return data[KEY_SIZE_OFFSET] >= 7 && data[KEY_SIZE_OFFSET] <= 16 &&
           (data[FLAGS_OFFSET] & BT_KEYS_SC) && (types & BT_KEYS_LTK_P256) &&
           !(types & (BT_KEYS_LTK | BT_KEYS_PERIPH_LTK)) && !(types & ~BT_KEYS_ALL);
}

struct adapted_value { uint8_t bytes[BT_KEYS_STORAGE_LEN]; };
static ssize_t read_adapted(void *arg, void *data, size_t len) {
    const struct adapted_value *value = arg;
    len = MIN(len, sizeof(value->bytes));
    memcpy(data, value->bytes, len);
    return len;
}

int __wrap_settings_call_set_handler(const char *name, size_t len, settings_read_cb read_cb,
                                    void *cb_arg, const struct settings_load_arg *load_arg) {
    /* Direct loads promise raw storage bytes; excluded subtrees and unrelated
     * settings must not be read or transformed by this adapter. */
    if (!is_bond_key(name) || !len ||
        (load_arg && (load_arg->cb || (load_arg->subtree &&
         !settings_name_steq(name, load_arg->subtree, NULL))))) {
        return __real_settings_call_set_handler(name, len, read_cb, cb_arg, load_arg);
    }
    if (len == BT_KEYS_STORAGE_LEN) {
        return __real_settings_call_set_handler(name, len, read_cb, cb_arg, load_arg);
    }
    if (len != OLD_STORAGE_LEN) {
        /* keys_set would clear an existing record on a length mismatch. */
        LOG_ERR("Unsupported bond record length %u; stored value preserved", (unsigned)len);
        return -EINVAL;
    }
    struct adapted_value value = {0};
    ssize_t got = read_cb(cb_arg, value.bytes, OLD_STORAGE_LEN);
    if (got < 0) return got;
    if (got != OLD_STORAGE_LEN || !sc_record(value.bytes)) {
        LOG_ERR("Invalid SC bond record; stored value preserved");
        return -EINVAL;
    }
    /* Old padding is not a Legacy key. Zero the complete new tail. */
    memset(value.bytes + OLD_FIELDS_LEN, 0, sizeof(value.bytes) - OLD_FIELDS_LEN);
    return __real_settings_call_set_handler(name, sizeof(value.bytes), read_adapted, &value,
                                           load_arg);
}

int __wrap_settings_save_one(const char *name, const void *value, size_t len) {
    if (is_bond_key(name) && value && len == BT_KEYS_STORAGE_LEN && sc_record(value)) {
        /* Keep PC/split SC records readable by the previous firmware too.
         * Never shorten a Legacy mouse's key or rewrite records during boot. */
        uint8_t old[OLD_STORAGE_LEN] = {0};
        memcpy(old, value, OLD_FIELDS_LEN);
        return __real_settings_save_one(name, old, sizeof(old));
    }
    return __real_settings_save_one(name, value, len);
}
