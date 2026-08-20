# Green Corridor Bridge — Setup Guide

## What this does
Reads Node 1's Serial output line-by-line, parses the existing
`[BEACON]` / `[NODE1 ARB]` / etc. prefixes, and pushes structured
events to Firebase Realtime Database in real time.
Node 1 firmware is **unchanged** — this reads it passively.

## Prerequisites
- Python 3.8+
- pip
- Node 1 plugged into this laptop via USB
- Arduino IDE **Serial Monitor closed** (can't share the serial port)
- Phone hotspot active on this laptop (NOT venue WiFi)
- Firebase project created (Realtime Database enabled, rules = public read/write for demo)

## Setup (one-time, ~3 minutes)

### 1. Install Python deps
```bash
pip install -r requirements.txt
```

### 2. Fill in your Firebase URL
Open `bridge.py` and update:
```python
FIREBASE_URL = "https://YOUR-PROJECT-ID-default-rtdb.firebaseio.com"
```
Get this from: Firebase Console → your project → Realtime Database → copy the URL shown at the top.

### 3. Set Firebase rules to public (for demo)
In Firebase Console → Realtime Database → Rules, paste:
```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```
This is demo-only. State "we'd add auth in production" if judges ask.

### 4. Find your serial port
- **Windows**: Device Manager → Ports (COM & LPT) → look for "Silicon Labs CP210x" or "CH340"
  Most common: COM3 or COM4
- **Linux/Mac**: `ls /dev/tty*` → look for `/dev/ttyUSB0` or `/dev/ttyACM0`

## Running

```bash
# Windows
python bridge.py --port COM3

# Linux / Mac
python bridge.py --port /dev/ttyUSB0
```

Expected startup output:
```
=======================================================
  Green Corridor — Serial → Firebase Bridge
  INTIUM 2026 Smart City Track
=======================================================
  Port      : COM3 @ 115200 baud
  Firebase  : https://your-project-default-rtdb.firebaseio.com
  Testing Firebase... OK
  Node states → RED (initialised)

  Listening on COM3. Press Ctrl+C to stop.

  [BEACON] Veh=A  tier=1  dist=3200  wait_ticks=  3
  [NODE1 ARB] Vehicle A: tier=1 dist=3200 wait=3 → score=126
  ...
```

## Troubleshooting

| Problem | Fix |
|---|---|
| `SerialException: could not open port` | Close Arduino IDE Serial Monitor first |
| `Firebase PUT failed: ConnectionError` | Check hotspot is active; re-run |
| Lines print but Firebase not updating | Check FIREBASE_URL has no trailing slash and is correct |
| Bridge starts but no Serial lines appear | Check Node 1 is powered and firmware is flashed |
