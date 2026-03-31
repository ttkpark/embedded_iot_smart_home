import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

import '../models/device_state.dart';
import '../models/log_entry.dart';

enum ConnectionStatus { disconnected, connecting, connected }

/// Node A 중앙 허브와 WebSocket 통신을 담당하는 서비스
class WebSocketService extends ChangeNotifier {
  WebSocketChannel? _channel;
  ConnectionStatus _status = ConnectionStatus.disconnected;
  String _serverIp = '192.168.0.1';
  int _serverPort = 80;

  DeviceState _deviceState = const DeviceState();
  final List<LogEntry> _logs = [];
  bool _emergencyAlert = false;
  Timer? _reconnectTimer;

  // Getters
  ConnectionStatus get status => _status;
  DeviceState get deviceState => _deviceState;
  List<LogEntry> get logs => List.unmodifiable(_logs);
  bool get emergencyAlert => _emergencyAlert;
  String get serverIp => _serverIp;
  int get serverPort => _serverPort;
  String get wsUrl => 'ws://$_serverIp:$_serverPort/ws';

  void configure({required String ip, int port = 80}) {
    _serverIp = ip;
    _serverPort = port;
    notifyListeners();
  }

  /// WebSocket 연결
  void connect() {
    if (_status == ConnectionStatus.connecting) return;
    disconnect();

    _status = ConnectionStatus.connecting;
    notifyListeners();

    try {
      final uri = Uri.parse(wsUrl);
      _channel = WebSocketChannel.connect(uri);

      _channel!.ready.then((_) {
        _status = ConnectionStatus.connected;
        _reconnectTimer?.cancel();
        notifyListeners();
        sendCommand('get_status', 1);
      }).catchError((error) {
        _status = ConnectionStatus.disconnected;
        notifyListeners();
        _scheduleReconnect();
      });

      _channel!.stream.listen(
        _onMessage,
        onError: (error) {
          debugPrint('WebSocket error: $error');
          _status = ConnectionStatus.disconnected;
          notifyListeners();
          _scheduleReconnect();
        },
        onDone: () {
          _status = ConnectionStatus.disconnected;
          notifyListeners();
          _scheduleReconnect();
        },
      );
    } catch (e) {
      debugPrint('WebSocket connect failed: $e');
      _status = ConnectionStatus.disconnected;
      notifyListeners();
      _scheduleReconnect();
    }
  }

  /// 연결 해제
  void disconnect() {
    _reconnectTimer?.cancel();
    _channel?.sink.close();
    _channel = null;
    _status = ConnectionStatus.disconnected;
    notifyListeners();
  }

  void _scheduleReconnect() {
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(const Duration(seconds: 3), connect);
  }

  /// 수신 메시지 처리
  void _onMessage(dynamic message) {
    try {
      final data = jsonDecode(message as String) as Map<String, dynamic>;
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
      debugPrint('Parse error: $e');
    }
  }

  /// Node A로 명령 전송
  void sendCommand(String cmd, int value) {
    if (_status != ConnectionStatus.connected || _channel == null) return;
    final json = jsonEncode({'cmd': cmd, 'value': value});
    _channel!.sink.add(json);
  }

  // 편의 메서드
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

  @override
  void dispose() {
    _reconnectTimer?.cancel();
    _channel?.sink.close();
    super.dispose();
  }
}
