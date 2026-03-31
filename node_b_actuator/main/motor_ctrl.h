#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"

/* ── 홀센서 상태 (상태 머신 + 히스테리시스) ─────────────────────────────────── */
typedef struct {
    int32_t  edge_count;     /* 감지된 회전 수 */
    bool     active;         /* true = 자석이 센서 범위 안에 있음 */
    int      deb_enter;      /* lo 이하 연속 카운트 (inactive→active 전이) */
    int      deb_exit;       /* hi 이상 연속 카운트 (active→inactive 전이) */
    int64_t  last_edge_us;   /* 마지막 엣지 타임스탬프 (us) */
    float    us_per_rev;     /* 최근 1회전 주기 실측 (us) */
} hall_state_t;

/* ── NVS 캘리브레이션 ────────────────────────────────────────────────────── */
typedef struct {
    float    x_pos;          /* 현재 커튼 위치 (0=닫힘, travel=열림) */
    float    travel;         /* 전체 커튼 길이 (바퀴) */
    uint32_t l_open_us;
    uint32_t l_close_us;
    uint32_t r_open_us;
    uint32_t r_close_us;
    float    ms_per_rev_l;   /* L 모터 실측 1바퀴 시간 (ms) */
    float    ms_per_rev_r;   /* R 모터 실측 1바퀴 시간 (ms) */
    uint8_t  valid;
} curtain_cal_t;

/* ── 글로벌 ───────────────────────────────────────────────────────────────── */
extern hall_state_t  g_hl, g_hr;
extern curtain_cal_t g_cal;
extern adc_oneshot_unit_handle_t g_adc_handle;
extern bool          g_cal_mode;

/* ── LEDC ─────────────────────────────────────────────────────────────────── */
#define SERVO_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER     LEDC_TIMER_0
#define SERVO_LEDC_CH_L      LEDC_CHANNEL_0
#define SERVO_LEDC_CH_R      LEDC_CHANNEL_1
#define SERVO_LEDC_RES       LEDC_TIMER_16_BIT

/* ── API ──────────────────────────────────────────────────────────────────── */
void motor_ctrl_init(void);
void motor_ctrl_nvs_save(void);

void servo_set_pulse(ledc_channel_t ch, uint32_t pulse_us);
void servo_both_stop(void);

void hall_poll(void);
void control_curtain(uint8_t action);

void motor_jog_both_revs(bool forward, int32_t revs);
void motor_jog_single_revs(bool left, bool forward, int32_t revs);
void motor_jog_stop(void);
