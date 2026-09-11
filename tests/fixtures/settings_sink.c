#include <test_runtime.h>
unsigned sink_read_calls, sink_save_calls;
size_t sink_len;
uint8_t sink_bytes[256];
int sink_result;
int settings_name_steq(const char *name, const char *key, const char **next) {
    size_t len = strlen(key);
    if (strncmp(name, key, len) || (name[len] && name[len] != '/')) return 0;
    if (next) *next = name + len + (name[len] == '/');
    return 1;
}
int settings_call_set_handler(const char *name, size_t len, settings_read_cb cb, void *arg,
                              const struct settings_load_arg *load) {
    sink_read_calls++;
    if (load && load->subtree && !settings_name_steq(name, load->subtree, NULL)) return 0;
    if (load && load->cb) return load->cb(name, len, cb, arg, load->param);
    sink_len = len;
    assert(len <= sizeof(sink_bytes));
    if (len) assert(cb(arg, sink_bytes, len) == (ssize_t)len);
    return sink_result;
}
int settings_save_one(const char *name, const void *value, size_t len) {
    (void)name; sink_save_calls++; sink_len = len;
    assert(len <= sizeof(sink_bytes));
    if (len) memcpy(sink_bytes, value, len);
    return sink_result;
}
