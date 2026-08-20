# AI-Powered Emergency Green Corridor
### INTIUM 2026 · Smart City Track · 8-Hour Hardware Hackathon

> **System summary:** 2 ESP32 ambulance units broadcast emergency beacons over ESP-NOW. 3 ESP32 intersection nodes receive them, run a live priority arbitration engine (scoring tier × proximity × wait-time), and cascade a green corridor through all 3 intersections — automatically, with no central controller. A Flutter mobile + web dashboard mirrors every decision to Firebase Realtime Database in real time via a laptop bridge.

---

## Table of Contents
1. [Project Overview](#1-project-overview)
2. [Repository Structure](#2-repository-structure)
3. [Hardware & Bill of Materials](#3-hardware--bill-of-materials)
4. [Wiring Reference](#4-wiring-reference)
5. [Firmware (ESP32)](#5-firmware-esp32)
   - [vehicle_unit.ino — Vehicle A & B](#vehicle_unitino--vehicle-a--b)
   - [intersection_node.ino — Nodes 1 2 3](#intersection_nodeino--nodes-1-2-3)
   - [Shared Packet Structure (critical)](#shared-packet-structure-critical)
   - [Priority Scoring Formula](#priority-scoring-formula)
   - [State Machine](#state-machine)
6. [Software Layer (Flutter + Firebase)](#6-software-layer-flutter--firebase)
   - [bridge.py — Serial to Firebase](#bridgepy--serial-to-firebase)
   - [Flutter App — Dashboard + Trigger](#flutter-app--dashboard--trigger)
7. [Firebase Setup](#7-firebase-setup)
8. [Parallel Development Split](#8-parallel-development-split)
9. [Step-by-Step Flash & Run Guide](#9-step-by-step-flash--run-guide)
10. [Serial Monitor Cheatsheet](#10-serial-monitor-cheatsheet)
11. [Troubleshooting](#11-troubleshooting)
12. [Architecture Diagram](#12-architecture-diagram)

---

## 1. Project Overview

| Property | Value |
|---|---|
| **Event** | INTIUM 2026, Smart City Track |
| **Category** | AI-Powered Emergency Green Corridor |
| **Core concept** | Decentralised, real-time priority arbitration across 3 intersection nodes for 2 simultaneous emergency vehicles |
| **Comms** | ESP-NOW (built-in ESP32 WiFi radio — no router, ~2–5ms latency) |
| **Monitoring** | Flutter mobile + web → Firebase Realtime Database (additive; safety path never touches internet) |
| **Total hardware cost** | ~₹4,320 |

### Key design decisions
- **No central controller.** Each node arbitrates independently using only local beacon data.
- **Broadcast, not unicast.** All 5 boards are on ESP-NOW channel 1; all packets are broadcast to `FF:FF:FF:FF:FF:FF`. Target filtering is done in software via `target_node_id`.
- **Anti-starvation.** `wait_ticks` grows each cycle a vehicle waits, so a lower-priority vehicle eventually outscores a late-arriving higher-priority one.
- **Hard failsafe.** 15-second max-GREEN timer on every node — reverts automatically even if a confirm packet is lost.
- **3-second all-RED buffer.** Before granting any GREEN, every node holds all-red for 3 seconds (answering the "unsafe transition" judge question).
- **MAC whitelist.** Nodes reject packets from any MAC not in a hardcoded allowlist.

---

## 2. Repository Structure

```
CET Hackathon/
│
├── Green_Corridor_Hackathon_Execution_Plan.md   ← Master plan doc (read this first)
│
├── firmware/
│   ├── vehicle_unit/
│   │   └── vehicle_unit.ino       ← Flash to Vehicle A and Vehicle B (38-pin ESP32)
│   ├── intersection_node/
│   │   └── intersection_node.ino  ← Flash to Node 1, Node 2, Node 3 (30-pin ESP32)
│   └── FLASHING_GUIDE.md          ← Quick-reference wiring + demo scenarios
│
└── software/
    ├── bridge/
    │   ├── bridge.py              ← Serial → Firebase bridge (run on laptop)
    │   ├── requirements.txt       ← pip install -r requirements.txt
    │   └── README.md              ← Bridge setup guide
    └── flutter_app/
        ├── pubspec.yaml
        ├── README.md              ← Flutter setup guide
        └── lib/
            ├── main.dart                        ← App entry, routing, Firebase init
            ├── firebase_options.dart            ← ⚠️ TEMPLATE — fill in before running
            ├── services/
            │   └── firebase_service.dart        ← All Realtime DB operations
            └── screens/
                ├── dashboard_screen.dart        ← Node LED cards + live event log
                └── trigger_screen.dart          ← Vehicle trigger buttons
```

---

## 3. Hardware & Bill of Materials

| # | Component | For | Qty | Cost |
|---|---|---|---|---|
| 1 | ESP32 DevKit — **38-pin** | Vehicle A, Vehicle B | 2 | ₹900 |
| 2 | ESP32 DevKit — **30-pin** | Node 1, Node 2, Node 3 | 3 | ₹1,350 |
| 3 | R/G/Y LED Traffic Cluster | Per intersection node | 3 | ₹150 |
| 4 | Breadboard | All units | 5 | ₹350 |
| 5 | Jumper Wire Packs | Wiring | 5 | ₹250 |
| 6 | Resistors (220Ω) | LED current limiting | 1 set | ₹100 |
| 7 | Li-ion Power Bank | Portable power for vehicles | 2 | ₹700 |
| 8 | USB Cables | Programming + node power | 5 | ₹300 |
| 9 | Push Buttons + 2-way switch | Trigger, tier-select, confirm | 6 | ₹120 |
| 10 | Potentiometer 10kΩ | "Distance" dial per vehicle | 2 | ₹60 |
| 11 | Piezo Buzzer | Audible trigger cue | 2 | ₹40 |
| | | | **Total** | **~₹4,320** |

---

## 4. Wiring Reference

### Per Intersection Node (30-pin ESP32)

| Signal | GPIO | Notes |
|---|---|---|
| Red LED | **GPIO25** | via 220Ω resistor to GND |
| Yellow LED | **GPIO26** | via 220Ω resistor to GND |
| Green LED | **GPIO27** | via 220Ω resistor to GND |
| Passage/Confirm button | **GPIO18** | `INPUT_PULLUP` — button connects pin to GND |

### Per Vehicle Unit (38-pin ESP32)

| Signal | GPIO | Notes |
|---|---|---|
| Trigger button | **GPIO18** | `INPUT_PULLUP` — press = start beaconing, press again = stop + confirm |
| Priority-tier switch | **GPIO19** | Open/HIGH = Tier 1 Critical · Closed/LOW = Tier 2 Standard |
| Distance potentiometer | **GPIO34** | ADC only (input-only pin — no pullup). Reads 0–4095. CW = close, CCW = far |
| Piezo buzzer | **GPIO4** | Short chirp on trigger. Uses `tone()` — requires Core ≥ 2.0.0 |
| Status LED (optional) | **GPIO23** | Blinks while beacon is active |

> **Power:** Nodes run from USB (laptop or USB charger). Vehicle units run from Li-ion power banks for wireless demo. Always verify GND is common across all components on each board.

---

## 5. Firmware (ESP32)

### Arduino IDE / Platform Setup

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. Add ESP32 board support:
   - File → Preferences → Additional Boards Manager URLs:
     `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Board Manager → search "esp32" → Install **esp32 by Espressif Systems** (≥ 2.0.0)
3. Board settings for both vehicle and node:
   - **Board:** `ESP32 Dev Module`
   - **Upload Speed:** `921600`
   - **CPU Frequency:** `240MHz`
   - **Flash Size:** `4MB`

---

### vehicle_unit.ino — Vehicle A & B

**File:** `firmware/vehicle_unit/vehicle_unit.ino`
**Flash to:** both 38-pin ESP32 boards

#### ⚠️ Change before flashing (2 lines at the top)

```cpp
#define VEHICLE_ID        'A'   // → 'A' for Vehicle A, 'B' for Vehicle B
#define BEACON_STAGGER_MS   0   // → 0 for Vehicle A, 250 for Vehicle B
```

#### What it does
- Sends `PKT_BEACON` every 500ms when triggered (button press)
  - Payload: `vehicle_id`, `tier` (read from GPIO19), `dist` (potentiometer ADC), `wait_ticks`
- Second button press sends `PKT_CONFIRM` and stops beaconing
- GPIO4 piezo: startup chirp + siren on trigger start + clear tone on stop
- GPIO23 LED blinks while beacon is active
- Stagger: Vehicle B delays first beacon by 250ms to avoid simultaneous transmission collision

---

### intersection_node.ino — Nodes 1, 2, 3

**File:** `firmware/intersection_node/intersection_node.ino`
**Flash to:** all three 30-pin ESP32 boards

#### ⚠️ Change before flashing (1 line + MAC array)

```cpp
#define NODE_ID  1   // → 1, 2, or 3
```

```cpp
// Fill all 5 MACs from your Hour 1 MAC-printer results:
const uint8_t ALLOWED_MACS[][6] = {
  {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01},  // Vehicle A
  {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02},  // Vehicle B
  {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03},  // Node 1
  {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x04},  // Node 2
  {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x05},  // Node 3
};
```

#### What it does
- Receives `PKT_BEACON` from vehicles → runs arbitration engine → grants GREEN
- Receives `PKT_CONFIRM` (or manual button press) → advances state machine
- Relays `PKT_PRECLEAR` to next downstream node
- Runs normal R→G→Y→R traffic cycle in `STATE_NORMAL`

---

### Shared Packet Structure (critical)

**This struct must be byte-for-byte identical in BOTH .ino files.** It is `__attribute__((packed))` — any mismatch silently corrupts all fields after the mismatch point.

```cpp
struct __attribute__((packed)) GCPacket {
  uint8_t  type;            // PKT_BEACON(0x01) / PKT_CONFIRM(0x02) / PKT_PRECLEAR(0x03)
  char     vid;             // 'A' or 'B'  (0 = node-originated packet)
  uint8_t  tier;            // 1 = critical, 2 = standard
  uint16_t dist;            // Potentiometer ADC 0–4095
  uint16_t wait_ticks;      // Beacon cycles this vehicle has been waiting
  uint8_t  node_id;         // Originating node (0 = vehicle)
  uint8_t  target_node_id;  // PRECLEAR: intended recipient node (0 = all)
  char     winner;          // PRECLEAR: vehicle granted GREEN
  char     queued;          // PRECLEAR: vehicle queued next (0 = none)
};
// Total size: 11 bytes
```

---

### Priority Scoring Formula

Run at each node independently per active vehicle:

```
score = base_tier_score + proximity_bonus + anti_starvation_bonus

where:
  base_tier_score   = (tier == 1) ? 100 : 60
  proximity_bonus   = map(dist_dial, 0, 4095, 0, 40)   // 0 = far, 40 = very close
  anti_starvation   = wait_ticks × 5
```

**Arbitration rules:**
1. One active vehicle → `STATE_ALLRED_BUFFER` (3s) → `STATE_PRECLEAR` (GREEN)
2. Two vehicles → compare scores → winner gets GREEN
3. Loser `dist_dial > 3000` → `STATE_EXTENDED` (extended window, both pass together)
4. Loser `dist_dial ≤ 3000` → `STATE_PRECLEAR` with loser queued (immediate switch after confirm)
5. Every GREEN grant preceded by 3-second all-red safety buffer

---

### State Machine

```
         BEACON received
              │
         STATE_NORMAL ──────────────────────────────────────────┐
              │                                                  │
         runArbitration()                                        │
              │                                                  │
         STATE_ALLRED_BUFFER  (3 seconds, all-RED)              │
              │                                                  │
      ┌───────┴────────┐                                        │
      │                │                                        │
 STATE_PRECLEAR   STATE_EXTENDED                                │
 (GREEN winner,   (GREEN extended,                              │
  loser queued)    both pass window)                            │
      │                │                                        │
  PKT_CONFIRM      timer expires                                │
  or 15s failsafe  or 15s failsafe                              │
      │                │                                        │
  ┌───┴───┐            └─────────────────────────────────────►  │
  │queued?│                                                      │
  │  yes  │                                                      │
  │       ▼                                                      │
  │  STATE_QUEUED ──── PKT_CONFIRM or 15s failsafe ────────────►│
  │  no   │                                                      │
  └───────┴──────────────────────────────────────────────────►  ┘
                                                          STATE_NORMAL
```

---

## 6. Software Layer (Flutter + Firebase)

> **Important:** This layer is purely additive. Unplugging the laptop or losing internet at any point leaves the ESP-NOW mesh working perfectly. The safety-critical path (Sections 3–5) never touches WiFi or Firebase.

### bridge.py — Serial to Firebase

**File:** `software/bridge/bridge.py`
**Runs on:** the demo laptop (Node 1 USB → laptop → phone hotspot → Firebase)

```bash
# Setup (one time)
pip install -r software/bridge/requirements.txt

# Edit FIREBASE_URL at the top of bridge.py, then run:
python software/bridge/bridge.py --port COM3        # Windows
python software/bridge/bridge.py --port /dev/ttyUSB0  # Linux/Mac
```

**What it parses and mirrors to Firebase:**

| Serial line pattern | Firebase event type |
|---|---|
| `[BEACON] Veh=A tier=1 dist=3200 wait_ticks=6` | `BEACON` |
| `[NODE1 ARB] Vehicle A: ... → score=147` | `ARBITRATION` |
| `[NODE1 ARB] WINNER → Vehicle B` | `WINNER_DECIDED` |
| `[NODE1] All-RED buffer (3000ms)` | `ALLRED_BUFFER` + node → RED |
| `[NODE1] → PRECLEAR winner=B queued=A` | `PRECLEAR` + node → GREEN |
| `[NODE1] → EXTENDED GREEN` | `EXTENDED` + node → GREEN |
| `[NODE1] Passage confirmed: Veh B cleared` | `PASSAGE_CONFIRM` |
| `[NODE1] → QUEUED: Immediate GREEN for Veh A` | `QUEUED_GREEN` + node → GREEN |
| `[NODE1] → NORMAL (traffic cycle)` | `NORMAL` + node → RED |
| `[NODE1] *** FAILSAFE ***` | `FAILSAFE_REVERT` + node → RED |

> **Note:** Close Arduino IDE Serial Monitor before running the bridge — they cannot share the serial port.

---

### Flutter App — Dashboard + Trigger

**File:** `software/flutter_app/`
**Runs as:** Android mobile app AND Flutter Web from same codebase

#### Screens

| Screen | What it shows |
|---|---|
| **Dashboard** | 3 glowing node LED cards (RED/GREEN/YELLOW) + live scrolling event log, newest first. Bridge online indicator (WiFi icon top-right). |
| **Trigger** | Vehicle A and B trigger buttons with tier selector (Tier 1 Critical / Tier 2 Standard). Writes to Firebase `/trigger_requests` — secondary failsafe path. Physical button is always primary. |

#### Quick setup
```bash
# 1. Scaffold the project (one-time — adds android/, ios/, web/ folders)
cd "c:\Users\athma\Downloads\CET Hackathon\software"
flutter create flutter_app --org com.greencorridor --platforms android,web

# 2. Install FlutterFire CLI and configure Firebase (generates firebase_options.dart)
dart pub global activate flutterfire_cli
cd flutter_app
flutterfire configure    # select your Firebase project when prompted

# 3. Install packages
flutter pub get

# 4. Run
flutter run -d chrome    # Web dashboard for judges' laptop
flutter run              # Android mobile app
```

---

## 7. Firebase Setup

### Create the project (~5 minutes)

1. Go to [console.firebase.google.com](https://console.firebase.google.com)
2. **Create a new project** (name: "green-corridor-intium" or anything)
3. Disable Google Analytics (not needed)
4. **Build → Realtime Database → Create Database**
   - Choose the region closest to you
   - Start in **test mode** (we'll set rules manually)
5. **Set Database Rules** (for demo — public read/write):
   ```json
   {
     "rules": {
       ".read": true,
       ".write": true
     }
   }
   ```
   > State to judges: "These rules are demo-only — we'd add authentication and user-scoped writes in production."
6. **Add a Web App** (Project Settings → Your Apps → Add App → Web)
   - Copy the config values for `firebase_options.dart`
7. **Add an Android App** (for mobile APK) — package name: `com.greencorridor.app`
   - Download `google-services.json` → place in `flutter_app/android/app/`

### Database paths used

| Path | Written by | Read by |
|---|---|---|
| `/events/{push_id}` | bridge.py | Flutter dashboard |
| `/node_status/{1,2,3}` | bridge.py | Flutter node LED cards |
| `/bridge_status` | bridge.py | Flutter online indicator |
| `/trigger_requests/{push_id}` | Flutter trigger screen | bridge.py (logged) |

---

## 8. Parallel Development Split

> This is the recommended way to split work between two people working simultaneously.

---

### Developer A — Hardware + Firmware
**Focus:** Get all 5 ESP32 boards flashed, wired, and communicating.

**Hour-by-hour tasks:**

| Hour | Task |
|---|---|
| 0:00–1:00 | Flash MAC-printer sketch to all 5 boards. Record MACs in the table (Execution Plan §6 Hour 1). Wire both vehicle units (button + switch + pot + buzzer). |
| 1:00–2:00 | Flash `vehicle_unit.ino` (A then B). Verify `[BEACON]` lines in Serial Monitor. Confirm both can beacon simultaneously without drops. |
| 2:00–3:00 | Flash all 3 `intersection_node.ino` files. Verify Node 1 receives beacons. Test PRECLEAR relay chain Node1→Node2→Node3. Verify PRECLEAR filtering (Node 3 must NOT show Node 1's relay). |
| 3:00–4:00 | Wire all 3 node LED clusters + confirm buttons. Verify normal R→G→Y→R cycle. Verify single-vehicle GREEN grant regression. |
| 4:00–5:00 | Test 2-vehicle arbitration — tier 1 vs tier 2, confirm correct winner. Verify Serial shows score breakdown. |
| 5:00–6:00 | Test EXTENDED mode (both dials high), QUEUED mode (one dial low). Test manual confirm button on all 3 nodes. Verify 15s failsafe auto-reverts. |
| 6:00–7:00 | Fill in real MACs in `ALLOWED_MACS[]` array, reflash all 3 nodes, verify MAC whitelist rejects an unknown board. Record backup video. |
| 7:00–8:00 | Battery test both vehicle units. Rehearse demo script with full hardware. Prep Serial Monitor window for judges. |

**Key files to work with:**
- `firmware/vehicle_unit/vehicle_unit.ino`
- `firmware/intersection_node/intersection_node.ino`
- `firmware/FLASHING_GUIDE.md`

**MACs to fill in (after Hour 1 Step 0):**

| Board | Type | MAC |
|---|---|---|
| Vehicle A | 38-pin | ___:___:___:___:___:___ |
| Vehicle B | 38-pin | ___:___:___:___:___:___ |
| Node 1    | 30-pin | ___:___:___:___:___:___ |
| Node 2    | 30-pin | ___:___:___:___:___:___ |
| Node 3    | 30-pin | ___:___:___:___:___:___ |

---

### Developer B — Software Layer (Firebase + Flutter + Bridge)
**Focus:** Get the Firebase project set up, bridge script running, and Flutter app on a phone and browser.

**Hour-by-hour tasks:**

| Hour | Task |
|---|---|
| 0:00–1:00 | Create Firebase project. Enable Realtime Database. Add Web + Android apps. Set rules to public. Run `flutterfire configure`. Verify `firebase_options.dart` compiles. |
| 1:00–2:00 | Install bridge dependencies (`pip install -r requirements.txt`). Fill in `FIREBASE_URL` in `bridge.py`. Run bridge against Developer A's Node 1 Serial output. Verify events appear in Firebase console. |
| 2:00–3:00 | Run Flutter web dashboard (`flutter run -d chrome`). Verify node LED cards update when bridge posts node_status. Verify event log scrolls live events. |
| 3:00–4:00 | Build Android APK (`flutter build apk --release`). Install on phone. Verify dashboard matches web version. |
| 4:00–5:00 | Test trigger screen — press Vehicle A/B buttons, confirm `/trigger_requests` entries appear in Firebase console. Add bridge console log for received trigger requests. |
| 5:00–6:00 | Rehearse the "co-driver trigger" demo flow. Capture screenshot of dashboard during a live arbitration decision for backup. |
| 6:00–7:00 | Set up phone hotspot. Connect laptop to phone hotspot. Verify bridge + Firebase + Flutter all work over hotspot (not venue WiFi). |
| 7:00–8:00 | Prep judge-facing web dashboard tab (full screen, dark background). Rehearse demo script addition (Section 11.6). Test bridge restart (Ctrl+C → re-run). |

**Key files to work with:**
- `software/bridge/bridge.py` — edit `FIREBASE_URL`
- `software/flutter_app/lib/firebase_options.dart` — fill in from Firebase console
- `software/flutter_app/lib/` — all dart files

**Checklist before demo:**
- [ ] `bridge.py` running, console showing live lines
- [ ] Firebase console → /node_status shows `{1: RED, 2: RED, 3: RED}` on startup
- [ ] Flutter web open in Chrome (full screen, dark mode)
- [ ] Flutter Android on phone (co-driver trigger demo)
- [ ] Phone hotspot active — laptop bridging through hotspot (not venue WiFi)
- [ ] `bridge_status.online = true` visible in Flutter (WiFi icon top-right)

---

### What both developers should NOT do simultaneously

| Risk | Mitigation |
|---|---|
| Changing the `GCPacket` struct | Only Developer A does this — Dev B never touches .ino files |
| Reflashing node firmware with MAC whitelist | Do as a team in Hour 6 — Dev A flashes, Dev B confirms bridge still receives |
| Using venue WiFi for bridge | Dev B: always use the phone hotspot exclusively for bridge + Flutter |
| Arduino IDE Serial Monitor + bridge.py at the same time | Dev B: close Serial Monitor before starting bridge.py |

---

## 9. Step-by-Step Flash & Run Guide

### Step 0 — Get all 5 MACs (10 minutes, do first)

Flash this to each board one at a time:

```cpp
#include <WiFi.h>
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
}
void loop() {}
```

Open Serial Monitor at **115200 baud**. Record the MAC. Label the board with tape.

---

### Step 1 — Flash vehicle_unit.ino

1. Open `firmware/vehicle_unit/vehicle_unit.ino` in Arduino IDE
2. **For Vehicle A:**
   ```cpp
   #define VEHICLE_ID        'A'
   #define BEACON_STAGGER_MS   0
   ```
3. Select the correct COM port, flash
4. **For Vehicle B:** change both values to `'B'` and `250`, then flash the second board

**Expected Serial output on startup:**
```
================================================
  GREEN CORRIDOR — VEHICLE UNIT  A
  INTIUM 2026 Smart City Track
================================================
  MAC: AA:BB:CC:DD:EE:01
  WiFi channel: 1 (hardcoded)
  READY.
  Trigger btn  : press to START beaconing, press again to STOP+CONFIRM
```

---

### Step 2 — Flash intersection_node.ino

1. Open `firmware/intersection_node/intersection_node.ino` in Arduino IDE
2. **For each board**, change `NODE_ID` (1, 2, or 3) and update `ALLOWED_MACS[]` with real MACs from Step 0
3. Flash each board

**Expected Serial output on startup:**
```
================================================
  GREEN CORRIDOR — INTERSECTION NODE  1
  INTIUM 2026 Smart City Track
================================================
  MAC: AA:BB:CC:DD:EE:03
  WiFi channel: 1 (hardcoded)
  READY — Waiting for emergency vehicle beacons.
```

---

### Step 3 — Run the bridge

```bash
cd "c:\Users\athma\Downloads\CET Hackathon\software"
pip install -r bridge/requirements.txt
python bridge/bridge.py --port COM3
```

```
=======================================================
  Green Corridor — Serial → Firebase Bridge
=======================================================
  Port      : COM3 @ 115200 baud
  Firebase  : https://green-corridor-default-rtdb.firebaseio.com
  Testing Firebase... OK
  Node states → RED (initialised)

  Listening on COM3. Press Ctrl+C to stop.
```

---

### Step 4 — Run Flutter

```bash
cd "c:\Users\athma\Downloads\CET Hackathon\software\flutter_app"
flutter pub get

flutter run -d chrome   # Web dashboard
flutter run             # Android mobile
```

---

## 10. Serial Monitor Cheatsheet

Open Serial Monitor at **115200 baud** on Node 1's COM port.

| Prefix | Meaning |
|---|---|
| `[BEACON] Veh=B tier=1 dist=3200 wait_ticks=6` | Vehicle B broadcasting — tier 1, close, waited 6 cycles |
| `[NODE1 ARB] Vehicle A: tier=1 dist=3200 wait=6 → score=147` | Score computed for Veh A |
| `[NODE1 ARB] WINNER → Vehicle B` | Arbitration decision before LED changes |
| `[NODE1] All-RED buffer (3000ms) before granting GREEN to Veh B` | 3s safety buffer started |
| `[NODE1] All-RED buffer complete → granting GREEN to Veh B` | Buffer done, GREEN granted |
| `[NODE1] → PRECLEAR  winner=B  queued=A` | Node 1 GREEN for B, A queued |
| `[NODE1] → EXTENDED GREEN  (both vehicles close)` | Extended window — both pass |
| `[NODE1] PRECLEAR relay → target=Node2  winner=B` | Advance notice sent to Node 2 |
| `[NODE2] Advance notice from Node1 → winner=B` | Node 2 received relay |
| `[NODE1] Passage confirmed: Veh B cleared` | B through, A getting GREEN next |
| `[NODE1] → QUEUED: Immediate GREEN for Veh A` | No full cycle wait — immediate |
| `[NODE1] → NORMAL (traffic cycle)` | All cleared, back to normal |
| `[NODE1] *** FAILSAFE *** No confirm after 15s — reverting` | Hard timer fired |
| `[NODE1] Rejected packet from unknown MAC AA:BB:CC:DD:EE:99` | MAC whitelist blocked |

---

## 11. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Node does not react to beacon | WiFi channel mismatch | Both boards must print `"WiFi channel: 1 (hardcoded)"` — reflash if not |
| `esp_now_send()` succeeds but nothing received | Broadcast peer not registered | `esp_now_add_peer()` with `FF:FF:FF:FF:FF:FF` is in `setup()` — confirm firmware version |
| Node reacts but all vehicles get same score | Pot and switch not wired or read wrong | Verify GPIO34 pot reads 0–4095; GPIO19 switch reads HIGH/LOW |
| Node stays GREEN forever | Confirm button missed or PKT_CONFIRM lost | 15s failsafe auto-reverts; or press confirm button manually |
| Node 2 reacts to Node 1's PRECLEAR relay | Old firmware (pre target_node_id fix) | Reflash intersection_node.ino — check `target_node_id` field is present |
| Buzzer no sound | Polarity reversed or wrong Core version | Swap piezo leads; ESP32 Arduino Core must be ≥ 2.0.0 for `tone()` |
| Serial garbage | Wrong baud rate | Set Serial Monitor to exactly **115200** |
| Bridge "Firebase PUT failed" | No internet on laptop | Connect laptop to phone hotspot before starting bridge |
| Bridge starts but no lines appear | Arduino IDE Serial Monitor is open | Close Arduino IDE Serial Monitor first — port can't be shared |
| Flutter: "Waiting for events..." indefinitely | Bridge offline or wrong Firebase URL | Check bridge console; check `FIREBASE_URL` matches your project |
| Flutter firebase_options.dart compile error | Template not filled in | Run `flutterfire configure` or fill values from Firebase Console manually |
| MAC whitelist rejecting own boards | MACs not updated in firmware | Update `ALLOWED_MACS[]` with real MACs from Step 0, reflash all 3 nodes |

---

## 12. Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                    ESP-NOW MESH  (WiFi channel 1)                   │
│                    Safety-critical — no internet                    │
│                                                                     │
│  ┌─────────────┐    PKT_BEACON (every 500ms)   ┌─────────────────┐ │
│  │  Vehicle A  │──────────────────────────────►│                 │ │
│  │  38-pin     │                               │   Node 1        │ │
│  │  ESP32      │    PKT_BEACON (every 500ms)   │   30-pin ESP32  │ │
│  └─────────────┘──────────────────────────────►│                 │ │
│                                                │  Arbitration    │ │
│  ┌─────────────┐    PKT_BEACON (every 500ms)   │  Engine         │ │
│  │  Vehicle B  │──────────────────────────────►│  R/G/Y LEDs     │ │
│  │  38-pin     │                               │  Confirm btn    │ │
│  │  ESP32      │◄─────── PKT_PRECLEAR ─────────│                 │ │
│  └─────────────┘         (winner,queued)       └────────┬────────┘ │
│         │                                               │           │
│         │                PKT_PRECLEAR relay             ▼           │
│         │                (target=Node2)       ┌─────────────────┐  │
│         └────────────────────────────────────►│   Node 2        │  │
│                                               │   30-pin ESP32  │  │
│                                               │                 │  │
│                                               └────────┬────────┘  │
│                                                        │           │
│                                          PKT_PRECLEAR  ▼           │
│                                          (target=Node3)            │
│                                               ┌─────────────────┐  │
│                                               │   Node 3        │  │
│                                               │   30-pin ESP32  │  │
│                                               └─────────────────┘  │
└───────────────────────────────────────────────────────┬────────────┘
                                              USB Serial │
                                         (115200 baud)   │
                                                         ▼
                                           ┌─────────────────────┐
                                           │  Laptop             │
                                           │  bridge.py          │
                                           │  (parses Serial)    │
                                           └──────────┬──────────┘
                                                      │ HTTP REST
                                                      │ (phone hotspot)
                                                      ▼
                                           ┌─────────────────────┐
                                           │  Firebase Realtime  │
                                           │  Database           │
                                           └──────┬──────────────┘
                                                  │
                                     ┌────────────┴─────────────┐
                                     ▼                          ▼
                             ┌──────────────┐        ┌──────────────────┐
                             │  Flutter Web │        │  Flutter Android │
                             │  Dashboard   │        │  Mobile Trigger  │
                             │  (judges)    │        │  (co-driver demo)│
                             └──────────────┘        └──────────────────┘
```

---

*README generated for INTIUM 2026 Round 1 hardware finale — AI-Powered Emergency Green Corridor System.*
*Last updated: 2026-08-20*
