#include <stdio.h>
#include <test_runtime.h>
#include <keys.h>
extern unsigned sink_read_calls, sink_save_calls;
extern size_t sink_len;
extern uint8_t sink_bytes[256];
extern int sink_result;
static const char *bond = "bt/keys/1122334455661";
static unsigned backend_reads;
static ssize_t read_result;
static uint8_t stored[256];
static ssize_t read_record(void *arg, void *data, size_t len) {
    (void)arg; backend_reads++;
    if (read_result < 0) return read_result;
    size_t n = MIN(len, (size_t)read_result);
    memcpy(data, stored, n); return n;
}
static int raw_cb(const char *key, size_t len, settings_read_cb cb, void *arg, void *param) {
    (void)key; (void)param;
    assert(len == (size_t)read_result);
    assert(cb(arg, sink_bytes, len) == read_result);
    assert(!memcmp(sink_bytes, stored, len));
    return 17;
}
int main(void) {
    const size_t full = BT_KEYS_STORAGE_LEN;
    /* 52 serialized bytes: metadata (4), LTK (26), IRK/address (22),
     * padded to the target's pointer alignment in the old SC-only image. */
#if defined(CONFIG_BT_SIGNING)
    const size_t fields = 52 + 2 * 20;
#else
    const size_t fields = 52;
#endif
    const size_t old = ROUND_UP(fields, sizeof(void *));
    for (size_t i = 0; i < old; ++i) stored[i] = (uint8_t)(i + 37);
    stored[0] = 16; stored[1] = BT_KEYS_SC;
    uint16_t types = BT_KEYS_LTK_P256 | BT_KEYS_IRK;
    memcpy(stored + 2, &types, 2);
    read_result = old;
    assert(settings_call_set_handler(bond, old, read_record, NULL, NULL) == 0);
    assert(sink_len == full && backend_reads == 1 && !sink_save_calls);
    assert(!memcmp(sink_bytes, stored, fields));
    for (size_t i = fields; i < full; i++) assert(sink_bytes[i] == 0);
    uint8_t expanded[256]; memcpy(expanded, sink_bytes, full);
    assert(settings_save_one(bond, expanded, full) == 0);
    assert(sink_len == old && !memcmp(sink_bytes, stored, fields));
    for (size_t i = fields; i < old; i++) assert(sink_bytes[i] == 0);
    /* The Legacy central mouse's LTK must remain in the expanded format. */
    expanded[1] = 0; types = BT_KEYS_LTK | BT_KEYS_IRK;
    memcpy(expanded + 2, &types, 2);
    expanded[full - 1] = 123;
    assert(settings_save_one(bond, expanded, full) == 0);
    assert(sink_len == full && !memcmp(sink_bytes, expanded, full));
    memcpy(stored, expanded, full); read_result = full;
    assert(settings_call_set_handler(bond, full, read_record, NULL, NULL) == 0);
    assert(sink_len == full && !memcmp(sink_bytes, stored, full));
    unsigned calls = sink_read_calls, saves = sink_save_calls;
    assert(settings_call_set_handler(bond, old - 1, read_record, NULL, NULL) == -EINVAL);
    assert(sink_read_calls == calls && sink_save_calls == saves);
    read_result = -EIO;
    assert(settings_call_set_handler(bond, old, read_record, NULL, NULL) == -EIO);
    read_result = old - 1;
    assert(settings_call_set_handler(bond, old, read_record, NULL, NULL) == -EINVAL);
    read_result = old; /* Short records must not be interpreted as Legacy keys. */
    assert(settings_call_set_handler(bond, old, read_record, NULL, NULL) == -EINVAL);
    assert(sink_read_calls == calls && sink_save_calls == saves);
    struct settings_load_arg direct = {.cb = raw_cb};
    assert(settings_call_set_handler(bond, old, read_record, NULL, &direct) == 17);
    struct settings_load_arg excluded = {.subtree = "ble_mouse"};
    unsigned reads = backend_reads;
    assert(settings_call_set_handler(bond, old, read_record, NULL, &excluded) == 0);
    assert(reads == backend_reads);
    assert(settings_call_set_handler("ble_mouse/peer", old, read_record, NULL, NULL) == 0);
    assert(sink_len == old && !memcmp(sink_bytes, stored, old));
    assert(settings_call_set_handler(bond, 0, read_record, NULL, NULL) == 0);
    assert(sink_len == 0);
    assert(settings_save_one(bond, NULL, 0) == 0 && sink_len == 0);
    sink_result = -ENOSPC;
    assert(settings_save_one(bond, expanded, full) == -ENOSPC);
    puts("Bond compatibility: old SC preserved, Legacy round trip, raw/subtree loads and errors passed");
    return 0;
}
