import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/websocket_service.dart';
import '../services/ble_service.dart';
import '../widgets/status_card.dart';
import '../widgets/emergency_banner.dart';
import '../widgets/control_panel.dart';
import '../widgets/log_list.dart';
import 'settings_screen.dart';

class DashboardScreen extends StatelessWidget {
  const DashboardScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final ws = context.watch<WebSocketService>();
    final ble = context.watch<BleService>();

    // 활성 연결: BLE 우선, 없으면 WS
    final bool wsOk = ws.status == ConnectionStatus.connected;
    final bool bleOk = ble.status == ConnectionStatus.connected;
    final state = bleOk ? ble.deviceState : ws.deviceState;
    final isConnected = wsOk || bleOk;
    final alert = bleOk ? ble.emergencyAlert : ws.emergencyAlert;

    void sendCmd(String cmd, int val) {
      if (bleOk) ble.sendCommand(cmd, val);
      if (wsOk) ws.sendCommand(cmd, val);
    }

    void dismiss() {
      if (bleOk) ble.dismissAlert();
      if (wsOk) ws.dismissAlert();
    }

    void emergency() {
      if (bleOk) ble.triggerEmergency();
      if (wsOk) ws.triggerEmergency();
    }

    return Scaffold(
      appBar: AppBar(
        title: const Text('Smart Home'),
        centerTitle: true,
        actions: [
          // BLE 연결 버튼
          _BleButton(ble: ble),
          // Wi-Fi 연결 상태
          _ConnectionIndicator(status: ws.status, isBle: false),
          IconButton(
            icon: const Icon(Icons.settings),
            onPressed: () => Navigator.push(
              context,
              MaterialPageRoute(builder: (_) => const SettingsScreen()),
            ),
          ),
        ],
      ),
      body: RefreshIndicator(
        onRefresh: () async {
          if (!wsOk) ws.connect();
          if (!bleOk) ble.scanAndConnect();
        },
        child: SingleChildScrollView(
          physics: const AlwaysScrollableScrollPhysics(),
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // 응급 알림 배너
              if (alert || state.isEmergency)
                Padding(
                  padding: const EdgeInsets.only(bottom: 16),
                  child: EmergencyBanner(onDismiss: dismiss),
                ),

              // 응급 트리거 버튼
              Padding(
                padding: const EdgeInsets.only(bottom: 16),
                child: _EmergencyButton(
                  isEmergency: state.isEmergency,
                  connected: isConnected,
                  onTrigger: emergency,
                  onDismiss: dismiss,
                ),
              ),

              // 상태 카드 그리드
              GridView.count(
                crossAxisCount: 2,
                shrinkWrap: true,
                physics: const NeverScrollableScrollPhysics(),
                mainAxisSpacing: 8,
                crossAxisSpacing: 8,
                childAspectRatio: 1.5,
                children: [
                  StatusCard(
                    title: '환자 상태',
                    value: state.isEmergency ? '응급' : '정상',
                    icon: state.isEmergency ? Icons.heart_broken : Icons.favorite,
                    color: state.isEmergency ? Colors.red : Colors.green,
                  ),
                  StatusCard(
                    title: '환풍기',
                    value: state.isFanOn ? 'ON' : 'OFF',
                    icon: Icons.air,
                    color: state.isFanOn ? Colors.green : Colors.grey,
                  ),
                  StatusCard(
                    title: '에어컨',
                    value: state.acTempStr,
                    icon: Icons.ac_unit,
                    color: state.acTemp > 0 ? Colors.cyan : Colors.grey,
                  ),
                  StatusCard(
                    title: '커튼',
                    value: state.windowStr,
                    icon: Icons.curtains,
                    color: state.windowAct == 1
                        ? Colors.orange
                        : state.windowAct == 2
                            ? Colors.deepPurple
                            : Colors.grey,
                  ),
                ],
              ),

              const SizedBox(height: 16),
              const ControlPanel(),
              const SizedBox(height: 16),
              const LogList(),
              const SizedBox(height: 32),
            ],
          ),
        ),
      ),
    );
  }
}

/// BLE 연결 토글 버튼
class _BleButton extends StatelessWidget {
  final BleService ble;
  const _BleButton({required this.ble});

  @override
  Widget build(BuildContext context) {
    final isConn = ble.status == ConnectionStatus.connected;
    final isScanning = ble.scanning;

    return IconButton(
      icon: Icon(
        Icons.bluetooth,
        color: isConn ? Colors.blue : (isScanning ? Colors.orange : Colors.grey),
        size: 22,
      ),
      tooltip: isConn ? 'BLE 연결됨' : (isScanning ? 'BLE 스캔 중...' : 'BLE 연결'),
      onPressed: () {
        if (isConn) {
          ble.disconnect();
        } else {
          ble.scanAndConnect();
        }
      },
    );
  }
}

class _ConnectionIndicator extends StatelessWidget {
  final ConnectionStatus status;
  final bool isBle;
  const _ConnectionIndicator({required this.status, this.isBle = false});

  @override
  Widget build(BuildContext context) {
    Color color;
    IconData icon;

    switch (status) {
      case ConnectionStatus.connected:
        color = Colors.green;
        icon = isBle ? Icons.bluetooth_connected : Icons.wifi;
        break;
      case ConnectionStatus.connecting:
        color = Colors.orange;
        icon = isBle ? Icons.bluetooth_searching : Icons.wifi_find;
        break;
      case ConnectionStatus.disconnected:
        color = Colors.red;
        icon = isBle ? Icons.bluetooth_disabled : Icons.wifi_off;
        break;
    }

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 4),
      child: Icon(icon, color: color, size: 22),
    );
  }
}

class _EmergencyButton extends StatefulWidget {
  final bool isEmergency;
  final bool connected;
  final VoidCallback onTrigger;
  final VoidCallback onDismiss;

  const _EmergencyButton({
    required this.isEmergency,
    required this.connected,
    required this.onTrigger,
    required this.onDismiss,
  });

  @override
  State<_EmergencyButton> createState() => _EmergencyButtonState();
}

class _EmergencyButtonState extends State<_EmergencyButton>
    with SingleTickerProviderStateMixin {
  late AnimationController _pulseController;
  bool _confirmPending = false;

  @override
  void initState() {
    super.initState();
    _pulseController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1200),
    );
    if (widget.isEmergency) _pulseController.repeat(reverse: true);
  }

  @override
  void didUpdateWidget(_EmergencyButton old) {
    super.didUpdateWidget(old);
    if (widget.isEmergency && !_pulseController.isAnimating) {
      _pulseController.repeat(reverse: true);
    } else if (!widget.isEmergency && _pulseController.isAnimating) {
      _pulseController.stop();
      _pulseController.value = 0;
    }
  }

  @override
  void dispose() {
    _pulseController.dispose();
    super.dispose();
  }

  void _handlePress() {
    if (!widget.connected) return;

    if (widget.isEmergency) {
      widget.onDismiss();
      setState(() => _confirmPending = false);
      return;
    }

    if (!_confirmPending) {
      setState(() => _confirmPending = true);
      Future.delayed(const Duration(seconds: 3), () {
        if (mounted) setState(() => _confirmPending = false);
      });
      return;
    }

    widget.onTrigger();
    setState(() => _confirmPending = false);
  }

  @override
  Widget build(BuildContext context) {
    final isEmergency = widget.isEmergency;

    return AnimatedBuilder(
      animation: _pulseController,
      builder: (context, child) {
        final glowOpacity = isEmergency ? 0.3 + _pulseController.value * 0.4 : 0.0;

        return Container(
          width: double.infinity,
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(16),
            boxShadow: isEmergency
                ? [BoxShadow(color: Colors.red.withOpacity(glowOpacity), blurRadius: 24, spreadRadius: 4)]
                : null,
          ),
          child: Material(
            color: isEmergency
                ? Color.lerp(Colors.red[700], Colors.red[900], _pulseController.value)
                : _confirmPending ? Colors.orange[700] : Colors.red[600],
            borderRadius: BorderRadius.circular(16),
            elevation: 4,
            child: InkWell(
              borderRadius: BorderRadius.circular(16),
              onTap: widget.connected ? _handlePress : null,
              child: Padding(
                padding: const EdgeInsets.symmetric(vertical: 20),
                child: Column(
                  children: [
                    Icon(
                      isEmergency ? Icons.cancel_outlined
                          : _confirmPending ? Icons.warning_amber_rounded : Icons.emergency,
                      color: Colors.white, size: 40,
                    ),
                    const SizedBox(height: 8),
                    Text(
                      isEmergency ? 'DISMISS EMERGENCY'
                          : _confirmPending ? 'TAP AGAIN TO CONFIRM' : 'EMERGENCY',
                      style: const TextStyle(
                        color: Colors.white, fontSize: 18,
                        fontWeight: FontWeight.bold, letterSpacing: 1.5,
                      ),
                    ),
                    if (!isEmergency && !_confirmPending)
                      const Text('응급 상황 시 탭하세요', style: TextStyle(color: Colors.white70, fontSize: 12)),
                    if (_confirmPending)
                      const Text('한 번 더 탭하면 응급 모드가 활성화됩니다', style: TextStyle(color: Colors.white70, fontSize: 12)),
                    if (!widget.connected)
                      const Text('연결 필요', style: TextStyle(color: Colors.white54, fontSize: 12)),
                  ],
                ),
              ),
            ),
          ),
        );
      },
    );
  }
}
