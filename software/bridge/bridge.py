"""
bridge.py — Green Corridor Hardware-to-Firebase Bridge
INTIUM 2026 · Smart City Track

Reads Serial from Node 1 (USB) and pushes typed event objects
and real-time node status to Firebase Realtime Database.

Run on the laptop that has Node 1 plugged in via USB:
  python bridge.py --port COM3
"""

import sys
import re
import time
import argparse
import requests
import serial
import serial.tools.list_ports

# ── CONFIG ──────────────────────────────────────────────────
FIREBASE_URL = "https://alt-f4-17fe1-default-rtdb.firebaseio.com"
BAUD_RATE    = 115200
# ────────────────────────────────────────────────────────────

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

    # [NODE1] CORRIDOR_SYNC: Node1=GREEN, Node2=RED, Node3=RED
    'CORRIDOR_SYNC': re.compile(
        r'\[NODE1\] CORRIDOR_SYNC:\s+Node1=(GREEN|YELLOW|RED),\s+Node2=(GREEN|YELLOW|RED),\s+Node3=(GREEN|YELLOW|RED)'),

    # [NODE1] SYNC: GREEN
    'SYNC': re.compile(r'\[NODE(\d)\] SYNC: (GREEN|YELLOW|RED)'),

    # [NODE1] → NORMAL (sequential cycle)
    'NORMAL': re.compile(r'\[NODE(\d)\] → NORMAL'),

    # [NODE1] *** FAILSAFE ***
    'FAILSAFE': re.compile(r'\[NODE(\d)\] \*\*\* FAILSAFE \*\*\*'),

    # [NODE1] All-RED buffer (3s before GREEN)
    'ALLRED': re.compile(r'\[NODE(\d)\] All-RED buffer'),
}

SERVER_TIMESTAMP = {".sv": "timestamp"}


def fb_post(path, data):
    """Append a new child under path."""
    try:
        r = requests.post(f"{FIREBASE_URL}/{path}.json", json=data, timeout=4)
        return r.status_code == 200
    except Exception as e:
        print(f"  [WARN] Firebase POST failed: {e}")
        return False


def fb_put(path, data):
    """Overwrite data at path."""
    try:
        r = requests.put(f"{FIREBASE_URL}/{path}.json", json=data, timeout=4)
        return r.status_code == 200
    except Exception as e:
        print(f"  [WARN] Firebase PUT failed: {e}")
        return False


def update_node(node_id, state):
    fb_put(f"node_status/{node_id}", {
        "state": state,
        "last_heartbeat": SERVER_TIMESTAMP,
    })


def update_all_nodes(s1, s2, s3):
    ts = SERVER_TIMESTAMP
    fb_put("node_status/1", {"state": s1, "last_heartbeat": ts})
    fb_put("node_status/2", {"state": s2, "last_heartbeat": ts})
    fb_put("node_status/3", {"state": s3, "last_heartbeat": ts})


def parse_and_push(raw_line):
    """Match one Serial line and push a typed event to Firebase."""
    line = raw_line.strip()
    if not line:
        return

    print(f"  {line}")
    ts = SERVER_TIMESTAMP

    # BEACON
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

    # CONFIRM
    m = PATTERNS['CONFIRM'].search(line)
    if m:
        fb_post("events", {
            "type":       "PASSAGE_CONFIRM",
            "vehicle_id": m.group(1),
            "timestamp":  ts,
        })
        return

    # ARBITRATION score line
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

    # ARBITRATION winner decided
    m = PATTERNS['ARB_WINNER'].search(line)
    if m:
        fb_post("events", {
            "type":       "WINNER_DECIDED",
            "node_id":    int(m.group(1)),
            "vehicle_id": m.group(2),
            "timestamp":  ts,
        })
        return

    # ALLRED Buffer before green
    m = PATTERNS['ALLRED'].search(line)
    if m:
        node_id = int(m.group(1))
        fb_post("events", {
            "type":      "ALLRED_BUFFER",
            "node_id":   node_id,
            "timestamp": ts,
        })
        update_all_nodes("ALLRED_BUFFER", "ALLRED_BUFFER", "ALLRED_BUFFER")
        return

    # PRECLEAR — corridor green granted
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
        update_all_nodes("GREEN", "GREEN", "GREEN")
        return

    # EXTENDED
    m = PATTERNS['EXTENDED'].search(line)
    if m:
        node_id = int(m.group(1))
        fb_post("events", {
            "type":       "EXTENDED",
            "node_id":    node_id,
            "vehicle_id": m.group(2),
            "timestamp":  ts,
        })
        update_all_nodes("GREEN", "GREEN", "GREEN")
        return

    # PASSAGE confirmed at node
    m = PATTERNS['PASSAGE'].search(line)
    if m:
        fb_post("events", {
            "type":       "PASSAGE_CONFIRM",
            "node_id":    int(m.group(1)),
            "vehicle_id": m.group(2),
            "timestamp":  ts,
        })
        return

    # QUEUED
    m = PATTERNS['QUEUED'].search(line)
    if m:
        node_id = int(m.group(1))
        fb_post("events", {
            "type":       "QUEUED_GREEN",
            "node_id":    node_id,
            "vehicle_id": m.group(2),
            "timestamp":  ts,
        })
        update_all_nodes("GREEN", "GREEN", "GREEN")
        return

    # CORRIDOR_SYNC — non-synchronous sequential traffic cycle
    m = PATTERNS['CORRIDOR_SYNC'].search(line)
    if m:
        update_all_nodes(m.group(1), m.group(2), m.group(3))
        return

    # Individual Node SYNC
    m = PATTERNS['SYNC'].search(line)
    if m:
        node_id = int(m.group(1))
        color   = m.group(2)
        update_node(node_id, color)
        return

    # NORMAL — back to standard sequential cycle
    m = PATTERNS['NORMAL'].search(line)
    if m:
        node_id = int(m.group(1))
        fb_post("events", {
            "type":      "NORMAL",
            "node_id":   node_id,
            "timestamp": ts,
        })
        update_all_nodes("GREEN", "RED", "RED")
        return

    # FAILSAFE
    m = PATTERNS['FAILSAFE'].search(line)
    if m:
        node_id = int(m.group(1))
        fb_post("events", {
            "type":      "FAILSAFE_REVERT",
            "node_id":   node_id,
            "timestamp": ts,
        })
        return


def auto_detect_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        desc = (p.description or "").lower()
        if any(k in desc for k in ["cp210", "ch340", "ftdi", "usb serial", "uart", "esp"]):
            return p.device
    return ports[0].device if ports else None


def main():
    parser = argparse.ArgumentParser(description="Green Corridor Serial-to-Firebase Bridge")
    parser.add_argument("--port", help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=BAUD_RATE)
    args = parser.parse_args()

    port = args.port or auto_detect_port()
    if not port:
        print("[ERROR] No serial port detected.")
        sys.exit(1)

    print(f"Connecting to Node 1 on {port} @ {args.baud} baud...")
    try:
        ser = serial.Serial(port, args.baud, timeout=1)
    except Exception as e:
        print(f"[ERROR] Failed to open {port}: {e}")
        sys.exit(1)

    print("Connected. Streaming data to Firebase Realtime Database...")
    fb_put("bridge_status/online", True)

    try:
        while True:
            raw = ser.readline().decode('utf-8', errors='replace')
            if raw:
                parse_and_push(raw)
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        fb_put("bridge_status/online", False)
        ser.close()


if __name__ == "__main__":
    main()
