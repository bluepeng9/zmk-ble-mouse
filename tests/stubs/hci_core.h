#pragma once
#include <test_runtime.h>
struct bt_dev { uint8_t supported_commands[64]; };
extern struct bt_dev bt_dev;
#define BT_CMD_TEST(commands, octet, bit) ((commands)[octet] & BIT(bit))
