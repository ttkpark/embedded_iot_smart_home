import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/device_state.dart';
import '../services/websocket_service.dart';

class ControlPanel extends StatelessWidget {
  const ControlPanel({super.key});

  @override
  Widget build(BuildContext context) {
    final ws = context.watch<WebSocketService>();
    final state = ws.deviceState;
    final connected = ws.status == ConnectionStatus.connected;

    return Card(
      elevation: 2,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(Icons.tune, color: Colors.blueGrey[700]),
                const SizedBox(width: 8),
                Text(
                  '수동 제어',
                  style: TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.bold,
                    color: Colors.blueGrey[800],
                  ),
                ),
              ],
            ),
            const Divider(height: 24),

            // 환풍기
            _buildFanControl(ws, state, connected),
            const SizedBox(height: 16),

            // 에어컨
            _buildAcControl(ws, state, connected),
            const SizedBox(height: 16),

            // 커튼/창문
            _buildWindowControl(ws, state, connected),
          ],
        ),
      ),
    );
  }

  Widget _buildFanControl(
      WebSocketService ws, DeviceState state, bool connected) {
    return Row(
      children: [
        Icon(
          Icons.air,
          color: state.isFanOn ? Colors.green : Colors.grey,
          size: 28,
        ),
        const SizedBox(width: 12),
        const Expanded(
          child: Text('환풍기', style: TextStyle(fontSize: 15)),
        ),
        Switch(
          value: state.isFanOn,
          onChanged: connected ? (v) => ws.setFan(v) : null,
          activeTrackColor: Colors.green[200],
          activeThumbColor: Colors.green,
        ),
      ],
    );
  }

  Widget _buildAcControl(
      WebSocketService ws, DeviceState state, bool connected) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Icon(
              Icons.ac_unit,
              color: state.acTemp > 0 ? Colors.cyan : Colors.grey,
              size: 28,
            ),
            const SizedBox(width: 12),
            const Expanded(
              child: Text('에어컨', style: TextStyle(fontSize: 15)),
            ),
            Text(
              state.acTempStr,
              style: TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.bold,
                color: state.acTemp > 0 ? Colors.cyan : Colors.grey,
              ),
            ),
          ],
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            const SizedBox(width: 40),
            Expanded(
              child: Slider(
                value: state.acTemp.toDouble(),
                min: 0,
                max: 30,
                divisions: 30,
                label: state.acTempStr,
                activeColor: Colors.cyan,
                onChanged: connected
                    ? (v) => ws.setAcTemp(v.round())
                    : null,
              ),
            ),
          ],
        ),
      ],
    );
  }

  Widget _buildWindowControl(
      WebSocketService ws, DeviceState state, bool connected) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Icon(
              Icons.curtains,
              color: state.windowAct == 1
                  ? Colors.orange
                  : state.windowAct == 2
                      ? Colors.deepPurple
                      : Colors.grey,
              size: 28,
            ),
            const SizedBox(width: 12),
            const Expanded(
              child: Text('커튼', style: TextStyle(fontSize: 15)),
            ),
            Text(
              state.windowStr,
              style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w500),
            ),
          ],
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            const SizedBox(width: 40),
            Expanded(
              child: SegmentedButton<int>(
                segments: const [
                  ButtonSegment(value: 1, label: Text('열기'), icon: Icon(Icons.open_in_full)),
                  ButtonSegment(value: 0, label: Text('정지'), icon: Icon(Icons.stop)),
                  ButtonSegment(value: 2, label: Text('닫기'), icon: Icon(Icons.close_fullscreen)),
                ],
                selected: {state.windowAct},
                onSelectionChanged: connected
                    ? (v) => ws.setWindow(v.first)
                    : null,
              ),
            ),
          ],
        ),
      ],
    );
  }
}
