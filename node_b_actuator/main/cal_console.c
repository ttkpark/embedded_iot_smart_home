/**
 * cal_console.c — UART 콘솔: 캘리브레이션 + jog + 상태
 *
 * 명령:
 *   cal start/set_start/set_end/save/status
 *   f          열림 방향 2바퀴 (양쪽 동시)
 *   r          닫힘 방향 2바퀴
 *   s          긴급 정지
 *   pos        홀센서/위치/PWM 표시
 */
#include <string.h>
#include <stdio.h>
#include "esp_console.h"
#include "esp_log.h"

#include "config.h"
#include "motor_ctrl.h"

static const char *TAG = "CAL";

static float s_start_l = 0, s_start_r = 0;
static float s_end_l = 0, s_end_r = 0;
static bool  s_start_set = false, s_end_set = false;

/* ── cal ───────────────────────────────────────────────────────────────────── */

static int cmd_cal(int argc, char **argv)
{
    if (argc < 2) {
        printf("cal <start|set_start|set_end|save|status>\n");
        return 0;
    }

    if (strcmp(argv[1], "start") == 0) {
        g_cal_mode = true;
        s_start_set = false;
        s_end_set = false;
        g_hl.edge_count = 0; g_hr.edge_count = 0;
        g_hl.edge_count = 0; g_hr.edge_count = 0;
        servo_both_stop();
        printf("=== CAL 모드 ===\n");
        printf("  f/r 로 이동, cal set_start → f → cal set_end → cal save\n");

    } else if (strcmp(argv[1], "set_start") == 0) {
        if (!g_cal_mode) { printf("먼저 cal start\n"); return 0; }
        s_start_l = g_hl.edge_count;
        s_start_r = g_hr.edge_count;
        s_start_set = true;
        printf("시작점: L=%.1f R=%.1f\n", s_start_l, s_start_r);

    } else if (strcmp(argv[1], "set_end") == 0) {
        if (!g_cal_mode) { printf("먼저 cal start\n"); return 0; }
        s_end_l = g_hl.edge_count;
        s_end_r = g_hr.edge_count;
        s_end_set = true;
        float dl = s_end_l - s_start_l;
        float dr = s_end_r - s_start_r;
        if (dl < 0) dl = -dl;
        if (dr < 0) dr = -dr;
        printf("끝점: L=%.1f R=%.1f (L=%.1f R=%.1f 바퀴, 평균=%.1f)\n",
               s_end_l, s_end_r, dl, dr, (dl + dr) / 2.0f);

    } else if (strcmp(argv[1], "save") == 0) {
        if (!g_cal_mode) { printf("CAL 모드 아님\n"); return 0; }
        if (!s_start_set || !s_end_set) { printf("start/end 모두 설정 필요\n"); return 0; }
        float dl = s_end_l - s_start_l;
        float dr = s_end_r - s_start_r;
        if (dl < 0) dl = -dl;
        if (dr < 0) dr = -dr;
        float travel = (dl + dr) / 2.0f;
        if (travel < 0.5f) { printf("너무 짧음 (%.1f)\n", travel); return 0; }
        g_cal.travel = travel;
        g_cal.x_pos = 0;
        g_cal.valid = 1;
        motor_ctrl_nvs_save();
        g_cal_mode = false;
        printf("=== 저장 완료: travel=%.1f, x=0 (닫힘), 일반모드 복귀 ===\n", travel);

    } else if (strcmp(argv[1], "status") == 0) {
        printf("valid=%d  x=%.2f  travel=%.1f\n", g_cal.valid, g_cal.x_pos, g_cal.travel);
        printf("PWM: L[%lu/%lu] R[%lu/%lu]\n",
               (unsigned long)g_cal.l_open_us, (unsigned long)g_cal.l_close_us,
               (unsigned long)g_cal.r_open_us, (unsigned long)g_cal.r_close_us);
        printf("모드: %s\n", g_cal_mode ? "CAL" : "NORMAL");
    } else {
        printf("? start/set_start/set_end/save/status\n");
    }
    return 0;
}

/* ── f/r/s ─────────────────────────────────────────────────────────────────── */

static int cmd_f(int argc, char **argv)
{
    if (!g_cal_mode) { printf("먼저 cal start\n"); return 0; }
    printf(">>> 열림 2바퀴...\n");
    motor_jog_both_revs(true, 2);
    printf("완료 L=%ld R=%ld\n", (long)g_hl.edge_count, (long)g_hr.edge_count);
    return 0;
}

static int cmd_r(int argc, char **argv)
{
    if (!g_cal_mode) { printf("먼저 cal start\n"); return 0; }
    printf("<<< 닫힘 2바퀴...\n");
    motor_jog_both_revs(false, 2);
    printf("완료 L=%ld R=%ld\n", (long)g_hl.edge_count, (long)g_hr.edge_count);
    return 0;
}

static int cmd_s(int argc, char **argv)
{
    motor_jog_stop();
    printf("정지\n");
    return 0;
}

/* t = travel 설정 (0.1 단위) */
static int cmd_t(int argc, char **argv)
{
    if (argc < 2) {
        printf("현재 travel=%.1f  사용법: t <바퀴수> (예: t 3.4)\n", g_cal.travel);
        return 0;
    }
    float val = 0;
    if (sscanf(argv[1], "%f", &val) != 1 || val < 0.1f || val > 20.0f) {
        printf("범위: 0.1 ~ 20.0\n");
        return 0;
    }
    g_cal.travel = val;
    motor_ctrl_nvs_save();
    printf("travel=%.1f 저장 완료\n", g_cal.travel);
    return 0;
}

/* ── pos ───────────────────────────────────────────────────────────────────── */

static int cmd_pos(int argc, char **argv)
{
    int raw_l = 0, raw_r = 0;
    adc_oneshot_read(g_adc_handle, HALL_ADC_CH_L, &raw_l);
    adc_oneshot_read(g_adc_handle, HALL_ADC_CH_R, &raw_r);

    printf("ADC[L:%d R:%d] edges[L:%ld R:%ld] x=%.2f/%.1f\n",
           raw_l, raw_r, (long)g_hl.edge_count, (long)g_hr.edge_count,
           g_cal.x_pos, g_cal.travel);
    printf("cal[L:%.0f R:%.0f]ms/rev\n",
           g_cal.ms_per_rev_l, g_cal.ms_per_rev_r);
    return 0;
}

/* ── 초기화 ───────────────────────────────────────────────────────────────── */

void cal_console_init(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "node_b>";
    repl_config.max_cmdline_length = 64;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    const esp_console_cmd_t cmds[] = {
        { .command = "cal", .help = "캘리브레이션", .func = &cmd_cal },
        { .command = "f",   .help = "열림 2바퀴",   .func = &cmd_f },
        { .command = "r",   .help = "닫힘 2바퀴",   .func = &cmd_r },
        { .command = "s",   .help = "정지",         .func = &cmd_s },
        { .command = "pos", .help = "상태 표시",     .func = &cmd_pos },
        { .command = "t",   .help = "커튼 길이: t <바퀴> (예: t 3.4)", .func = &cmd_t },
    };
    for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    ESP_LOGI(TAG, "콘솔 시작 (cal/f/r/s/pos)");
}
