# Embedded IoT Smart Home — 프로젝트 가이드

## COM 포트 매핑 (USB 시리얼)

| 노드 | 보드 | COM 포트 | 역할 |
|------|------|----------|------|
| Node A (Central) | ESP32-WROOM | **COM6** | 중앙 허브, 웹서버, ESP-NOW 중계 |
| Node B (Actuator) | ESP32-LoRa | **COM24** | 환풍기 릴레이, 듀얼 커튼 모터, 리밋 스위치 |
| Node C (Sensor Mock) | ESP32-LoRa | **COM9** | 버튼/자동 트리거 → Node A 전송 |

## 빌드 & 플래시 명령

ESP-IDF 환경 활성화 후 각 노드 디렉터리에서:
```
idf.py build
idf.py -p COM## flash
idf.py -p COM## monitor
```

## ESP-NOW 통신 흐름

```
Node C (센서/트리거) --[ESP-NOW]--> Node A (중앙) --[ESP-NOW]--> Node B (액추에이터)
                                        |
                                   WebSocket/HTTP
                                        |
                                   웹 대시보드 / 앱
```

## Wi-Fi 설정

- SSID: `Smart Meeting`
- ESPNOW_CHANNEL: 공유기 채널과 일치 필요 (config.h에서 설정)
