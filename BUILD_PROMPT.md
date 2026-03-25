# ESP-IDF 빌드 지시용 프롬프트

이 문서는 **AI 에이전트** 또는 **자동화 스크립트**에게 ESP-IDF 프로젝트 빌드를 지시할 때 전달할 **맥락(프롬프트)** 입니다.  
복사하여 채팅/스크립트에 붙여 넣으면 됩니다.

---

## 프롬프트 (복사용)

```
당신은 ESP-IDF v5.x 환경에서 멀티 프로젝트 워크스페이스를 빌드하는 작업을 수행합니다.
ld
────────────────────────────────────────────────────────────────
## 1. 워크스페이스 구조
────────────────────────────────────────────────────────────────

- 프로젝트 루트: embedded_iot_smart_home (이 폴더가 작업 디렉터리 기준입니다)
- ESP-IDF 프로젝트는 3개이며, 각각 독립적인 idf.py 빌드 대상입니다.
- 공통 헤더는 프로젝트 외부의 common/ 폴더에 있으며, 각 프로젝트의 main/CMakeLists.txt에서 INCLUDE_DIRS "." "../../common" 로 참조합니다.

디렉터리 구조:

  embedded_iot_smart_home/
  ├── common/
  │   ├── config.h           # Wi-Fi, MAC 주소, GPIO 핀 번호 (모든 노드 공유)
  │   └── struct_message.h   # ESP-NOW 페이로드 구조체 (모든 노드 공유)
  ├── node_a_central/        # ESP32-WROOM — 중앙·웹서버·ESP-NOW 중계
  │   ├── CMakeLists.txt
  │   ├── sdkconfig.defaults
  │   ├── partitions.csv     # app0 + spiffs 파티션 (Node A 전용)
  │   ├── main/
  │   │   ├── CMakeLists.txt
  │   │   └── main.c
  │   └── spiffs_data/       # 웹 대시보드 파일 (빌드 후 별도 SPIFFS 이미지 생성용)
  ├── node_b_actuator/       # ESP32-LoRa — 액추에이터(릴레이/IR/SG90-HV Continuous)
  │   ├── CMakeLists.txt
  │   ├── sdkconfig.defaults
  │   └── main/
  │       ├── CMakeLists.txt
  │       └── main.c
  └── node_c_sensor_mock/    # ESP32-LoRa — 버튼 입력·ESP-NOW 트리거
      ├── CMakeLists.txt
      ├── sdkconfig.defaults
      └── main/
          ├── CMakeLists.txt
          └── main.c

- 빌드 시 반드시 각 프로젝트 폴더(node_a_central, node_b_actuator, node_c_sensor_mock)를 현재 작업 디렉터리로 한 상태에서 idf.py를 실행해야 합니다. (상위에서 idf.py -C node_xxx build 형태도 가능)

────────────────────────────────────────────────────────────────
## 2. 빌드 환경 전제 조건
────────────────────────────────────────────────────────────────

- ESP-IDF v5.0 이상이 설치되어 있고, 현재 셸에서 idf.py를 실행할 수 있어야 합니다.
  (Windows: ESP-IDF 5.x CMD 또는 PowerShell에서 export.bat 실행 후 사용)
- 타겟 칩: esp32 (모든 노드 동일). set-target은 최초 1회 또는 sdkconfig 삭제 후에만 필요합니다.
- common/config.h 는 수정하지 않고 그대로 두고 빌드만 수행합니다. (MAC 주소·Wi-Fi 설정은 사용자가 별도 편집)

────────────────────────────────────────────────────────────────
## 3. 빌드 순서 및 명령
────────────────────────────────────────────────────────────────

다음 순서로 각 프로젝트를 빌드하세요. 실패 시 해당 단계에서 중단하고 오류 메시지를 보고합니다.

(1) node_c_sensor_mock
    - 이동: cd node_c_sensor_mock
    - 타겟 설정(최초 1회): idf.py set-target esp32
    - 빌드: idf.py build
    - 성공 시 build/node_c_sensor_mock.bin 등이 생성됨

(2) node_b_actuator
    - 이동: cd node_b_actuator (또는 프로젝트 루트에서 cd node_b_actuator)
    - 타겟 설정(최초 1회): idf.py set-target esp32
    - 빌드: idf.py build
    - 성공 시 build/node_b_actuator.bin 등이 생성됨

(3) node_a_central
    - 이동: cd node_a_central
    - 타겟 설정(최초 1회): idf.py set-target esp32
    - 빌드: idf.py build
    - sdkconfig.defaults에 CONFIG_PARTITION_TABLE_CUSTOM=y 등이 있으므로, 기본 파티션 대신 partitions.csv가 사용됩니다.
    - 성공 시 build/node_a_central.bin, build/flash_args 등이 생성됨. SPIFFS 이미지는 이 단계에서 자동 생성되지 않으며, 별도 spiffsgen.py 등으로 생성합니다.

────────────────────────────────────────────────────────────────
## 4. 한 번에 실행할 수 있는 명령 시퀀스 (복사용)
────────────────────────────────────────────────────────────────

아래는 프로젝트 루트(embedded_iot_smart_home)를 기준으로 한 Bash/PowerShell 유사 순차 실행 예시입니다. 경로는 실제 워크스페이스 경로로 치환하세요.

  cd <프로젝트_루트_경로>

  cd node_c_sensor_mock && idf.py set-target esp32 && idf.py build && cd ..
  cd node_b_actuator    && idf.py set-target esp32 && idf.py build && cd ..
  cd node_a_central     && idf.py set-target esp32 && idf.py build && cd ..

(이미 set-target을 한 적이 있으면 set-target 단계는 생략 가능합니다. 생략 시: idf.py build 만 실행)

────────────────────────────────────────────────────────────────
## 5. 성공 기준
────────────────────────────────────────────────────────────────

- 각 노드에서 idf.py build 종료 시 "Project build complete" 메시지가 출력되어야 합니다.
- 각 프로젝트의 build/ 디렉터리에 .bin, .elf, flash_args 등이 생성되어야 합니다.
- 컴파일 오류(undefined reference, No such file 등)가 없어야 합니다.
- common/config.h 또는 common/struct_message.h 를 찾을 수 없다는 오류가 나오면, 해당 프로젝트의 main/CMakeLists.txt에 INCLUDE_DIRS "." "../../common" 이 포함되어 있는지 확인합니다.

────────────────────────────────────────────────────────────────
## 6. 출력 요청
────────────────────────────────────────────────────────────────

- 위 순서대로 3개 프로젝트를 빌드한 뒤, 각 프로젝트별 빌드 성공 여부와 생성된 이미지 경로(예: build/*.bin)를 요약해서 알려주세요.
- 실패한 경우: 마지막 오류 로그의 관련 부분을 인용하고, 가능하면 원인과 수정 제안을 해주세요.
```

---

## 활용 방법

| 용도 | 사용 방법 |
|------|-----------|
| AI에게 빌드 실행 요청 | 위 "프롬프트 (복사용)" 블록 전체를 복사해 채팅에 붙여넣기 |
| 빌드 순서만 전달 | "## 3. 빌드 순서 및 명령" 또는 "## 4. 한 번에 실행할 수 있는 명령 시퀀스"만 복사 |
| CI/스크립트용 | "## 4"의 명령을 셸 스크립트(.sh / .ps1)로 감싸서 사용 |

---

## Windows 경로 예시

프로젝트 루트가 `c:\Users\GH\Desktop\embedded_iot_smart_home` 인 경우:

```powershell
cd c:\Users\GH\Desktop\embedded_iot_smart_home
cd node_c_sensor_mock; idf.py set-target esp32; idf.py build; cd ..
cd node_b_actuator;    idf.py set-target esp32; idf.py build; cd ..
cd node_a_central;     idf.py set-target esp32; idf.py build; cd ..
```

(set-target은 이미 되어 있으면 `idf.py build` 만 반복해도 됩니다.)
