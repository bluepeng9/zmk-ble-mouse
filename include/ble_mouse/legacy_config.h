/* SPDX-License-Identifier: MIT */
#pragma once

/* Forced in after autoconf.h, consistently across the entire receiver image. */
#if defined(CONFIG_ZMK_BLE_MOUSE_LEGACY_PAIRING)
#if !defined(CONFIG_BT_SMP_SC_PAIR_ONLY)
#error "Expected the original SC-only configuration before the Legacy shim"
#endif
#if defined(CONFIG_BT_SMP_SC_ONLY) || defined(CONFIG_BT_SMP_OOB_LEGACY_PAIR_ONLY)
#error "Cannot override a stronger or OOB-only SMP policy"
#endif
#undef CONFIG_BT_SMP_SC_PAIR_ONLY
#define ZMK_BLE_MOUSE_LEGACY_CONFIG_APPLIED 1
#endif
