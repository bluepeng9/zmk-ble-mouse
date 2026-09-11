/* Public scanner API sequencing; no real controller or concurrent threads. */
#include <stdio.h>
#include <test_runtime.h>
#include "../src/central_scan.c"

static bool wanted;
static unsigned advertisements, attempts;
bool zmk_ble_mouse_wants_scan(void) { return wanted; }
bool zmk_ble_mouse_advertisement(const bt_addr_le_t *addr, uint8_t type, struct net_buf_simple *ad) {
    advertisements++;
    return true;
}
static int attempt(const bt_addr_le_t *addr) {
    assert(!fake_scan_active && addr->a.val[0] == 9);
    attempts++;
    wanted = false;
    return 0;
}
static void reset_radio(void) {
    fake_runtime_reset();
    initialized = connecting = scan_owned = split_settling = 0;
    wanted = true;
    advertisements = attempts = 0;
    zmk_ble_central_scan_init();
    fake_transport_status.available = fake_transport_status.enabled = true;
}
static void all_connected(void) {
    fake_transport_status.connections = ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_ALL_CONNECTED;
}
static void test_split_priority_and_scan_ownership(void) {
    reset_radio();
    zmk_ble_central_scan_refresh();
    fake_drain();
    assert(!fake_scan_start_count); /* Wait for both keyboard halves. */
    all_connected();
    fake_scan_active = true; /* Another owner still scanning. */
    fake_advance(1000);
    assert(!scan_owned && !fake_scan_stop_count);
    fake_scan_active = false;
    fake_advance(1000);
    assert(scan_owned && fake_scan_active);
    bt_addr_le_t addr = fake_address(9);
    struct net_buf_simple ad = {0};
    fake_scan_callback(&addr, -30, BT_GAP_ADV_TYPE_ADV_IND, &ad);
    assert(advertisements == 1);
    fake_connection_callbacks->disconnected(&fake_connection, 0x13);
    assert(!scan_owned && !fake_scan_active && split_settling);
    fake_transport_status.connections = ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_SOME_CONNECTED;
    fake_scan_active = true; /* Original split callback starts its scan. */
    fake_advance(1000);
    assert(fake_scan_active && fake_scan_stop_count == 1);
    wanted = false;
    zmk_ble_central_scan_refresh();
    fake_drain();
    assert(fake_scan_active && fake_scan_stop_count == 1);
}
static void test_deferred_connection_and_stop(void) {
    reset_radio();
    all_connected();
    zmk_ble_central_scan_refresh();
    fake_drain();
    assert(scan_owned);
    bt_addr_le_t addr = fake_address(9);
    assert(zmk_ble_central_connect(&addr, attempt) == 0);
    assert(zmk_ble_central_connect(&addr, attempt) == -EBUSY);
    assert(!attempts && fake_scan_active);
    fake_drain();
    assert(attempts == 1 && !fake_scan_active && !connecting);
    zmk_ble_central_connection_complete();
    fake_drain();
    assert(fake_scan_stop_count == 1);
}
int main(void) {
    test_split_priority_and_scan_ownership();
    test_deferred_connection_and_stop();
    puts("Mouse radio sequencing: passed (public API fake, production coordinator)");
    return 0;
}
