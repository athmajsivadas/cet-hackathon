#!/usr/bin/env python3
"""
=======================================================
 AI-Powered Emergency Green Corridor
 Serial → Firebase Realtime Database Bridge
 INTIUM 2026 · Smart City Track
=======================================================

 Reads Node 1's Serial output (existing firmware, zero changes needed)
 and mirrors every event to Firebase in real time.

 Run on the laptop that has Node 1 connected via USB.
 Use a phone hotspot — do NOT rely on venue WiFi.

 Usage:
   python bridge.py --port COM3          (Windows)
   python bridge.py --port /dev/ttyUSB0  (Linux/Mac)

 Setup:
   pip install -r requirements.txt
   → Edit FIREBASE_URL below with your project URL.

=======================================================
"""

import serial
import requests
import re
import json
import time
import argparse

# ── CONFIG — EDIT BEFORE RUNNING ────────────────────────────
FIREBASE_URL = "https://alt-f4-17fe1-default-rtdb.firebaseio.com"
BAUD_RATE    = 115200
# ────────────────────────────────────────────────────────────

# Regex patterns match the firmware's exact Serial.printf() format strings.
# Update if you change any print format in the firmware.
PATTERNS = {
    # [BEACON] Veh=A  tier=1  dist=3200  wait_ticks=  6
    'BEACON': re.compile(
        r'\[BEACON\] Veh=([AB])\s+tier=(\d)\s+dist=\s*(\d+)\s+wait_ticks=\s*(\d+)'),

    # [CONFIRM] Veh=A — passage complete
    'CONFIRM': re.compile(r'\[CONFIRM\] Veh=([AB])'),

    # [NODE1 ARB] Vehicle A: tier=1 dist=3200 wait=  6 → score=147
    'ARB_SCORE': re.compile(
        r'\[NODE(\d) ARB\] Vehicle ([AB]): tier=(\d) dist=\s*(\d+) wait=\s*(\d+).*score=\s*(\d+)'),

    # [NODE1 ARB] WINNER → Vehicle A
    'ARB_WINNER': re.compile(r'\[NODE(\d) ARB\] WINNER → Vehicle ([AB])'),

    # [NODE1] → PRECLEAR  winner=A  queued=B
    'PRECLEAR': re.compile(r'\[NODE(\d)\] → PRECLEAR\s+winner=([AB])\s+queued=([AB-])'),

    # [NODE1] → EXTENDED GREEN  (both vehicles close)  winner=A
    'EXTENDED': re.compile(r'\[NODE(\d)\] → EXTENDED GREEN.*winner=([AB])'),

    # [NODE1] Passage confirmed: Veh A cleared
    'PASSAGE': re.compile(r'\[NODE(\d)\] Passage confirmed: Veh ([AB])'),

    # [NODE1] → QUEUED: Immediate GREEN for Veh B
    'QUEUED': re.compile(r'\[NODE(\d)\] → QUEUED.*Veh ([AB])'),

    # [NODE1] → NORMAL (traffic cycle)
    'NORMAL': re.compile(r'\[NODE(\d)\] → NORMAL'),

    # [NODE1] *** FAILSAFE ***
    'FAILSAFE': re.compile(r'\[NODE(\d)\] \*\*\* FAILSAFE \*\*\*'),

    # [NODE1] All-RED buffer (3s before GREEN)
    'ALLRED': re.compile(r'\[NODE(\d)\] All-RED buffer'),

    # [trigger_requests] — logged but not relayed to hardware in this version
    'TRIGGER_LOG': re.compile(r'TRIGGER'),
}

SERVER_TIMESTAMP = {".sv": "timestamp"}  # Firebase server-side timestamp


# ── Firebase helpers ──────────────────────────────────────────
def fb_post(path, data):
    """Append a new child (auto-key) under path."""
    try:
        r = requests.post(f"{FIREBASE_URL}/{path}.json",
                          json=data, timeout=4)
        return r.status_code == 200
    except Exception as e:
        print(f"  [WARN] Firebase POST failed: {e}")
        return False


def fb_put(path, data):
    """Overwrite data at path."""
    try:
        r = requests.put(f"{FIREBASE_URL}/{path}.json",
                         json=data, timeout=4)
        return r.status_code == 200
    except Exception as e:
        print(f"  [WARN] Firebase PUT failed: {e}")
        return False


def update_node(node_id, state):
    """Mirror node LED state to Firebase /node_status/{id}."""
    fb_put(f"node_status/{node_id}", {
        "state": state,
        "last_heartbeat": SERVER_TIMESTAMP,
    })


# ── Line parser ───────────────────────────────────────────────
def parse_and_push(raw_line):
    """Match one Serial line and push a typed event to Firebase."""
    line = raw_line.strip()
    if not line:
        return

    print(f"  {line}")   # Echo raw line to console for monitoring
    ts = SERVER_TIMESTAMP

    # BEACON ───────────────────────────────────────────────────
    m = PATTERNS['BEACON'].search(line)
    if m:
        fb_post("events", {
            "type":       "BEACON",
            "vehicle_id": m.group(1),
            "tier":       int(m.group(2)),
            "dist_dial":  int(m.group(3)),
            "wait_ticks": int(m.group(4)),
            "timestamp":  ts,
        })
        return

    # CONFIRM (vehicle passage done) ───────────────────────────
    m = PATTERNS['CONFIRM'].search(line)
    if m:
        fb_post("events", {
            "type":       "PASSAGE_CONFIRM",
            "vehicle_id": m.group(1),
            "timestamp":  ts,
        })
        return

    # ARBITRATION score line ────────────────────────────────────
    m = PATTERNS['ARB_SCORE'].search(line)
    if m:
        fb_post("events", {
            "type":       "ARBITRATION",
            "node_id":    int(m.group(1)),
            "vehicle_id": m.group(2),
            "tier":       int(m.group(3)),
            "dist_dial":  int(m.group(4)),
            "wait_ticks": int(m.group(5)),
            "score":      int(m.group(6)),
            "timestamp":  ts,
        })
        return

    # ARBITRATION winner decided ────────────────────────────────
    m = PATTERNS['ARB_WINNER'].search(line)
    if m:
        fb_post("events", {
            "type":       "WINNER_DECIDED",
            "node_id":    int(m.group(1)),
            "vehicle_id": m.group(2),
            "timestamp":  ts,
        })
        return

    # PRECLEAR — standard sequential queue ─────────────────────
    m = PATTERNS['PRECLEAR'].search(line)
    if m:
        node_id = int(m.group(1))
        queued  = m.group(3)
        fb_post("events", {
            "type":            "PRECLEAR",
            "node_id":         node_id,
            "vehicle_id":      m.group(2),
            "queued_vehicle":  queued if queued not in ('-', '') else None,
            "timestamp":       ts,
        })
        update_node(node_id, "GREEN")
        return

    # EXTENDED — both vehicles close, one wide green window ────
    m = PATTERNS['EXTENDED'].search(line)
    if m:
        node_id = int(m.group(1))
        fb_post("events", {
            "type":       "EXTENDED",
            "node_id":    node_id,
            "vehicle_id": m.group(2),
            "timestamp":  ts,
        })
        update_node(node_id, "GREEN")
        return

    # PASSAGE confirmed at node ─────────────────────────────────
    m = PATTERNS['PASSAGE'].search(line)
    if m:
        fb_post("events", {
            "type":       "PASSAGE_CONFIRM",
            "node_id":    int(m.group(1)),
            "vehicle_id": m.group(2),
            "timestamp":  ts,
        })
        return

    # QUEUED — immediate GREEN for second vehicle ───────────────
    m = PATTERNS['QUEUED'].search(line)
    if m:
        node_id = int(m.group(1))
        fb_post("events", {
            "type":       "QUEUED_GREEN",
            "node_id":    node_id,
            "vehicle_id": m.group(2),
            "timestamp":  ts,
        })
        update_node(node_id, "GREEN")
        return

    # NORMAL — back to standard traffic cycle ───────────────────
    m = PATTERNS['NORMAL'].search(line)
    if m:
        node_id = int(m.group(1))
        fb_post("events", {
            "type":      "NORMAL",
            "node_id":   node_id,
            "timestamp": ts,
        })
        update_node(node_id, "RED")
        return

    # FAILSAFE auto-revert ──────────────────────────────────────
    m = PATTERNS['FAILSAFE'].search(line)
    if m:
        node_id = int(m.group(1))
        fb_post("events", {
            "type":      "FAILSAFE_REVERT",
            "node_id":   node_id,
            "timestamp": ts,
        })
        update_node(node_id, "RED")
        return

    # ALL-RED safety buffer ─────────────────────────────────────
    m = PATTERNS['ALLRED'].search(line)
    if m:
        node_id = int(m.group(1))
        fb_post("events", {
            "type":      "ALLRED_BUFFER",
            "node_id":   node_id,
            "timestamp": ts,
        })
        update_node(node_id, "RED")
        return


# ── Main ──────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Green Corridor Serial → Firebase Bridge")
    parser.add_argument(
        "--port", default="COM3",
        help="Serial port of Node 1 (e.g. COM3, /dev/ttyUSB0)")
    args = parser.parse_args()

    print("=" * 55)
    print("  Green Corridor — Serial → Firebase Bridge")
    print("  INTIUM 2026 Smart City Track")
    print("=" * 55)
    print(f"  Port      : {args.port} @ {BAUD_RATE} baud")
    print(f"  Firebase  : {FIREBASE_URL}")
    print()

    # Validate config
    if "YOUR-PROJECT-ID" in FIREBASE_URL:
        print("ERROR: Update FIREBASE_URL at the top of this file.")
        print("  e.g. https://green-corridor-abc12-default-rtdb.firebaseio.com")
        return

    # Test Firebase connection
    print("  Testing Firebase... ", end="", flush=True)
    ok = fb_put("bridge_status", {
        "online": True,
        "start_time": SERVER_TIMESTAMP,
    })
    print("OK" if ok else "FAILED — check URL and internet access")
    if not ok:
        return

    # Initialise node LED states to RED (safe default)
    for nid in [1, 2, 3]:
        update_node(nid, "RED")
    print("  Node states → RED (initialised)")
    print()
    print(f"  Listening on {args.port}. Press Ctrl+C to stop.")
    print()

    try:
        ser = serial.Serial(args.port, BAUD_RATE, timeout=1)
        time.sleep(2)   # let port settle
        ser.flushInput()

        while True:
            raw = ser.readline()
            if raw:
                line = raw.decode("utf-8", errors="replace")
                parse_and_push(line)

    except KeyboardInterrupt:
        print("\n  Bridge stopped.")
        fb_put("bridge_status", {"online": False})

    except serial.SerialException as e:
        print(f"\nSerial error: {e}")
        print(f"Check that Node 1 is on {args.port} and no other app (e.g. Arduino IDE Serial Monitor) is using it.")


if __name__ == "__main__":
    main()
