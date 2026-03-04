# AI 프롬프트 — SmartCare+ 대시보드 웹 파일 완성 요청

> 이 문서는 `node_a_central/spiffs_data/` 폴더에 업로드될
> 웹 대시보드 파일(index.html / style.css / app.js)의 완성 구현을
> AI 에이전트 또는 외부 개발자에게 위임하기 위한 프롬프트입니다.

---

## 프롬프트 (복사 후 AI에게 전달)

```
당신은 임베디드 IoT 시스템의 웹 프론트엔드를 구현하는 전문 개발자입니다.
아래 조건에 맞게 index.html / style.css / app.js 3개 파일을 완성해주세요.

─────────────────────────────────────────────
## 1. 실행 환경 제약 (반드시 준수)
─────────────────────────────────────────────

- 이 파일들은 ESP32(ESP-IDF v5.x)의 SPIFFS 파티션에 저장되어
  esp_http_server 로 서빙됩니다.
- SPIFFS 가용 공간: 약 2MB. 3개 파일 합산 100KB 이하를 목표로 합니다.
- 외부 CDN(Google Fonts, Bootstrap, Chart.js 등) 절대 사용 금지.
  모든 스타일과 스크립트는 style.css / app.js 파일 내에 자급합니다.
- JavaScript 프레임워크(React, Vue 등) 사용 금지. 순수 Vanilla JS만 허용.
- WebSocket 경로: ws://[ESP32_IP]/ws  (포트 80, 별도 설정 없음)
- HTTP 경로:  /  → index.html
              /style.css → style.css
              /app.js    → app.js

─────────────────────────────────────────────
## 2. WebSocket 통신 규격 (JSON)
─────────────────────────────────────────────

### 2-A. 서버(ESP32) → 브라우저 수신 메시지

```json
// 상태 전체 업데이트 (접속 시 1회 + 상태 변경마다)
{
  "type": "status",
  "patient_stat": 0,   // 0: 정상, 1: 응급
  "fan_state": 0,      // 0: OFF, 1: ON
  "ac_temp": 0,        // 0: OFF, 18~30: 설정온도(°C)
  "window_act": 0      // 0: 정지, 1: 열림, 2: 닫힘
}

// 응급 경고 (즉시 화면 점멸 트리거)
{
  "type": "alert",
  "alert": "EMERGENCY",
  "patient_stat": 1
}

// 이벤트 로그 1건 추가
{
  "type": "log",
  "time": "14:22:15",
  "message": "환자 이상 감지 — 자동 환경 제어 실행",
  "level": 2           // 0: info, 1: warning, 2: emergency
}
```

### 2-B. 브라우저 → 서버 송신 메시지

```json
{"cmd": "fan",     "value": 1}        // 환풍기 ON(1) / OFF(0)
{"cmd": "ac",      "value": 22}       // 에어컨 온도(18~30), 0=OFF
{"cmd": "window",  "value": 1}        // 열기(1) / 닫기(2) / 정지(0)
{"cmd": "dismiss", "value": 1}        // 응급 경고 수동 해제
```

─────────────────────────────────────────────
## 3. UI/UX 요구사항
─────────────────────────────────────────────

### 3-1. 전체 테마
- 다크 모드 전용 (라이트 모드 불필요)
- 배경: #1E1E1E (메인), #2A2A2A (카드), #333333 (내부 요소)
- 색상 팔레트:
    정상/활성:   #00E676 (형광 그린)
    응급/경고:   #FF1744 (경고 레드)
    비활성/정지: #888888 (회색)
    에어컨 온도: #29B6F6 (스카이블루)
    경고(중간):  #FFA726 (주황)
- 텍스트: #E0E0E0 (기본), #888888 (보조)
- 폰트: system-ui / -apple-system / Roboto 계열 (시스템 폰트 스택)

### 3-2. 레이아웃 구조

```
┌──────────────────────────────────────────────────────────────┐
│  HEADER: SmartCare+ 로고 | 시스템 상태(WebSocket 연결 표시)    │
├──────────────────────────────────────────────────────────────┤
│  ALERT BANNER (응급 시에만 표시, #FF1744 배경 점멸)            │
├────────────────────────┬─────────────────────────────────────┤
│  LEFT: 환자 모니터링    │  RIGHT: 환경 제어 패널               │
│                        │                                      │
│  - 가상 카메라 뷰        │  [에어컨]  슬라이더(18~30°C) + OFF   │
│    (상태 표시 아이콘)    │  [환풍기]  토글 스위치 ON/OFF         │
│  - 현재 상태 요약 행    │  [창문]    버튼 3개 (열기/닫기/정지)  │
│    (환자/팬/에어컨/창문) │                                      │
├────────────────────────┴─────────────────────────────────────┤
│  BOTTOM: 이벤트 로그 콘솔 (터미널 스타일, 스크롤)              │
└──────────────────────────────────────────────────────────────┘
```

- CSS Grid / Flexbox 사용
- 반응형: 모바일(max-width: 640px)에서 좌/우 패널을 세로로 쌓을 것

### 3-3. 응급 시각 효과 (중요)
- `patient_stat = 1` 수신 시:
    1. body 에 `.emergency` 클래스 추가
    2. body 의 border(3px)가 #FF1744 ↔ transparent 로 1초 주기 점멸
       (CSS `@keyframes blink-border` 사용)
    3. 상단 ALERT BANNER 표시 (#FF1744 배경 자체도 점멸)
    4. 환자 아이콘 색상 변경 (green → red)
- 정상 복귀 또는 '경고 해제' 버튼 클릭 시 즉시 해제

### 3-4. 각 UI 컴포넌트 상세

**환자 모니터링 카드 (좌측)**
- 가상 카메라 뷰: 어두운 배경의 박스 안에 큰 원형 아이콘(●)으로 환자 상태 표시
    - 정상: 녹색(#00E676) + "안정 (Stable)" 텍스트
    - 응급: 빨간색(#FF1744) + "응급 (Emergency)" 텍스트, 아이콘 박동 애니메이션
- 하단 상태 요약 행(sensor-row): 환자상태 / 환풍기 / 에어컨 / 창문 4개
    각 행은 키(label) + 값(colored badge) 형태

**환경 제어 카드 (우측)**
- 에어컨: range 슬라이더(18~30°C) + 현재값 표시 + OFF 버튼
    슬라이더 드래그 완료(onchange) 시에만 WebSocket 전송 (oninput은 UI만 갱신)
- 환풍기: 토글 스위치(CSS-only) — 켜짐 시 녹색, 꺼짐 시 회색
- 창문: 버튼 3개 (열기 ↑ / 닫기 ↓ / 정지)
    현재 동작 중인 버튼은 활성화 색상(주황)으로 강조

**이벤트 로그 콘솔 (하단)**
- 터미널 스타일: 모노스페이스 폰트, 어두운 배경
- 각 항목 형식: `> [HH:MM:SS] 메시지 내용`
- 최신 항목이 맨 위에 표시 (insertBefore)
- level별 색상: info(기본 텍스트) / warning(주황) / emergency(빨간색 + bold)
- 최대 50개 유지 (초과 시 하단 항목 자동 삭제)
- '지우기' 버튼

**WebSocket 연결 상태 표시 (헤더 우측)**
- 연결: 녹색 점 + "Online (ESP-NOW)" 텍스트
- 끊김: 회색 점(박동 애니메이션) + "연결 끊김" 텍스트
- 끊김 시 3초마다 자동 재연결 시도

─────────────────────────────────────────────
## 4. 현재 존재하는 스켈레톤 파일 (참고 및 개선 대상)
─────────────────────────────────────────────

아래는 이미 작성된 초안입니다. 이를 기반으로 미완성 부분을 완성하고
시각적 완성도와 UX를 개선해주세요.

### index.html (현재 초안)

```html
<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SmartCare+ Ambient System</title>
<link rel="stylesheet" href="/style.css">
</head>
<body class="normal">

<header>
  <div class="header-left">
    <span class="logo">SmartCare+</span>
    <span class="subtitle">Ambient Control System</span>
  </div>
  <div class="header-right">
    <span id="ws-status" class="status-dot offline"></span>
    <span id="ws-label">연결 중...</span>
  </div>
</header>

<div id="alert-banner" class="alert-banner hidden">
  ⚠ 응급 상황 감지 — Patient Room #101
  <button id="dismiss-btn" onclick="dismissAlert()">경고 해제</button>
</div>

<main class="grid">
  <section class="card monitor-card">
    <h2>환자 모니터링</h2>
    <div class="camera-view">
      <div class="camera-label">Patient Room #101</div>
      <div id="patient-icon" class="patient-icon stable">●</div>
      <div id="patient-label" class="patient-label">안정 (Stable)</div>
    </div>
    <div class="sensor-summary">
      <div class="sensor-row">
        <span class="sensor-key">환자 상태</span>
        <span id="stat-patient" class="sensor-val green">● 정상</span>
      </div>
      <div class="sensor-row">
        <span class="sensor-key">환풍기</span>
        <span id="stat-fan" class="sensor-val">● OFF</span>
      </div>
      <div class="sensor-row">
        <span class="sensor-key">에어컨</span>
        <span id="stat-ac" class="sensor-val">● OFF</span>
      </div>
      <div class="sensor-row">
        <span class="sensor-key">창문</span>
        <span id="stat-window" class="sensor-val">● 정지</span>
      </div>
    </div>
  </section>

  <section class="card control-card">
    <h2>환경 제어 <span class="mode-badge">수동</span></h2>
    <div class="control-group">
      <label>환풍기</label>
      <label class="toggle">
        <input type="checkbox" id="fan-toggle" onchange="sendCmd('fan', this.checked?1:0)">
        <span class="slider"></span>
      </label>
      <span id="fan-state-label" class="state-label">OFF</span>
    </div>
    <div class="control-group">
      <label>에어컨</label>
      <input type="range" id="ac-slider" min="18" max="30" value="24"
             oninput="updateAcLabel(this.value)"
             onchange="sendCmd('ac', parseInt(this.value))">
      <span id="ac-label" class="state-label">24°C</span>
      <button class="btn-small" onclick="sendCmd('ac',0)">OFF</button>
    </div>
    <div class="control-group">
      <label>창문</label>
      <div class="btn-group">
        <button class="btn-act" onclick="sendCmd('window',1)">열기 ↑</button>
        <button class="btn-act" onclick="sendCmd('window',2)">닫기 ↓</button>
        <button class="btn-act" onclick="sendCmd('window',0)">정지</button>
      </div>
    </div>
  </section>
</main>

<section class="log-section">
  <div class="log-header">
    <span>이벤트 로그</span>
    <button class="btn-small" onclick="clearLog()">지우기</button>
  </div>
  <div id="log-console" class="log-console"></div>
</section>

<script src="/app.js"></script>
</body>
</html>
```

### app.js (현재 초안 — 개선 필요)

현재 기본 WebSocket 연결 및 상태 갱신 로직이 구현되어 있으나
아래 항목이 미완성입니다:
1. 응급 시 환자 아이콘 박동(pulse) 애니메이션 트리거
2. 창문 버튼 활성화 상태 시각적 강조 (현재 동작 방향 표시)
3. 에어컨 슬라이더 서버 상태와 동기화 시 OFF 상태 처리
4. WebSocket 재연결 시 지수 백오프(exponential backoff) 적용
   (현재는 고정 3초)
5. 페이지 최초 로드 시 서버에 상태 요청 메시지 전송

─────────────────────────────────────────────
## 5. 출력 형식 요구사항
─────────────────────────────────────────────

- index.html / style.css / app.js 3개 파일을 각각 완전한 코드 블록으로 출력
- 각 파일 크기 목표: index.html < 8KB, style.css < 12KB, app.js < 10KB
- 주석은 한국어로 작성 (단, CSS 변수명·JS 식별자는 영어 유지)
- 인라인 스타일(style="") 사용 금지 — 모든 스타일은 style.css로 분리
- 인라인 이벤트 핸들러(onclick="") 는 index.html에서 허용
  (SPIFFS 파일 크기 최소화 목적)
- 절대 외부 리소스(CDN, 이미지 URL 등) 참조 금지

─────────────────────────────────────────────
## 6. 추가 개선 권장사항 (선택)
─────────────────────────────────────────────

아래는 필수가 아니지만 시연 현장에서 임팩트를 높일 수 있는 사항입니다:

- 응급 → 정상 전환 시 "All Clear" 텍스트 3초간 녹색 플래시 표시
- 에어컨 온도 슬라이더에 18°C~30°C 구간 색상 그라디언트 표시
  (낮을수록 파란색, 높을수록 붉은색)
- 로그 콘솔에 처음 접속 시 부팅 애니메이션 텍스트 효과
  (터미널 커서 깜빡임)
- 창문 상태를 아이콘(🔓열림 / 🔒닫힘 / ⏸정지)으로 표시
```

---

## 프롬프트 활용 가이드

| 목적 | 방법 |
|------|------|
| 코드 전체 재생성 | 위 프롬프트 전체를 그대로 AI에 붙여넣기 |
| style.css만 개선 | 섹션 3(UI/UX)만 발췌하여 전달 |
| app.js 기능 추가 | 섹션 2(WebSocket 규격) + 섹션 4의 미완성 항목만 전달 |
| 응급 효과만 수정 | 섹션 3-3(응급 시각 효과)만 발췌 전달 |

## SPIFFS 업로드 명령

파일 완성 후 아래 명령으로 ESP32에 플래시합니다.

```bash
# node_a_central 폴더에서 실행
idf.py spiffs_image -p /dev/ttyUSB0 flash

# 업로드 전 파일 크기 확인 (3개 합산 2MB 이하)
# Windows PowerShell:
Get-ChildItem spiffs_data | Measure-Object -Property Length -Sum
```
