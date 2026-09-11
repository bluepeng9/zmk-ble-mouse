/* SPDX-License-Identifier: MIT */
#pragma once

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <ble_mouse/hid_parser.h>

struct bt_conn;

void zmk_ble_mouse_start(void);
int zmk_ble_mouse_pair(void);
void zmk_ble_mouse_clear_hold(bool pressed);
bool zmk_ble_mouse_wants_scan(void);
bool zmk_ble_mouse_advertisement(const bt_addr_le_t *addr, uint8_t type,
                                 struct net_buf_simple *ad);

/* Private module interfaces, not hooks inserted into ZMK. */
void zmk_ble_mouse_submit(struct k_work *work);
void zmk_ble_mouse_schedule(struct k_work_delayable *work, int32_t delay_ms);
void zmk_ble_central_scan_refresh(void);
int zmk_ble_central_connect(const bt_addr_le_t *addr,
                           int (*connect)(const bt_addr_le_t *addr));
void zmk_ble_central_connection_complete(void);
void zmk_ble_central_scan_init(void);
void zmk_mouse_output_receive(const struct mouse_hid_frame *frame, uint8_t buttons);
void zmk_mouse_output_disconnect(void);
