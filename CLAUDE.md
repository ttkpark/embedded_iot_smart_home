# Embedded IoT Smart Home — 프로젝트 가이드

## COM 포트 매핑 (USB 시리얼)

| 노드 | 보드 | COM 포트 | 칩셋 | 역할 |
|------|------|----------|------|------|
| Node A (Central) | ESP32-WROOM | **COM6** | CH340 | 중앙 허브, 웹서버, BLE GATT, ESP-NOW 중계 |
| Node B (Actuator) | ESP32-LoRa | **COM4** | CP210x | 환풍기 릴레이, 듀얼 커튼 모터, 홀센서 |
| Node C (Sensor Mock) | ESP32-LoRa | **COM9** | CH340 | 버튼/자동 트리거 → Node A 전송 |

## 빌드 환경 (새 컴퓨터)

- ESP-IDF: `C:\Users\parkg\esp\v5.4.2\esp-idf`
- 도구: `C:\Espressif`
- Python: `C:\Espressif\python_env\idf5.4_py3.13_env`
- **한글 경로 문제**: 프로젝트가 `E:\바탕화면\...`에 있어서 ccache/assembler 충돌 발생
  - 해결: `C:\iot_build`에 ASCII 복사본 생성 후 빌드
  - `PYTHONUTF8=1`, `CCACHE_DISABLE=1` 환경변수 필수
  - `MSYSTEM` 환경변수 제거 필요 (Git Bash에서 빌드 시)
- Flutter APK도 `C:\tmp_flutter_build`에서 빌드

## 빌드 & 플래시 명령

ESP-IDF CMD 터미널에서:
```
set PYTHONUTF8=1
cd /d "E:\바탕화면\archived project\embedded_iot_smart_home\node_X"
idf.py build
idf.py -p COMX flash
idf.py -p COMX monitor
```

## 통신 흐름

```
Node C (센서/트리거) --[ESP-NOW]--> Node A (중앙) --[ESP-NOW]--> Node B (액추에이터)
                                        |
                                   WebSocket/HTTP (Wi-Fi)
                                   BLE GATT (Wi-Fi 없이)
                                        |
                                   Flutter 앱 / 웹 대시보드
```

## Node B 모터 제어 아키텍처

### 하드웨어
- **L 모터 (GPIO23)**: 캐터필러 벨트 — 커튼 직선 이동
- **R 모터 (GPIO27)**: 스풀 와인딩
- **L 홀센서 (GPIO36/VP)**: ADC1_CH0, WSH135-XPAN2
- **R 홀센서 (GPIO39/VN)**: ADC1_CH3, WSH135-XPAN2
- **환풍기 릴레이 (GPIO25)**: SSR, Active High
- **리밋 스위치 (GPIO17)**: Active LOW, 내부 풀업

### 홀센서 엣지 검출 (히스테리시스 상태 머신)
- **진입**: N회 연속 THRESH_LO 이하 → 자석 확인 + 엣지 카운트
- **이탈**: N회 연속 THRESH_HI 이상 → 자석 벗어남
- **엣지 락아웃**: 최소 간격 미만 엣지 무시 (더블카운트 방지)
- L/R 각각 별도 threshold (L이 더 개방적)

### 모터 제어 모델
- **주 정지 기준**: 홀센서 엣지 카운트 (각 모터 독립 정지)
- **폴백**: 시간 기반 타임아웃 (ms_per_rev × 바퀴수 × 1.5)
- **캘리브레이션**: 엣지 감지마다 ms_per_rev을 alpha=0.05로 미세 보정
- **NVS 저장**: 위치(x_pos), travel, ms_per_rev_l/r, PWM 값

### 콘솔 명령어 (Node B UART)
| 명령 | 설명 |
|------|------|
| `cal start` | 캘리브레이션 모드 진입 |
| `cal set_start` | 현재 위치 = 닫힘 원점 |
| `cal set_end` | 현재 위치 = 열림 끝점 |
| `cal save` | 저장 후 일반 모드 복귀 |
| `cal status` | 캘리브레이션 값 표시 |
| `f` | 열림 방향 2바퀴 (양쪽 동시) |
| `r` | 닫힘 방향 2바퀴 |
| `s` | 긴급 정지 |
| `t <N>` | 커튼 길이 설정 (예: `t 2.5`) |
| `pos` | 홀센서/위치/RPM 표시 |

## Node A BLE GATT

- 디바이스 이름: `SmartHome-A`
- Service UUID: `0x00FF`
- Status Characteristic (Notify+Read): `0xFF01` — JSON 상태 전송
- Command Characteristic (Write): `0xFF02` — JSON 명령 수신
- WebSocket과 동일한 JSON 포맷 사용

## Flutter 앱

- **Wi-Fi**: WebSocket으로 Node A 연결 (기존)
- **BLE**: `flutter_blue_plus`로 SmartHome-A 연결 (Wi-Fi 없을 때)
- 앱바에 블루투스 아이콘 탭 → BLE 스캔+연결
- 두 연결 모두 활성이면 양쪽에 명령 전송
- APK 빌드: `C:\tmp_flutter_build`에서 수행

## Wi-Fi 설정

- SSID 목록: `Smart Meeting`, `junespark-zone2` (config.h)
- ESPNOW_CHANNEL: 공유기 채널과 일치 필요
- Wi-Fi 실패 시: ESP-NOW 전용 모드 (채널 1 고정)
