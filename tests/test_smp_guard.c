#include <stdio.h>
#include <hci_core.h>
struct bt_dev bt_dev;
extern int smp_result, smp_calls;
int bt_smp_init(void);
int main(void) {
    assert(bt_smp_init() == -ENOENT && smp_calls == 0);
    bt_dev.supported_commands[34] = BIT(1);
    assert(bt_smp_init() == -ENOENT && smp_calls == 0);
    bt_dev.supported_commands[34] = BIT(2);
    assert(bt_smp_init() == -ENOENT && smp_calls == 0);
    bt_dev.supported_commands[34] = BIT(1) | BIT(2);
    assert(bt_smp_init() == 0 && smp_calls == 1);
    smp_result = -EIO;
    assert(bt_smp_init() == -EIO && smp_calls == 2);
    puts("SMP guard: local SC capability and upstream init errors preserved");
    return 0;
}
