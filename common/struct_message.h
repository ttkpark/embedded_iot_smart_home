#pragma once
#include <stdint.h>

/**
 * ESP-NOW 공통 페이로드 구조체 (6 Bytes)
 * 모든 노드 공유 — 수정 시 전체 노드 재컴파일 필요
 */
typedef struct {
    uint8_t msg_type;     /* 0: keep-alive  1: 센서 트리거  2: 제어 명령 */
    uint8_t node_id;      /* 1: Node A  2: Node B  3: Node C             */
    uint8_t patient_stat; /* 0: 정상  1: 응급                             */
    uint8_t fan_state;    /* 0: OFF  1: ON                                */
    uint8_t ac_temp;      /* 0: OFF  18~30: 설정 온도(°C)                 */
    uint8_t window_act;   /* 0: 정지  1: 열림  2: 닫힘                    */
} __attribute__((packed)) struct_message_t;

/* msg_type */
#define MSG_KEEPALIVE   0
#define MSG_TRIGGER     1
#define MSG_COMMAND     2

/* node_id */
#define NODE_A  1
#define NODE_B  2
#define NODE_C  3

/* patient_stat */
#define PATIENT_NORMAL     0
#define PATIENT_EMERGENCY  1

/* fan_state */
#define FAN_OFF  0
#define FAN_ON   1

/* ac_temp: 0 = OFF, 18~30 = 설정 온도 */
#define AC_OFF  0

/* window_act */
#define WINDOW_STOP   0
#define WINDOW_OPEN   1
#define WINDOW_CLOSE  2
