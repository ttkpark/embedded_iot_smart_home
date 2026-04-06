#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"

/* ── 커튼 상태 ────────────────────────────────────────────────────────────── */
typedef enum {
    CURTAIN_UNKNOWN = 0,  /* 위치 모름 (부팅 직후) */
    CURTAIN_CLOSED,       /* 닫힘 홀센서 감지 → 완전 닫힘 */
    CURTAIN_OPEN,         /* 열림 홀센서 감지 → 완전 열림 */
    CURTAIN_MOVING,       /* 이동 중 */
} curtain_state_t;

/* ── 글로벌 ───────────────────────────────────────────────────────────────── */
extern adc_oneshot_unit_handle_t g_adc_handle;
extern bool          g_cal_mode;
extern volatile bool g_stop_flag;  /* s 명령 시 즉시 정지 */
extern curtain_state_t g_curtain_state;

/* ── LEDC ─────────────────────────────────────────────────────────────────── */
#define SERVO_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER     LEDC_TIMER_0
#define SERVO_LEDC_CH        LEDC_CHANNEL_0
#define SERVO_LEDC_RES       LEDC_TIMER_16_BIT

/* ── API ──────────────────────────────────────────────────────────────────── */
void motor_ctrl_init(void);

void servo_set_pulse(uint32_t pulse_us);
void servo_stop(void);

/* 홀센서 읽기 (raw ADC) */
int hall_read_close(void);  /* GPIO34: 닫힘 센서 */
int hall_read_open(void);   /* GPIO36: 열림 센서 */

/* 자석 감지 여부 (디바운스 적용) */
bool hall_is_closed(void);  /* 닫힘 자석 감지 중? */
bool hall_is_open(void);    /* 열림 자석 감지 중? */

/* 커튼 제어 */
void control_curtain(uint8_t action);

/* 콘솔 jog */
void motor_jog_timed(bool forward, int ms);
void motor_jog_stop(void);
