import "package:flutter/material.dart";
import "../services/firebase_service.dart";

class TriggerScreen extends StatefulWidget {
  const TriggerScreen({super.key});
  @override
  State<TriggerScreen> createState() => _TriggerScreenState();
}

class _TriggerScreenState extends State<TriggerScreen> {
  int _tierA = 1;
  int _tierB = 2;
  String? _feedback;
  bool _sending = false;

  Future<void> _trigger(String vehicleId, int tier) async {
    if (_sending) return;
    setState(() {
      _sending  = true;
      _feedback = null;
    });
    try {
      await FirebaseService.pushTriggerRequest(vehicleId, tier);
      setState(() => _feedback =
          "✓  Request sent — Vehicle $vehicleId · Tier $tier · Source: flutter_mobile\n"
          "    Physical operator: press the vehicle button to activate corridor.");
    } catch (e) {
      setState(() => _feedback = "✗  Failed: $e");
    } finally {
      setState(() => _sending = false);
    }
  }

  Widget _vehicleSection(String vehicleId, int selectedTier,
      ValueChanged<int> onTierChange) {
    final isA = vehicleId == "A";
    final accent = isA ? const Color(0xFF2196F3) : const Color(0xFFFF9800);

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          // Header
          Row(children: [
            CircleAvatar(
              radius: 18,
              backgroundColor: accent.withOpacity(0.15),
              child: Icon(Icons.local_hospital, color: accent, size: 20),
            ),
            const SizedBox(width: 12),
            Text("Vehicle $vehicleId",
                style: TextStyle(
                    fontSize: 18,
                    fontWeight: FontWeight.bold,
                    color: accent)),
          ]),
          const SizedBox(height: 18),

          // Tier selector
          Text("PRIORITY TIER",
              style: const TextStyle(
                  fontSize: 10,
                  color: Colors.white38,
                  letterSpacing: 1.1)),
          const SizedBox(height: 8),
          SegmentedButton<int>(
            segments: const [
              ButtonSegment(
                  value: 1,
                  label: Text("Tier 1 — Critical"),
                  icon: Icon(Icons.priority_high, size: 16)),
              ButtonSegment(
                  value: 2,
                  label: Text("Tier 2 — Standard"),
                  icon: Icon(Icons.medical_services_outlined, size: 16)),
            ],
            selected: {selectedTier},
            onSelectionChanged: (s) => onTierChange(s.first),
            style: ButtonStyle(
              side: MaterialStateProperty.all(
                  BorderSide(color: accent.withOpacity(0.4))),
            ),
          ),
          const SizedBox(height: 20),

          // Trigger button
          SizedBox(
            width: double.infinity,
            height: 52,
            child: FilledButton.icon(
              icon: _sending
                  ? const SizedBox(
                      width: 18,
                      height: 18,
                      child: CircularProgressIndicator(
                          strokeWidth: 2, color: Colors.white))
                  : const Icon(Icons.emergency, size: 20),
              label: Text(
                _sending
                    ? "SENDING..."
                    : "TRIGGER VEHICLE $vehicleId",
                style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.bold,
                    letterSpacing: 0.8),
              ),
              style: FilledButton.styleFrom(
                backgroundColor: accent,
                shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(10)),
              ),
              onPressed: _sending ? null : () => _trigger(vehicleId, selectedTier),
            ),
          ),
        ]),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: const Color(0xFF111111),
        title: const Row(children: [
          Icon(Icons.emergency, color: Colors.red),
          SizedBox(width: 8),
          Text("Emergency Trigger",
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600)),
        ]),
      ),
      body: SingleChildScrollView(
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          // Context note
          const Padding(
            padding: EdgeInsets.fromLTRB(16, 16, 16, 4),
            child: Text(
              "Secondary/failsafe trigger path — writes a request to Firebase, "
              "logged by the bridge. Physical button on the vehicle unit is always primary.",
              style: TextStyle(
                  color: Colors.white38, fontSize: 12, height: 1.5),
            ),
          ),

          _vehicleSection("A", _tierA, (t) => setState(() => _tierA = t)),
          _vehicleSection("B", _tierB, (t) => setState(() => _tierB = t)),

          // Feedback banner
          if (_feedback != null)
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 8, 16, 0),
              child: AnimatedContainer(
                duration: const Duration(milliseconds: 300),
                padding: const EdgeInsets.all(14),
                decoration: BoxDecoration(
                  color: _feedback!.startsWith("✓")
                      ? const Color(0xFF00C853).withOpacity(0.12)
                      : Colors.red.withOpacity(0.12),
                  borderRadius: BorderRadius.circular(10),
                  border: Border.all(
                    color: _feedback!.startsWith("✓")
                        ? const Color(0xFF00C853)
                        : Colors.red,
                    width: 1,
                  ),
                ),
                child: Text(
                  _feedback!,
                  style: TextStyle(
                    fontSize: 12,
                    height: 1.6,
                    color: _feedback!.startsWith("✓")
                        ? const Color(0xFF00C853)
                        : Colors.red,
                  ),
                ),
              ),
            ),

          const SizedBox(height: 24),

          // Demo script quote
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: const Color(0xFF1A1A1A),
                borderRadius: BorderRadius.circular(8),
                border: const Border(
                    left: BorderSide(color: Color(0xFF00C853), width: 3)),
              ),
              child: const Text(
                '"If the driver can\'t reach the physical button, the co-driver triggers '
                'the same request from their phone. Same event, same priority logic, '
                'no difference to the corridor."',
                style: TextStyle(
                    fontSize: 12,
                    color: Colors.white54,
                    fontStyle: FontStyle.italic,
                    height: 1.6),
              ),
            ),
          ),
          const SizedBox(height: 24),
        ]),
      ),
    );
  }
}
