#pragma once
#define SYS_INIT(fn, level, priority) int fake_sys_init(void) { return fn(); } typedef int init_stub
