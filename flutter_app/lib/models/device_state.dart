/// Node A에서 수신하는 장비 상태 모델
class DeviceState {
  final int patientStat; // 0: 정상, 1: 응급
  final int fanState; // 0: OFF, 1: ON
  final int acTemp; // 0: OFF, 18~30
  final int windowAct; // 0: 정지, 1: 열림, 2: 닫힘
  final int temperature; // 센서 온도 (°C, offset 적용됨)
  final int humidity; // 센서 습도 (%, offset 적용됨)
  final int tempOffset; // 테스트용 온도 offset
  final int humiOffset; // 테스트용 습도 offset

  const DeviceState({
    this.patientStat = 0,
    this.fanState = 0,
    this.acTemp = 0,
    this.windowAct = 0,
    this.temperature = 0,
    this.humidity = 0,
    this.tempOffset = 0,
    this.humiOffset = 0,
  });

  DeviceState copyWith({
    int? patientStat,
    int? fanState,
    int? acTemp,
    int? windowAct,
    int? temperature,
    int? humidity,
    int? tempOffset,
    int? humiOffset,
  }) {
    return DeviceState(
      patientStat: patientStat ?? this.patientStat,
      fanState: fanState ?? this.fanState,
      acTemp: acTemp ?? this.acTemp,
      windowAct: windowAct ?? this.windowAct,
      temperature: temperature ?? this.temperature,
      humidity: humidity ?? this.humidity,
      tempOffset: tempOffset ?? this.tempOffset,
      humiOffset: humiOffset ?? this.humiOffset,
    );
  }

  factory DeviceState.fromJson(Map<String, dynamic> json) {
    return DeviceState(
      patientStat: json['patient_stat'] as int? ?? 0,
      fanState: json['fan_state'] as int? ?? 0,
      acTemp: json['ac_temp'] as int? ?? 0,
      windowAct: json['window_act'] as int? ?? 0,
      temperature: json['temperature'] as int? ?? 0,
      humidity: json['humidity'] as int? ?? 0,
      tempOffset: json['temp_offset'] as int? ?? 0,
      humiOffset: json['humi_offset'] as int? ?? 0,
    );
  }

  bool get isEmergency => patientStat == 1;
  bool get isFanOn => fanState == 1;
  String get acTempStr => acTemp == 0 ? 'OFF' : '$acTemp°C';

  String get windowStr {
    switch (windowAct) {
      case 1:
        return '열림';
      case 2:
        return '닫힘';
      default:
        return '정지';
    }
  }
}
