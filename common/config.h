#pragma once

/* ── Wi-Fi ─────────────────────────────────────────────────────────────────
 * Node A가 연결할 공유기 SSID/PW.
 * ESPNOW_CHANNEL: 공유기 채널과 반드시 일치해야 함 (공유기 설정에서 확인).
 * ────────────────────────────────────────────────────────────────────────── */
#define WIFI_SSID        "YOUR_SSID"
#define WIFI_PASSWORD    "YOUR_PASSWORD"
#define ESPNOW_CHANNEL   1   /* 1~13 — 공유기 고정 채널로 변경 */

/* ── MAC 주소 ──────────────────────────────────────────────────────────────
 * 최초 부팅 시 시리얼 모니터(115200 baud)에서 출력되는 값을 확인 후 기입.
 * 예: I (xxx) NODE_A: MAC: AA:BB:CC:DD:EE:FF
 * ────────────────────────────────────────────────────────────────────────── */
#define MAC_NODE_A  {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define MAC_NODE_B  {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define MAC_NODE_C  {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

/* ── Node A GPIO ───────────────────────────────────────────────────────────*/
#define PIN_STATUS_LED   2    /* On-board LED */
#define PIN_OLED_SDA     21   /* Optional I2C OLED */
#define PIN_OLED_SCL     22

/* ── Node B GPIO ───────────────────────────────────────────────────────────
 * GPIO 25/27/14: 외부 10kΩ 풀다운 저항 부착 필수 (부팅 Glitch 방지)
 * ────────────────────────────────────────────────────────────────────────── */
#define PIN_FAN_RELAY    25   /* SSR 릴레이 — 환풍기 Active High */
#define PIN_IR_LED       26   /* IR LED — 에어컨 (NPN 증폭 회로, PWM/RMT) */
#define PIN_MOTOR_IN1    27   /* L293D IN1 — 창문 전진(열림) */
#define PIN_MOTOR_IN2    14   /* L293D IN2 — 창문 후진(닫힘) */
#define MOTOR_RUN_MS     3000 /* 창문 완전 개폐 시간(ms) — 캘리브레이션 필요 */

/* ── Node C GPIO ───────────────────────────────────────────────────────────*/
#define PIN_BTN_NORMAL    32  /* 정상 버튼 — INPUT_PULLUP, FALLING 인터럽트 */
#define PIN_BTN_EMERGENCY 33  /* 응급 버튼 — INPUT_PULLUP, FALLING 인터럽트 */
#define DEBOUNCE_MS       200 /* 디바운스 간격(ms) */

/* ── 응급 자동 제어 기본값 ─────────────────────────────────────────────────*/
#define AUTO_FAN_STATE    FAN_ON
#define AUTO_AC_TEMP      18
#define AUTO_WINDOW_ACT   WINDOW_OPEN
