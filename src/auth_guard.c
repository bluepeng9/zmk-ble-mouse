/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zmk/ble_mouse.h>

LOG_MODULE_REGISTER(ble_mouse_auth, CONFIG_ZMK_BLE_MOUSE_LOG_LEVEL);

/* Bluetooth Core, Vol 3, Part H, AuthReq bit 3. The SMP init wrapper also
 * preserves the requirement that the local controller supports SC. */
#define AUTH_SECURE_CONNECTIONS BIT(3)

int __real_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb);

static struct bt_conn_auth_cb guarded_auth;
static enum bt_security_err (*application_accept)(struct bt_conn *,
                                                const struct bt_conn_pairing_feat *);
static bool application_registered;

static enum bt_security_err sc_only_accept(struct bt_conn *conn,
                                          const struct bt_conn_pairing_feat *feat) {
    ARG_UNUSED(conn);
    return feat && (feat->auth_req & AUTH_SECURE_CONNECTIONS)
               ? BT_SECURITY_ERR_SUCCESS : BT_SECURITY_ERR_AUTH_REQUIREMENT;
}

static enum bt_security_err guarded_accept(struct bt_conn *conn,
                                          const struct bt_conn_pairing_feat *feat) {
    if (!conn || !feat) return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
    if (!(feat->auth_req & AUTH_SECURE_CONNECTIONS) &&
        !zmk_ble_mouse_legacy_pairing_allowed(conn)) {
        LOG_WRN("Legacy pairing refused outside the selected mouse Pair attempt");
        return BT_SECURITY_ERR_AUTH_REQUIREMENT;
    }
    /* Never bypass ZMK's occupied-profile checks or application rejection. */
    return application_accept ? application_accept(conn, feat) : BT_SECURITY_ERR_SUCCESS;
}

/* Install before Bluetooth can receive traffic. This immutable fallback also
 * protects early connections that latch auth before ZMK registers callbacks. */
static const struct bt_conn_auth_cb initial_auth = {.pairing_accept = sc_only_accept};
static int auth_guard_init(void) {
    return application_registered ? 0 : __real_bt_conn_auth_cb_register(&initial_auth);
}
SYS_INIT(auth_guard_init, PRE_KERNEL_1, 0);

int __wrap_bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb) {
    /* ZMK 0.3 registers one static callback table. Keep the installed table
     * immutable because SMP latches it for the lifetime of a connection.
     * Removal or replacement must not turn the policy off mid-connection. */
    if (!cb) return -ENOTSUP;
    if (application_registered) return -EALREADY;
    if (!cb->cancel && (cb->passkey_display || cb->passkey_entry ||
                       cb->passkey_confirm || cb->pairing_confirm)) return -EINVAL;

    guarded_auth = *cb;
    application_accept = cb->pairing_accept;
    guarded_auth.pairing_accept = guarded_accept;
    int err = __real_bt_conn_auth_cb_register(NULL);
    if (err) return err;
    err = __real_bt_conn_auth_cb_register(&guarded_auth);
    if (err) {
        __real_bt_conn_auth_cb_register(&initial_auth);
        return err;
    }
    application_registered = true;
    return 0;
}

int __wrap_bt_conn_auth_cb_overlay(struct bt_conn *conn, const struct bt_conn_auth_cb *cb) {
    ARG_UNUSED(cb);
    /* A per-connection overlay (including NULL) bypasses the global guard.
     * The supported ZMK application does not use overlays. Fail closed. */
    return conn ? -ENOTSUP : -EINVAL;
}
