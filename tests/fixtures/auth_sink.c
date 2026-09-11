#include <test_runtime.h>
const struct bt_conn_auth_cb *sink_auth;
int sink_registration_error;
int bt_conn_auth_cb_register(const struct bt_conn_auth_cb *cb) {
    if (!cb) { sink_auth = NULL; return 0; }
    if (sink_registration_error) {
        int err = sink_registration_error; sink_registration_error = 0; return err;
    }
    if (sink_auth) return -EALREADY;
    sink_auth = cb;
    return 0;
}
int bt_conn_auth_cb_overlay(struct bt_conn *conn, const struct bt_conn_auth_cb *cb) {
    (void)conn; (void)cb;
    assert(!"Overlay must not bypass the guard");
    return 0;
}
