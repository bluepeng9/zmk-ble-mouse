/* SPDX-License-Identifier: MIT */
#include <zmk/ble_mouse.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/logging/log.h>
#include <zmk/split/transport/central.h>

LOG_MODULE_REGISTER(ble_mouse_scan, CONFIG_ZMK_BLE_MOUSE_LOG_LEVEL);

static struct k_work_q mouse_queue;
K_THREAD_STACK_DEFINE(mouse_stack, CONFIG_ZMK_BLE_MOUSE_STACK_SIZE);
static atomic_t initialized, connecting, scan_owned, split_settling;
static bt_addr_le_t pending_address;
static int (*pending_connect)(const bt_addr_le_t *addr);

void zmk_ble_mouse_submit(struct k_work *work) {
    if (atomic_get(&initialized)) k_work_submit_to_queue(&mouse_queue, work);
}
void zmk_ble_mouse_schedule(struct k_work_delayable *work, int32_t delay_ms) {
    if (atomic_get(&initialized)) {
        k_work_reschedule_for_queue(&mouse_queue, work, K_MSEC(delay_ms));
    }
}

/* Observe the public transport status without taking over its status callback,
 * disabling it, or changing its connection slots. Initial mouse pairing waits
 * until the keyboard transport has finished connecting all its peripherals. */
static bool split_ready(void) {
    bool enabled = false;
    STRUCT_SECTION_FOREACH(zmk_split_transport_central, transport) {
        struct zmk_split_transport_status status = transport->api->get_status();
        if (!status.available || !status.enabled) continue;
        enabled = true;
        if (status.connections != ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_ALL_CONNECTED) {
            return false;
        }
    }
    return enabled && !atomic_get(&split_settling);
}

static int stop_own_scan(void) {
    /* Never stop a scan started by the split transport or another module. */
    if (!atomic_cas(&scan_owned, 1, 0)) return 0;
    int err = bt_le_scan_stop();
    if (err && err != -EALREADY) {
        atomic_set(&scan_owned, 1);
        return err;
    }
    return 0;
}

static void advertisement(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                           struct net_buf_simple *ad) {
    zmk_ble_mouse_advertisement(addr, type, ad);
}

static void scan_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(scan_work, scan_work_cb);
static void scan_work_cb(struct k_work *work) {
    if (atomic_get(&connecting)) return;
    bool needed = zmk_ble_mouse_wants_scan();
    if (!needed || !split_ready()) {
        stop_own_scan();
        if (needed) zmk_ble_mouse_schedule(&scan_work, 1000);
        return;
    }
    if (atomic_get(&scan_owned)) return;
    struct bt_le_scan_param params = {
        .type = BT_LE_SCAN_TYPE_ACTIVE, .options = BT_LE_SCAN_OPT_NONE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL, .window = BT_GAP_SCAN_FAST_WINDOW,
    };
    int err = bt_le_scan_start(&params, advertisement);
    if (err) {
        /* EALREADY means somebody else owns the scan. Do not stop/restart it. */
        zmk_ble_mouse_schedule(&scan_work, 1000);
        return;
    }
    atomic_set(&scan_owned, 1);
    if (!split_ready()) {
        stop_own_scan();
        zmk_ble_mouse_schedule(&scan_work, 1000);
    }
}
void zmk_ble_central_scan_refresh(void) { zmk_ble_mouse_schedule(&scan_work, 0); }

static void connect_work_cb(struct k_work *work) {
    int err = stop_own_scan();
    if (!err) err = pending_connect(&pending_address);
    /* A queued address is consumed here; the client owns the connection wait.
     * Its auto-connect request yields to explicit scans in Zephyr. */
    atomic_clear(&connecting);
    if (err) zmk_ble_mouse_schedule(&scan_work, 1000);
}
K_WORK_DEFINE(connect_work, connect_work_cb);
int zmk_ble_central_connect(const bt_addr_le_t *addr, int (*connect)(const bt_addr_le_t *)) {
    if (!atomic_get(&initialized) || !atomic_cas(&connecting, 0, 1)) return -EBUSY;
    bt_addr_le_copy(&pending_address, addr);
    pending_connect = connect;
    zmk_ble_mouse_submit(&connect_work);
    return 0;
}
void zmk_ble_central_connection_complete(void) { zmk_ble_central_scan_refresh(); }

static void settle_work_cb(struct k_work *work) {
    atomic_clear(&split_settling);
    zmk_ble_central_scan_refresh();
}
K_WORK_DELAYABLE_DEFINE(settle_work, settle_work_cb);
static void connection_changed(struct bt_conn *conn, uint8_t status) {
    /* Registered from settings commit, after split initialization. Zephyr 3.5
     * invokes dynamically registered callbacks in reverse registration order;
     * release our scan before the split callback restarts its own scan.
     * Controller-level start/disconnect races still require hardware testing. */
    atomic_set(&split_settling, 1);
    stop_own_scan();
    zmk_ble_mouse_schedule(&settle_work, 1000);
}
static struct bt_conn_cb radio_callbacks = {
    .connected = connection_changed, .disconnected = connection_changed,
};
void zmk_ble_central_scan_init(void) {
    if (atomic_get(&initialized)) return;
    static const struct k_work_queue_config config = {.name = "BLE mouse"};
    k_work_queue_start(&mouse_queue, mouse_stack, K_THREAD_STACK_SIZEOF(mouse_stack), 5, &config);
    bt_conn_cb_register(&radio_callbacks);
    atomic_set(&initialized, 1);
}
