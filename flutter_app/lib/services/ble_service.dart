import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../models/device_state.dart';
import '../models/log_entry.dart';
import 'websocket_service.dart' show ConnectionStatus;

/// BLE GATT 클라이언트 — WebSocketService와 동일한 인터페이스
/// Node A의 BLE GATT 서버 (SmartHome-A)에 연결
class BleService extends ChangeNotifier {
  static const String _targetName = 'SmartHome-A';
  static final Guid _serviceUuid = Guid('000000ff-0000-1000-8000-00805f9b34fb');
  static final Guid _statusUuid  = Guid('0000ff01-0000-1000-8000-00805f9b34fb');
  static final Guid _commandUuid = Guid('0000ff02-0000-1000-8000-00805f9b34fb');

  BluetoothDevice? _device;
  BluetoothCharacteristic? _statusChar;
  BluetoothCharacteristic? _commandChar;
  StreamSubscription? _scanSub;
  StreamSubscription? _notifySub;
  StreamSubscription? _connSub;

  ConnectionStatus _status = ConnectionStatus.disconnected;
  DeviceState _deviceState = const DeviceState();
  final List<LogEntry> _logs = [];
  bool _emergencyAlert = false;
  bool _scanning = false;

  // Getters (WebSocketService와 동일한 인터페이스)
  ConnectionStatus get status => _status;
  DeviceState get deviceState => _deviceState;
  List<LogEntry> get logs => List.unmodifiable(_logs);
  bool get emergencyAlert => _emergencyAlert;
  bool get scanning => _scanning;
  String? get deviceName => _device?.platformName;

  /// BLE 스캔 시작 → SmartHome-A 자동 연결
  Future<void> scanAndConnect() async {
    if (_scanning || _status == ConnectionStatus.connected) return;

    _scanning = true;
    _status = ConnectionStatus.connecting;
    notifyListeners();

    await FlutterBluePlus.startScan(
      timeout: const Duration(seconds: 10),
      withNames: [_targetName],
    );

    _scanSub = FlutterBluePlus.scanResults.listen((results) {
      for (final r in results) {
        if (r.device.platformName == _targetName) {
          FlutterBluePlus.stopScan();
          _scanning = false;
          _connectToDevice(r.device);
          return;
        }
      }
    });

    // 스캔 타임아웃
    Future.delayed(const Duration(seconds: 12), () {
      if (_scanning) {
        _scanning = false;
        _status = ConnectionStatus.disconnected;
        _scanSub?.cancel();
        notifyListeners();
      }
    });
  }

  Future<void> _connectToDevice(BluetoothDevice device) async {
    try {
      _device = device;
      await device.connect(autoConnect: false, timeout: const Duration(seconds: 8));

      // 연결 상태 모니터링
      _connSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          _status = ConnectionStatus.disconnected;
          _notifySub?.cancel();
          notifyListeners();
        }
      });

      // MTU 요청
      await device.requestMtu(256);

      // 서비스 검색
      final services = await device.discoverServices();
      for (final svc in services) {
        if (svc.uuid == _serviceUuid) {
          for (final chr in svc.characteristics) {
            if (chr.uuid == _statusUuid) {
              _statusChar = chr;
              // Notify 구독
              await chr.setNotifyValue(true);
              _notifySub = chr.onValueReceived.listen(_onNotify);
            } else if (chr.uuid == _commandUuid) {
              _commandChar = chr;
            }
          }
        }
      }

      if (_statusChar != null && _commandChar != null) {
        _status = ConnectionStatus.connected;
        // 초기 상태 요청
        sendCommand('get_status', 1);
      } else {
        _status = ConnectionStatus.disconnected;
        await device.disconnect();
      }
    } catch (e) {
      debugPrint('BLE connect error: $e');
      _status = ConnectionStatus.disconnected;
    }
    notifyListeners();
  }

  /// BLE Notify 수신 → JSON 파싱 (WebSocket과 동일한 포맷)
  void _onNotify(List<int> value) {
    try {
      final json = utf8.decode(value);
      final data = jsonDecode(json) as Map<String, dynamic>;
      final type = data['type'] as String?;

      switch (type) {
        case 'status':
          _deviceState = DeviceState.fromJson(data);
          break;
        case 'log':
          _logs.insert(0, LogEntry.fromJson(data));
          if (_logs.length > 100) _logs.removeLast();
          break;
        case 'alert':
          final alertType = data['alert'] as String?;
          if (alertType == 'DISMISS') {
            _emergencyAlert = false;
          } else {
            _emergencyAlert = true;
          }
          if (data.containsKey('patient_stat')) {
            _deviceState = _deviceState.copyWith(
              patientStat: data['patient_stat'] as int?,
            );
          }
          break;
      }
      notifyListeners();
    } catch (e) {
      debugPrint('BLE parse error: $e');
    }
  }

  /// Node A로 명령 전송 (WebSocketService와 동일한 인터페이스)
  void sendCommand(String cmd, int value) {
    if (_status != ConnectionStatus.connected || _commandChar == null) return;
    final json = jsonEncode({'cmd': cmd, 'value': value});
    _commandChar!.write(utf8.encode(json), withoutResponse: true);
  }

  // 편의 메서드 (WebSocketService와 동일)
  void setFan(bool on) => sendCommand('fan', on ? 1 : 0);
  void setAcTemp(int temp) => sendCommand('ac', temp);
  void setWindow(int action) => sendCommand('window', action);
  void triggerEmergency() => sendCommand('emergency', 1);
  void dismissAlert() {
    sendCommand('dismiss', 1);
    _emergencyAlert = false;
    notifyListeners();
  }

  void clearLogs() {
    _logs.clear();
    notifyListeners();
  }

  /// 연결 해제
  void disconnect() {
    _scanSub?.cancel();
    _notifySub?.cancel();
    _connSub?.cancel();
    _device?.disconnect();
    _device = null;
    _statusChar = null;
    _commandChar = null;
    _status = ConnectionStatus.disconnected;
    notifyListeners();
  }

  @override
  void dispose() {
    disconnect();
    super.dispose();
  }
}
