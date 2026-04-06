/**
 * SmartCare+ Dashboard — app.js
 * WebSocket 연동, 실시간 UI 갱신, 수동 제어 명령 전송
 */

(function () {
  'use strict';

  /* ── 상수 ──────────────────────────────────────────────────────────────*/
  var WS_URL      = 'ws://' + location.host + '/ws';
  var MAX_LOGS    = 50;
  var RECONNECT_BASE = 2000;   // 최초 재연결 대기 ms
  var RECONNECT_MAX  = 30000;  // 최대 재연결 대기 ms

  /* ── 상태 추적 ────────────────────────────────────────────────────────*/
  var ws             = null;
  var reconnectTimer = null;
  var reconnectDelay = RECONNECT_BASE;
  var prevEmergency  = false;   // 이전 응급 상태 (All Clear 감지용)
  var isEmergency    = false;
  var allClearTimer  = null;
  var bootDone       = false;

  /* ── WebSocket 연결 ───────────────────────────────────────────────────*/
  function connect() {
    ws = new WebSocket(WS_URL);

    ws.onopen = function () {
      setWsStatus(true);
      reconnectDelay = RECONNECT_BASE;  // 성공 시 지연 초기화
      clearTimeout(reconnectTimer);

      /* 접속 직후 서버에 전체 상태 요청 */
      ws.send(JSON.stringify({ cmd: 'get_status', value: 1 }));

      addLog(now(), 'WebSocket 연결됨 — 상태 요청 전송', 'info');
    };

    ws.onclose = function () {
      setWsStatus(false);
      addLog(now(),
        'WebSocket 연결 끊김 — ' + (reconnectDelay / 1000).toFixed(0) + '초 후 재연결',
        'warning');

      /* 지수 백오프: 다음 재연결 시 대기 시간을 2배로 (최대 30초) */
      reconnectTimer = setTimeout(function () {
        reconnectDelay = Math.min(reconnectDelay * 2, RECONNECT_MAX);
        connect();
      }, reconnectDelay);
    };

    ws.onerror = function () {
      ws.close();
    };

    ws.onmessage = function (evt) {
      try {
        var msg = JSON.parse(evt.data);
        handleMessage(msg);
      } catch (e) {
        console.warn('JSON 파싱 오류:', e);
      }
    };
  }

  function setWsStatus(online) {
    var dot   = document.getElementById('ws-status');
    var label = document.getElementById('ws-label');
    dot.className     = 'status-dot ' + (online ? 'online' : 'offline');
    label.textContent = online ? 'Online (ESP-NOW)' : '연결 끊김';
  }

  /* ── 메시지 라우팅 ────────────────────────────────────────────────────*/
  function handleMessage(msg) {
    switch (msg.type) {
      case 'status':
        updateStatus(msg);
        break;
      case 'alert':
        if (msg.alert === 'EMERGENCY') showAlert();
        break;
      case 'log':
        var lvl = msg.level === 2 ? 'emergency' : msg.level === 1 ? 'warning' : 'info';
        addLog(msg.time, msg.message, lvl);
        break;
    }
  }

  /* ── 상태 갱신 ────────────────────────────────────────────────────────*/
  var WIN_LABELS = ['⏸ 정지', '🔓 열림', '🔒 닫힘'];
  var WIN_IDS    = ['win-stop', 'win-open', 'win-close'];

  function updateStatus(s) {
    /* ── 환자 상태 ────────────────────────────────────────────────────*/
    var wasEmergency = isEmergency;
    var patStat  = document.getElementById('stat-patient');
    var patIcon  = document.getElementById('patient-icon');
    var patLabel = document.getElementById('patient-label');

    if (s.patient_stat === 1) {
      isEmergency = true;
      patStat.textContent  = '● 응급';
      patStat.className    = 'sensor-val red';
      patIcon.className    = 'patient-icon emergency';
      patLabel.textContent = '응급 (Emergency)';
      showAlert();
    } else {
      isEmergency = false;
      patStat.textContent  = '● 정상';
      patStat.className    = 'sensor-val green';
      patIcon.className    = 'patient-icon stable';
      patLabel.textContent = '안정 (Stable)';
      clearAlert();

      /* 응급 → 정상 전환 시 All Clear 플래시 */
      if (wasEmergency) showAllClear();
    }

    /* ── 환풍기 ───────────────────────────────────────────────────────*/
    var fanOn   = !!s.fan_state;
    var fanStat = document.getElementById('stat-fan');
    fanStat.textContent = fanOn ? '● 가동중' : '● OFF';
    fanStat.className   = 'sensor-val ' + (fanOn ? 'green' : 'dim');

    var fanToggle = document.getElementById('fan-toggle');
    fanToggle.checked = fanOn;
    document.getElementById('fan-state-label').textContent = fanOn ? 'ON' : 'OFF';

    /* ── 에어컨 ───────────────────────────────────────────────────────*/
    var acStat   = document.getElementById('stat-ac');
    var acSlider = document.getElementById('ac-slider');
    var acLabel  = document.getElementById('ac-label');

    if (s.ac_temp > 0) {
      acStat.textContent  = '● ' + s.ac_temp + '°C';
      acStat.className    = 'sensor-val blue';
      acSlider.value      = s.ac_temp;
      acSlider.classList.remove('ac-off');
      acLabel.textContent = s.ac_temp + '°C';
      acLabel.className   = 'state-label';
    } else {
      acStat.textContent  = '● OFF';
      acStat.className    = 'sensor-val dim';
      acSlider.classList.add('ac-off');
      acLabel.textContent = 'OFF';
      acLabel.className   = 'state-label ac-off-label';
    }

    /* ── 창문 ─────────────────────────────────────────────────────────*/
    var winStat = document.getElementById('stat-window');
    var act     = s.window_act;
    winStat.textContent = WIN_LABELS[act] || '⏸ 정지';
    winStat.className   = 'sensor-val ' + (act ? 'orange' : 'dim');

    /* 현재 동작 중인 창문 버튼 강조 */
    WIN_IDS.forEach(function (id, idx) {
      var btn = document.getElementById(id);
      if (!btn) return;
      /* WIN_IDS 순서: stop(0), open(1), close(2) → act와 매핑 */
      var btnAct = [0, 1, 2][idx];
      btn.classList.toggle('active', btnAct === act && act !== 0);
    });

    /* ── 온습도 ───────────────────────────────────────────────────────*/
    var tempEl = document.getElementById('stat-temp');
    var humiEl = document.getElementById('stat-humi');
    if (s.temperature !== undefined) {
      tempEl.textContent = s.temperature + ' °C';
      tempEl.className = 'sensor-val blue';
    }
    if (s.humidity !== undefined) {
      var hc = s.humidity >= 80 ? 'red' : (s.humidity >= 60 ? 'orange' : 'green');
      humiEl.textContent = s.humidity + ' %';
      humiEl.className = 'sensor-val ' + hc;
    }

    /* offset 슬라이더 동기화 */
    if (s.temp_offset !== undefined) {
      document.getElementById('temp-offset').value = s.temp_offset;
      document.getElementById('temp-off-val').textContent = s.temp_offset + '°C';
    }
    if (s.humi_offset !== undefined) {
      document.getElementById('humi-offset').value = s.humi_offset;
      document.getElementById('humi-off-val').textContent = s.humi_offset + '%';
    }
  }

  /* ── 응급 경고 표시/해제 ──────────────────────────────────────────────*/
  function showAlert() {
    document.getElementById('alert-banner').classList.remove('hidden');
    document.body.classList.add('emergency');
  }

  function clearAlert() {
    document.getElementById('alert-banner').classList.add('hidden');
    document.body.classList.remove('emergency');
  }

  window.dismissAlert = function () {
    isEmergency = false;
    clearAlert();
    sendCmd('dismiss', 1);
    addLog(now(), '경고 수동 해제됨', 'warning');
  };

  /* ── All Clear 플래시 (응급 → 정상) ──────────────────────────────────*/
  function showAllClear() {
    var banner = document.getElementById('all-clear');
    clearTimeout(allClearTimer);
    banner.classList.remove('hidden');
    addLog(now(), 'All Clear — 환자 상태 정상으로 복귀', 'info');
    allClearTimer = setTimeout(function () {
      banner.classList.add('hidden');
    }, 3000);
  }

  /* ── 수동 제어 명령 전송 ──────────────────────────────────────────────*/
  window.sendCmd = function (cmd, value) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ cmd: cmd, value: value }));
    } else {
      addLog(now(), '명령 전송 실패 — 연결 끊김', 'warning');
    }
  };

  /* AC 슬라이더 드래그 중 레이블 실시간 갱신 */
  window.updateAcLabel = function (val) {
    var acLabel  = document.getElementById('ac-label');
    var acSlider = document.getElementById('ac-slider');
    acSlider.classList.remove('ac-off');
    acLabel.textContent = val + '°C';
    acLabel.className   = 'state-label';
  };

  /* ── 이벤트 로그 ──────────────────────────────────────────────────────*/
  function addLog(time, message, level) {
    var con   = document.getElementById('log-console');
    var entry = document.createElement('div');
    var lvlClass = level === 'emergency' ? ' emergency'
                 : level === 'warning'   ? ' warning' : '';
    entry.className   = 'log-entry' + lvlClass;
    entry.textContent = '> [' + time + '] ' + message;
    con.insertBefore(entry, con.firstChild);

    /* 최대 50개 유지 */
    while (con.children.length > MAX_LOGS) {
      con.removeChild(con.lastChild);
    }
  }

  window.clearLog = function () {
    document.getElementById('log-console').innerHTML = '';
  };

  /* ── 현재 시각 문자열 반환 (HH:MM:SS) ────────────────────────────────*/
  function now() {
    var d = new Date();
    return [d.getHours(), d.getMinutes(), d.getSeconds()]
      .map(function (n) { return String(n).padStart(2, '0'); })
      .join(':');
  }

  /* ── 부팅 시 터미널 스타일 초기 로그 출력 ────────────────────────────*/
  function runBootSequence() {
    if (bootDone) return;
    bootDone = true;

    var lines = [
      { t: 0,    msg: 'SmartCare+ Ambient v1.0 — 시스템 초기화 중...', lvl: 'info' },
      { t: 400,  msg: 'SPIFFS 마운트 완료', lvl: 'info' },
      { t: 800,  msg: 'ESP-NOW 피어 등록 완료 (Node B, Node C)', lvl: 'info' },
      { t: 1200, msg: 'WebSocket 서버 시작 (포트 80)', lvl: 'info' },
      { t: 1600, msg: '대시보드 준비 완료 — WebSocket 연결 시도 중...', lvl: 'info' },
    ];

    /* 커서 깜빡임 표시용 임시 엔트리 */
    var con    = document.getElementById('log-console');
    var cursor = document.createElement('div');
    cursor.className   = 'log-entry';
    cursor.innerHTML   = '> <span class="boot-cursor">_</span>';
    con.appendChild(cursor);

    lines.forEach(function (item) {
      setTimeout(function () {
        /* 커서 엔트리 제거 후 실제 로그 추가 */
        if (cursor.parentNode) cursor.parentNode.removeChild(cursor);
        addLog(now(), item.msg, item.lvl);
      }, item.t);
    });
  }

  /* ── 초기화 ───────────────────────────────────────────────────────────*/
  runBootSequence();
  connect();

})();
