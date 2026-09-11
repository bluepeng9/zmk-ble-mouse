/* Drive the production BLE client using deterministic GATT/controller callbacks.
 * This verifies lifecycle behavior, not a physical M720 or the Zephyr radio. */
#include <stdio.h>
#include <test_runtime.h>
#include "../src/ble_mouse.c"
#include "fixtures/hid_reports.h"

static unsigned received, released;
static struct mouse_hid_frame last_frame;
static uint8_t last_buttons;

void zmk_mouse_output_receive(const struct mouse_hid_frame *frame, uint8_t physical) {
    received++;
    last_frame = *frame;
    last_buttons = physical;
}
void zmk_mouse_output_disconnect(void) { released++; }

static void reset_client(unsigned existing_bonds) {
    fake_runtime_reset();
    memset(&session, 0, sizeof(session));
    memset(advertisers, 0, sizeof(advertisers));
    mouse_conn = NULL;
    connected_event_conn = NULL;
    peer = fake_any;
    have_peer = pairing = scan_wanted = stage = failure = clear_held = started = 0;
    forget_requested = candidate_bonded = new_candidate = false;
    connected_error = 0;
    retry_ms = 1000;
    received = released = 0;
    fake_bond_count = existing_bonds;
    for (unsigned i = 0; i < existing_bonds; i++) fake_bonds[i].addr = fake_address(i + 1);
    assert(ble_mouse_settings_commit() == 0);
    fake_drain();
}

static void pair_request(void) {
    assert(zmk_ble_mouse_pair() == 0);
    fake_drain();
}

static bool advertise(uint8_t id, uint8_t type, const char *name) {
    uint8_t bytes[64] = {0};
    size_t length = name ? strlen(name) : 0;
    if (length) {
        assert(length + 2 <= sizeof(bytes));
        bytes[0] = length + 1;
        bytes[1] = BT_DATA_NAME_COMPLETE;
        memcpy(bytes + 2, name, length);
    }
    struct net_buf_simple ad = {.data = bytes, .len = length ? length + 2 : 0};
    bt_addr_le_t addr = fake_address(id);
    return zmk_ble_mouse_advertisement(&addr, type, &ad);
}

static void link_connected(void) {
    fake_scan_pending = false;
    fake_connection.state = BT_CONN_STATE_CONNECTED;
    fake_connection_callbacks->connected(&fake_connection, 0);
    fake_drain();
    assert(stage == SECURITY);
}

static void encrypted(void) {
    if (new_candidate) {
        assert(fake_bond_count < ARRAY_SIZE(fake_bonds));
        fake_bonds[fake_bond_count++].addr = fake_connection.addr;
        fake_auth_callbacks->pairing_complete(&fake_connection, true);
    }
    fake_connection.security = BT_SECURITY_L2;
    fake_connection_callbacks->security_changed(&fake_connection, BT_SECURITY_L2,
                                                  BT_SECURITY_ERR_SUCCESS);
    fake_drain();
    assert(stage == SERVICE && fake_discovery->uuid == BT_UUID_HIDS);
}

static void characteristic(uint16_t declaration, uint16_t value,
                           const struct bt_uuid *uuid, uint8_t properties) {
    struct bt_gatt_chrc ch = {.uuid = uuid, .value_handle = value, .properties = properties};
    struct bt_gatt_attr attr = {.handle = declaration, .user_data = &ch};
    assert(fake_discovery->func(&fake_connection, &attr, fake_discovery) == BT_GATT_ITER_CONTINUE);
}

static void descriptor_attr(uint16_t handle, const struct bt_uuid *uuid) {
    struct bt_gatt_attr attr = {.handle = handle, .uuid = uuid};
    assert(fake_discovery->func(&fake_connection, &attr, fake_discovery) == BT_GATT_ITER_CONTINUE);
}

static void discover_reports(bool include_ccc) {
    struct bt_gatt_service_val service = {.end_handle = 50};
    struct bt_gatt_attr attr = {.handle = 1, .user_data = &service};
    assert(fake_discovery->func(&fake_connection, &attr, fake_discovery) == BT_GATT_ITER_STOP);
    fake_drain();
    assert(stage == CHARACTERISTICS);
    characteristic(2, 3, BT_UUID_HIDS_REPORT_MAP, 0);
    characteristic(4, 5, BT_UUID_HIDS_REPORT, 0);
    characteristic(8, 9, BT_UUID_HIDS_REPORT, 0);
    characteristic(12, 13, BT_UUID_HIDS_PROTOCOL_MODE, BT_GATT_CHRC_WRITE_WITHOUT_RESP);
    characteristic(14, 15, BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP);
    fake_discovery->func(&fake_connection, NULL, fake_discovery);
    fake_drain();
    assert(stage == DESCRIPTORS);
    if (include_ccc) descriptor_attr(6, BT_UUID_GATT_CCC);
    descriptor_attr(7, BT_UUID_HIDS_REPORT_REF);
    descriptor_attr(10, BT_UUID_GATT_CCC);
    descriptor_attr(11, BT_UUID_HIDS_REPORT_REF);
    fake_discovery->func(&fake_connection, NULL, fake_discovery);
    fake_drain();
    assert(stage == REPORT_MAP && fake_read->single.handle == 3);
}

static void read_value(const uint8_t *data, size_t length) {
    while (length) {
        size_t part = MIN(length, 20);
        assert(fake_read->func(&fake_connection, 0, fake_read, data, part) == BT_GATT_ITER_CONTINUE);
        data += part;
        length -= part;
    }
    assert(fake_read->func(&fake_connection, 0, fake_read, NULL, 0) == BT_GATT_ITER_STOP);
    fake_drain();
}

static void map_and_references(uint8_t first_id) {
    read_value(descriptor, sizeof(descriptor));
    assert(stage == REPORT_REFERENCES && fake_read->single.handle == 7);
    const uint8_t input[] = {first_id, 1}, feature[] = {0x11, 3};
    read_value(input, sizeof(input));
    assert(fake_read->single.handle == 11);
    read_value(feature, sizeof(feature));
}

static void new_ready_mouse(void) {
    reset_client(2);
    pair_request();
    assert(advertise(9, BT_GAP_ADV_TYPE_ADV_IND, "M720 Triathlon"));
    link_connected();
    encrypted();
    discover_reports(true);
    map_and_references(7);
    assert(stage == SUBSCRIPTIONS && fake_subscription->value_handle == 5);
    fake_subscription->subscribe(&fake_connection, 0, fake_subscription);
    fake_drain();
    assert(stage == READY && have_peer && !pairing);
    assert(fake_save_count == 1 && fake_bond_count == 3 && !fake_unpair_count);
}

static void disconnect_link(void) {
    fake_connection.state = BT_CONN_STATE_DISCONNECTED;
    fake_connection_callbacks->disconnected(&fake_connection, 0x13);
    fake_drain();
    assert(mouse_conn == NULL && fake_connection.references == 0 && stage == IDLE);
}

static void test_capacity_and_discovery_window(void) {
    reset_client(6);
    pair_request();
    assert(!pairing && !scan_wanted);
    advertise(9, BT_GAP_ADV_TYPE_ADV_IND, "M720 Triathlon");
    assert(!fake_create_count && !fake_unpair_count && fake_bond_count == 6);
    reset_client(2);
    pair_request();
    assert(pairing && scan_wanted);
    advertise(1, BT_GAP_ADV_TYPE_ADV_IND, "M720 Triathlon");
    assert(!fake_create_count && !fake_unpair_count); /* Existing bonds cannot be adopted. */
    assert(!advertise(8, BT_GAP_ADV_TYPE_ADV_IND, "Different mouse"));
    assert(!advertise(9, BT_GAP_ADV_TYPE_SCAN_RSP, "M720 Triathlon"));
    fake_advance(2100); /* A stale scan response must not qualify a new advertisement. */
    assert(!advertise(9, BT_GAP_ADV_TYPE_ADV_IND, NULL));
    assert(advertise(9, BT_GAP_ADV_TYPE_SCAN_RSP, "M720 Triathlon"));
    assert(fake_create_count == 1 && stage == CONNECTING && !scan_wanted);
    reset_client(2);
    pair_request();
    fake_advance(60000);
    assert(!pairing && !scan_wanted && !fake_create_count);
    reset_client(5);
    pair_request();
    fake_bonds[fake_bond_count++].addr = fake_address(6); /* Another device took the last slot. */
    advertise(9, BT_GAP_ADV_TYPE_ADV_IND, "M720 Triathlon");
    assert(!fake_create_count && !fake_unpair_count && !pairing);
}

static void test_reports_reconnect_and_clear(void) {
    new_ready_mouse();
    uint8_t report[] = {0x19, 0x00, 0xf8, 0x7f, 0xff, 0x02};
    fake_subscription->notify(&fake_connection, fake_subscription, report, sizeof(report));
    assert(received == 1 && last_buttons == 0x19);
    assert(last_frame.x == -2048 && last_frame.y == 2047);
    assert(last_frame.wheel == -1 && last_frame.pan == 2);
    fake_subscription->notify(&fake_connection, fake_subscription, report, sizeof(report) - 1);
    struct bt_conn other = {.addr = fake_address(1)};
    fake_subscription->notify(&other, fake_subscription, report, sizeof(report));
    fake_connection_callbacks->disconnected(&other, 0x13);
    fake_drain();
    assert(received == 1 && stage == READY);
    disconnect_link();
    assert(released == 1 && have_peer && fake_bond_count == 3 && !fake_unpair_count);
    fake_advance(1000);
    assert(stage == CONNECTING && !scan_wanted && !pairing && fake_auto_connect);
    assert(!advertise(8, BT_GAP_ADV_TYPE_ADV_IND, "M720 Triathlon"));
    assert(!advertise(9, BT_GAP_ADV_TYPE_ADV_DIRECT_IND, NULL));
    link_connected();
    encrypted();
    discover_reports(true);
    map_and_references(7);
    fake_subscription->subscribe(&fake_connection, 0, fake_subscription);
    fake_drain();
    assert(stage == READY && fake_save_count == 1 && fake_bond_count == 3);
    zmk_ble_mouse_clear_hold(true);
    fake_advance(1999);
    zmk_ble_mouse_clear_hold(false);
    fake_advance(1);
    assert(!fake_disconnect_count && !fake_unpair_count);
    zmk_ble_mouse_clear_hold(true);
    fake_advance(2000);
    assert(fake_disconnect_count == 1 && stage == CLOSING && !fake_unpair_count);
    disconnect_link();
    zmk_ble_mouse_clear_hold(false);
    assert(!have_peer && !pairing && !scan_wanted && fake_delete_count == 1);
    assert(fake_unpair_count == 1 && fake_bond_count == 2);
    assert(fake_bonds[0].addr.a.val[0] == 1 && fake_bonds[1].addr.a.val[0] == 2);
}

static void begin_discovery(void) {
    reset_client(2);
    pair_request();
    advertise(9, BT_GAP_ADV_TYPE_ADV_IND, "M720 Triathlon");
    link_connected();
    encrypted();
}

static void assert_failed_candidate(void) {
    assert(stage == CLOSING && fake_disconnect_count == 1 && !fake_save_count);
    disconnect_link();
    assert(fake_unpair_count == 1 && fake_bond_count == 2 && !have_peer);
}

static void test_setup_failures(void) {
    begin_discovery();
    fake_discovery->func(&fake_connection, NULL, fake_discovery); /* No HID service. */
    fake_drain();
    assert_failed_candidate();
    begin_discovery();
    discover_reports(true);
    const uint8_t invalid[] = {0x05};
    read_value(invalid, sizeof(invalid));
    assert_failed_candidate();
    begin_discovery();
    discover_reports(false);
    map_and_references(7); /* Useful input without a CCC cannot be subscribed. */
    assert_failed_candidate();
    begin_discovery();
    discover_reports(true);
    map_and_references(0x2a); /* A descriptor alone cannot promise missing movement reports. */
    fake_subscription->subscribe(&fake_connection, 0, fake_subscription);
    fake_drain();
    assert_failed_candidate();
    begin_discovery();
    fake_advance(20000);
    assert_failed_candidate();
}

static ssize_t read_peer(void *arg, void *data, size_t length) {
    memcpy(data, arg, length);
    return length;
}

static void test_cancel_race_and_saved_settings(void) {
    reset_client(2);
    bt_addr_le_t saved = fake_address(9);
    assert(settings_set("peer", sizeof(saved) - 1, read_peer, &saved) == -EINVAL);
    assert(settings_set("peer", sizeof(saved), read_peer, &saved) == 0);
    pair_request();
    assert(!scan_wanted); /* Missing bond is not silently recreated. */
    fake_bonds[fake_bond_count++].addr = saved;
    pair_request();
    assert(stage == CONNECTING && fake_auto_connect);
    fake_scan_pending = false; /* Controller is already initiating the link. */
    zmk_ble_mouse_clear_hold(true);
    fake_advance(2000);
    assert(stage == CLOSING);
    fake_connection_callbacks->connected(&fake_connection, 0);
    fake_drain();
    assert(stage == CLOSING && fake_discovery == NULL);
    disconnect_link();
    assert(!have_peer && fake_unpair_count == 1 && fake_bond_count == 2);
    reset_client(2);
    pair_request();
    advertise(9, BT_GAP_ADV_TYPE_ADV_IND, "M720 Triathlon");
    fake_scan_pending = false;
    zmk_ble_mouse_clear_hold(true);
    fake_advance(2000);
    fake_connection_callbacks->connected(&fake_connection, 0x3e);
    fake_drain();
    assert(!mouse_conn && !failure && !pairing && !scan_wanted && !forget_requested);
    assert(!fake_unpair_count && fake_bond_count == 2);

    reset_client(2);
    pair_request();
    advertise(9, BT_GAP_ADV_TYPE_ADV_IND, "M720 Triathlon");
    assert(fake_scan_pending);
    zmk_ble_mouse_clear_hold(true);
    fake_advance(2000); /* Cancels passive auto-connect with no BT callback. */
    assert(!mouse_conn && stage == IDLE && !failure && !fake_auto_connect);
    assert(!fake_connection.references && !forget_requested && !pairing);
}

static void test_legacy_pairing_scope(void) {
    reset_client(2);
    assert(!zmk_ble_mouse_legacy_pairing_allowed(NULL));
    assert(!zmk_ble_mouse_legacy_pairing_allowed(&fake_connection));
    pair_request();
    assert(!zmk_ble_mouse_legacy_pairing_allowed(&fake_connection));
    advertise(9, BT_GAP_ADV_TYPE_ADV_IND, "M720 Triathlon");
    assert(zmk_ble_mouse_legacy_pairing_allowed(&fake_connection));
    struct bt_conn other = {.addr = fake_address(8)};
    assert(!zmk_ble_mouse_legacy_pairing_allowed(&other));
    fake_connection.role = BT_CONN_ROLE_PERIPHERAL;
    assert(!zmk_ble_mouse_legacy_pairing_allowed(&fake_connection));
    fake_connection.role = BT_CONN_ROLE_CENTRAL;
    fake_connection.id = 1;
    assert(!zmk_ble_mouse_legacy_pairing_allowed(&fake_connection));
    fake_connection.id = 0;
    link_connected();
    assert(zmk_ble_mouse_legacy_pairing_allowed(&fake_connection));
    encrypted();
    assert(!zmk_ble_mouse_legacy_pairing_allowed(&fake_connection));

    reset_client(2);
    pair_request();
    advertise(9, BT_GAP_ADV_TYPE_ADV_IND, "M720 Triathlon");
    /* The early auto-connect callback can arrive before lookup returns. */
    struct bt_conn *held = mouse_conn;
    mouse_conn = NULL;
    assert(zmk_ble_mouse_legacy_pairing_allowed(&fake_connection));
    assert(!zmk_ble_mouse_legacy_pairing_allowed(&other));
    mouse_conn = held;
    atomic_set(&failure, -ECANCELED);
    assert(!zmk_ble_mouse_legacy_pairing_allowed(&fake_connection));
    atomic_clear(&failure);
    atomic_clear(&pairing);
    assert(!zmk_ble_mouse_legacy_pairing_allowed(&fake_connection));
    atomic_set(&pairing, 1);
    atomic_set(&have_peer, 1);
    assert(!zmk_ble_mouse_legacy_pairing_allowed(&fake_connection));
}

int main(void) {
    test_capacity_and_discovery_window();
    test_reports_reconnect_and_clear();
    test_setup_failures();
    test_cancel_race_and_saved_settings();
    test_legacy_pairing_scope();
    puts("BLE client lifecycle: passed (mocked controller/GATT, production client)");
    return 0;
}
