/**
 * motor_ctrl.c — Hall-sensor feedback motor control
 *
 * L (GPIO23): Caterpillar belt drive — linear curtain movement
 * R (GPIO27): Spool winding drive
 *
 * Hall sensor edge detection: state-machine + hysteresis
 *   - ENTER: N consecutive reads below THRESH_LO → magnet confirmed
 *   - EXIT:  N consecutive reads above THRESH_HI → magnet gone
 *   - Edge lockout prevents double-counting from noise
 *   - Single threshold replaced by hi/lo hysteresis band
 *
 * Jog: hall-sensor based revolution counting (precise 1-rev)
 * Curtain open/close: hall-sensor based with time fallback
 */
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"

#include "config.h"
#include "struct_message.h"
#include "motor_ctrl.h"

static const char *TAG = "MOTOR";

#define CAL_ALPHA  0.05f

/* ── Globals ─────────────────────────────────────────────────────────────── */
hall_state_t  g_hl = {0}, g_hr = {0};
curtain_cal_t g_cal = {0};
adc_oneshot_unit_handle_t g_adc_handle = NULL;
bool g_cal_mode = false;

/* ── Servo PWM ───────────────────────────────────────────────────────────── */

void servo_set_pulse(ledc_channel_t ch, uint32_t pulse_us)
{
    const uint32_t period_us = 1000000UL / SERVO_PWM_HZ;
    const uint32_t max_duty  = (1UL << SERVO_LEDC_RES) - 1UL;
    const uint32_t duty      = (pulse_us * max_duty) / period_us;
    ledc_set_duty(SERVO_LEDC_MODE, ch, duty);
    ledc_update_duty(SERVO_LEDC_MODE, ch);
}

void servo_both_stop(void)
{
    servo_set_pulse(SERVO_LEDC_CH_L, SERVO_PULSE_STOP_US);
    servo_set_pulse(SERVO_LEDC_CH_R, SERVO_PULSE_STOP_US);
}

/* ── NVS ─────────────────────────────────────────────────────────────────── */

static void set_defaults(void)
{
    g_cal.travel      = 2.5f;
    g_cal.x_pos       = 0;
    g_cal.l_open_us   = SERVO_L_OPEN_US;
    g_cal.l_close_us  = SERVO_L_CLOSE_US;
    g_cal.r_open_us   = SERVO_R_OPEN_US;
    g_cal.r_close_us  = SERVO_R_CLOSE_US;
    g_cal.ms_per_rev_l = 0;
    g_cal.ms_per_rev_r = 0;
    g_cal.valid       = 0;
}

static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CURTAIN, NVS_READWRITE, &h) != ESP_OK) {
        set_defaults();
        return;
    }
    size_t sz = sizeof(g_cal);
    esp_err_t err = nvs_get_blob(h, "cal", &g_cal, &sz);
    if (err != ESP_OK || sz != sizeof(g_cal)) {
        ESP_LOGW(TAG, "NVS mismatch — reset");
        nvs_erase_key(h, "cal");
        set_defaults();
        nvs_set_blob(h, "cal", &g_cal, sizeof(g_cal));
        nvs_commit(h);
    }
    nvs_close(h);
    ESP_LOGI(TAG, "NVS: x=%.2f travel=%.1f L=%.0fms/rev R=%.0fms/rev",
             g_cal.x_pos, g_cal.travel, g_cal.ms_per_rev_l, g_cal.ms_per_rev_r);
}

void motor_ctrl_nvs_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CURTAIN, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "cal", &g_cal, sizeof(g_cal));
    nvs_commit(h);
    nvs_close(h);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Hall sensor — state-machine + hysteresis edge detection
 *
 * State: inactive (magnet away) ↔ active (magnet present)
 *
 * inactive → active:  need HALL_DEB_ENTER consecutive reads < THRESH_LO
 *                     AND (now - last_edge) > HALL_EDGE_LOCKOUT_MS
 *                     → record EDGE, update calibration
 *
 * active → inactive:  need HALL_DEB_EXIT consecutive reads > THRESH_HI
 *
 * This eliminates false edges from ADC noise near a single threshold.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void hall_process_one(hall_state_t *hs, int raw,
                             int thresh_lo, int thresh_hi,
                             float *cal_ms, int64_t now)
{
    if (hs->active) {
        /* Currently in "magnet present" state — wait for exit */
        if (raw > thresh_hi) {
            hs->deb_exit++;
            hs->deb_enter = 0;
            if (hs->deb_exit >= HALL_DEB_EXIT) {
                hs->active = false;
                hs->deb_exit = 0;
            }
        } else {
            hs->deb_exit = 0;
        }
    } else {
        /* Currently in "magnet away" state — wait for entry */
        if (raw < thresh_lo) {
            hs->deb_enter++;
            hs->deb_exit = 0;
            if (hs->deb_enter >= HALL_DEB_ENTER) {
                /* Lockout check */
                int64_t gap_ms = (now - hs->last_edge_us) / 1000;
                if (hs->last_edge_us == 0 || gap_ms >= HALL_EDGE_LOCKOUT_MS) {
                    /* === CONFIRMED EDGE === */
                    hs->edge_count++;

                    /* Calibrate ms_per_rev */
                    if (hs->last_edge_us > 0) {
                        float period = (float)(now - hs->last_edge_us) / 1000.0f;
                        hs->us_per_rev = (float)(now - hs->last_edge_us);
                        if (*cal_ms > 50.0f) {
                            *cal_ms = *cal_ms * (1.0f - CAL_ALPHA)
                                    + period * CAL_ALPHA;
                        } else {
                            *cal_ms = period;
                        }
                    }
                    hs->last_edge_us = now;
                }
                hs->active = true;
                hs->deb_enter = 0;
            }
        } else {
            hs->deb_enter = 0;
        }
    }
}

void hall_poll(void)
{
    int raw_l = 0, raw_r = 0;
    adc_oneshot_read(g_adc_handle, HALL_ADC_CH_L, &raw_l);
    adc_oneshot_read(g_adc_handle, HALL_ADC_CH_R, &raw_r);
    int64_t now = esp_timer_get_time();

    hall_process_one(&g_hl, raw_l, HALL_THRESH_LO_L, HALL_THRESH_HI_L,
                     &g_cal.ms_per_rev_l, now);
    hall_process_one(&g_hr, raw_r, HALL_THRESH_LO_R, HALL_THRESH_HI_R,
                     &g_cal.ms_per_rev_r, now);
}

/* Robust initial state: majority vote from multiple reads */
static void sync_active(void)
{
    int lo_l = 0, lo_r = 0;
    for (int i = 0; i < 8; i++) {
        int raw_l = 0, raw_r = 0;
        adc_oneshot_read(g_adc_handle, HALL_ADC_CH_L, &raw_l);
        adc_oneshot_read(g_adc_handle, HALL_ADC_CH_R, &raw_r);
        if (raw_l < HALL_THRESH_LO_L) lo_l++;
        if (raw_r < HALL_THRESH_LO_R) lo_r++;
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    g_hl.active    = (lo_l >= 5);
    g_hl.deb_enter = g_hl.active ? HALL_DEB_ENTER : 0;
    g_hl.deb_exit  = g_hl.active ? 0 : HALL_DEB_EXIT;

    g_hr.active    = (lo_r >= 5);
    g_hr.deb_enter = g_hr.active ? HALL_DEB_ENTER : 0;
    g_hr.deb_exit  = g_hr.active ? 0 : HALL_DEB_EXIT;
}

static void reset_hall_counters(void)
{
    g_hl.edge_count = 0; g_hr.edge_count = 0;
    g_hl.last_edge_us = 0; g_hr.last_edge_us = 0;
    sync_active();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Run time calculation
 * ═══════════════════════════════════════════════════════════════════════════ */

static int calc_run_time(float revs)
{
    float ms_l = g_cal.ms_per_rev_l;
    float ms_r = g_cal.ms_per_rev_r;

    if (ms_l < 50.0f && ms_r < 50.0f)
        return MOTOR_RUN_MS;

    if (ms_l < 50.0f) ms_l = ms_r;
    if (ms_r < 50.0f) ms_r = ms_l;

    float slower = (ms_l > ms_r) ? ms_l : ms_r;
    return (int)(revs * slower * 1.5f);  /* 50% margin as timeout */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Curtain control — hall-sensor based with time fallback
 * Each motor stops independently when it reaches target revs
 * ═══════════════════════════════════════════════════════════════════════════ */

void control_curtain(uint8_t action)
{
    if (action == WINDOW_STOP) {
        servo_both_stop();
        return;
    }

    bool opening = (action == WINDOW_OPEN);
    float target_x = opening ? g_cal.travel : 0.0f;
    float delta = fabsf(target_x - g_cal.x_pos);

    if (delta < 0.05f) {
        ESP_LOGW(TAG, "Already at target (x=%.2f)", g_cal.x_pos);
        return;
    }

    int timeout_ms = calc_run_time(delta);
    int32_t target_revs = (int32_t)(delta + 0.5f);
    if (target_revs < 1) target_revs = 1;

    ESP_LOGI(TAG, "Curtain %s: x %.2f->%.2f (%d revs, timeout %dms)",
             opening ? "OPEN" : "CLOSE", g_cal.x_pos, target_x,
             (int)target_revs, timeout_ms);

    reset_hall_counters();

    uint32_t l_pwm = opening ? g_cal.l_open_us : g_cal.l_close_us;
    uint32_t r_pwm = opening ? g_cal.r_open_us : g_cal.r_close_us;
    bool l_done = false, r_done = false;

    servo_set_pulse(SERVO_LEDC_CH_L, l_pwm);
    servo_set_pulse(SERVO_LEDC_CH_R, r_pwm);

    int64_t start_us = esp_timer_get_time();
    int64_t last_edge_time = start_us;

    for (int elapsed = 0; elapsed < timeout_ms; elapsed += HALL_SYNC_CHECK_MS) {
        vTaskDelay(pdMS_TO_TICKS(HALL_SYNC_CHECK_MS));

        int32_t prev_total = g_hl.edge_count + g_hr.edge_count;
        hall_poll();
        if ((g_hl.edge_count + g_hr.edge_count) > prev_total)
            last_edge_time = esp_timer_get_time();

        /* Stop each motor independently */
        if (!l_done && g_hl.edge_count >= target_revs) {
            servo_set_pulse(SERVO_LEDC_CH_L, SERVO_PULSE_STOP_US);
            l_done = true;
            ESP_LOGI(TAG, "L done at %ld edges", (long)g_hl.edge_count);
        }
        if (!r_done && g_hr.edge_count >= target_revs) {
            servo_set_pulse(SERVO_LEDC_CH_R, SERVO_PULSE_STOP_US);
            r_done = true;
            ESP_LOGI(TAG, "R done at %ld edges", (long)g_hr.edge_count);
        }
        if (l_done && r_done) break;

        /* Stall detection */
        float stall_ms = (float)(esp_timer_get_time() - last_edge_time) / 1000.0f;
        if (stall_ms > 2000.0f) {
            ESP_LOGW(TAG, "Stall detected — stop");
            break;
        }

        /* Limit switch */
        if (gpio_get_level(PIN_CURTAIN_LIMIT) == 0) {
            ESP_LOGI(TAG, "Limit switch hit");
            break;
        }
    }

    servo_both_stop();

    g_cal.x_pos = target_x;
    motor_ctrl_nvs_save();

    ESP_LOGI(TAG, "Curtain %s done: x=%.2f (L=%ld R=%ld edges, cal[L:%.0f R:%.0f]ms/rev)",
             opening ? "OPEN" : "CLOSE", g_cal.x_pos,
             (long)g_hl.edge_count, (long)g_hr.edge_count,
             g_cal.ms_per_rev_l, g_cal.ms_per_rev_r);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Jog — precise revolution counting via hall sensor
 * ═══════════════════════════════════════════════════════════════════════════ */

void motor_jog_stop(void)
{
    servo_both_stop();
}

void motor_jog_both_revs(bool forward, int32_t revs)
{
    reset_hall_counters();

    uint32_t l_pwm = forward ? g_cal.l_open_us : g_cal.l_close_us;
    uint32_t r_pwm = forward ? g_cal.r_open_us : g_cal.r_close_us;
    bool l_done = false, r_done = false;

    servo_set_pulse(SERVO_LEDC_CH_L, l_pwm);
    servo_set_pulse(SERVO_LEDC_CH_R, r_pwm);

    int64_t start_us = esp_timer_get_time();
    int64_t last_edge_time = start_us;

    for (int elapsed = 0; elapsed < 15000; elapsed += HALL_SYNC_CHECK_MS) {
        vTaskDelay(pdMS_TO_TICKS(HALL_SYNC_CHECK_MS));

        int32_t prev = g_hl.edge_count + g_hr.edge_count;
        hall_poll();
        if ((g_hl.edge_count + g_hr.edge_count) > prev)
            last_edge_time = esp_timer_get_time();

        if (!l_done && g_hl.edge_count >= revs) {
            servo_set_pulse(SERVO_LEDC_CH_L, SERVO_PULSE_STOP_US);
            l_done = true;
        }
        if (!r_done && g_hr.edge_count >= revs) {
            servo_set_pulse(SERVO_LEDC_CH_R, SERVO_PULSE_STOP_US);
            r_done = true;
        }
        if (l_done && r_done) break;

        if ((float)(esp_timer_get_time() - last_edge_time) / 1000.0f > 3000.0f) {
            ESP_LOGW(TAG, "Jog stall — stop");
            break;
        }
    }

    servo_both_stop();

    float total_ms = (float)(esp_timer_get_time() - start_us) / 1000.0f;
    motor_ctrl_nvs_save();

    ESP_LOGI(TAG, "JogBoth %s: L=%ld R=%ld / target=%ld (%.0fms)",
             forward ? "FWD" : "REV",
             (long)g_hl.edge_count, (long)g_hr.edge_count,
             (long)revs, total_ms);
}

void motor_jog_single_revs(bool left, bool forward, int32_t revs)
{
    reset_hall_counters();
    hall_state_t *hs = left ? &g_hl : &g_hr;

    ledc_channel_t ch = left ? SERVO_LEDC_CH_L : SERVO_LEDC_CH_R;
    uint32_t pwm;
    if (left)
        pwm = forward ? g_cal.l_open_us : g_cal.l_close_us;
    else
        pwm = forward ? g_cal.r_open_us : g_cal.r_close_us;

    servo_set_pulse(ch, pwm);

    int64_t start_us = esp_timer_get_time();
    int64_t last_edge_time = start_us;

    for (int elapsed = 0; elapsed < 15000; elapsed += HALL_SYNC_CHECK_MS) {
        vTaskDelay(pdMS_TO_TICKS(HALL_SYNC_CHECK_MS));

        int32_t prev = hs->edge_count;
        hall_poll();
        if (hs->edge_count > prev)
            last_edge_time = esp_timer_get_time();

        if (hs->edge_count >= revs)
            break;

        if ((float)(esp_timer_get_time() - last_edge_time) / 1000.0f > 3000.0f) {
            ESP_LOGW(TAG, "SingleJog stall — stop");
            break;
        }
    }

    /* Brief reverse brake to reduce inertia overshoot */
    uint32_t brake_pwm;
    if (left)
        brake_pwm = forward ? g_cal.l_close_us : g_cal.l_open_us;
    else
        brake_pwm = forward ? g_cal.r_close_us : g_cal.r_open_us;
    servo_set_pulse(ch, brake_pwm);
    vTaskDelay(pdMS_TO_TICKS(30));
    servo_set_pulse(ch, SERVO_PULSE_STOP_US);

    float total_ms = (float)(esp_timer_get_time() - start_us) / 1000.0f;
    motor_ctrl_nvs_save();

    ESP_LOGI(TAG, "SingleJog %s %s: edges=%ld / target=%ld (%.0fms)",
             left ? "L" : "R", forward ? "FWD" : "REV",
             (long)hs->edge_count, (long)revs, total_ms);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Init
 * ═══════════════════════════════════════════════════════════════════════════ */

void motor_ctrl_init(void)
{
    adc_oneshot_unit_init_cfg_t adc_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &g_adc_handle));
    adc_oneshot_chan_cfg_t ch_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc_handle, HALL_ADC_CH_L, &ch_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc_handle, HALL_ADC_CH_R, &ch_cfg));

    ledc_timer_config_t timer_cfg = {
        .speed_mode = SERVO_LEDC_MODE, .timer_num = SERVO_LEDC_TIMER,
        .duty_resolution = SERVO_LEDC_RES, .freq_hz = SERVO_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_l = {
        .gpio_num = PIN_CURTAIN_MOTOR_L, .speed_mode = SERVO_LEDC_MODE,
        .channel = SERVO_LEDC_CH_L, .timer_sel = SERVO_LEDC_TIMER,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_l));

    ledc_channel_config_t ch_r = {
        .gpio_num = PIN_CURTAIN_MOTOR_R, .speed_mode = SERVO_LEDC_MODE,
        .channel = SERVO_LEDC_CH_R, .timer_sel = SERVO_LEDC_TIMER,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_r));
    servo_both_stop();

    nvs_load();
    ESP_LOGI(TAG, "Init done (travel=%.1f cal[L:%.0f R:%.0f]ms/rev)",
             g_cal.travel, g_cal.ms_per_rev_l, g_cal.ms_per_rev_r);
}
