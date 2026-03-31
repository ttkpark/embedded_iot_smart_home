import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'services/websocket_service.dart';
import 'services/ble_service.dart';
import 'screens/dashboard_screen.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  final prefs = await SharedPreferences.getInstance();
  final savedIp = prefs.getString('server_ip') ?? '192.168.0.1';
  final savedPort = prefs.getInt('server_port') ?? 80;

  final wsService = WebSocketService();
  wsService.configure(ip: savedIp, port: savedPort);
  wsService.connect();

  final bleService = BleService();

  runApp(SmartHomeApp(wsService: wsService, bleService: bleService));
}

class SmartHomeApp extends StatelessWidget {
  final WebSocketService wsService;
  final BleService bleService;

  const SmartHomeApp({
    super.key,
    required this.wsService,
    required this.bleService,
  });

  @override
  Widget build(BuildContext context) {
    return MultiProvider(
      providers: [
        ChangeNotifierProvider.value(value: wsService),
        ChangeNotifierProvider.value(value: bleService),
      ],
      child: MaterialApp(
        title: 'Smart Home',
        debugShowCheckedModeBanner: false,
        theme: ThemeData(
          colorSchemeSeed: Colors.blueGrey,
          useMaterial3: true,
          brightness: Brightness.light,
          appBarTheme: const AppBarTheme(centerTitle: true, elevation: 1),
        ),
        darkTheme: ThemeData(
          colorSchemeSeed: Colors.blueGrey,
          useMaterial3: true,
          brightness: Brightness.dark,
          appBarTheme: const AppBarTheme(centerTitle: true, elevation: 1),
        ),
        home: const DashboardScreen(),
      ),
    );
  }
}
