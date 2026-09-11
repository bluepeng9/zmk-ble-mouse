/* SPDX-License-Identifier: MIT */
#include <zmk/ble_mouse.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_mouse, CONFIG_ZMK_BLE_MOUSE_LOG_LEVEL);

BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST),
             "BLE mouse reception must not evict existing bonds");

#define MAX_CHARACTERISTICS 12
#define PAIR_TIME_MS 60000
#define DISCOVERY_TIME_MS 20000

enum client_stage {
    IDLE, CONNECTING, SECURITY, SERVICE, CHARACTERISTICS, DESCRIPTORS,
    REPORT_MAP, REPORT_REFERENCES, PROTOCOL, SUBSCRIPTIONS, READY, CLOSING,
};

struct report_characteristic {
    uint16_t handle, end, reference, ccc;
    uint8_t id, type, buttons;
    struct bt_gatt_subscribe_params subscription;
};

static struct {
    uint16_t service_start, service_end, map_handle, protocol_handle, control_handle;
    uint8_t protocol_properties;
    uint8_t count, index, subscribed, covered_buttons, covered_axes;
    struct report_characteristic reports[MAX_CHARACTERISTICS];
    struct bt_gatt_discover_params discovery;
    struct bt_gatt_read_params read;
    struct bt_gatt_write_params write;
    struct mouse_hid_map map;
    uint8_t map_data[MOUSE_HID_MAX_MAP_BYTES];
    size_t map_size;
    uint8_t reference_data[2], reference_size, protocol;
} session;

static struct bt_conn *mouse_conn;
static struct bt_conn *connected_event_conn;
static bt_addr_le_t connecting_address;
static bt_addr_le_t peer;
static atomic_t have_peer, pairing, scan_wanted, stage, failure, clear_held, started;
static bool forget_requested, candidate_bonded, new_candidate;
static uint8_t connected_error;
static uint32_t retry_ms = 1000;

static void discovery_work_cb(struct k_work *work);
static void failure_work_cb(struct k_work *work);
static void retry_work_cb(struct k_work *work);
static void timeout_work_cb(struct k_work *work);
static void pair_timeout_cb(struct k_work *work);
static void forget_peer(void);
static void finish_ready(void);
static int connect_mouse(const bt_addr_le_t *addr);
static void disconnected_work_cb(struct k_work *work);
K_WORK_DEFINE(discovery_work, discovery_work_cb);
K_WORK_DEFINE(failure_work, failure_work_cb);
K_WORK_DELAYABLE_DEFINE(retry_work, retry_work_cb);
K_WORK_DELAYABLE_DEFINE(timeout_work, timeout_work_cb);
K_WORK_DELAYABLE_DEFINE(pair_timeout, pair_timeout_cb);

static bool owns(struct bt_conn *conn) { return conn && conn == mouse_conn; }

static bool valid_callback(struct bt_conn *conn) {
    return owns(conn) && !atomic_get(&failure) && atomic_get(&stage) != CLOSING;
}

static void next_stage(enum client_stage next) {
    atomic_set(&stage, next);
    zmk_ble_mouse_submit(&discovery_work);
}

static void fail(int err) {
    atomic_cas(&failure, 0, err ? err : -EIO);
    zmk_ble_mouse_submit(&failure_work);
}

static void count_bond(const struct bt_bond_info *info, void *data) {
    (*(unsigned *)data)++;
}

static unsigned bond_count(void) {
    unsigned count = 0;
    bt_foreach_bond(BT_ID_DEFAULT, count_bond, &count);
    return count;
}

struct bond_lookup { const bt_addr_le_t *addr; bool found; };
static void find_bond(const struct bt_bond_info *info, void *data) {
    struct bond_lookup *lookup = data;
    if (!bt_addr_le_cmp(&info->addr, lookup->addr)) {
        lookup->found = true;
    }
}

static bool is_bonded(const bt_addr_le_t *addr) {
    struct bond_lookup lookup = {.addr = addr};
    bt_foreach_bond(BT_ID_DEFAULT, find_bond, &lookup);
    return lookup.found;
}

bool zmk_ble_mouse_wants_scan(void) { return atomic_get(&scan_wanted); }

static void retry_work_cb(struct k_work *work) {
    bool wanted = !mouse_conn && (atomic_get(&have_peer) || atomic_get(&pairing));
    if (atomic_get(&have_peer) && !is_bonded(&peer)) {
        LOG_WRN("Saved mouse bond is missing; hold Mouse Clear, then pair again");
        wanted = false;
    }
    atomic_set(&scan_wanted, wanted);
    if (wanted && atomic_get(&have_peer)) {
        /* Background auto-connect also accepts directed advertisements and
         * pauses while the split keyboard owns an explicit scan. */
        zmk_ble_central_connect(&peer, connect_mouse);
    }
    zmk_ble_central_scan_refresh();
}

static void request_retry(void) {
    atomic_clear(&scan_wanted);
    zmk_ble_central_scan_refresh();
    zmk_ble_mouse_schedule(&retry_work, retry_ms);
    retry_ms = MIN(retry_ms * 2, 16000);
}

static void failure_work_cb(struct k_work *work) {
    LOG_WRN("Mouse connection/setup failed: %d", (int)atomic_get(&failure));
    atomic_set(&stage, CLOSING);
    atomic_clear(&scan_wanted);
    if (mouse_conn) {
        int err = bt_conn_disconnect(mouse_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        if (err == -ENOTCONN) {
            /* Cancelling CONNECTING_SCAN may produce no disconnected callback. */
            disconnected_work_cb(NULL);
            return;
        }
        if (err && err != -ENOTCONN) {
            LOG_WRN("Mouse disconnect failed: %d", err);
        }
    } else {
        atomic_set(&stage, IDLE);
        atomic_clear(&failure);
        request_retry();
    }
}

static void timeout_work_cb(struct k_work *work) {
    if (mouse_conn && atomic_get(&stage) != READY) {
        fail(-ETIMEDOUT);
    }
}

static void pair_timeout_cb(struct k_work *work) {
    atomic_clear(&pairing);
    LOG_INF("Mouse pairing window closed");
    if (mouse_conn && new_candidate && atomic_get(&stage) != READY) {
        fail(-ETIMEDOUT);
    }
    retry_work_cb(NULL);
}

static int connect_mouse(const bt_addr_le_t *addr) {
    if (mouse_conn || !atomic_get(&scan_wanted)) {
        return -EALREADY;
    }
    bool known = atomic_get(&have_peer);
    if ((known && bt_addr_le_cmp(addr, &peer)) || (!known && !atomic_get(&pairing))) {
        return -ECANCELED;
    }
    if (!known && is_bonded(addr)) {
        /* Do not adopt/re-pair a bond owned by an existing keyboard/PC slot. */
        return -EALREADY;
    }
    if (!known && bond_count() >= CONFIG_BT_MAX_PAIRED) {
        LOG_WRN("Mouse pairing refused: all %d bond slots are occupied", CONFIG_BT_MAX_PAIRED);
        atomic_clear(&pairing);
        atomic_clear(&scan_wanted);
        return -ENOMEM;
    }
    memset(&session, 0, sizeof(session));
    new_candidate = !known;
    candidate_bonded = false;
    atomic_clear(&failure);
    atomic_clear(&scan_wanted);
    atomic_set(&stage, CONNECTING);
    bt_addr_le_copy(&connecting_address, addr);
    /* 7.5-15 ms; no peripheral latency requested. Keep the keyboard settings. */
    int err = bt_le_set_auto_conn(addr, BT_LE_CONN_PARAM(6, 12, 0, 400));
    if (!err) {
        mouse_conn = bt_conn_lookup_addr_le(BT_ID_DEFAULT, addr);
        if (!mouse_conn) {
            bt_le_set_auto_conn(addr, NULL);
            err = -ENOTCONN;
        }
    }
    if (err) {
        atomic_set(&stage, IDLE);
        request_retry();
        return err;
    }
    zmk_ble_mouse_schedule(&timeout_work, DISCOVERY_TIME_MS);
    return 0;
}

struct advertiser {
    bt_addr_le_t addr;
    uint32_t seen;
    bool connectable, m720;
};
/* Accessed only by the Bluetooth advertising callback. */
static struct advertiser advertisers[8];

static bool parse_ad(struct bt_data *data, void *context) {
    struct advertiser *advertiser = context;
    if ((data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) &&
        data->data_len >= 4 && !memcmp(data->data, "M720", 4)) {
        advertiser->m720 = true;
    }
    return true;
}

bool zmk_ble_mouse_advertisement(const bt_addr_le_t *addr, uint8_t type,
                                 struct net_buf_simple *ad) {
    if (!atomic_get(&scan_wanted)) {
        return false;
    }
    if (atomic_get(&have_peer)) {
        if (bt_addr_le_cmp(addr, &peer)) {
            return false;
        }
        if (type == BT_GAP_ADV_TYPE_ADV_IND || type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
            zmk_ble_central_connect(addr, connect_mouse);
        }
        return true;
    }
    if (!atomic_get(&pairing)) {
        return false;
    }
    uint32_t now = k_uptime_get_32();
    struct advertiser *entry = NULL, *oldest = &advertisers[0];
    for (size_t i = 0; i < ARRAY_SIZE(advertisers); i++) {
        if (!bt_addr_le_cmp(addr, &advertisers[i].addr) &&
            now - advertisers[i].seen < 2000) {
            entry = &advertisers[i];
            break;
        }
        if (now - advertisers[i].seen > now - oldest->seen) {
            oldest = &advertisers[i];
        }
    }
    if (!entry) {
        entry = oldest;
        memset(entry, 0, sizeof(*entry));
        bt_addr_le_copy(&entry->addr, addr);
    }
    entry->seen = now;
    entry->connectable |= type == BT_GAP_ADV_TYPE_ADV_IND;
    struct net_buf_simple copy = *ad;
    bt_data_parse(&copy, parse_ad, entry);
    if (entry->connectable && entry->m720) {
        zmk_ble_central_connect(addr, connect_mouse);
        return true;
    }
    return false;
}

static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            struct bt_gatt_discover_params *params) {
    if (!valid_callback(conn)) {
        return BT_GATT_ITER_STOP;
    }
    enum client_stage current = atomic_get(&stage);
    if (!attr) {
        switch (current) {
        case SERVICE: fail(-ENOENT); break;
        case CHARACTERISTICS:
            if (!session.map_handle || !session.count) {
                fail(-ENOTSUP);
            } else {
                next_stage(DESCRIPTORS);
            }
            break;
        case DESCRIPTORS: next_stage(REPORT_MAP); break;
        default: fail(-EINVAL); break;
        }
        return BT_GATT_ITER_STOP;
    }
    if (current == SERVICE) {
        const struct bt_gatt_service_val *service = attr->user_data;
        session.service_start = attr->handle + 1;
        session.service_end = service->end_handle;
        next_stage(CHARACTERISTICS);
        return BT_GATT_ITER_STOP;
    }
    if (current == CHARACTERISTICS) {
        const struct bt_gatt_chrc *characteristic = attr->user_data;
        if (session.count && session.reports[session.count - 1].end == session.service_end) {
            session.reports[session.count - 1].end = attr->handle - 1;
        }
        if (!bt_uuid_cmp(characteristic->uuid, BT_UUID_HIDS_REPORT_MAP)) {
            session.map_handle = characteristic->value_handle;
        } else if (!bt_uuid_cmp(characteristic->uuid, BT_UUID_HIDS_PROTOCOL_MODE)) {
            session.protocol_handle = characteristic->value_handle;
            session.protocol_properties = characteristic->properties;
        } else if (!bt_uuid_cmp(characteristic->uuid, BT_UUID_HIDS_CTRL_POINT)) {
            session.control_handle = characteristic->value_handle;
        } else if (!bt_uuid_cmp(characteristic->uuid, BT_UUID_HIDS_REPORT)) {
            if (session.count == MAX_CHARACTERISTICS) {
                fail(-E2BIG);
                return BT_GATT_ITER_STOP;
            }
            session.reports[session.count++] = (struct report_characteristic){
                .handle = characteristic->value_handle, .end = session.service_end,
            };
        }
    } else if (current == DESCRIPTORS) {
        for (uint8_t i = 0; i < session.count; i++) {
            struct report_characteristic *report = &session.reports[i];
            if (attr->handle <= report->handle || attr->handle > report->end) {
                continue;
            }
            if (!bt_uuid_cmp(attr->uuid, BT_UUID_HIDS_REPORT_REF)) {
                report->reference = attr->handle;
            } else if (!bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC)) {
                report->ccc = attr->handle;
            }
        }
    }
    return BT_GATT_ITER_CONTINUE;
}

static uint8_t read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
                       const void *data, uint16_t length) {
    if (!valid_callback(conn)) {
        return BT_GATT_ITER_STOP;
    }
    if (err) {
        fail(-EIO);
        return BT_GATT_ITER_STOP;
    }
    if (atomic_get(&stage) == REPORT_MAP) {
        if (data) {
            if (length > sizeof(session.map_data) - session.map_size) {
                fail(-E2BIG);
                return BT_GATT_ITER_STOP;
            }
            memcpy(session.map_data + session.map_size, data, length);
            session.map_size += length;
        } else {
            /* Parsing and further GATT requests run outside the RX thread. */
            next_stage(REPORT_REFERENCES);
        }
    } else if (atomic_get(&stage) == REPORT_REFERENCES) {
        if (data) {
            if (length > sizeof(session.reference_data) - session.reference_size) {
                fail(-EINVAL);
                return BT_GATT_ITER_STOP;
            }
            memcpy(session.reference_data + session.reference_size, data, length);
            session.reference_size += length;
        } else {
            if (session.reference_size != 2) {
                fail(-EINVAL);
                return BT_GATT_ITER_STOP;
            }
            struct report_characteristic *report = &session.reports[session.index++];
            report->id = session.reference_data[0];
            report->type = session.reference_data[1];
            zmk_ble_mouse_submit(&discovery_work);
        }
    }
    return data ? BT_GATT_ITER_CONTINUE : BT_GATT_ITER_STOP;
}

static uint8_t notify_cb(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
                         const void *data, uint16_t length) {
    if (!valid_callback(conn) || !data) {
        if (data == NULL && valid_callback(conn) && atomic_get(&stage) == READY) {
            fail(-EIO);
        }
        return BT_GATT_ITER_STOP;
    }
    if (atomic_get(&stage) != READY) {
        return BT_GATT_ITER_CONTINUE;
    }
    struct report_characteristic *report =
        CONTAINER_OF(params, struct report_characteristic, subscription);
    struct mouse_hid_frame frame;
    int err = mouse_hid_decode(&session.map, report->id, data, length, &frame);
    if (err) {
        LOG_DBG("Ignoring invalid mouse report id=%u len=%u: %d", report->id, length, err);
        return BT_GATT_ITER_CONTINUE;
    }
    report->buttons = frame.buttons;
    uint8_t physical = 0;
    for (uint8_t i = 0; i < session.count; i++) {
        physical |= session.reports[i].buttons;
    }
    zmk_mouse_output_receive(&frame, physical);
    return BT_GATT_ITER_CONTINUE;
}

static void subscribed_cb(struct bt_conn *conn, uint8_t err,
                          struct bt_gatt_subscribe_params *params) {
    if (!valid_callback(conn)) {
        return;
    }
    if (err) {
        fail(-EIO);
        return;
    }
    struct report_characteristic *report =
        CONTAINER_OF(params, struct report_characteristic, subscription);
    int index = mouse_hid_report_index(&session.map, report->id);
    for (uint8_t i = 0; i < session.map.field_count; i++) {
        const struct mouse_hid_field *field = &session.map.fields[i];
        if (field->report != index) {
            continue;
        }
        if (field->usage == MOUSE_BUTTON) {
            session.covered_buttons |= BIT(field->button);
        } else {
            session.covered_axes |= BIT(field->usage);
        }
    }
    session.subscribed++;
    session.index++;
    zmk_ble_mouse_submit(&discovery_work);
}

static void written_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params) {
    if (!valid_callback(conn)) {
        return;
    }
    if (err) {
        fail(-EIO);
    } else {
        next_stage(SUBSCRIPTIONS);
    }
}

static void finish_ready(void) {
    if (!session.subscribed || session.covered_buttons != 0x1f ||
        session.covered_axes != 0x0f || (new_candidate && !candidate_bonded)) {
        fail(-ENOTSUP);
        return;
    }
    if (new_candidate) {
        bt_addr_le_t address = *bt_conn_get_dst(mouse_conn);
        if (!is_bonded(&address)) {
            fail(-EACCES);
            return;
        }
        int err = settings_save_one("ble_mouse/peer", &address, sizeof(address));
        if (err) {
            fail(err);
            return;
        }
        bt_addr_le_copy(&peer, &address);
        atomic_set(&have_peer, 1);
        new_candidate = false;
    }
    atomic_clear(&pairing);
    atomic_set(&stage, READY);
    retry_ms = 1000;
    k_work_cancel_delayable(&pair_timeout);
    k_work_cancel_delayable(&timeout_work);
    LOG_INF("M720 ready: movement, five buttons, wheel and horizontal pan");
}

static void discovery_work_cb(struct k_work *work) {
    if (!mouse_conn || atomic_get(&failure) || atomic_get(&stage) == CLOSING) {
        return;
    }
    int err = 0;
    switch (atomic_get(&stage)) {
    case SERVICE:
        session.discovery = (struct bt_gatt_discover_params){
            .uuid = BT_UUID_HIDS, .start_handle = 1, .end_handle = 0xffff,
            .type = BT_GATT_DISCOVER_PRIMARY, .func = discover_cb,
        };
        err = bt_gatt_discover(mouse_conn, &session.discovery);
        break;
    case CHARACTERISTICS:
    case DESCRIPTORS:
        session.discovery = (struct bt_gatt_discover_params){
            .start_handle = session.service_start, .end_handle = session.service_end,
            .type = atomic_get(&stage) == CHARACTERISTICS ? BT_GATT_DISCOVER_CHARACTERISTIC :
                                                          BT_GATT_DISCOVER_DESCRIPTOR,
            .func = discover_cb,
        };
        err = bt_gatt_discover(mouse_conn, &session.discovery);
        break;
    case REPORT_MAP:
        session.read = (struct bt_gatt_read_params){
            .func = read_cb, .handle_count = 1, .single = {.handle = session.map_handle},
        };
        err = bt_gatt_read(mouse_conn, &session.read);
        break;
    case REPORT_REFERENCES:
        if (!session.map.report_count) {
            err = mouse_hid_parse(session.map_data, session.map_size, &session.map);
            if (err) {
                break;
            }
            if (session.map.buttons != 0x1f || !session.map.wheel || !session.map.pan) {
                LOG_WRN("HID map lacks required M720 buttons/wheels");
                err = -ENOTSUP;
                break;
            }
            LOG_DBG("HID map parsed: %u input reports, %u fields",
                    session.map.report_count, session.map.field_count);
        }
        while (session.index < session.count && !session.reports[session.index].reference) {
            session.index++;
        }
        if (session.index == session.count) {
            session.index = 0;
            next_stage(PROTOCOL);
            return;
        }
        session.reference_size = 0;
        session.read = (struct bt_gatt_read_params){
            .func = read_cb, .handle_count = 1,
            .single = {.handle = session.reports[session.index].reference},
        };
        err = bt_gatt_read(mouse_conn, &session.read);
        break;
    case PROTOCOL:
        session.protocol = 1; /* Report protocol; boot mouse loses extra buttons. */
        if (session.control_handle) {
            err = bt_gatt_write_without_response(mouse_conn, session.control_handle,
                                                  &session.protocol, 1, false);
            if (err) {
                break;
            }
        }
        if (!session.protocol_handle) {
            next_stage(SUBSCRIPTIONS);
            return;
        }
        if (session.protocol_properties & BT_GATT_CHRC_WRITE_WITHOUT_RESP) {
            err = bt_gatt_write_without_response(mouse_conn, session.protocol_handle,
                                                  &session.protocol, 1, false);
            if (!err) {
                next_stage(SUBSCRIPTIONS);
            }
        } else {
            session.write = (struct bt_gatt_write_params){
                .func = written_cb, .handle = session.protocol_handle,
                .data = &session.protocol, .length = 1,
            };
            err = bt_gatt_write(mouse_conn, &session.write);
        }
        break;
    case SUBSCRIPTIONS:
        while (session.index < session.count) {
            struct report_characteristic *report = &session.reports[session.index];
            int index = mouse_hid_report_index(&session.map, report->id);
            if (report->type != 1 || index < 0 || !session.map.reports[index].useful) {
                session.index++;
                continue;
            }
            if (!report->ccc) {
                err = -ENOTSUP;
                break;
            }
            report->subscription = (struct bt_gatt_subscribe_params){
                .notify = notify_cb, .subscribe = subscribed_cb,
                .value_handle = report->handle, .ccc_handle = report->ccc,
                .value = BT_GATT_CCC_NOTIFY, .min_security = BT_SECURITY_L2,
            };
            atomic_set_bit(report->subscription.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);
            err = bt_gatt_subscribe(mouse_conn, &report->subscription);
            break;
        }
        if (session.index == session.count) {
            finish_ready();
        }
        break;
    default: return;
    }
    if (err) {
        fail(err);
    }
}

static void connected_work_cb(struct k_work *work) {
    struct bt_conn *event_conn = connected_event_conn;
    connected_event_conn = NULL;
    if (event_conn) bt_conn_unref(event_conn);
    if (!mouse_conn) {
        return;
    }
    bt_le_set_auto_conn(bt_conn_get_dst(mouse_conn), NULL);
    zmk_ble_central_connection_complete();
    if (connected_error) {
        bt_conn_unref(mouse_conn);
        mouse_conn = NULL;
        atomic_clear(&failure);
        atomic_set(&stage, IDLE);
        k_work_cancel_delayable(&timeout_work);
        if (forget_requested) {
            forget_peer();
        }
        request_retry();
        return;
    }
    /* A cancel/timeout may run while the controller is completing the link. */
    if (atomic_get(&failure) || atomic_get(&stage) == CLOSING) {
        fail(-ECANCELED);
        return;
    }
    atomic_set(&stage, SECURITY);
    int err = bt_conn_set_security(mouse_conn, BT_SECURITY_L2);
    if (err) {
        fail(err);
    } else if (bt_conn_get_security(mouse_conn) >= BT_SECURITY_L2 &&
               atomic_cas(&stage, SECURITY, SERVICE)) {
        zmk_ble_mouse_submit(&discovery_work);
    }
}
K_WORK_DEFINE(connected_work, connected_work_cb);

static void connected_cb(struct bt_conn *conn, uint8_t err) {
    if (owns(conn) || (atomic_get(&stage) == CONNECTING &&
                      !bt_addr_le_cmp(bt_conn_get_dst(conn), &connecting_address))) {
        /* Keep early completion alive until auto-connect returns and the
         * client worker can take its connection reference. */
        connected_event_conn = bt_conn_ref(conn);
        connected_error = err;
        zmk_ble_mouse_submit(&connected_work);
    }
}

static void disconnected_work_cb(struct k_work *work) {
    if (!mouse_conn) {
        return;
    }
    bt_le_set_auto_conn(bt_conn_get_dst(mouse_conn), NULL);
    /* Only a bond made during this explicitly requested, unsuccessful attempt
     * is removed. Existing PC, split and saved mouse bonds are preserved. */
    if (new_candidate && candidate_bonded) {
        bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(mouse_conn));
    }
    bt_conn_unref(mouse_conn);
    mouse_conn = NULL;
    candidate_bonded = false;
    atomic_clear(&failure);
    atomic_set(&stage, IDLE);
    k_work_cancel_delayable(&timeout_work);
    zmk_mouse_output_disconnect();
    if (forget_requested) {
        forget_peer();
    }
    request_retry();
}
K_WORK_DEFINE(disconnected_work, disconnected_work_cb);

static void disconnected_cb(struct bt_conn *conn, uint8_t reason) {
    if (owns(conn)) {
        atomic_set(&stage, CLOSING);
        atomic_clear(&scan_wanted);
        zmk_ble_mouse_submit(&disconnected_work);
    }
}

static void security_cb(struct bt_conn *conn, bt_security_t level, enum bt_security_err err) {
    if (!valid_callback(conn)) {
        return;
    }
    if (err) {
        fail(-EACCES);
    } else if (level >= BT_SECURITY_L2 && atomic_cas(&stage, SECURITY, SERVICE)) {
        zmk_ble_mouse_submit(&discovery_work);
    }
}

static void pairing_complete_cb(struct bt_conn *conn, bool bonded) {
    if (owns(conn)) {
        candidate_bonded = bonded;
        if (!bonded) {
            fail(-ENOSPC);
        }
    }
}

static void pairing_failed_cb(struct bt_conn *conn, enum bt_security_err reason) {
    if (owns(conn)) {
        fail(-EACCES);
    }
}

static struct bt_conn_cb connection_callbacks = {
    .connected = connected_cb, .disconnected = disconnected_cb, .security_changed = security_cb,
};
static struct bt_conn_auth_info_cb auth_callbacks = {
    .pairing_complete = pairing_complete_cb, .pairing_failed = pairing_failed_cb,
};

static int settings_set(const char *name, size_t length, settings_read_cb read, void *arg) {
    if (strcmp(name, "peer")) {
        return -ENOENT;
    }
    if (length != sizeof(peer)) {
        return -EINVAL;
    }
    int err = read(arg, &peer, sizeof(peer));
    if (err == sizeof(peer) && peer.type <= BT_ADDR_LE_RANDOM &&
        bt_addr_le_cmp(&peer, BT_ADDR_LE_ANY)) {
        atomic_set(&have_peer, 1);
        return 0;
    }
    return -EINVAL;
}
static int settings_commit(void) {
    zmk_ble_mouse_start();
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(ble_mouse, "ble_mouse", NULL, settings_set, settings_commit, NULL);

static void pair_work_cb(struct k_work *work) {
    if (mouse_conn) {
        LOG_INF("Mouse is already connected/connecting");
        return;
    }
    if (atomic_get(&have_peer)) {
        retry_ms = 1000;
        retry_work_cb(NULL);
        return;
    }
    if (bond_count() >= CONFIG_BT_MAX_PAIRED) {
        LOG_WRN("Mouse pairing refused: no free bond slot; existing bonds preserved");
        return;
    }
    atomic_set(&pairing, 1);
    retry_ms = 1000;
    zmk_ble_mouse_schedule(&pair_timeout, PAIR_TIME_MS);
    retry_work_cb(NULL);
    LOG_INF("Pairing M720 for 60 seconds");
}
K_WORK_DEFINE(pair_work, pair_work_cb);

int zmk_ble_mouse_pair(void) {
    if (!atomic_get(&started)) return -ENOTSUP;
    zmk_ble_mouse_submit(&pair_work);
    return 0;
}

static void forget_peer(void) {
    atomic_clear(&pairing);
    atomic_clear(&scan_wanted);
    k_work_cancel_delayable(&pair_timeout);
    if (atomic_get(&have_peer)) {
        int err = bt_unpair(BT_ID_DEFAULT, &peer);
        if (err) {
            LOG_WRN("Mouse unpair failed: %d", err);
            forget_requested = false;
            return;
        }
        err = settings_delete("ble_mouse/peer");
        if (err) {
            LOG_WRN("Mouse peer settings delete failed: %d", err);
        }
        atomic_clear(&have_peer);
    }
    forget_requested = false;
    zmk_mouse_output_disconnect();
    zmk_ble_central_scan_refresh();
    LOG_INF("Mouse registration cleared");
}

static void clear_work_cb(struct k_work *work) {
    if (!atomic_get(&clear_held)) {
        return;
    }
    atomic_clear(&pairing);
    atomic_clear(&scan_wanted);
    forget_requested = true;
    if (mouse_conn) {
        fail(-ECANCELED);
    } else {
        forget_peer();
    }
}
K_WORK_DELAYABLE_DEFINE(clear_work, clear_work_cb);

void zmk_ble_mouse_clear_hold(bool pressed) {
    atomic_set(&clear_held, pressed);
    if (pressed) {
        zmk_ble_mouse_schedule(&clear_work, 2000);
    } else {
        k_work_cancel_delayable(&clear_work);
    }
}

void zmk_ble_mouse_start(void) {
    if (!atomic_cas(&started, 0, 1)) return;
    bt_conn_cb_register(&connection_callbacks);
    bt_conn_auth_info_cb_register(&auth_callbacks);
    zmk_ble_central_scan_init();
    zmk_ble_mouse_schedule(&retry_work, 0);
}
