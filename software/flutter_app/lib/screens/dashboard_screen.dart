import "package:flutter/material.dart";
import "../services/firebase_service.dart";

class DashboardScreen extends StatelessWidget {
  const DashboardScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: const Color(0xFF111111),
        title: const Row(children: [
          Icon(Icons.traffic, color: Color(0xFF00C853)),
          SizedBox(width: 8),
          Text("Green Corridor Monitor",
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600)),
        ]),
        actions: [
          // Live bridge connection indicator
          Padding(
            padding: const EdgeInsets.only(right: 14),
            child: StreamBuilder<bool>(
              stream: FirebaseService.bridgeOnlineStream(),
              builder: (_, snap) {
                final online = snap.data ?? false;
                return Tooltip(
                  message: online ? "Bridge online" : "Bridge offline",
                  child: Icon(
                    online ? Icons.wifi : Icons.wifi_off,
                    size: 18,
                    color: online ? const Color(0xFF00C853) : Colors.red,
                  ),
                );
              },
            ),
          ),
        ],
      ),
      body: Column(children: [
        // ── Node Status Cards ────────────────────────────────
        Padding(
          padding: const EdgeInsets.fromLTRB(12, 12, 12, 4),
          child: StreamBuilder<Map<int, String>>(
            stream: FirebaseService.nodeStatusStream(),
            builder: (_, snap) {
              final status = snap.data ?? {1: "RED", 2: "RED", 3: "RED"};
              return Row(
                children: [1, 2, 3]
                    .map((id) => Expanded(
                          child: Padding(
                            padding: const EdgeInsets.symmetric(horizontal: 4),
                            child: _NodeCard(
                                nodeId: id, state: status[id] ?? "RED"),
                          ),
                        ))
                    .toList(),
              );
            },
          ),
        ),

        // ── Log header ───────────────────────────────────────
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
          child: Row(children: [
            const Icon(Icons.list_alt, size: 14, color: Colors.white38),
            const SizedBox(width: 6),
            Text("Live Event Log",
                style: Theme.of(context)
                    .textTheme
                    .labelSmall
                    ?.copyWith(color: Colors.white38, letterSpacing: 0.8)),
          ]),
        ),

        // ── Event log list ───────────────────────────────────
        Expanded(
          child: StreamBuilder<List<Map<String, dynamic>>>(
            stream: FirebaseService.eventsStream(),
            builder: (_, snap) {
              if (snap.hasError) {
                return Center(
                    child: Text("Firebase error: ${snap.error}",
                        style: const TextStyle(color: Colors.red)));
              }
              if (!snap.hasData || snap.data!.isEmpty) {
                return const Center(
                  child: Column(mainAxisSize: MainAxisSize.min, children: [
                    CircularProgressIndicator(strokeWidth: 2),
                    SizedBox(height: 12),
                    Text("Waiting for events from Node 1...",
                        style: TextStyle(color: Colors.white38, fontSize: 13)),
                  ]),
                );
              }
              return ListView.separated(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                itemCount: snap.data!.length,
                separatorBuilder: (_, __) =>
                    const Divider(height: 1, color: Color(0xFF222222)),
                itemBuilder: (_, i) => _EventTile(event: snap.data![i]),
              );
            },
          ),
        ),
      ]),
    );
  }
}

// ─── Node Status Card ─────────────────────────────────────────
class _NodeCard extends StatelessWidget {
  final int nodeId;
  final String state;
  const _NodeCard({required this.nodeId, required this.state});

  Color get _ledColor {
    switch (state) {
      case "GREEN":  return const Color(0xFF00C853);
      case "YELLOW": return const Color(0xFFFFD600);
      default:       return const Color(0xFFD50000);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 14, horizontal: 8),
      decoration: BoxDecoration(
        color: const Color(0xFF1A1A1A),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: _ledColor.withOpacity(0.5), width: 1.5),
      ),
      child: Column(children: [
        Text("NODE $nodeId",
            style: const TextStyle(
                fontSize: 10, color: Colors.white38, letterSpacing: 1.2)),
        const SizedBox(height: 10),
        // LED dot with glow
        Container(
          width: 26,
          height: 26,
          decoration: BoxDecoration(
            color: _ledColor,
            shape: BoxShape.circle,
            boxShadow: [
              BoxShadow(
                  color: _ledColor.withOpacity(0.65), blurRadius: 10, spreadRadius: 2)
            ],
          ),
        ),
        const SizedBox(height: 8),
        Text(state,
            style: TextStyle(
                fontSize: 10, fontWeight: FontWeight.bold, color: _ledColor)),
      ]),
    );
  }
}

// ─── Event Log Tile ───────────────────────────────────────────
class _EventTile extends StatelessWidget {
  final Map<String, dynamic> event;
  const _EventTile({required this.event});

  Color get _color {
    switch (event["type"]) {
      case "WINNER_DECIDED":
      case "ARBITRATION":    return const Color(0xFFFFA726);  // orange
      case "PRECLEAR":
      case "EXTENDED":
      case "QUEUED_GREEN":   return const Color(0xFF00C853);  // green
      case "PASSAGE_CONFIRM": return const Color(0xFF29B6F6); // blue
      case "FAILSAFE_REVERT": return const Color(0xFFF44336); // red
      case "ALLRED_BUFFER":   return const Color(0xFFEF5350); // light red
      case "NORMAL":          return Colors.white30;
      default:                return Colors.white24;
    }
  }

  IconData get _icon {
    switch (event["type"]) {
      case "BEACON":          return Icons.wifi_tethering;
      case "ARBITRATION":     return Icons.balance;
      case "WINNER_DECIDED":  return Icons.emoji_events;
      case "PRECLEAR":        return Icons.traffic;
      case "EXTENDED":        return Icons.open_in_full;
      case "PASSAGE_CONFIRM": return Icons.check_circle_outline;
      case "QUEUED_GREEN":    return Icons.queue_play_next;
      case "FAILSAFE_REVERT": return Icons.warning_amber_rounded;
      case "ALLRED_BUFFER":   return Icons.pause_circle_outline;
      case "NORMAL":          return Icons.loop;
      default:                return Icons.info_outline;
    }
  }

  String get _text {
    final t  = event["type"]       ?? "";
    final v  = event["vehicle_id"] ?? "";
    final n  = event["node_id"] != null ? "Node${event["node_id"]}" : "";
    switch (t) {
      case "ARBITRATION":
        return "$n  Veh $v  tier=${event["tier"]}  dist=${event["dist_dial"]}  score=${event["score"]}";
      case "WINNER_DECIDED":
        return "$n  ▶  WINNER → Vehicle $v";
      case "PRECLEAR":
        final q = event["queued_vehicle"];
        return "$n  GREEN for Veh $v${q != null ? "  (queued: $q)" : ""}";
      case "EXTENDED":
        return "$n  EXTENDED GREEN — Veh $v  (both vehicles close)";
      case "PASSAGE_CONFIRM":
        return "${n.isEmpty ? "" : "$n  "}Veh $v passage confirmed";
      case "QUEUED_GREEN":
        return "$n  Immediate GREEN → queued Veh $v";
      case "NORMAL":
        return "$n  → Normal traffic cycle";
      case "FAILSAFE_REVERT":
        return "$n  ★ FAILSAFE — auto-reverted after 15s";
      case "ALLRED_BUFFER":
        return "$n  All-RED safety buffer (3s)";
      default:
        return t;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 5, horizontal: 4),
      child: Row(children: [
        Icon(_icon, size: 15, color: _color),
        const SizedBox(width: 10),
        Expanded(
          child: Text(_text,
              style: TextStyle(fontSize: 12, color: _color, height: 1.4)),
        ),
      ]),
    );
  }
}
