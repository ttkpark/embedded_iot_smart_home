import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../services/websocket_service.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late TextEditingController _ipController;
  late TextEditingController _portController;

  @override
  void initState() {
    super.initState();
    final ws = context.read<WebSocketService>();
    _ipController = TextEditingController(text: ws.serverIp);
    _portController = TextEditingController(text: ws.serverPort.toString());
  }

  @override
  void dispose() {
    _ipController.dispose();
    _portController.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    final ip = _ipController.text.trim();
    final port = int.tryParse(_portController.text.trim()) ?? 80;

    if (ip.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('IP 주소를 입력하세요')),
      );
      return;
    }

    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('server_ip', ip);
    await prefs.setInt('server_port', port);

    if (!mounted) return;
    final ws = context.read<WebSocketService>();
    ws.configure(ip: ip, port: port);
    ws.connect();

    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text('$ip:$port 에 연결 시도 중...')),
    );
    Navigator.pop(context);
  }

  @override
  Widget build(BuildContext context) {
    final ws = context.watch<WebSocketService>();

    return Scaffold(
      appBar: AppBar(title: const Text('연결 설정')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          // 연결 상태
          Card(
            child: ListTile(
              leading: Icon(
                ws.status == ConnectionStatus.connected
                    ? Icons.check_circle
                    : Icons.error_outline,
                color: ws.status == ConnectionStatus.connected
                    ? Colors.green
                    : Colors.red,
              ),
              title: Text(
                ws.status == ConnectionStatus.connected
                    ? '연결됨'
                    : ws.status == ConnectionStatus.connecting
                        ? '연결 중...'
                        : '연결 끊김',
              ),
              subtitle: Text(ws.wsUrl),
            ),
          ),
          const SizedBox(height: 24),

          // IP 입력
          TextField(
            controller: _ipController,
            decoration: const InputDecoration(
              labelText: 'Node A IP 주소',
              hintText: '192.168.0.xxx',
              prefixIcon: Icon(Icons.router),
              border: OutlineInputBorder(),
            ),
            keyboardType: TextInputType.number,
          ),
          const SizedBox(height: 16),

          // 포트 입력
          TextField(
            controller: _portController,
            decoration: const InputDecoration(
              labelText: '포트',
              hintText: '80',
              prefixIcon: Icon(Icons.numbers),
              border: OutlineInputBorder(),
            ),
            keyboardType: TextInputType.number,
          ),
          const SizedBox(height: 24),

          // 연결 버튼
          FilledButton.icon(
            onPressed: _save,
            icon: const Icon(Icons.wifi),
            label: const Text('연결'),
            style: FilledButton.styleFrom(
              minimumSize: const Size.fromHeight(48),
            ),
          ),
          const SizedBox(height: 12),

          // 연결 해제 버튼
          OutlinedButton.icon(
            onPressed: ws.status != ConnectionStatus.disconnected
                ? () {
                    ws.disconnect();
                    ScaffoldMessenger.of(context).showSnackBar(
                      const SnackBar(content: Text('연결 해제됨')),
                    );
                  }
                : null,
            icon: const Icon(Icons.wifi_off),
            label: const Text('연결 해제'),
            style: OutlinedButton.styleFrom(
              minimumSize: const Size.fromHeight(48),
            ),
          ),

          const SizedBox(height: 32),
          const Divider(),
          const SizedBox(height: 16),

          // 도움말
          Text(
            '연결 방법',
            style: Theme.of(context).textTheme.titleMedium,
          ),
          const SizedBox(height: 8),
          const Text(
            '1. 스마트폰과 Node A가 같은 Wi-Fi(Smart Meeting)에 연결되어 있어야 합니다.\n'
            '2. Node A의 시리얼 모니터에서 "IP 획득: x.x.x.x" 메시지를 확인하세요.\n'
            '3. 해당 IP 주소를 위에 입력하고 연결 버튼을 누르세요.\n'
            '4. 포트는 기본값 80을 사용합니다.',
            style: TextStyle(fontSize: 13, height: 1.6),
          ),
        ],
      ),
    );
  }
}
