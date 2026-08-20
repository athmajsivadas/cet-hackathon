import "package:flutter/material.dart";
import "package:firebase_core/firebase_core.dart";
import "firebase_options.dart";
import "screens/dashboard_screen.dart";
import "screens/trigger_screen.dart";

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Firebase.initializeApp(
    options: DefaultFirebaseOptions.currentPlatform,
  );
  runApp(const GreenCorridorApp());
}

class GreenCorridorApp extends StatelessWidget {
  const GreenCorridorApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: "Green Corridor",
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF00C853),
          brightness: Brightness.dark,
        ),
        useMaterial3: true,
        scaffoldBackgroundColor: const Color(0xFF0D0D0D),
        cardTheme: CardTheme(
          color: const Color(0xFF1A1A1A),
          elevation: 0,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(12),
            side: const BorderSide(color: Color(0xFF2A2A2A)),
          ),
        ),
      ),
      home: const MainShell(),
    );
  }
}

class MainShell extends StatefulWidget {
  const MainShell({super.key});
  @override
  State<MainShell> createState() => _MainShellState();
}

class _MainShellState extends State<MainShell> {
  int _tab = 0;

  final List<Widget> _screens = const [
    DashboardScreen(),
    TriggerScreen(),
  ];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: _screens[_tab],
      bottomNavigationBar: NavigationBar(
        selectedIndex: _tab,
        backgroundColor: const Color(0xFF111111),
        onDestinationSelected: (i) => setState(() => _tab = i),
        destinations: const [
          NavigationDestination(
            icon: Icon(Icons.dashboard_outlined),
            selectedIcon: Icon(Icons.dashboard),
            label: "Dashboard",
          ),
          NavigationDestination(
            icon: Icon(Icons.emergency_outlined),
            selectedIcon: Icon(Icons.emergency),
            label: "Trigger",
          ),
        ],
      ),
    );
  }
}
