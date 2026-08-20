import "package:firebase_database/firebase_database.dart";

/// All Firebase Realtime Database operations in one place.
/// Schema mirrors the bridge.py event types and node_status paths exactly.
class FirebaseService {
  static final _db = FirebaseDatabase.instance;

  // ── Live event stream ────────────────────────────────────────
  /// Last 50 events ordered by server timestamp, newest first.
  /// Excludes BEACON lines by default to keep the log readable on stage —
  /// pass [includeBEACON: true] to see raw beacon chatter.
  static Stream<List<Map<String, dynamic>>> eventsStream({
    bool includeBEACON = false,
  }) {
    return _db
        .ref("events")
        .orderByChild("timestamp")
        .limitToLast(50)
        .onValue
        .map((event) {
      final raw = event.snapshot.value as Map<dynamic, dynamic>? ?? {};
      final list = raw.entries
          .map((e) => Map<String, dynamic>.from(e.value as Map))
          .where((e) => includeBEACON || e["type"] != "BEACON")
          .toList()
        ..sort((a, b) {
          final ta = (a["timestamp"] as int?) ?? 0;
          final tb = (b["timestamp"] as int?) ?? 0;
          return tb.compareTo(ta); // newest first
        });
      return list;
    });
  }

  // ── Node LED status stream ───────────────────────────────────
  /// Returns {1: "RED", 2: "GREEN", 3: "YELLOW"} etc., updated live.
  static Stream<Map<int, String>> nodeStatusStream() {
    return _db.ref("node_status").onValue.map((event) {
      final raw = event.snapshot.value as Map<dynamic, dynamic>? ?? {};
      return {
        for (final e in raw.entries)
          int.tryParse(e.key.toString()) ?? 0:
              (e.value as Map?)?["state"]?.toString() ?? "RED",
      };
    });
  }

  // ── Bridge online status ─────────────────────────────────────
  static Stream<bool> bridgeOnlineStream() {
    return _db.ref("bridge_status/online").onValue.map(
          (e) => (e.snapshot.value as bool?) ?? false,
        );
  }

  // ── Trigger request ──────────────────────────────────────────
  /// Writes a trigger request to Firebase.
  /// The bridge script logs receipt. Physical button is still primary trigger.
  static Future<void> pushTriggerRequest(String vehicleId, int tier) {
    return _db.ref("trigger_requests").push().set({
      "vehicle_id": vehicleId,
      "tier":       tier,
      "source":     "flutter_mobile",
      "timestamp":  ServerValue.timestamp,
    });
  }
}
