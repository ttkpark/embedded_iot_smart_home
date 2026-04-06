/**
 * motor_ctrl.c — 홀센서 리밋 기반 단일 서보 커튼 제어
 *
 * 구조:
 *   - 모터 1개 (GPIO27, SG90-HV continuous)
 *   - 홀센서 CLOSE (GPIO34): 자석 감지 = 커튼 완전 닫힘
 *   - 홀센서 OPEN  (GPIO36): 자석 감지 = 커튼 완전 열림
 *
 * 제어:
 *   - 열기: OPEN 방향 PWM → OPEN 홀센서 자석 감지 시 즉시 정지
 *   - 닫기: CLOSE 방향 PWM → CLOSE 홀센서 자석 감지 시 즉시 정지
 *   - 안전: 자석 범위 밖에서 더 돌리지 않음
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"

#include "config.h"
#include "struct_message.h"
#include "motor_ctrl.h"

static const char *TAG = "MOTOR";

/* ── 글로벌 ───────────────────────────────────────────────────────────────── */
adc_oneshot_unit_handle_t g_adc_handle = NULL;
bool g_cal_mode = false;
volatile bool g_stop_flag = false;
curtain_state_t g_curtain_state = CURTAIN_UNKNOWN;

/* 디바운스 카운터 */
static int s_deb_close = 0;
static int s_deb_open  = 0;

/* ── 서보 PWM ─────────────────────────────────────────────────────────────── */

void servo_set_pulse(uint32_t pulse_us)
{
    const uint32_t period_us = 1000000UL / SERVO_PWM_HZ;
    const uint32_t max_duty  = (1UL << SERVO_LEDC_RES) - 1UL;
    const uint32_t duty      = (pulse_us * max_duty) / period_us;
    ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CH, duty);
    ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CH);
}

void servo_stop(void)
{
    servo_set_pulse(SERVO_PULSE_STOP_US);
}

/* ── 홀센서 읽기 ──────────────────────────────────────────────────────────── */

int hall_read_close(void)
{
    int raw = 0;
    adc_oneshot_read(g_adc_handle, HALL_ADC_CH_CLOSE, &raw);
    return raw;
}

int hall_read_open(void)
{
    int raw = 0;
    adc_oneshot_read(g_adc_handle, HALL_ADC_CH_OPEN, &raw);
    return raw;
}

bool hall_is_closed(void)
{
    int raw = hall_read_close();
    if (raw >= HALL_THRESH_CLOSE) {
        s_deb_close++;
    } else {
        s_deb_close = 0;
    }
    return (s_deb_close >= HALL_DEB_COUNT);
}

bool hall_is_open(void)
{
    int raw = hall_read_open();
    if (raw >= HALL_THRESH_OPEN) {
        s_deb_open++;
    } else {
        s_deb_open = 0;
    }
    return (s_deb_open >= HALL_DEB_COUNT);
}

/* ── 커튼 제어 ─────────────────────────────────────────────────────────────── */

/** 센서 감지 후 역방향으로 45도(~150ms) 되돌려서 오동작 방지 */
static void reverse_nudge(bool opening)
{
    vTaskDelay(pdMS_TO_TICKS(250));
    /* 역회전 속도 1/3: 중립(1500)에서 편차를 1/3로 줄임 */
    int full_rev = opening ? (int)SERVO_CLOSE_US : (int)SERVO_OPEN_US;
    int slow_rev = 1500 + (full_rev - 1500) / 3;
    servo_set_pulse((uint32_t)slow_rev);
    vTaskDelay(pdMS_TO_TICKS(300));
    servo_stop();
    ESP_LOGI(TAG, "Reverse nudge pwm=%d 300ms", slow_rev);
}

void control_curtain(uint8_t action)
{
    if (action == WINDOW_STOP) {
        servo_stop();
        g_curtain_state = CURTAIN_UNKNOWN;
        return;
    }

    bool opening = (action == WINDOW_OPEN);

    if (opening && g_curtain_state == CURTAIN_OPEN) {
        ESP_LOGW(TAG, "Already OPEN");
        return;
    }
    if (!opening && g_curtain_state == CURTAIN_CLOSED) {
        ESP_LOGW(TAG, "Already CLOSED");
        return;
    }

    ESP_LOGI(TAG, "Curtain %s start", opening ? "OPEN" : "CLOSE");
    g_curtain_state = CURTAIN_MOVING;
    s_deb_close = 0;
    s_deb_open = 0;
    g_stop_flag = false;

    uint32_t pwm = opening ? SERVO_OPEN_US : SERVO_CLOSE_US;
    servo_set_pulse(pwm);

    /* 시작 시 이미 센서 위에 있으면 벗어날 때까지 무시 */
    bool was_on_sensor = opening ? (hall_read_open() >= HALL_THRESH_OPEN)
                                 : (hall_read_close() >= HALL_THRESH_CLOSE);
    if (was_on_sensor) {
        ESP_LOGI(TAG, "Starting on sensor — waiting to leave");
        for (int t = 0; t < 5000; t += 5) {
            vTaskDelay(pdMS_TO_TICKS(5));
            if (g_stop_flag) break;
            bool still_on = opening ? (hall_read_open() >= HALL_THRESH_OPEN)
                                    : (hall_read_close() >= HALL_THRESH_CLOSE);
            if (!still_on) {
                ESP_LOGI(TAG, "Left sensor zone (%dms)", t);
                break;
            }
        }
        s_deb_close = 0;
        s_deb_open = 0;
    }

    /* 메인 루프: threshold 도달까지 구동 */
    for (int elapsed = 0; elapsed < 30000; elapsed += 5) {
        vTaskDelay(pdMS_TO_TICKS(5));

        if (g_stop_flag) {
            ESP_LOGW(TAG, "Stop flag — abort");
            break;
        }

        if (opening && hall_is_open()) {
            ESP_LOGI(TAG, "OPEN threshold reached (%dms)", elapsed);
            g_curtain_state = CURTAIN_OPEN;
            break;
        }
        if (!opening && hall_is_closed()) {
            ESP_LOGI(TAG, "CLOSE threshold reached (%dms)", elapsed);
            g_curtain_state = CURTAIN_CLOSED;
            break;
        }

        if (gpio_get_level(PIN_CURTAIN_LIMIT) == 0) {
            ESP_LOGI(TAG, "Limit switch");
            break;
        }
    }

    servo_stop();

    /* 센서 감지 후 45도 역회전 — 오동작 방지 */
    if (g_curtain_state == CURTAIN_OPEN || g_curtain_state == CURTAIN_CLOSED) {
        reverse_nudge(opening);
    }

    if (g_curtain_state == CURTAIN_MOVING) {
        ESP_LOGW(TAG, "Timeout — sensor not triggered");
        g_curtain_state = CURTAIN_UNKNOWN;
    }

    ESP_LOGI(TAG, "Curtain %s done (state=%d)",
             opening ? "OPEN" : "CLOSE", g_curtain_state);
}

/* ── Jog (시간 기반, 테스트/캘리브레이션용) ────────────────────────────────── */

void motor_jog_stop(void)
{
    g_stop_flag = true;
    servo_stop();
}

void motor_jog_timed(bool forward, int ms)
{
    s_deb_close = 0;
    s_deb_open = 0;

    g_stop_flag = false;

    uint32_t pwm = forward ? SERVO_OPEN_US : SERVO_CLOSE_US;
    servo_set_pulse(pwm);

    for (int elapsed = 0; elapsed < ms; elapsed += 5) {
        vTaskDelay(pdMS_TO_TICKS(5));

        if (g_stop_flag) {
            ESP_LOGW(TAG, "Jog stop flag");
            break;
        }

        /* 안전: 홀센서 감지 시 즉시 정지 (커튼 손상 방지) */
        if (forward && hall_is_open()) {
            ESP_LOGW(TAG, "Jog FWD — OPEN limit reached, stop");
            break;
        }
        if (!forward && hall_is_closed()) {
            ESP_LOGW(TAG, "Jog REV — CLOSE limit reached, stop");
            break;
        }
    }

    servo_stop();

    /* 홀센서 리밋 도달 시 45도 역회전 */
    bool hit_open  = (forward && hall_read_open() >= HALL_THRESH_OPEN);
    bool hit_close = (!forward && hall_read_close() >= HALL_THRESH_CLOSE);
    if (hit_open || hit_close) {
        reverse_nudge(forward);
    }

    ESP_LOGI(TAG, "Jog %s done (%dms)", forward ? "FWD" : "REV", ms);
}

/* ── 초기화 ───────────────────────────────────────────────────────────────── */

void motor_ctrl_init(void)
{
    /* ADC */
    adc_oneshot_unit_init_cfg_t adc_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &g_adc_handle));
    adc_oneshot_chan_cfg_t ch_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc_handle, HALL_ADC_CH_CLOSE, &ch_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc_handle, HALL_ADC_CH_OPEN, &ch_cfg));

    /* LEDC — 단일 채널 */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = SERVO_LEDC_MODE, .timer_num = SERVO_LEDC_TIMER,
        .duty_resolution = SERVO_LEDC_RES, .freq_hz = SERVO_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch = {
        .gpio_num = PIN_CURTAIN_MOTOR, .speed_mode = SERVO_LEDC_MODE,
        .channel = SERVO_LEDC_CH, .timer_sel = SERVO_LEDC_TIMER,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    servo_stop();

    /* 부팅 시 현재 위치 감지 */
    if (hall_read_close() >= HALL_THRESH_CLOSE)
        g_curtain_state = CURTAIN_CLOSED;
    else if (hall_read_open() >= HALL_THRESH_OPEN)
        g_curtain_state = CURTAIN_OPEN;
    else
        g_curtain_state = CURTAIN_UNKNOWN;

    ESP_LOGI(TAG, "Init done (motor:GPIO%d, hall_close:GPIO%d, hall_open:GPIO%d, state=%d)",
             PIN_CURTAIN_MOTOR, PIN_HALL_CLOSE, PIN_HALL_OPEN, g_curtain_state);
}
