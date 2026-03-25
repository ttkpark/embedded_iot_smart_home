import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/websocket_service.dart';
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
    final state = ws.deviceState;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Smart Home'),
        centerTitle: true,
        actions: [
          // 연결 상태 표시
          _ConnectionIndicator(status: ws.status),
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
          if (ws.status != ConnectionStatus.connected) {
            ws.connect();
          }
        },
        child: SingleChildScrollView(
          physics: const AlwaysScrollableScrollPhysics(),
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // 응급 알림 배너
              if (ws.emergencyAlert || state.isEmergency)
                Padding(
                  padding: const EdgeInsets.only(bottom: 16),
                  child: EmergencyBanner(onDismiss: ws.dismissAlert),
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
                    icon: state.isEmergency
                        ? Icons.heart_broken
                        : Icons.favorite,
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

              // 수동 제어 패널
              const ControlPanel(),

              const SizedBox(height: 16),

              // 이벤트 로그
              const LogList(),

              const SizedBox(height: 32),
            ],
          ),
        ),
      ),
    );
  }
}

class _ConnectionIndicator extends StatelessWidget {
  final ConnectionStatus status;
  const _ConnectionIndicator({required this.status});

  @override
  Widget build(BuildContext context) {
    Color color;
    IconData icon;
    String tooltip;

    switch (status) {
      case ConnectionStatus.connected:
        color = Colors.green;
        icon = Icons.wifi;
        tooltip = '연결됨';
        break;
      case ConnectionStatus.connecting:
        color = Colors.orange;
        icon = Icons.wifi_find;
        tooltip = '연결 중...';
        break;
      case ConnectionStatus.disconnected:
        color = Colors.red;
        icon = Icons.wifi_off;
        tooltip = '연결 끊김';
        break;
    }

    return Tooltip(
      message: tooltip,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 8),
        child: Icon(icon, color: color, size: 22),
      ),
    );
  }
}
