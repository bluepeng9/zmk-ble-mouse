/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include "hci_core.h"

int __real_bt_smp_init(void);

int __wrap_bt_smp_init(void) {
    /* Preserve SC_PAIR_ONLY's startup capability check. Checking the remote
     * AuthReq SC bit alone is insufficient if the local side lacks SC. */
    if (!BT_CMD_TEST(bt_dev.supported_commands, 34, 1) ||
        !BT_CMD_TEST(bt_dev.supported_commands, 34, 2)) return -ENOENT;
    return __real_bt_smp_init();
}
