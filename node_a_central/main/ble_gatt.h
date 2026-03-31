#pragma once

/** BLE GATT 서버 초기화 — NimBLE 기반 */
void ble_gatt_init(void);

/** 상태/알림 JSON을 BLE 클라이언트에 notify */
void ble_notify_json(const char *json);
