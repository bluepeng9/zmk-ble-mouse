#include <test_runtime.h>
#include <zmk/ble_mouse.h>

static struct k_work *works[256];
static size_t work_count;
static struct k_work_delayable *timers[32];
static size_t timer_count;
static uint32_t now_ms = 100;
const bt_addr_le_t fake_any = {0};
const struct bt_uuid fake_uuids[6] = {{0x1812},{0x2a4b},{0x2a4d},{0x2a4e},{0x2a4c},{0x2902}};
const struct bt_uuid fake_report_ref = {0x2908};
struct bt_conn fake_connection;
struct bt_bond_info fake_bonds[6];
unsigned fake_bond_count, fake_create_count, fake_disconnect_count, fake_unpair_count;
unsigned fake_save_count, fake_delete_count, fake_scan_refresh_count;
int fake_create_error, fake_gatt_error;
bool fake_auto_connect, fake_scan_pending;
bool fake_scan_active;
unsigned fake_scan_start_count, fake_scan_stop_count;
bt_le_scan_cb_t *fake_scan_callback;
struct zmk_split_transport_status fake_transport_status;
static struct zmk_split_transport_status fake_get_status(void) { return fake_transport_status; }
static const struct zmk_split_transport_central_api fake_transport_api = {.get_status = fake_get_status};
struct zmk_split_transport_central fake_transport = {.api = &fake_transport_api};
struct bt_conn_cb *fake_connection_callbacks;
struct bt_conn_auth_info_cb *fake_auth_callbacks;
struct bt_gatt_discover_params *fake_discovery;
struct bt_gatt_read_params *fake_read;
struct bt_gatt_subscribe_params *fake_subscription;

int k_work_submit(struct k_work *work) {
    if (!work->pending) {
        assert(work_count < ARRAY_SIZE(works));
        work->pending = true;
        works[work_count++] = work;
    }
    return 0;
}

static void fake_schedule(struct k_work_delayable *work, int32_t ms) {
    size_t i;
    for (i = 0; i < timer_count && timers[i] != work; i++) {}
    if (i == timer_count) {
        assert(timer_count < ARRAY_SIZE(timers));
        timers[timer_count++] = work;
    }
    work->due = now_ms + ms;
}
void k_work_queue_start(struct k_work_q *q, uint8_t *stack, size_t size, int priority,
                        const struct k_work_queue_config *config) { q->started = true; }
int k_work_submit_to_queue(struct k_work_q *q, struct k_work *work) {
    assert(q->started); return k_work_submit(work);
}
int k_work_reschedule_for_queue(struct k_work_q *q, struct k_work_delayable *work, int delay) {
    assert(q->started); fake_schedule(work, delay); return 0;
}
#ifndef FAKE_REAL_RADIO
void zmk_ble_mouse_submit(struct k_work *work) { k_work_submit(work); }
void zmk_ble_mouse_schedule(struct k_work_delayable *work, int32_t ms) { fake_schedule(work, ms); }
#endif
int k_work_cancel_delayable(struct k_work_delayable *work) { work->due = -1; return 0; }
void fake_drain(void) {
    for (unsigned iterations = 0;; iterations++) {
        assert(iterations < 1000);
        for (size_t i = 0; i < timer_count; i++) {
            if (timers[i]->due >= 0 && timers[i]->due <= now_ms) {
                timers[i]->due = -1;
                k_work_submit(&timers[i]->work);
            }
        }
        if (!work_count) return;
        struct k_work *work = works[0];
        memmove(works, works + 1, --work_count * sizeof(works[0]));
        work->pending = false;
        work->handler(work);
    }
}
void fake_advance(uint32_t ms) { now_ms += ms; fake_drain(); }
uint32_t k_uptime_get_32(void) { return now_ms; }
int k_msgq_put(struct k_msgq *q, const void *data, int timeout) {
    if (q->count == q->capacity) return -ENOMSG;
    memcpy(q->data + q->tail * q->size, data, q->size);
    q->tail = (q->tail + 1) % q->capacity; q->count++; return 0;
}
int k_msgq_get(struct k_msgq *q, void *data, int timeout) {
    if (!q->count) return -ENOMSG;
    memcpy(data, q->data + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->capacity; q->count--; return 0;
}
void k_msgq_purge(struct k_msgq *q) { q->head = q->tail = q->count = 0; }

#ifndef FAKE_REAL_RADIO
void zmk_ble_central_scan_refresh(void) { fake_scan_refresh_count++; }
void zmk_ble_central_scan_init(void) {}
void zmk_ble_central_connection_complete(void) {}
int zmk_ble_central_connect(const bt_addr_le_t *addr, int (*connect)(const bt_addr_le_t *)) {
    return connect(addr);
}
#endif
int bt_le_scan_start(const struct bt_le_scan_param *params, bt_le_scan_cb_t *callback) {
    fake_scan_start_count++;
    if (fake_scan_active) return -EALREADY;
    fake_scan_active = true;
    fake_scan_callback = callback;
    return 0;
}
int bt_le_scan_stop(void) {
    fake_scan_stop_count++;
    if (!fake_scan_active) return -EALREADY;
    fake_scan_active = false;
    fake_scan_callback = NULL;
    return 0;
}
void bt_data_parse(struct net_buf_simple *buf, bool (*callback)(struct bt_data *,void *), void *data) {
    size_t i = 0;
    while (i < buf->len) {
        unsigned length = buf->data[i++];
        if (!length || length > buf->len - i) return;
        struct bt_data field = {.type = buf->data[i], .data = buf->data + i + 1, .data_len = length - 1};
        if (!callback(&field, data)) return;
        i += length;
    }
}
bt_addr_le_t fake_address(uint8_t value) {
    return (bt_addr_le_t){.type = 1, .a = {.val = {value,2,3,4,5,6}}};
}
int bt_le_set_auto_conn(const bt_addr_le_t *addr, const struct bt_le_conn_param *params) {
    if (!params) {
        fake_auto_connect = false;
        if (fake_scan_pending) {
            fake_connection.state = BT_CONN_STATE_DISCONNECTED;
            fake_scan_pending = false;
        }
        return 0;
    }
    fake_create_count++;
    if (fake_create_error) return fake_create_error;
    fake_connection = (struct bt_conn){.addr = *addr, .state = BT_CONN_STATE_CONNECTING};
    fake_auto_connect = fake_scan_pending = true;
    return 0;
}
struct bt_conn *bt_conn_lookup_addr_le(uint8_t id, const bt_addr_le_t *addr) {
    return bt_addr_le_cmp(addr, &fake_connection.addr) ? NULL : bt_conn_ref(&fake_connection);
}
struct bt_conn *bt_conn_ref(struct bt_conn *conn) { conn->references++; return conn; }
int bt_conn_disconnect(struct bt_conn *conn, uint8_t reason) {
    fake_disconnect_count++;
    bt_le_set_auto_conn(&conn->addr, NULL);
    if (conn->state == BT_CONN_STATE_DISCONNECTED) return -ENOTCONN;
    conn->state = BT_CONN_STATE_DISCONNECTING;
    return 0;
}
void bt_conn_unref(struct bt_conn *conn) { assert(conn->references > 0); conn->references--; }
int bt_conn_set_security(struct bt_conn *conn, bt_security_t level) { return 0; }
bt_security_t bt_conn_get_security(struct bt_conn *conn) { return conn->security; }
const bt_addr_le_t *bt_conn_get_dst(struct bt_conn *conn) { return &conn->addr; }
void bt_foreach_bond(uint8_t id, void (*fn)(const struct bt_bond_info *,void *), void *data) {
    for (unsigned i = 0; i < fake_bond_count; i++) fn(&fake_bonds[i], data);
}
int bt_unpair(uint8_t id, const bt_addr_le_t *peer) {
    assert(peer); /* A whole-controller bond erase is forbidden in these tests. */
    fake_unpair_count++;
    for (unsigned i = 0; i < fake_bond_count; i++) {
        if (!bt_addr_le_cmp(peer, &fake_bonds[i].addr)) {
            memmove(fake_bonds + i, fake_bonds + i + 1, (--fake_bond_count - i) * sizeof(fake_bonds[0]));
            return 0;
        }
    }
    return 0;
}
void bt_conn_cb_register(struct bt_conn_cb *cb) { fake_connection_callbacks = cb; }
int bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *cb) { fake_auth_callbacks = cb; return 0; }
int bt_gatt_discover(struct bt_conn *conn, struct bt_gatt_discover_params *params) {
    fake_discovery = params; return fake_gatt_error;
}
int bt_gatt_read(struct bt_conn *conn, struct bt_gatt_read_params *params) {
    fake_read = params; return fake_gatt_error;
}
int bt_gatt_write(struct bt_conn *conn, struct bt_gatt_write_params *params) { return fake_gatt_error; }
int bt_gatt_write_without_response(struct bt_conn *conn, uint16_t handle,
                                   const void *data, uint16_t length, bool sign) {
    assert(length == 1 && *(uint8_t *)data == 1); return fake_gatt_error;
}
int bt_gatt_subscribe(struct bt_conn *conn, struct bt_gatt_subscribe_params *params) {
    fake_subscription = params; return fake_gatt_error;
}
int settings_save_one(const char *name, const void *value, size_t length) {
    assert(!strcmp(name, "ble_mouse/peer") && length == sizeof(bt_addr_le_t));
    fake_save_count++; return 0;
}
int settings_delete(const char *name) {
    assert(!strcmp(name, "ble_mouse/peer")); fake_delete_count++; return 0;
}

void fake_runtime_reset(void) {
    for (size_t i = 0; i < work_count; i++) works[i]->pending = false;
    for (size_t i = 0; i < timer_count; i++) {
        timers[i]->due = -1; timers[i]->work.pending = false;
    }
    timer_count = work_count = 0;
    now_ms = 100;
    fake_bond_count = fake_create_count = fake_disconnect_count = fake_unpair_count = 0;
    fake_save_count = fake_delete_count = fake_scan_refresh_count = 0;
    fake_create_error = fake_gatt_error = 0;
    fake_auto_connect = fake_scan_pending = false;
    fake_scan_active = false;
    fake_scan_start_count = fake_scan_stop_count = 0;
    fake_scan_callback = NULL;
    fake_transport_status = (struct zmk_split_transport_status){0};
    fake_connection_callbacks = NULL;
    fake_auth_callbacks = NULL;
    fake_discovery = NULL; fake_read = NULL; fake_subscription = NULL;
    memset(&fake_connection, 0, sizeof(fake_connection));
}
