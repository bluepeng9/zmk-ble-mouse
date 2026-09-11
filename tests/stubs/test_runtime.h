/* A small deterministic host fake for exercising the production client/output code. */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#define CONFIG_BT_MAX_PAIRED 6
#define CONFIG_BT_KEYS_OVERWRITE_OLDEST 0
#define CONFIG_ZMK_BLE_MOUSE_QUEUE_SIZE 8
#define CONFIG_ZMK_BLE_MOUSE_LOG_LEVEL 3
#define CONFIG_ZMK_BLE_MOUSE_CENTRAL 1
#define IS_ENABLED(x) (x)
#define BUILD_ASSERT(c, m) _Static_assert(c, m)
#define BIT(n) (1u << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define CLAMP(v,a,b) ((v) < (a) ? (a) : (v) > (b) ? (b) : (v))
#define CONTAINER_OF(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define K_FOREVER -1
#define K_NO_WAIT 0
#define K_MSEC(ms) (ms)
#define LOG_MODULE_REGISTER(name, level) typedef int log_stub_##name
#define LOG_INF(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)

struct device { int unused; };
#define DEVICE_DT_INST_DEFINE(...) static const struct device fake_receiver = {0}
#define DEVICE_DT_INST_GET(n) (&fake_receiver)
#define INPUT_BTN_0 0x100
#define INPUT_REL_X 0
#define INPUT_REL_Y 1
#define INPUT_REL_HWHEEL 6
#define INPUT_REL_WHEEL 8
int input_report_key(const struct device *, uint16_t, int32_t, bool, int);
int input_report_rel(const struct device *, uint16_t, int32_t, bool, int);
typedef struct { bool endpoint; } zmk_event_t;
static inline const void *as_zmk_endpoint_changed(const zmk_event_t *event) {
    return event->endpoint ? event : NULL;
}
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_LISTENER(n,f) typedef int listener_stub_##n
#define ZMK_SUBSCRIPTION(n,e) typedef int subscription_stub_##n

typedef int atomic_t;
static inline int atomic_get(const atomic_t *p) { return *p; }
static inline int atomic_set(atomic_t *p, int v) { int old = *p; *p = v; return old; }
static inline int atomic_clear(atomic_t *p) { return atomic_set(p, 0); }
static inline int atomic_inc(atomic_t *p) { return (*p)++; }
static inline bool atomic_cas(atomic_t *p, int old, int value) {
    if (*p != old) return false;
    *p = value;
    return true;
}
static inline void atomic_set_bit(atomic_t *p, int bit) { *p |= BIT(bit); }

struct k_work { void (*handler)(struct k_work *); bool pending; };
struct k_work_delayable { struct k_work work; int64_t due; };
struct k_work_q { bool started; };
struct k_work_queue_config { const char *name; };
#define CONFIG_ZMK_BLE_MOUSE_STACK_SIZE 4096
#define K_THREAD_STACK_DEFINE(n,s) static uint8_t n[s]
#define K_THREAD_STACK_SIZEOF(n) sizeof(n)
void k_work_queue_start(struct k_work_q *, uint8_t *, size_t, int, const struct k_work_queue_config *);
int k_work_submit_to_queue(struct k_work_q *, struct k_work *);
int k_work_reschedule_for_queue(struct k_work_q *, struct k_work_delayable *, int);
struct k_spinlock { bool held; };
typedef int k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held); lock->held = true; return 0;
}
static inline void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    assert(lock->held); lock->held = false;
}
struct k_msgq { uint8_t *data; size_t size, capacity, head, tail, count; };
#define K_WORK_DEFINE(n,f) struct k_work n = {.handler = f}
#define K_WORK_DELAYABLE_DEFINE(n,f) struct k_work_delayable n = {.work = {.handler = f}, .due = -1}
#define K_MSGQ_DEFINE(n,s,c,a) static uint8_t n##_data[(s)*(c)]; \
    struct k_msgq n = {.data = n##_data, .size = s, .capacity = c}
int k_work_submit(struct k_work *work);
int k_work_cancel_delayable(struct k_work_delayable *work);
int k_msgq_put(struct k_msgq *q, const void *data, int timeout);
int k_msgq_get(struct k_msgq *q, void *data, int timeout);
void k_msgq_purge(struct k_msgq *q);
uint32_t k_uptime_get_32(void);
void fake_drain(void);
void fake_advance(uint32_t ms);
void fake_runtime_reset(void);

typedef struct { uint8_t val[6]; } bt_addr_t;
typedef struct { uint8_t type; bt_addr_t a; } bt_addr_le_t;
extern const bt_addr_le_t fake_any;
#define BT_ADDR_LE_ANY (&fake_any)
#define BT_ADDR_LE_RANDOM 1
#define BT_ID_DEFAULT 0
static inline int bt_addr_le_cmp(const bt_addr_le_t *a, const bt_addr_le_t *b) {
    return memcmp(a, b, sizeof(*a));
}
static inline void bt_addr_le_copy(bt_addr_le_t *a, const bt_addr_le_t *b) { *a = *b; }
struct net_buf_simple { uint8_t *data; size_t len; };
struct bt_data { uint8_t type, data_len; const uint8_t *data; };
typedef void bt_le_scan_cb_t(const bt_addr_le_t *, int8_t, uint8_t, struct net_buf_simple *);
struct bt_le_scan_param { uint8_t type, options; uint16_t interval, window; };
#define BT_LE_SCAN_TYPE_ACTIVE 1
#define BT_LE_SCAN_OPT_NONE 0
#define BT_GAP_SCAN_FAST_INTERVAL 96
#define BT_GAP_SCAN_FAST_WINDOW 48
int bt_le_scan_start(const struct bt_le_scan_param *, bt_le_scan_cb_t *);
int bt_le_scan_stop(void);
extern bool fake_scan_active;
extern unsigned fake_scan_start_count, fake_scan_stop_count;
extern bt_le_scan_cb_t *fake_scan_callback;
#define BT_DATA_NAME_COMPLETE 9
#define BT_DATA_NAME_SHORTENED 8
#define BT_GAP_ADV_TYPE_ADV_IND 0
#define BT_GAP_ADV_TYPE_ADV_DIRECT_IND 1
#define BT_GAP_ADV_TYPE_SCAN_RSP 4
void bt_data_parse(struct net_buf_simple *, bool (*cb)(struct bt_data *,void *), void *);

typedef uint8_t bt_security_t;
enum bt_security_err { BT_SECURITY_ERR_SUCCESS, BT_SECURITY_ERR_AUTH_FAIL };
#define BT_SECURITY_L2 2
#define BT_HCI_ERR_REMOTE_USER_TERM_CONN 0x13
enum bt_conn_state { BT_CONN_STATE_DISCONNECTED, BT_CONN_STATE_CONNECTING,
                     BT_CONN_STATE_CONNECTED, BT_CONN_STATE_DISCONNECTING };
struct bt_conn { bt_addr_le_t addr; uint8_t security; int references; enum bt_conn_state state; };
struct bt_bond_info { bt_addr_le_t addr; };
struct bt_le_conn_param { int min, max, latency, timeout; };
#define BT_LE_CONN_PARAM(a,b,c,d) (&(struct bt_le_conn_param){a,b,c,d})
struct bt_conn_cb {
    void (*connected)(struct bt_conn *,uint8_t);
    void (*disconnected)(struct bt_conn *,uint8_t);
    void (*security_changed)(struct bt_conn *,bt_security_t,enum bt_security_err);
};
struct bt_conn_auth_info_cb {
    void (*pairing_complete)(struct bt_conn *,bool);
    void (*pairing_failed)(struct bt_conn *,enum bt_security_err);
};
int bt_conn_disconnect(struct bt_conn *, uint8_t);
int bt_le_set_auto_conn(const bt_addr_le_t *, const struct bt_le_conn_param *);
struct bt_conn *bt_conn_lookup_addr_le(uint8_t, const bt_addr_le_t *);
struct bt_conn *bt_conn_ref(struct bt_conn *);
void bt_conn_unref(struct bt_conn *);
int bt_conn_set_security(struct bt_conn *, bt_security_t);
bt_security_t bt_conn_get_security(struct bt_conn *);
const bt_addr_le_t *bt_conn_get_dst(struct bt_conn *);
void bt_foreach_bond(uint8_t, void (*)(const struct bt_bond_info *,void *), void *);
int bt_unpair(uint8_t, const bt_addr_le_t *);
void bt_conn_cb_register(struct bt_conn_cb *);
int bt_conn_auth_info_cb_register(struct bt_conn_auth_info_cb *);

struct bt_uuid { uint16_t value; };
extern const struct bt_uuid fake_uuids[6];
#define BT_UUID_HIDS (&fake_uuids[0])
#define BT_UUID_HIDS_REPORT_MAP (&fake_uuids[1])
#define BT_UUID_HIDS_REPORT (&fake_uuids[2])
#define BT_UUID_HIDS_PROTOCOL_MODE (&fake_uuids[3])
#define BT_UUID_HIDS_CTRL_POINT (&fake_uuids[4])
#define BT_UUID_GATT_CCC (&fake_uuids[5])
extern const struct bt_uuid fake_report_ref;
#define BT_UUID_HIDS_REPORT_REF (&fake_report_ref)
static inline int bt_uuid_cmp(const struct bt_uuid *a, const struct bt_uuid *b) {
    return a->value - b->value;
}
struct bt_gatt_attr { const struct bt_uuid *uuid; uint16_t handle; void *user_data; };
struct bt_gatt_service_val { uint16_t end_handle; };
struct bt_gatt_chrc { const struct bt_uuid *uuid; uint16_t value_handle; uint8_t properties; };
struct bt_gatt_discover_params {
    const struct bt_uuid *uuid;
    uint8_t (*func)(struct bt_conn *,const struct bt_gatt_attr *,struct bt_gatt_discover_params *);
    uint16_t start_handle, end_handle; uint8_t type;
};
struct bt_gatt_read_params {
    uint8_t (*func)(struct bt_conn *,uint8_t,struct bt_gatt_read_params *,const void *,uint16_t);
    size_t handle_count; struct { uint16_t handle, offset; } single;
};
struct bt_gatt_write_params {
    void (*func)(struct bt_conn *,uint8_t,struct bt_gatt_write_params *);
    uint16_t handle; const void *data; uint16_t length;
};
struct bt_gatt_subscribe_params {
    uint8_t (*notify)(struct bt_conn *,struct bt_gatt_subscribe_params *,const void *,uint16_t);
    void (*subscribe)(struct bt_conn *,uint8_t,struct bt_gatt_subscribe_params *);
    uint16_t value_handle, ccc_handle, value; bt_security_t min_security; atomic_t flags[1];
};
#define BT_GATT_ITER_STOP 0
#define BT_GATT_ITER_CONTINUE 1
#define BT_GATT_DISCOVER_PRIMARY 0
#define BT_GATT_DISCOVER_CHARACTERISTIC 3
#define BT_GATT_DISCOVER_DESCRIPTOR 4
#define BT_GATT_CHRC_WRITE_WITHOUT_RESP 4
#define BT_GATT_CCC_NOTIFY 1
#define BT_GATT_SUBSCRIBE_FLAG_VOLATILE 0
int bt_gatt_discover(struct bt_conn *, struct bt_gatt_discover_params *);
int bt_gatt_read(struct bt_conn *, struct bt_gatt_read_params *);
int bt_gatt_write(struct bt_conn *, struct bt_gatt_write_params *);
int bt_gatt_write_without_response(struct bt_conn *, uint16_t, const void *, uint16_t, bool);
int bt_gatt_subscribe(struct bt_conn *, struct bt_gatt_subscribe_params *);

typedef int (*settings_read_cb)(void *, void *, size_t);
#define SETTINGS_STATIC_HANDLER_DEFINE(n,p,a,b,c,d) typedef int settings_stub_##n
int settings_save_one(const char *,const void *,size_t);
int settings_delete(const char *);

extern struct bt_conn fake_connection;
extern struct bt_bond_info fake_bonds[6];
extern unsigned fake_bond_count, fake_create_count, fake_disconnect_count, fake_unpair_count;
extern unsigned fake_save_count, fake_delete_count, fake_scan_refresh_count;
extern int fake_create_error, fake_gatt_error;
extern bool fake_auto_connect, fake_scan_pending;
extern struct bt_conn_cb *fake_connection_callbacks;
extern struct bt_conn_auth_info_cb *fake_auth_callbacks;
extern struct bt_gatt_discover_params *fake_discovery;
extern struct bt_gatt_read_params *fake_read;
extern struct bt_gatt_subscribe_params *fake_subscription;
bt_addr_le_t fake_address(uint8_t value);

enum zmk_split_transport_connections_status {
    ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_DISCONNECTED,
    ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_SOME_CONNECTED,
    ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_ALL_CONNECTED,
};
struct zmk_split_transport_status {
    bool available, enabled;
    enum zmk_split_transport_connections_status connections;
};
struct zmk_split_transport_central_api { struct zmk_split_transport_status (*get_status)(void); };
struct zmk_split_transport_central { const struct zmk_split_transport_central_api *api; };
enum zmk_split_transport_peripheral_event_type {
    ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_KEY_POSITION_EVENT,
    ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_SENSOR_EVENT,
    ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_INPUT_EVENT,
    ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_BATTERY_EVENT,
    ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_HEART_BEAT_EVENT,
    ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_RELAY_EVENT,
};
struct zmk_split_transport_peripheral_event {
    enum zmk_split_transport_peripheral_event_type type;
    union {
        struct { uint8_t level; } battery_event;
        uint8_t payload[16];
    } data;
} __attribute__((packed));
int zmk_split_transport_central_peripheral_event_handler(
    const struct zmk_split_transport_central *, uint8_t,
    struct zmk_split_transport_peripheral_event);
extern struct zmk_split_transport_central fake_transport;
extern struct zmk_split_transport_status fake_transport_status;
#define STRUCT_SECTION_FOREACH(type, name) \
    for (struct type *name = &fake_transport; name; name = NULL)
