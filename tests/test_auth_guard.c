#include <stdio.h>
#include <test_runtime.h>
extern const struct bt_conn_auth_cb *sink_auth;
extern int sink_registration_error;
int fake_sys_init(void);
static struct bt_conn selected;
static bool pair_window;
static unsigned accept_calls, cancel_calls;
static enum bt_security_err application_result;
bool zmk_ble_mouse_legacy_pairing_allowed(struct bt_conn *conn) {
    return pair_window && conn == &selected;
}
static enum bt_security_err accept(struct bt_conn *conn, const struct bt_conn_pairing_feat *feat) {
    (void)conn; (void)feat; accept_calls++; return application_result;
}
static void cancel(struct bt_conn *conn) { (void)conn; cancel_calls++; }
static void entry(struct bt_conn *conn) { (void)conn; }
int main(void) {
    struct bt_conn pc = {0};
    struct bt_conn_pairing_feat legacy = {.auth_req = 1}, sc = {.auth_req = 9};
    assert(fake_sys_init() == 0);
    const struct bt_conn_auth_cb *early = sink_auth;
    assert(early->pairing_accept(&selected, &legacy) == BT_SECURITY_ERR_AUTH_REQUIREMENT);
    assert(early->pairing_accept(&pc, &sc) == BT_SECURITY_ERR_SUCCESS);
    struct bt_conn_auth_cb app = {.pairing_accept = accept, .passkey_entry = entry};
    assert(bt_conn_auth_cb_register(&app) == -EINVAL);
    assert(sink_auth == early);
    app.cancel = cancel;
    sink_registration_error = -EIO;
    assert(bt_conn_auth_cb_register(&app) == -EIO);
    assert(sink_auth == early);
    assert(bt_conn_auth_cb_register(&app) == 0);
    const struct bt_conn_auth_cb *installed = sink_auth;
    assert(installed->passkey_entry == entry && installed->cancel == cancel);
    assert(bt_conn_auth_cb_register(&app) == -EALREADY && sink_auth == installed);
    assert(bt_conn_auth_cb_register(NULL) == -ENOTSUP && sink_auth == installed);
    assert(bt_conn_auth_cb_overlay(&pc, NULL) == -ENOTSUP);
    assert(bt_conn_auth_cb_overlay(&pc, &app) == -ENOTSUP);
    assert(bt_conn_auth_cb_overlay(NULL, &app) == -EINVAL);
    assert(installed->pairing_accept(&selected, &legacy) == BT_SECURITY_ERR_AUTH_REQUIREMENT);
    pair_window = true;
    assert(installed->pairing_accept(&selected, &legacy) == BT_SECURITY_ERR_SUCCESS);
    assert(accept_calls == 1);
    assert(installed->pairing_accept(&pc, &legacy) == BT_SECURITY_ERR_AUTH_REQUIREMENT);
    assert(accept_calls == 1);
    assert(installed->pairing_accept(&pc, &sc) == BT_SECURITY_ERR_SUCCESS);
    application_result = BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
    assert(installed->pairing_accept(&selected, &legacy) == application_result);
    assert(installed->pairing_accept(&pc, &sc) == application_result);
    installed->cancel(&pc);
    assert(cancel_calls == 1);
    /* A connection which latched the boot table retains its SC-only policy. */
    assert(early->pairing_accept(&selected, &legacy) == BT_SECURITY_ERR_AUTH_REQUIREMENT);
    puts("Auth guard: mouse scope, application rejection, callbacks, early policy and bypass refusal passed");
    return 0;
}
