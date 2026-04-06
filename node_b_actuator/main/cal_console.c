/**
 * cal_console.c — UART 콘솔
 *
 * 명령:
 *   c          닫힘 (CLOSE 홀센서 감지 시 자동 정지)
 *   o          열림 (OPEN 홀센서 감지 시 자동 정지)
 *   s          즉시 정지
 *   pos        홀센서 ADC 값 + 커튼 상태 표시
 */
#include <string.h>
#include <stdio.h>
#include "esp_console.h"
#include "esp_log.h"

#include "config.h"
#include "struct_message.h"
#include "motor_ctrl.h"

static const char *TAG = "CAL";

/* c = 닫힘 (CLOSE 홀센서까지) */
static int cmd_c(int argc, char **argv)
{
    printf("<<< 닫힘...\n");
    control_curtain(WINDOW_CLOSE);
    const char *ss[] = {"???","CLOSED","OPEN","MOVING"};
    printf("완료 [%s]\n", ss[g_curtain_state <= 3 ? g_curtain_state : 0]);
    return 0;
}

/* o = 열림 (OPEN 홀센서까지) */
static int cmd_o(int argc, char **argv)
{
    printf(">>> 열림...\n");
    control_curtain(WINDOW_OPEN);
    const char *ss[] = {"???","CLOSED","OPEN","MOVING"};
    printf("완료 [%s]\n", ss[g_curtain_state <= 3 ? g_curtain_state : 0]);
    return 0;
}

/* s = 즉시 정지 */
static int cmd_s(int argc, char **argv)
{
    motor_jog_stop();
    printf("정지\n");
    return 0;
}

/* pos = 홀센서 ADC + 상태 */
static int cmd_pos(int argc, char **argv)
{
    int hc = hall_read_close();
    int ho = hall_read_open();
    const char *sstr[] = {"UNKNOWN", "CLOSED", "OPEN", "MOVING"};
    int si = (g_curtain_state <= 3) ? g_curtain_state : 0;

    printf("CLOSE(34):%d  OPEN(36):%d  state=%s\n", hc, ho, sstr[si]);
    printf("thresh: C>=%d  O>=%d  deb=%d\n",
           HALL_THRESH_CLOSE, HALL_THRESH_OPEN, HALL_DEB_COUNT);
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
        { .command = "c",   .help = "닫힘 (CLOSE센서까지)", .func = &cmd_c },
        { .command = "o",   .help = "열림 (OPEN센서까지)",  .func = &cmd_o },
        { .command = "s",   .help = "즉시 정지",           .func = &cmd_s },
        { .command = "pos", .help = "홀센서 값 + 상태",    .func = &cmd_pos },
    };
    for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    ESP_LOGI(TAG, "콘솔 시작 (c/o/s/pos)");
}
