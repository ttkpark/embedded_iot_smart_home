import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/log_entry.dart';
import '../services/websocket_service.dart';

class LogList extends StatelessWidget {
  const LogList({super.key});

  @override
  Widget build(BuildContext context) {
    final ws = context.watch<WebSocketService>();
    final logs = ws.logs;

    return Card(
      elevation: 2,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 16, 8, 8),
            child: Row(
              children: [
                Icon(Icons.list_alt, color: Colors.blueGrey[700]),
                const SizedBox(width: 8),
                Text(
                  '이벤트 로그',
                  style: TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.bold,
                    color: Colors.blueGrey[800],
                  ),
                ),
                const Spacer(),
                if (logs.isNotEmpty)
                  TextButton.icon(
                    onPressed: ws.clearLogs,
                    icon: const Icon(Icons.clear_all, size: 18),
                    label: const Text('지우기'),
                  ),
              ],
            ),
          ),
          const Divider(height: 1),
          if (logs.isEmpty)
            const Padding(
              padding: EdgeInsets.all(24),
              child: Center(
                child: Text(
                  '로그가 없습니다',
                  style: TextStyle(color: Colors.grey),
                ),
              ),
            )
          else
            ListView.separated(
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              itemCount: logs.length > 20 ? 20 : logs.length,
              separatorBuilder: (_, _) => const Divider(height: 1),
              itemBuilder: (context, index) => _LogTile(entry: logs[index]),
            ),
        ],
      ),
    );
  }
}

class _LogTile extends StatelessWidget {
  final LogEntry entry;
  const _LogTile({required this.entry});

  @override
  Widget build(BuildContext context) {
    Color levelColor;
    IconData levelIcon;

    switch (entry.level) {
      case 2:
        levelColor = Colors.red;
        levelIcon = Icons.error;
        break;
      case 1:
        levelColor = Colors.orange;
        levelIcon = Icons.warning_amber;
        break;
      default:
        levelColor = Colors.blue;
        levelIcon = Icons.info_outline;
    }

    return ListTile(
      dense: true,
      leading: Icon(levelIcon, color: levelColor, size: 20),
      title: Text(
        entry.message,
        style: TextStyle(
          fontSize: 13,
          color: entry.isEmergency ? Colors.red[700] : null,
          fontWeight: entry.isEmergency ? FontWeight.bold : FontWeight.normal,
        ),
      ),
      trailing: Text(
        entry.time,
        style: TextStyle(fontSize: 11, color: Colors.grey[500]),
      ),
    );
  }
}
