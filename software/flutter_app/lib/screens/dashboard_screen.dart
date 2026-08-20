import "package:flutter/material.dart";
import "../services/firebase_service.dart";

class DashboardScreen extends StatelessWidget {
  const DashboardScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF070B14), // Deep cyber-blue background
      appBar: AppBar(
        elevation: 0,
        backgroundColor: Colors.transparent,
        title: const Row(children: [
          Icon(Icons.radar, color: Color(0xFF00E676), size: 22),
          SizedBox(width: 12),
          Text("Green Corridor Command",
              style: TextStyle(
                fontSize: 18, 
                fontWeight: FontWeight.w700, 
                letterSpacing: 0.5,
                color: Colors.white,
              )),
        ]),
        actions: [
          // Live bridge connection indicator
          Padding(
            padding: const EdgeInsets.only(right: 20),
            child: StreamBuilder<bool>(
              stream: FirebaseService.bridgeOnlineStream(),
              builder: (_, snap) {
                final online = snap.data ?? false;
                return Container(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                  decoration: BoxDecoration(
                    color: online ? const Color(0xFF00E676).withOpacity(0.1) : Colors.red.withOpacity(0.1),
                    borderRadius: BorderRadius.circular(20),
                    border: Border.all(
                      color: online ? const Color(0xFF00E676).withOpacity(0.3) : Colors.red.withOpacity(0.3),
                    ),
                  ),
                  child: Row(
                    children: [
                      Icon(
                        online ? Icons.wifi : Icons.wifi_off,
                        size: 14,
                        color: online ? const Color(0xFF00E676) : Colors.red,
                      ),
                      const SizedBox(width: 6),
                      Text(
                        online ? "BRIDGE ONLINE" : "OFFLINE",
                        style: TextStyle(
                          fontSize: 11,
                          fontWeight: FontWeight.bold,
                          letterSpacing: 1,
                          color: online ? const Color(0xFF00E676) : Colors.red,
                        ),
                      )
                    ],
                  ),
                );
              },
            ),
          ),
        ],
      ),
      body: Container(
        decoration: const BoxDecoration(
          gradient: RadialGradient(
            colors: [Color(0xFF0D1629), Color(0xFF070B14)],
            radius: 1.5,
            center: Alignment.topCenter,
          ),
        ),
        child: Column(children: [
          const SizedBox(height: 10),
          // ── Node Status Cards ──
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
            child: StreamBuilder<Map<int, String>>(
              stream: FirebaseService.nodeStatusStream(),
              builder: (_, snap) {
                final status = snap.data ?? {1: "RED", 2: "RED", 3: "RED"};
                return Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [1, 2, 3].map((id) {
                    return Expanded(
                      child: Padding(
                        padding: const EdgeInsets.symmetric(horizontal: 8),
                        child: ConstrainedBox(
                          constraints: const BoxConstraints(maxWidth: 280),
                          child: _NodeCard(nodeId: id, state: status[id] ?? "RED"),
                        ),
                      ),
                    );
                  }).toList(),
                );
              },
            ),
          ),
          
          const SizedBox(height: 20),

          // ── Log header ──
          Padding(
            padding: const EdgeInsets.fromLTRB(24, 0, 24, 8),
            child: Row(children: [
              const Icon(Icons.receipt_long, size: 16, color: Colors.white54),
              const SizedBox(width: 8),
              Text("LIVE EVENT STREAM",
                  style: Theme.of(context)
                      .textTheme
                      .labelSmall
                      ?.copyWith(
                        color: Colors.white54, 
                        letterSpacing: 1.2, 
                        fontWeight: FontWeight.w600,
                      )),
            ]),
          ),

          // ── Event log list ──
          Expanded(
            child: Container(
              margin: const EdgeInsets.symmetric(horizontal: 16),
              decoration: BoxDecoration(
                color: const Color(0xFF0B101E).withOpacity(0.5),
                borderRadius: const BorderRadius.vertical(top: Radius.circular(20)),
                border: Border.all(color: Colors.white.withOpacity(0.05)),
              ),
              child: StreamBuilder<List<Map<String, dynamic>>>(
                stream: FirebaseService.eventsStream(),
                builder: (_, snap) {
                  if (snap.hasError) {
                    return Center(
                        child: Text("Connection error: ${snap.error}",
                            style: const TextStyle(color: Colors.red)));
                  }
                  if (!snap.hasData || snap.data!.isEmpty) {
                    return const Center(
                      child: Column(mainAxisSize: MainAxisSize.min, children: [
                        CircularProgressIndicator(strokeWidth: 2, color: Color(0xFF00E676)),
                        SizedBox(height: 16),
                        Text("Awaiting telemetry from Node 1...",
                            style: TextStyle(color: Colors.white54, fontSize: 13, letterSpacing: 0.5)),
                      ]),
                    );
                  }
                  return ListView.builder(
                    padding: const EdgeInsets.all(12),
                    itemCount: snap.data!.length,
                    itemBuilder: (_, i) => _EventTile(event: snap.data![i]),
                  );
                },
              ),
            ),
          ),
        ]),
      ),
    );
  }
}

// ─── Node Status Card ─────────────────────────────────────────
class _NodeCard extends StatelessWidget {
  final int nodeId;
  final String state;
  const _NodeCard({required this.nodeId, required this.state});

  Widget _buildLight(Color onColor, bool isOn) {
    return AnimatedContainer(
      duration: const Duration(milliseconds: 300),
      curve: Curves.easeOut,
      width: 44,
      height: 44,
      margin: const EdgeInsets.symmetric(vertical: 8),
      decoration: BoxDecoration(
        shape: BoxShape.circle,
        // When off, look like dark recessed glass. When on, bright vivid color.
        gradient: isOn 
            ? RadialGradient(
                colors: [Colors.white, onColor, onColor.withOpacity(0.8)],
                stops: const [0.0, 0.4, 1.0],
              )
            : RadialGradient(
                colors: [Colors.black, Colors.black87, onColor.withOpacity(0.05)],
              ),
        border: Border.all(
          color: isOn ? onColor.withOpacity(0.8) : Colors.white.withOpacity(0.02),
          width: 2,
        ),
        boxShadow: isOn
            ? [
                BoxShadow(color: onColor.withOpacity(0.6), blurRadius: 20, spreadRadius: 6),
                BoxShadow(color: onColor.withOpacity(0.3), blurRadius: 40, spreadRadius: 15),
              ]
            : [
                const BoxShadow(color: Colors.black54, blurRadius: 4)
              ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 24, horizontal: 12),
      decoration: BoxDecoration(
        gradient: const LinearGradient(
          colors: [Color(0xFF141D2D), Color(0xFF0D1421)],
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
        ),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08), width: 1),
        boxShadow: [
          BoxShadow(color: Colors.black.withOpacity(0.5), blurRadius: 20, offset: const Offset(0, 10))
        ],
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text("INTERSECTION",
              style: TextStyle(
                  fontSize: 10, fontWeight: FontWeight.bold, color: Colors.white.withOpacity(0.4), letterSpacing: 2.0)),
          Text("NODE $nodeId",
              style: const TextStyle(
                  fontSize: 20, fontWeight: FontWeight.w800, color: Colors.white, letterSpacing: 1.5)),
          const SizedBox(height: 24),
          // Classic Traffic Light Housing (Aesthetic version)
          Container(
            padding: const EdgeInsets.symmetric(vertical: 16, horizontal: 20),
            decoration: BoxDecoration(
              color: const Color(0xFF05070A),
              borderRadius: BorderRadius.circular(40),
              border: Border.all(color: Colors.white.withOpacity(0.05), width: 2),
              boxShadow: [
                BoxShadow(color: Colors.black.withOpacity(0.8), blurRadius: 10, offset: const Offset(0, 8)),
                BoxShadow(color: Colors.black.withOpacity(0.5), blurRadius: 15, offset: const Offset(0, 5)),
              ]
            ),
            child: Column(
              children: [
                _buildLight(const Color(0xFFFF2A2A), state == "RED" || state == "ALLRED_BUFFER"),
                _buildLight(const Color(0xFFFFC400), state == "YELLOW"),
                _buildLight(const Color(0xFF00E676), state == "GREEN"),
              ],
            ),
          ),
          const SizedBox(height: 28),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 6),
            decoration: BoxDecoration(
              color: _getStateColor(state).withOpacity(0.1),
              borderRadius: BorderRadius.circular(20),
              border: Border.all(color: _getStateColor(state).withOpacity(0.3)),
            ),
            child: Text(
              state.replaceAll("_", " "),
              style: TextStyle(
                fontSize: 12,
                fontWeight: FontWeight.bold,
                letterSpacing: 1.2,
                color: _getStateColor(state),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Color _getStateColor(String state) {
    if (state == "GREEN") return const Color(0xFF00E676);
    if (state == "YELLOW") return const Color(0xFFFFC400);
    return const Color(0xFFFF2A2A);
  }
}

// ─── Event Log Tile ───────────────────────────────────────────
class _EventTile extends StatelessWidget {
  final Map<String, dynamic> event;
  const _EventTile({required this.event});

  Color get _color {
    switch (event["type"]) {
      case "WINNER_DECIDED":
      case "ARBITRATION":    return const Color(0xFFFFB74D);  // soft orange
      case "PRECLEAR":
      case "EXTENDED":
      case "QUEUED_GREEN":   return const Color(0xFF00E676);  // bright green
      case "PASSAGE_CONFIRM": return const Color(0xFF4FC3F7); // light blue
      case "FAILSAFE_REVERT": return const Color(0xFFE57373); // soft red
      case "ALLRED_BUFFER":   return const Color(0xFFFF8A65); // deep orange
      case "NORMAL":          return Colors.white54;
      default:                return Colors.white38;
    }
  }

  IconData get _icon {
    switch (event["type"]) {
      case "BEACON":          return Icons.wifi_tethering;
      case "ARBITRATION":     return Icons.memory;
      case "WINNER_DECIDED":  return Icons.military_tech;
      case "PRECLEAR":        return Icons.rocket_launch;
      case "EXTENDED":        return Icons.all_out;
      case "PASSAGE_CONFIRM": return Icons.task_alt;
      case "QUEUED_GREEN":    return Icons.queue_play_next;
      case "FAILSAFE_REVERT": return Icons.gpp_bad;
      case "ALLRED_BUFFER":   return Icons.front_hand;
      case "NORMAL":          return Icons.sync;
      default:                return Icons.info_outline;
    }
  }

  String get _text {
    final t  = event["type"]       ?? "";
    final v  = event["vehicle_id"] ?? "";
    final n  = event["node_id"] != null ? "Node ${event["node_id"]}" : "";
    switch (t) {
      case "ARBITRATION":
        return "$n: Veh $v [Tier ${event["tier"]}]  dist=${event["dist_dial"]}  →  Score: ${event["score"]}";
      case "WINNER_DECIDED":
        return "$n: ARBITRATION WON → Vehicle $v overrides intersection";
      case "PRECLEAR":
        final q = event["queued_vehicle"];
        return "$n: PRE-CLEARING GREEN for Vehicle $v${q != null ? " (Veh $q queued)" : ""}";
      case "EXTENDED":
        return "$n: EXTENDED GREEN — Maintaining for Vehicle $v";
      case "PASSAGE_CONFIRM":
        return "${n.isEmpty ? "" : "$n: "}Vehicle $v passage confirmed securely";
      case "QUEUED_GREEN":
        return "$n: IMMEDIATE GREEN → Queued Vehicle $v";
      case "NORMAL":
        return "$n: Returned to standard automated traffic cycle";
      case "FAILSAFE_REVERT":
        return "$n: FAILSAFE TRIGGERED — Reverting after 15s timeout";
      case "ALLRED_BUFFER":
        return "$n: Executing All-RED safety buffer (3.0s)";
      default:
        return t;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 16),
      decoration: BoxDecoration(
        color: Colors.white.withOpacity(0.02),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: _color.withOpacity(0.1)),
      ),
      child: Row(children: [
        Container(
          padding: const EdgeInsets.all(8),
          decoration: BoxDecoration(
            color: _color.withOpacity(0.1),
            shape: BoxShape.circle,
          ),
          child: Icon(_icon, size: 16, color: _color),
        ),
        const SizedBox(width: 16),
        Expanded(
          child: Text(_text,
              style: TextStyle(
                fontSize: 13, 
                color: Colors.white.withOpacity(0.9), 
                height: 1.4,
                letterSpacing: 0.3,
              )),
        ),
      ]),
    );
  }
}
