# 환경 테스트 가이드

이 문서는 **개발 환경 검증**부터 **3노드 통합 동작 확인**까지 단계별로 테스트하는 방법을 정리합니다.

---

## 목차

1. [사전 준비물](#1-사전-준비물)
2. [ESP-IDF 환경 확인](#2-esp-idf-환경-확인)
3. [빌드만 테스트 (보드 없이)](#3-빌드만-테스트-보드-없이)
4. [노드별 플래시 & 시리얼 테스트](#4-노드별-플래시--시리얼-테스트)
5. [Node A SPIFFS 웹 파일 업로드](#5-node-a-spiffs-웹-파일-업로드)
6. [통합 테스트 (3노드 + 대시보드)](#6-통합-테스트-3노드--대시보드)
7. [문제 해결 (트러블슈팅)](#7-문제-해결-트러블슈팅)

---

## 1. 사전 준비물

| 항목 | 내용 |
|------|------|
| **ESP-IDF** | v5.0 이상 (공식 설치 가이드: https://docs.espressif.com/projects/esp-idf/) |
| **USB 케이블** | ESP32 보드 3개 연결용 (데이터 전송 지원) |
| **보드** | Node A: ESP32-WROOM 1개, Node B/C: ESP32-LoRa 2개 |
| **공유기** | Node A가 연결할 Wi-Fi (SSID/비밀번호 + **채널 번호** 확인 가능해야 함) |
| **터미널** | ESP-IDF 터미널(PowerShell 또는 CMD) — `idf.py` 사용 가능한 환경 |

### Windows에서 ESP-IDF 터미널 열기

- **ESP-IDF 5.x 설치 시:** 시작 메뉴에서 `ESP-IDF 5.x CMD` 또는 `ESP-IDF 5.x PowerShell` 실행
- 또는 수동: `%IDF_PATH%\export.bat` 실행 후 같은 창에서 작업

---

## 2. ESP-IDF 환경 확인

아래 명령을 **ESP-IDF가 활성화된 터미널**에서 실행합니다.

```bash
# 버전 확인 (v5.0 이상이어야 함)
idf.py --version

# ESP32 타겟 선택 확인
idf.py --list-targets
# 출력에 esp32 가 있어야 함

# 환경 변수 확인 (Windows CMD)
echo %IDF_PATH%
# 경로가 출력되면 정상 (예: C:\Espressif\frameworks\esp-idf-v5.x)
```

| 확인 항목 | 예상 결과 |
|-----------|-----------|
| `idf.py --version` | `ESP-IDF v5.x.x` 등 |
| `%IDF_PATH%` | ESP-IDF 설치 경로 |
| `idf.py --list-targets` | `esp32` 포함 |

---

## 3. 빌드만 테스트 (보드 없이)

보드 없이 **컴파일만** 성공하는지 확인합니다.  
각 노드 폴더에서 순서대로 실행하세요.

```bash
# 프로젝트 루트로 이동
cd c:\Users\GH\Desktop\embedded_iot_smart_home

# Node C (의존성 가장 적음)
cd node_c_sensor_mock
idf.py set-target esp32
idf.py build
cd ..

# Node B
cd node_b_actuator
idf.py set-target esp32
idf.py build
cd ..

# Node A (HTTP 서버·SPIFFS 등 의존성 많음)
cd node_a_central
idf.py set-target esp32
idf.py build
cd ..
```

**성공 시:** 각 노드에서 `Project build complete` 메시지가 나오고,  
`build/` 폴더에 `*.bin` 파일이 생성됩니다.

**실패 시:** [7. 문제 해결](#7-문제-해결-트러블슈팅) 참고.

---

## 4. 노드별 플래시 & 시리얼 테스트

USB로 **한 번에 한 보드만** 연결한 상태에서 진행합니다.  
포트는 환경에 따라 `COM3`, `COM4` 등으로 바뀝니다.

### 4-1. Node C (센서 목) — MAC 주소 확인용

1. Node C 보드만 USB 연결
2. 터미널에서:

```bash
cd node_c_sensor_mock
idf.py -p COM3 flash monitor
```

3. 시리얼 모니터(115200 baud)에서 다음을 확인:
   - `Node C 부팅`
   - `MAC: XX:XX:XX:XX:XX:XX` ← **이 값을 메모**
4. `Ctrl+]` 로 모니터 종료

### 4-2. Node B (액추에이터) — MAC 주소 확인용

1. Node C 분리 후 Node B만 USB 연결
2. 터미널에서:

```bash
cd node_b_actuator
idf.py -p COM3 flash monitor
```

3. 시리얼에서 `MAC: XX:XX:XX:XX:XX:XX` 확인 후 **메모**
4. `Ctrl+]` 로 종료

### 4-3. Node A (중앙) — Wi-Fi & MAC 확인

1. **먼저 `common/config.h` 수정:**
   - `WIFI_SSID` / `WIFI_PASSWORD`: 사용할 공유기 정보
   - `ESPNOW_CHANNEL`: 공유기 Wi-Fi 채널 (1~13, 공유기 관리 페이지에서 확인)

2. Node B 분리 후 Node A만 USB 연결
3. 터미널에서:

```bash
cd node_a_central
idf.py -p COM3 flash monitor
```

4. 시리얼에서 확인:
   - `Wi-Fi 연결 성공`
   - `IP 획득: 192.168.x.x` ← **이 IP 메모** (대시보드 접속용)
   - `MAC: XX:XX:XX:XX:XX:XX` ← **메모**

### 4-4. MAC 주소를 config.h에 반영

세 노드의 MAC을 모두 확인한 뒤, **`common/config.h`** 에 넣습니다.

```c
// 예시 (실제 출력값으로 교체)
#define MAC_NODE_A  {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x01}  // Node A 시리얼 출력
#define MAC_NODE_B  {0xAA, 0xBB, 0xCC, 0xDD, 0x22, 0x02}  // Node B 시리얼 출력
#define MAC_NODE_C  {0xAA, 0xBB, 0xCC, 0xDD, 0x33, 0x03}  // Node C 시리얼 출력
```

저장 후 **Node A, B, C를 각각 한 번씩 다시 빌드 & 플래시**해야 ESP-NOW 통신이 동작합니다.

---

## 5. Node A SPIFFS 웹 파일 업로드

대시보드(HTML/CSS/JS)를 SPIFFS 파티션에 넣는 단계입니다.

### 5-1. SPIFFS 이미지 생성 (ESP-IDF 기본 도구)

Node A는 **SPIFFS** 파티션을 사용합니다. `spiffs_data` 폴더 내용을 이미지로 만들려면 ESP-IDF의 `spiffsgen.py`를 사용합니다.

```bash
cd node_a_central

# SPIFFS 이미지 생성 (파티션 크기에 맞춤 — partitions.csv 기준 0x200000 = 2MB)
python %IDF_PATH%/components/spiffs/spiffsgen.py 0x200000 spiffs_data build/spiffs.bin

# 이미지 플래시 (파티션 테이블에서 spiffs 오프셋 확인: 0x200000)
python -m esptool --chip esp32 --port COM3 write_flash 0x200000 build/spiffs.bin
```

- **Windows PowerShell** 에서 `%IDF_PATH%`가 동작하지 않으면, `$env:IDF_PATH` 사용  
  예: `python $env:IDF_PATH/components/spiffs/spiffsgen.py 0x200000 spiffs_data build/spiffs.bin`
- 포트 `COM3`는 실제 연결된 포트로 변경

### 5-2. 대시보드 접속 확인

1. Node A 전원 연결 유지
2. PC/스마트폰을 **같은 공유기**에 연결
3. 브라우저에서 `http://[Node_A_IP]` 접속 (예: `http://192.168.0.10`)
4. 다음 확인:
   - SmartCare+ 로고와 다크 테마 화면
   - 우측 상단 WebSocket 상태가 **Online**으로 표시
   - 하단 이벤트 로그에 `시스템 부팅 완료` 등 로그 출력

---

## 6. 통합 테스트 (3노드 + 대시보드)

모든 노드에 올바른 MAC이 들어간 상태에서 진행합니다.

### 6-1. 전원 및 연결 순서

1. **Node A** 전원 (USB 또는 5V) — Wi-Fi 연결 대기
2. **Node B** 전원
3. **Node C** 전원
4. PC에서 `http://[Node_A_IP]` 로 대시보드 접속

### 6-2. 테스트 시나리오

| 순서 | 동작 | 기대 결과 |
|------|------|-----------|
| 1 | Node C **응급 버튼** 누름 | 대시보드 화면 테두리 붉은 점멸, 응급 배너 표시 |
| 2 | | 이벤트 로그에 "환자 이상 감지 — 자동 환경 제어 실행" 등 출력 |
| 3 | | 좌측 패널 환자 상태가 "● 응급" (빨간색)으로 변경 |
| 4 | Node C **정상 버튼** 누름 | 경고 배너 사라짐, 환자 상태 "● 정상" (녹색) 복귀 |
| 5 | 대시보드에서 **환풍기 토글** ON | Node B에서 릴레이 동작 (연결된 경우 LED/팬 등으로 확인) |
| 6 | 대시보드에서 **에어컨 슬라이더** 조절 | Node B에서 IR LED 신호 출력 (IR 리시버로 확인 가능) |
| 7 | 대시보드에서 **창문 열기/닫기** 클릭 | Node B에서 모터 구동 (리니어 액추에이터 연결 시) |

### 6-3. 하드웨어 없이 확인할 수 있는 것

- Node A: Wi-Fi 연결, 시리얼 로그, 대시보드 접속, WebSocket 연결
- Node B: 시리얼에 "명령 수신 — fan:x ac:x window:x" 로그
- Node C: 버튼 누를 때 시리얼에 "트리거 전송 — 응급/정상" 로그

릴레이/모터/IR 없이도 **시리얼 로그**만으로 3노드 간 ESP-NOW·대시보드 연동은 검증 가능합니다.

---

## 7. 문제 해결 (트러블슈팅)

| 증상 | 확인 사항 | 조치 |
|------|-----------|------|
| `idf.py`를 찾을 수 없음 | ESP-IDF 터미널 사용 여부 | `ESP-IDF 5.x CMD` 또는 `export.bat` 실행 후 재시도 |
| `set-target esp32` 실패 | IDF_PATH, Python 경로 | `echo %IDF_PATH%`, `python --version` 확인 |
| Node A 빌드 시 `spiffs` 관련 오류 | partitions.csv 경로 | `sdkconfig.defaults`의 `PARTITION_TABLE_CUSTOM_FILENAME`이 `partitions.csv`인지 확인 |
| Wi-Fi 연결 실패 (Node A) | SSID/비밀번호, 채널 | config.h의 `WIFI_SSID`, `WIFI_PASSWORD` 확인. 5GHz 전용 AP면 2.4GHz AP 사용 |
| 대시보드 접속 안 됨 | IP, 방화벽 | 같은 공유기인지, 브라우저에서 `http://IP` (https 아님) |
| WebSocket 연결 끊김 | Node A 재부팅, 라우터 | Node A 시리얼에서 오류 로그 확인 |
| Node B/C가 명령을 받지 못함 | MAC 주소, 채널 | common/config.h의 MAC이 각 보드 시리얼 출력과 일치하는지, ESPNOW_CHANNEL이 공유기 채널과 같은지 확인 |
| Node C 버튼 무반응 | GPIO 32/33 배선 | 내부 풀업 사용 중이면 버튼은 GND와 연결 (누르면 LOW) |

---

## 요약 체크리스트

- [ ] ESP-IDF v5 설치 및 `idf.py --version` 확인
- [ ] Node C → Node B → Node A 순서로 `idf.py build` 성공
- [ ] Node C, B, A 각각 플래시 후 시리얼에서 **MAC 주소** 확인
- [ ] `common/config.h`에 Wi-Fi(SSID/비밀번호/채널) 및 **MAC_NODE_A/B/C** 기입
- [ ] Node A에 SPIFFS 이미지 플래시 (spiffs_data → build/spiffs.bin → 0x200000)
- [ ] 브라우저에서 `http://[Node_A_IP]` 접속, 대시보드 및 WebSocket 동작 확인
- [ ] Node C 응급/정상 버튼 → 대시보드 경고/복귀 확인
- [ ] 대시보드 수동 제어 → Node B 시리얼 로그 또는 실제 액추에이터 동작 확인

위 항목을 순서대로 진행하면 환경 테스트와 기본 통합 동작까지 검증할 수 있습니다.
