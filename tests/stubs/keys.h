/* Test model of the reviewed Zephyr 3.5 storage ABI, not a host implementation.
 * Firmware builds include the selected host's real keys.h instead. */
#pragma once
#include <test_runtime.h>
#define BT_KEYS_PERIPH_LTK BIT(0)
#define BT_KEYS_IRK BIT(1)
#define BT_KEYS_LTK BIT(2)
#define BT_KEYS_LTK_P256 BIT(5)
#define BT_KEYS_ALL 0x3f
#define BT_KEYS_SC BIT(4)
struct bt_ltk { uint8_t rand[8], ediv[2], val[16]; };
struct bt_irk { uint8_t val[16]; bt_addr_t rpa; };
struct bt_csrk { uint8_t val[16]; uint32_t cnt; };
struct bt_keys {
    uint8_t id;
    bt_addr_le_t addr;
    uint8_t state;
    /* A union expresses the zero-sized upstream storage marker in ISO C. */
    union {
        _Alignas(sizeof(void *)) uint8_t storage_start[1];
        struct {
            uint8_t enc_size, flags;
            uint16_t keys;
            struct bt_ltk ltk;
            struct bt_irk irk;
#if defined(CONFIG_BT_SIGNING)
            struct bt_csrk local_csrk, remote_csrk;
#endif
            struct bt_ltk periph_ltk;
        };
    };
};
#define BT_KEYS_STORAGE_LEN (sizeof(struct bt_keys) - offsetof(struct bt_keys, storage_start))
