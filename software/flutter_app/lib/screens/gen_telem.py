import os

p = r'C:\Users\athma\Downloads\CET Hackathon\software\flutter_app\lib\screens\dashboard_screen.dart'
with open(p, 'r', encoding='utf-8') as f:
    code = f.read()

telemetry_code = '''
// ─── Telemetry Panel ──────────────────────────────────────────
class _TelemetryPanel extends StatefulWidget {
  const _TelemetryPanel();
  @override
  State<_TelemetryPanel> createState() => _TelemetryPanelState();
}

class _TelemetryPanelState extends State<_TelemetryPanel> {
  double distance = 3800.0;
  double speed = 65.0;
  
  @override
  void initState() {
    super.initState();
    _simulate();
  }
  
  void _simulate() async {
    while (mounted) {
      await Future.delayed(const Duration(milliseconds: 1500));
      if (!mounted) break;
      setState(() {
        if (distance > 100) {
          distance -= (speed * 1000 / 3600) * 1.5; 
        } else {
          distance = 3800.0;
          speed = 65.0;
        }
        speed += (distance % 5 == 0) ? 2.0 : -1.0;
        if (speed > 80) speed = 80;
        if (speed < 40) speed = 40;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    int etaSeconds = (distance / (speed * 1000 / 3600)).round();
    if (etaSeconds < 0) etaSeconds = 0;
    
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 16),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: const Color(0xFF0B101E),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: Colors.white.withOpacity(0.05)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const Icon(Icons.speed, color: Color(0xFF1E88E5), size: 16),
              const SizedBox(width: 8),
              Text("VEHICLE APPROACH TELEMETRY (SIMULATED)", 
                style: TextStyle(color: Colors.white.withOpacity(0.5), fontSize: 11, fontWeight: FontWeight.bold, letterSpacing: 1.0)),
            ],
          ),
          const SizedBox(height: 16),
          Row(
            children: [
              _buildStat("SPEED", "${speed.toInt()} km/h", Icons.trending_up, Colors.greenAccent),
              _buildStat("DISTANCE", "${distance.toInt()} m", Icons.straighten, Colors.blueAccent),
              _buildStat("ETA", "00:${etaSeconds.toString().padLeft(2, '0')}", Icons.timer, etaSeconds < 30 ? Colors.redAccent : Colors.orangeAccent),
            ],
          ),
          const SizedBox(height: 16),
          ClipRRect(
            borderRadius: BorderRadius.circular(4),
            child: LinearProgressIndicator(
              value: 1.0 - (distance / 3800.0).clamp(0.0, 1.0),
              backgroundColor: Colors.white.withOpacity(0.05),
              valueColor: AlwaysStoppedAnimation<Color>(etaSeconds < 30 ? Colors.redAccent : const Color(0xFF1E88E5)),
              minHeight: 6,
            ),
          )
        ],
      ),
    );
  }
  
  Widget _buildStat(String label, String value, IconData icon, Color color) {
    return Expanded(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(value, style: const TextStyle(fontFamily: 'monospace', fontSize: 24, fontWeight: FontWeight.bold, color: Colors.white)),
          const SizedBox(height: 4),
          Row(
            children: [
              Icon(icon, size: 12, color: color),
              const SizedBox(width: 4),
              Text(label, style: TextStyle(fontSize: 10, color: Colors.white.withOpacity(0.5), letterSpacing: 1.0)),
            ],
          )
        ],
      ),
    );
  }
}
'''

if '_TelemetryPanel' not in code:
    code += '\n' + telemetry_code
    target = 'const SizedBox(height: 20),'
    replacement = 'const SizedBox(height: 16),\n          const _TelemetryPanel(),\n          const SizedBox(height: 16),'
    code = code.replace(target, replacement)
    
    with open(p, 'w', encoding='utf-8') as f:
        f.write(code)
    print("Telemetry injected")
else:
    print("Already injected")
