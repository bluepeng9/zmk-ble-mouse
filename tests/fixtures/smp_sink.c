#include <test_runtime.h>
int smp_result, smp_calls;
int bt_smp_init(void) { smp_calls++; return smp_result; }
