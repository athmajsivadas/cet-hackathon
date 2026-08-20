import "package:flutter/material.dart";
import "../services/firebase_service.dart";

class TriggerScreen extends StatefulWidget {
  const TriggerScreen({super.key});

  @override
  State<TriggerScreen> createState() => _TriggerScreenState();
}

class _TriggerScreenState extends State<TriggerScreen> {
  int tierA = 1;
  int tierB = 2;
  int targetNodeA = 1; // 1 = Left, 2 = Straight, 3 = Right
  int targetNodeB = 2;

  void _trigger(String vehicleId, int tier, int targetNode) {
    FirebaseService.pushTriggerRequest(vehicleId, tier);
    final dirLabel = targetNode == 1 ? "LEFT (Node 1)" :
                     targetNode == 2 ? "STRAIGHT (Node 2)" : "RIGHT (Node 3)";

    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text("COMMAND SENT: Override initiated for Vehicle $vehicleId → $dirLabel", 
            style: const TextStyle(fontFamily: 'monospace', fontWeight: FontWeight.bold)),
        backgroundColor: vehicleId == 'A' ? const Color(0xFF1E88E5) : const Color(0xFFFF8F00),
        duration: const Duration(seconds: 2),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF070B14),
      appBar: AppBar(
        elevation: 0,
        backgroundColor: Colors.transparent,
        title: const Row(children: [
          Icon(Icons.warning_amber_rounded, color: Color(0xFFFF3D00), size: 22),
          SizedBox(width: 12),
          Text("MANUAL OVERRIDE COMMAND",
              style: TextStyle(
                fontSize: 18, 
                fontWeight: FontWeight.w800, 
                letterSpacing: 1.0,
                color: Colors.white,
              )),
        ]),
      ),
      body: Container(
        decoration: const BoxDecoration(
          gradient: RadialGradient(
            colors: [Color(0xFF0D1629), Color(0xFF070B14)],
            radius: 1.5,
            center: Alignment.topCenter,
          ),
        ),
        child: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            const Padding(
              padding: EdgeInsets.only(bottom: 24),
              child: Text(
                "SELECT TARGET INTERSECTION DIRECTION (LEFT / STRAIGHT / RIGHT) & TRANSMIT OVERRIDE.",
                style: TextStyle(color: Colors.white38, fontSize: 11, fontWeight: FontWeight.bold, letterSpacing: 0.5),
              ),
            ),
            _buildVehicleControlCard(
              id: "A", 
              color: const Color(0xFF1E88E5), 
              tier: tierA, 
              targetNode: targetNodeA,
              onTierChanged: (v) => setState(() => tierA = v),
              onDirectionChanged: (n) => setState(() => targetNodeA = n),
              onTrigger: () => _trigger("A", tierA, targetNodeA),
            ),
            const SizedBox(height: 24),
            _buildVehicleControlCard(
              id: "B", 
              color: const Color(0xFFFF8F00), 
              tier: tierB, 
              targetNode: targetNodeB,
              onTierChanged: (v) => setState(() => tierB = v),
              onDirectionChanged: (n) => setState(() => targetNodeB = n),
              onTrigger: () => _trigger("B", tierB, targetNodeB),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildVehicleControlCard({
    required String id, 
    required Color color, 
    required int tier, 
    required int targetNode,
    required Function(int) onTierChanged,
    required Function(int) onDirectionChanged,
    required VoidCallback onTrigger,
  }) {
    final dirLabel = targetNode == 1 ? "NODE 1 (LEFT)" :
                     targetNode == 2 ? "NODE 2 (STRAIGHT)" : "NODE 3 (RIGHT)";

    return Container(
      padding: const EdgeInsets.all(28),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          colors: [color.withOpacity(0.08), const Color(0xFF0B101E)],
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
        ),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: color.withOpacity(0.2), width: 1.5),
        boxShadow: [
          BoxShadow(color: Colors.black.withOpacity(0.5), blurRadius: 20, offset: const Offset(0, 10))
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                padding: const EdgeInsets.all(8),
                decoration: BoxDecoration(
                  color: color.withOpacity(0.15),
                  borderRadius: BorderRadius.circular(12),
                  border: Border.all(color: color.withOpacity(0.3)),
                ),
                child: Icon(Icons.local_shipping, color: color, size: 24),
              ),
              const SizedBox(width: 16),
              Text("VEHICLE $id", 
                style: TextStyle(fontSize: 24, fontWeight: FontWeight.w900, color: color, letterSpacing: 1.5)
              ),
            ],
          ),
          const SizedBox(height: 24),
          
          // Target Intersection Direction
          Text("TARGET INTERSECTION ROUTE", 
            style: TextStyle(fontSize: 11, fontWeight: FontWeight.bold, color: Colors.white54, letterSpacing: 2.0)
          ),
          const SizedBox(height: 12),
          Row(
            children: [
              _buildSegment(
                title: "LEFT (N1)", 
                icon: Icons.turn_left, 
                isSelected: targetNode == 1, 
                activeColor: const Color(0xFF4FC3F7),
                onTap: () => onDirectionChanged(1),
              ),
              const SizedBox(width: 8),
              _buildSegment(
                title: "STRAIGHT (N2)", 
                icon: Icons.straight, 
                isSelected: targetNode == 2, 
                activeColor: const Color(0xFF81C784),
                onTap: () => onDirectionChanged(2),
              ),
              const SizedBox(width: 8),
              _buildSegment(
                title: "RIGHT (N3)", 
                icon: Icons.turn_right, 
                isSelected: targetNode == 3, 
                activeColor: const Color(0xFFFFB74D),
                onTap: () => onDirectionChanged(3),
              ),
            ],
          ),

          const SizedBox(height: 24),
          Text("PRIORITY TIER LEVEL", 
            style: TextStyle(fontSize: 11, fontWeight: FontWeight.bold, color: Colors.white54, letterSpacing: 2.0)
          ),
          const SizedBox(height: 12),
          Row(
            children: [
              _buildSegment(
                title: "TIER 1 - CRITICAL", 
                icon: Icons.gpp_bad, 
                isSelected: tier == 1, 
                activeColor: const Color(0xFFFF3D00),
                onTap: () => onTierChanged(1),
              ),
              const SizedBox(width: 12),
              _buildSegment(
                title: "TIER 2 - STANDARD", 
                icon: Icons.health_and_safety, 
                isSelected: tier == 2, 
                activeColor: const Color(0xFF00E676),
                onTap: () => onTierChanged(2),
              ),
            ],
          ),
          const SizedBox(height: 32),
          GestureDetector(
            onTap: onTrigger,
            child: Container(
              width: double.infinity,
              padding: const EdgeInsets.symmetric(vertical: 20),
              decoration: BoxDecoration(
                gradient: LinearGradient(
                  colors: [color, color.withOpacity(0.7)],
                  begin: Alignment.topCenter,
                  end: Alignment.bottomCenter,
                ),
                borderRadius: BorderRadius.circular(16),
                boxShadow: [
                  BoxShadow(color: color.withOpacity(0.4), blurRadius: 15, spreadRadius: 2, offset: const Offset(0, 4)),
                  BoxShadow(color: color.withOpacity(0.2), blurRadius: 30, spreadRadius: 10),
                ],
              ),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(Icons.satellite_alt, color: Colors.white, size: 22),
                  const SizedBox(width: 12),
                  Text("TRANSMIT OVERRIDE → $dirLabel",
                    style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w900, color: Colors.white, letterSpacing: 1.2)
                  ),
                ],
              ),
            ),
          )
        ],
      ),
    );
  }

  Widget _buildSegment({
    required String title, 
    required IconData icon, 
    required bool isSelected, 
    required Color activeColor,
    required VoidCallback onTap,
  }) {
    return Expanded(
      child: GestureDetector(
        onTap: onTap,
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 200),
          padding: const EdgeInsets.symmetric(vertical: 14),
          decoration: BoxDecoration(
            color: isSelected ? activeColor.withOpacity(0.15) : Colors.white.withOpacity(0.02),
            borderRadius: BorderRadius.circular(12),
            border: Border.all(
              color: isSelected ? activeColor : Colors.white.withOpacity(0.05),
              width: isSelected ? 2 : 1,
            ),
          ),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(icon, size: 15, color: isSelected ? activeColor : Colors.white38),
              const SizedBox(width: 6),
              Flexible(
                child: Text(title, 
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontSize: 11, 
                    fontWeight: FontWeight.bold, 
                    color: isSelected ? activeColor : Colors.white38,
                    letterSpacing: 0.3,
                  )
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
