/**
 * Node A — Central & Dashboard Node
 * ESP32-WROOM | ESP-IDF v5.x
 *
 * 역할:
 *   - Wi-Fi APSTA 모드: 공유기 연결(웹 서버) + ESP-NOW 동시 구동
 *   - ESP-NOW 수신: Node C 응급 트리거 → Node B 자동 제어 명령 발송
 *   - HTTP 서버: SPIFFS에서 정적 파일 서빙 (index.html/style.css/app.js)
 *   - WebSocket: 브라우저와 실시간 양방향 통신
 */

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "cJSON.h"

#include "struct_message.h"
#include "config.h"

/* ── 상수 ──────────────────────────────────────────────────────────────────*/
static const char *TAG        = "NODE_A";
#define WIFI_MAX_RETRY          5
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1
#define MAX_WS_CLIENTS          4
#define LOG_RING_SIZE           50
#define WS_BUF_SIZE             384

/* ── 이벤트 로그 링 버퍼 ───────────────────────────────────────────────────*/
typedef struct {
    char time_str[10];   /* "HH:MM:SS" */
    char message[120];
    uint8_t level;       /* 0: info  1: warning  2: emergency */
} log_entry_t;

static log_entry_t  g_logs[LOG_RING_SIZE];
static int          g_log_count  = 0;
static int          g_log_head   = 0; /* 다음 쓸 위치 */
static SemaphoreHandle_t g_log_mutex;

/* ── 시스템 상태 ────────────────────────────────────────────────────────────*/
static struct {
    uint8_t patient_stat;
    uint8_t fan_state;
    uint8_t ac_temp;
    uint8_t window_act;
} g_state = {0, 0, 0, 0};

/* ── HTTP 서버 핸들 & MAC ──────────────────────────────────────────────────*/
static httpd_handle_t g_server     = NULL;
static uint8_t        mac_node_b[] = MAC_NODE_B;

/* ── Wi-Fi 이벤트 그룹 ─────────────────────────────────────────────────────*/
static EventGroupHandle_t s_wifi_event_group;
static int                s_retry_num = 0;

/* ═══════════════════════════════════════════════════════════════════════════
 * 유틸리티
 * ═══════════════════════════════════════════════════════════════════════════*/

/** 부팅 후 경과 시간을 "HH:MM:SS" 문자열로 변환 */
static void get_time_str(char *buf, size_t len)
{
    uint64_t us  = esp_timer_get_time();
    uint32_t sec = (uint32_t)(us / 1000000);
    snprintf(buf, len, "%02lu:%02lu:%02lu",
             (unsigned long)(sec / 3600),
             (unsigned long)((sec % 3600) / 60),
             (unsigned long)(sec % 60));
}

/** 이벤트 로그 추가 (링 버퍼, thread-safe) */
static void log_event(const char *message, uint8_t level)
{
    if (xSemaphoreTake(g_log_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        log_entry_t *e = &g_logs[g_log_head];
        get_time_str(e->time_str, sizeof(e->time_str));
        strlcpy(e->message, message, sizeof(e->message));
        e->level    = level;
        g_log_head  = (g_log_head + 1) % LOG_RING_SIZE;
        if (g_log_count < LOG_RING_SIZE) g_log_count++;
        xSemaphoreGive(g_log_mutex);
    }
    ESP_LOGI(TAG, "[LOG] %s", message);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * WebSocket 브로드캐스트 (비동기 — httpd_queue_work 활용)
 * ═══════════════════════════════════════════════════════════════════════════*/

typedef struct {
    httpd_handle_t server;
    char data[WS_BUF_SIZE];
} ws_broadcast_arg_t;

static void ws_broadcast_task(void *arg)
{
    ws_broadcast_arg_t *send_arg = (ws_broadcast_arg_t *)arg;
    size_t clients = MAX_WS_CLIENTS;
    int    fds[MAX_WS_CLIENTS];

    if (httpd_get_client_list(send_arg->server, &clients, fds) == ESP_OK) {
        for (size_t i = 0; i < clients; i++) {
            if (httpd_ws_get_fd_info(send_arg->server, fds[i])
                    == HTTPD_WS_CLIENT_WEBSOCKET) {
                httpd_ws_frame_t pkt = {
                    .payload = (uint8_t *)send_arg->data,
                    .len     = strlen(send_arg->data),
                    .type    = HTTPD_WS_TYPE_TEXT,
                    .final   = true,
                };
                httpd_ws_send_frame_async(send_arg->server, fds[i], &pkt);
            }
        }
    }
    free(send_arg);
}

static void ws_broadcast(const char *json_str)
{
    if (!g_server) return;
    ws_broadcast_arg_t *arg = malloc(sizeof(ws_broadcast_arg_t));
    if (!arg) return;
    arg->server = g_server;
    strlcpy(arg->data, json_str, sizeof(arg->data));
    httpd_queue_work(g_server, ws_broadcast_task, arg);
}

/* 상태 전체를 JSON으로 브로드캐스트 */
static void broadcast_status(void)
{
    char buf[WS_BUF_SIZE];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"status\","
             "\"patient_stat\":%d,\"fan_state\":%d,"
             "\"ac_temp\":%d,\"window_act\":%d}",
             g_state.patient_stat, g_state.fan_state,
             g_state.ac_temp,      g_state.window_act);
    ws_broadcast(buf);
}

/* 로그 하나를 JSON으로 브로드캐스트 */
static void broadcast_log(const char *msg, uint8_t level)
{
    char time_buf[10];
    get_time_str(time_buf, sizeof(time_buf));

    char buf[WS_BUF_SIZE];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"log\",\"time\":\"%s\","
             "\"message\":\"%s\",\"level\":%d}",
             time_buf, msg, level);
    ws_broadcast(buf);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ESP-NOW
 * ═══════════════════════════════════════════════════════════════════════════*/

static void send_command_to_b(uint8_t fan, uint8_t ac, uint8_t window)
{
    struct_message_t cmd = {
        .msg_type   = MSG_COMMAND,
        .node_id    = NODE_A,
        .fan_state  = fan,
        .ac_temp    = ac,
        .window_act = window,
    };
    esp_err_t ret = esp_now_send(mac_node_b, (uint8_t *)&cmd, sizeof(cmd));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_send failed: %s", esp_err_to_name(ret));
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                           const uint8_t *data, int data_len)
{
    if (data_len != sizeof(struct_message_t)) return;

    struct_message_t msg;
    memcpy(&msg, data, sizeof(msg));

    switch (msg.msg_type) {
    case MSG_TRIGGER:
        if (msg.patient_stat == PATIENT_EMERGENCY) {
            /* 응급: 상태 갱신 → 대시보드 경고 → Node B 자동 제어 */
            g_state.patient_stat = PATIENT_EMERGENCY;
            g_state.fan_state    = AUTO_FAN_STATE;
            g_state.ac_temp      = AUTO_AC_TEMP;
            g_state.window_act   = AUTO_WINDOW_ACT;

            char alert_buf[128];
            snprintf(alert_buf, sizeof(alert_buf),
                     "{\"type\":\"alert\",\"alert\":\"EMERGENCY\","
                     "\"patient_stat\":1}");
            ws_broadcast(alert_buf);
            broadcast_status();

            log_event("환자 이상 감지 — 자동 환경 제어 실행", 2);
            broadcast_log("환자 이상 감지 — 자동 환경 제어 실행", 2);

            send_command_to_b(AUTO_FAN_STATE, AUTO_AC_TEMP, AUTO_WINDOW_ACT);
            log_event("Node B 제어 명령 전송 완료", 1);
            broadcast_log("Node B 제어 명령 전송 완료", 1);

        } else {
            /* 정상 복귀 */
            g_state.patient_stat = PATIENT_NORMAL;
            broadcast_status();
            log_event("환자 상태 정상 복귀", 0);
            broadcast_log("환자 상태 정상 복귀", 0);
        }
        break;

    case MSG_KEEPALIVE:
        ESP_LOGD(TAG, "Keep-alive from node %d", msg.node_id);
        break;

    default:
        break;
    }
}

static void espnow_send_cb(const uint8_t *mac_addr,
                           esp_now_send_status_t status)
{
    ESP_LOGD(TAG, "Send to " MACSTR " %s",
             MAC2STR(mac_addr),
             status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

static esp_err_t espnow_init(void)
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    esp_now_peer_info_t peer = {
        .channel = ESPNOW_CHANNEL,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, mac_node_b, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    ESP_LOGI(TAG, "ESP-NOW initialized");
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Wi-Fi
 * ═══════════════════════════════════════════════════════════════════════════*/

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT
               && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Wi-Fi 재연결 시도 %d/%d", s_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP 획득: " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    /* APSTA 모드: 공유기 연결(웹 서버) + ESP-NOW 채널 공존 */
    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi 연결 성공: %s", WIFI_SSID);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Wi-Fi 연결 실패");
    return ESP_FAIL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SPIFFS
 * ═══════════════════════════════════════════════════════════════════════════*/

static esp_err_t spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = NULL,
        .max_files              = 8,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS 마운트 실패: %s", esp_err_to_name(ret));
        return ret;
    }
    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: %u / %u bytes 사용", (unsigned)used, (unsigned)total);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * HTTP 서버 핸들러
 * ═══════════════════════════════════════════════════════════════════════════*/

static esp_err_t serve_file(httpd_req_t *req,
                            const char *path,
                            const char *content_type)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File Not Found");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, content_type);
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, (ssize_t)n) != ESP_OK) break;
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    return serve_file(req, "/spiffs/index.html", "text/html");
}

static esp_err_t css_handler(httpd_req_t *req)
{
    return serve_file(req, "/spiffs/style.css", "text/css");
}

static esp_err_t js_handler(httpd_req_t *req)
{
    return serve_file(req, "/spiffs/app.js", "application/javascript");
}

/* WebSocket 핸들러: 브라우저 → Node A 명령 수신 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* WebSocket 핸드셰이크 — esp_http_server 가 자동 처리 */
        ESP_LOGI(TAG, "WebSocket 클라이언트 연결");
        /* 접속 즉시 현재 상태 전송 */
        broadcast_status();
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = { .type = HTTPD_WS_TYPE_TEXT };

    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK || pkt.len == 0) return ret;

    uint8_t *buf = malloc(pkt.len + 1);
    if (!buf) return ESP_ERR_NO_MEM;

    pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
    if (ret == ESP_OK) {
        buf[pkt.len] = '\0';

        /* JSON 파싱: {"cmd":"fan","value":1} 형태 */
        cJSON *root = cJSON_Parse((char *)buf);
        if (root) {
            cJSON *cmd_j   = cJSON_GetObjectItem(root, "cmd");
            cJSON *value_j = cJSON_GetObjectItem(root, "value");

            if (cJSON_IsString(cmd_j) && cJSON_IsNumber(value_j)) {
                const char *cmd = cmd_j->valuestring;
                int val         = value_j->valueint;

                struct_message_t ctrl = {
                    .msg_type   = MSG_COMMAND,
                    .node_id    = NODE_A,
                    .fan_state  = g_state.fan_state,
                    .ac_temp    = g_state.ac_temp,
                    .window_act = g_state.window_act,
                };

                if (strcmp(cmd, "fan") == 0) {
                    g_state.fan_state = ctrl.fan_state = (uint8_t)val;
                    log_event(val ? "수동 — 환풍기 ON" : "수동 — 환풍기 OFF", 0);
                    broadcast_log(val ? "수동 — 환풍기 ON" : "수동 — 환풍기 OFF", 0);
                } else if (strcmp(cmd, "ac") == 0) {
                    g_state.ac_temp = ctrl.ac_temp = (uint8_t)val;
                    char lmsg[64];
                    snprintf(lmsg, sizeof(lmsg),
                             "수동 — 에어컨 %d°C", val);
                    log_event(lmsg, 0);
                    broadcast_log(lmsg, 0);
                } else if (strcmp(cmd, "window") == 0) {
                    g_state.window_act = ctrl.window_act = (uint8_t)val;
                    const char *wstr[] = {"창문 정지", "창문 열기", "창문 닫기"};
                    char lmsg[64];
                    snprintf(lmsg, sizeof(lmsg), "수동 — %s",
                             val < 3 ? wstr[val] : "창문 ?");
                    log_event(lmsg, 0);
                    broadcast_log(lmsg, 0);
                } else if (strcmp(cmd, "dismiss") == 0) {
                    /* 응급 경고 수동 해제 */
                    g_state.patient_stat = PATIENT_NORMAL;
                    log_event("응급 경고 수동 해제", 1);
                    broadcast_log("응급 경고 수동 해제", 1);
                }

                esp_now_send(mac_node_b, (uint8_t *)&ctrl, sizeof(ctrl));
                broadcast_status();
            }
            cJSON_Delete(root);
        }
    }
    free(buf);
    return ret;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = MAX_WS_CLIENTS + 3;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 서버 시작 실패");
        return NULL;
    }

    httpd_uri_t uris[] = {
        { .uri = "/",          .method = HTTP_GET, .handler = index_handler },
        { .uri = "/style.css", .method = HTTP_GET, .handler = css_handler   },
        { .uri = "/app.js",    .method = HTTP_GET, .handler = js_handler    },
        {
            .uri              = "/ws",
            .method           = HTTP_GET,
            .handler          = ws_handler,
            .is_websocket     = true,
            .handle_ws_control_frames = true,
        },
    };
    for (int i = 0; i < 4; i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }

    ESP_LOGI(TAG, "HTTP 서버 시작");
    return server;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Keep-alive 타이머 (5초 주기)
 * ═══════════════════════════════════════════════════════════════════════════*/

static void keepalive_timer_cb(void *arg)
{
    struct_message_t ka = { .msg_type = MSG_KEEPALIVE, .node_id = NODE_A };
    esp_now_send(mac_node_b, (uint8_t *)&ka, sizeof(ka));
    gpio_set_level(PIN_STATUS_LED, !gpio_get_level(PIN_STATUS_LED));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * app_main
 * ═══════════════════════════════════════════════════════════════════════════*/

void app_main(void)
{
    ESP_LOGI(TAG, "Node A 부팅");

    /* NVS 초기화 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES
        || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 뮤텍스 생성 */
    g_log_mutex = xSemaphoreCreateMutex();

    /* Status LED */
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << PIN_STATUS_LED),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_cfg);
    gpio_set_level(PIN_STATUS_LED, 1);

    /* 본인 MAC 출력 (config.h에 기입하기 위해) */
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "MAC: " MACSTR, MAC2STR(mac));

    /* Wi-Fi → ESP-NOW → SPIFFS → HTTP 서버 순서 준수 */
    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(espnow_init());
    ESP_ERROR_CHECK(spiffs_init());

    g_server = start_webserver();
    if (!g_server) {
        ESP_LOGE(TAG, "웹 서버 시작 실패 — 재부팅");
        esp_restart();
    }

    log_event("시스템 부팅 완료 — ESP-NOW 네트워크 준비", 0);

    /* Keep-alive 타이머 (5초 주기) */
    esp_timer_handle_t ka_timer;
    esp_timer_create_args_t ka_args = {
        .callback = keepalive_timer_cb,
        .name     = "keepalive",
    };
    esp_timer_create(&ka_args, &ka_timer);
    esp_timer_start_periodic(ka_timer, 5000000); /* 5s */

    ESP_LOGI(TAG, "초기화 완료 — 대기 중");

    /* 메인 루프: 상태 변화 시 브로드캐스트 (필요 시 확장) */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
