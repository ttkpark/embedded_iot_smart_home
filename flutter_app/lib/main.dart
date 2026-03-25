import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'services/websocket_service.dart';
import 'screens/dashboard_screen.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  final prefs = await SharedPreferences.getInstance();
  final savedIp = prefs.getString('server_ip') ?? '192.168.0.1';
  final savedPort = prefs.getInt('server_port') ?? 80;

  final wsService = WebSocketService();
  wsService.configure(ip: savedIp, port: savedPort);
  wsService.connect();

  runApp(SmartHomeApp(wsService: wsService));
}

class SmartHomeApp extends StatelessWidget {
  final WebSocketService wsService;

  const SmartHomeApp({super.key, required this.wsService});

  @override
  Widget build(BuildContext context) {
    return ChangeNotifierProvider.value(
      value: wsService,
      child: MaterialApp(
        title: 'Smart Home',
        debugShowCheckedModeBanner: false,
        theme: ThemeData(
          colorSchemeSeed: Colors.blueGrey,
          useMaterial3: true,
          brightness: Brightness.light,
          appBarTheme: const AppBarTheme(
            centerTitle: true,
            elevation: 1,
          ),
        ),
        darkTheme: ThemeData(
          colorSchemeSeed: Colors.blueGrey,
          useMaterial3: true,
          brightness: Brightness.dark,
          appBarTheme: const AppBarTheme(
            centerTitle: true,
            elevation: 1,
          ),
        ),
        home: const DashboardScreen(),
      ),
    );
  }
}
