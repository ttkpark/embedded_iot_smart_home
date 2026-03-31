/**
 * ble_gatt.c — BLE GATT 서버 (NimBLE)
 *
 * Service:  0x00FF
 *   - Status characteristic (Notify+Read): Node A → 앱  (JSON)
 *   - Command characteristic (Write):      앱 → Node A  (JSON: {"cmd":"...","value":N})
 *
 * 기존 WebSocket과 동일한 JSON 포맷 사용 → Flutter 앱에서 파싱 코드 공유
 */
#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble_gatt.h"

static const char *TAG = "BLE";

/* ── UUID 정의 ─────────────────────────────────────────────────────────────── */
/* Service: 0x00FF */
static const ble_uuid16_t svc_uuid = BLE_UUID16_INIT(0x00FF);
/* Status (Notify + Read): 0xFF01 */
static const ble_uuid16_t chr_status_uuid = BLE_UUID16_INIT(0xFF01);
/* Command (Write): 0xFF02 */
static const ble_uuid16_t chr_cmd_uuid = BLE_UUID16_INIT(0xFF02);

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_status_val_handle;
static bool     s_notify_enabled = false;

/* 마지막 상태 JSON (BLE 읽기용) */
static char s_last_status[256] = "{}";

/* Node A 명령 처리 함수 (main.c에서 제공) */
extern void handle_ble_command(const char *json, int len);

/* ── GATT 콜백 ─────────────────────────────────────────────────────────────── */

static int chr_status_access(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int len = strlen(s_last_status);
        os_mbuf_append(ctxt->om, s_last_status, len);
    }
    return 0;
}

static int chr_cmd_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0 && len < 200) {
            char buf[200];
            os_mbuf_copydata(ctxt->om, 0, len, buf);
            buf[len] = '\0';
            ESP_LOGI(TAG, "BLE CMD: %s", buf);
            handle_ble_command(buf, len);
        }
    }
    return 0;
}

/* ── GATT 서비스 정의 ──────────────────────────────────────────────────────── */

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {   /* Status: Notify + Read */
                .uuid = &chr_status_uuid.u,
                .access_cb = chr_status_access,
                .val_handle = &s_status_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {   /* Command: Write */
                .uuid = &chr_cmd_uuid.u,
                .access_cb = chr_cmd_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 }, /* sentinel */
        },
    },
    { 0 }, /* sentinel */
};

/* ── GAP 이벤트 ────────────────────────────────────────────────────────────── */

static void start_advertise(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "BLE 연결됨 (handle=%d)", s_conn_handle);
        } else {
            start_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE 연결 해제");
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;
        start_advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_status_val_handle) {
            s_notify_enabled = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "BLE Notify %s", s_notify_enabled ? "ON" : "OFF");
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "BLE MTU=%d", event->mtu.value);
        break;

    default:
        break;
    }
    return 0;
}

static void start_advertise(void)
{
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"SmartHome-A";
    fields.name_len = strlen("SmartHome-A");
    fields.name_is_complete = 1;
    fields.uuids16 = (ble_uuid16_t[]){ BLE_UUID16_INIT(0x00FF) };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    ble_gap_adv_set_fields(&fields);
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &adv_params, gap_event_cb, NULL);
    ESP_LOGI(TAG, "BLE Advertising 시작 (SmartHome-A)");
}

/* ── NimBLE Host 태스크 ────────────────────────────────────────────────────── */

static void ble_on_sync(void)
{
    ble_hs_id_infer_auto(0, NULL);
    start_advertise();
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ── 공개 API ──────────────────────────────────────────────────────────────── */

void ble_notify_json(const char *json)
{
    /* 마지막 상태 저장 (BLE 읽기용) */
    strncpy(s_last_status, json, sizeof(s_last_status) - 1);
    s_last_status[sizeof(s_last_status) - 1] = '\0';

    if (!s_notify_enabled || s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        return;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(json, strlen(json));
    if (om) {
        ble_gatts_notify_custom(s_conn_handle, s_status_val_handle, om);
    }
}

void ble_gatt_init(void)
{
    ESP_ERROR_CHECK(nimble_port_init());

    ble_svc_gap_device_name_set("SmartHome-A");
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE GATT 서버 초기화 완료");
}
