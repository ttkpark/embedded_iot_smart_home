/// Node A에서 수신하는 이벤트 로그 모델
class LogEntry {
  final String time;
  final String message;
  final int level; // 0: info, 1: warning, 2: emergency

  const LogEntry({
    required this.time,
    required this.message,
    required this.level,
  });

  factory LogEntry.fromJson(Map<String, dynamic> json) {
    return LogEntry(
      time: json['time'] as String? ?? '',
      message: json['message'] as String? ?? '',
      level: json['level'] as int? ?? 0,
    );
  }

  bool get isEmergency => level == 2;
  bool get isWarning => level == 1;
  bool get isInfo => level == 0;

  String get levelStr {
    switch (level) {
      case 2:
        return 'EMERGENCY';
      case 1:
        return 'WARNING';
      default:
        return 'INFO';
    }
  }
}
