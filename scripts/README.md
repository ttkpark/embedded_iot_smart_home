# 테스트 스크립트 사용법

각 노드를 **하나씩** 빌드 → 플래시 → 시리얼 모니터까지 실행할 때 사용합니다.

## 전제 조건

- **ESP-IDF 5.x**가 설치되어 있음  
  - 이 PC 기준: `C:\Users\GH\esp\v5.4.2\esp-idf\export.ps1` 로 환경 로드 가능
- 보드가 **COM24**에 연결됨 (다른 포트면 각 .bat 파일 안의 `set PORT=COM24` 를 수정)

## 실행 방법

1. **시작 메뉴**에서 **`ESP-IDF 5.x CMD`** (또는 ESP-IDF 5.x PowerShell) 실행
2. 아래 중 테스트할 노드에 맞는 스크립트 실행

```cmd
cd /d c:\Users\GH\Desktop\embedded_iot_smart_home\scripts

REM Node C (센서 목) 테스트
build_and_test_node_c.bat

REM Node B (액추에이터) 테스트 — 위 종료 후 보드 바꿔 끼운 뒤
build_and_test_node_b.bat

REM Node A (중앙) 테스트 — Wi-Fi 설정 후
build_and_test_node_a.bat
```

## 동작 순서

각 스크립트는 다음을 순서대로 수행합니다.

1. `idf.py set-target esp32`
2. `idf.py build`
3. `idf.py -p COM24 flash monitor`

모니터를 끝낼 때는 **Ctrl + ]** 를 누르세요.

## 포트 변경

다른 COM 포트를 쓰려면 해당 .bat 파일을 열어 `set PORT=COM24` 를 예를 들어 `set PORT=COM3` 처럼 수정하면 됩니다.

---

## PowerShell에서 export.ps1 로드 후 빌드

일반 PowerShell(또는 Cursor 터미널)에서 ESP-IDF 전용 CMD를 쓰지 않으려면, 먼저 환경을 로드한 뒤 빌드할 수 있습니다.

**방법 1 — 한 줄로 환경 로드 후 명령 실행**

```powershell
. C:\Users\GH\esp\v5.4.2\esp-idf\export.ps1
cd c:\Users\GH\Desktop\embedded_iot_smart_home\node_c_sensor_mock
idf.py build
idf.py -p COM24 flash monitor
```

**방법 2 — run_with_idf.ps1 사용**

```powershell
cd c:\Users\GH\Desktop\embedded_iot_smart_home\scripts
.\run_with_idf.ps1 "cd node_c_sensor_mock; idf.py build"
.\run_with_idf.ps1 "cd node_c_sensor_mock; idf.py -p COM24 flash monitor"
```

(위 스크립트는 내부에서 `C:\Users\GH\esp\v5.4.2\esp-idf\export.ps1` 를 로드합니다.)
