# AI-Powered Emergency Green Corridor — Hackathon Execution Plan
**INTIUM 2026 · Smart City Track · 8-Hour Hardware Finale**

> **Update 2:** With 5 ESP32 boards available, the build now demonstrates **multiple simultaneous emergency vehicles** (2 ambulance units) converging on the same 3-node corridor, with **dynamic priority arbitration** — the system decides, in real time, which vehicle gets the green first and how long to hold it, instead of a fixed rule.

> Comms layer: **ESP-NOW** (built into ESP32 WiFi radio) — no external RF module, no router pairing, ~2-5ms latency, peer-to-peer.

---

## 1. Scope for Today (Tabletop Demo Build)

- **2 Ambulance/Emergency Units** — Vehicle A, Vehicle B (each ESP32 + trigger button + priority-tier switch)
- **3 Intersection Nodes** (ESP32 + R/Y/G LED cluster each)
- **ESP-NOW mesh**: both vehicles broadcast independently; each node runs a **priority arbitration engine** that decides who clears first and dynamically adjusts hold time
- **Auto-revert**: simulated via timer + manual "passage" button, per vehicle

---

## 2. Component List (5 ESP32 build)

| # | Component | Purpose | Qty | Unit Cost (INR) | Total (INR) |
|---|---|---|---|---|---|
| 1 | ESP32 Dev Board | 2× ambulance units + 3× intersection nodes, ESP-NOW handles all comms | 5 | 450 | 2,250 |
| 2 | LED Traffic-Light Cluster (R/Y/G) | Signal output per node | 3 | 50 | 150 |
| 3 | Breadboard | Prototyping per node/unit | 5 | 70 | 350 |
| 4 | Jumper Wires (M-M/M-F/F-F pack) | Wiring | 5 packs | 50 | 250 |
| 5 | Resistors, misc passives | LED current limiting | 1 set | 100 | 100 |
| 6 | Li-ion battery pack / power bank | Portable power, both ambulance units | 2 | 350 | 700 |
| 7 | USB cables / power supply | Node/unit power, programming | 5 | 60 | 300 |
| 8 | Push buttons / 2-way switch | Trigger + priority-tier select per vehicle, passage-confirm per node | 6 | 20 | 120 |
| 9 | Potentiometer (10kΩ) | "Distance" dial per vehicle unit — reliable stand-in for RSSI/GPS proximity in a tabletop demo | 2 | 30 | 60 |
| 10 | Piezo buzzer | Audible trigger cue per vehicle unit (siren-concept callback) | 2 | 20 | 40 |

**Estimated total: ~INR 4,320**

*Talking point:* "We didn't just add a second ambulance for show — it forces the system to actually arbitrate, which is the real-world case: multiple emergency calls active in the same zone at once."

---

## 3. Architecture — Multi-Vehicle Dynamic Arbitration

```
Vehicle A (ESP32)                 Vehicle B (ESP32)
  BEACON {id:A, tier, rssi_est}     BEACON {id:B, tier, rssi_est}
        │        every 500ms              │
        └───────────────┬─────────────────┘
                         ▼
                   Node 1 (ESP32)
        Arbitration Engine: scores A vs B
        → grants GREEN to winner
        → computes dynamic hold time
        → relays PRECLEAR(winner) + PRECLEAR(loser, queued)
                         ▼
                   Node 2 (ESP32)  ── same arbitration, independently
                         ▼
                   Node 3 (ESP32)  ── same arbitration, independently
```

Each node arbitrates **independently and locally** — no central controller — matching the original submission's decentralized design philosophy.

---

## 4. Priority Scoring & Dynamic Time Adjustment (the core new logic)

Each BEACON packet carries:
- `vehicle_id`
- `priority_tier` (1 = critical: cardiac/trauma/stroke, 2 = standard transport) — set via a switch on the vehicle unit for demo purposes
- `dist_dial` — proximity estimate read from the vehicle unit's potentiometer (demo stand-in for real-world RSSI/GPS distance calc — chosen because RSSI is too noisy to control reliably at tabletop range)
- `wait_ticks` — how many cycles this vehicle has been waiting at this node (anti-starvation)

**Score formula (compute at each node, per active vehicle):**

```
score = (100 - (tier - 1) * 40)      // tier 1 → 100, tier 2 → 60 base
      + proximity_bonus(dist_dial)    // closer vehicle → higher bonus, 0-40
      + wait_ticks * 5                // grows the longer it's waited, prevents starvation
```

**Arbitration rules at a node:**
1. Only one active claim → grant GREEN immediately, standard PRECLEAR relay (as before).
2. Two active claims → compare `score`. Higher score gets GREEN now.
3. **Dynamic hold/extend:** if the losing vehicle's `dist_dial` puts it within a short buffer window (e.g. dialed to "<3s behind" the winner), **extend the winner's green** to let both pass in the same window — this is the "clear the route ASAP" behavior instead of a full stop-restart cycle.
4. If the gap is larger, the winner completes its pass, passage-confirm fires, and the node **immediately** (not on the normal fixed cycle) grants GREEN to the queued second vehicle — still faster than a reactive system waiting for audible siren range.
5. `wait_ticks` increments each cycle a vehicle is queued, so a lower-tier vehicle that's been waiting too long eventually outscores a higher-tier vehicle that just arrived — avoids indefinite starvation of "standard" transports.
6. **Failsafe:** every node also runs a hard 15s max-green timer, independent of the passage-confirm button. If a "cleared" packet is ever missed, the node reverts on its own instead of holding green indefinitely — a cheap but real fail-safe talking point.

This logic is the same at Node 2 and Node 3 independently — so if Vehicle A wins at Node 1 but Vehicle B is actually catching up faster, the two nodes can resolve differently, which is a good "self-correcting, not centrally dictated" talking point.

---

## 5. Wiring

**Per Intersection Node**

| Signal | ESP32 GPIO | Notes |
|---|---|---|
| Red LED | GPIO25 | via 220Ω resistor |
| Yellow LED | GPIO26 | via 220Ω resistor |
| Green LED | GPIO27 | via 220Ω resistor |
| Passage/Confirm button | GPIO18 | INPUT_PULLUP, active LOW |

**Per Vehicle Unit (A and B)**

| Signal | ESP32 GPIO | Notes |
|---|---|---|
| Trigger button (start beacon) | GPIO18 | INPUT_PULLUP |
| Priority-tier switch | GPIO19 | HIGH = tier 1 (critical), LOW = tier 2 (standard) |
| Distance dial (potentiometer) | GPIO34 (ADC) | Manual "proximity" input — reads 0-4095, mapped to proximity_bonus; used instead of RSSI so the close/far demo scenarios are fully controllable on stage |
| Piezo buzzer | GPIO4 | Short tone on trigger press — audible cue paired with the LED cascade |
| Status LED (beacon active) | GPIO23 | optional, blinks while broadcasting |

Camera, LoRa, GPS, mic, IR/ultrasonic — still out of scope for today's tabletop build (Future Scope talking points).

---

## 6. Hour-by-Hour Timeline (8 Hours)

### Hour 1 (0:00–1:00) — Setup & Bench Test

**Step 0 — MAC address collection (do before anything else)**

Flash this 5-line sketch to each board one at a time, open Serial Monitor at 115200 baud, and record the printed MAC. Label each board with tape.

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

| Board | Type | MAC |
|---|---|---|
| Vehicle A | 38-pin | ___:___:___:___:___:___ |
| Vehicle B | 38-pin | ___:___:___:___:___:___ |
| Node 1    | 30-pin | ___:___:___:___:___:___ |
| Node 2    | 30-pin | ___:___:___:___:___:___ |
| Node 3    | 30-pin | ___:___:___:___:___:___ |

> Note: the firmware uses ESP-NOW **broadcast** — no MAC configuration is needed in the code. The broadcast peer (`FF:FF:FF:FF:FF:FF`) is registered in `setup()` on all 5 boards, so `esp_now_send()` works without per-device peer registration. MACs are recorded here only as a reference log in case you need to debug individual boards.

**Step 1 — Flash firmware**
- Flash `vehicle_unit.ino` to both 38-pin boards (`VEHICLE_ID 'A'`/`'B'`).
- Flash `intersection_node.ino` to all three 30-pin boards (`NODE_ID 1`/`2`/`3`).

**Step 2 — Wire bench test**
- Wire the 3 node LED clusters + confirm buttons.
- Wire trigger button + tier-switch + potentiometer on both vehicle units.
- Open Serial Monitor on Node 1.

**Step 3 — Verify Serial output**
- Power all 5 boards. Each should print its MAC + `"WiFi channel: 1 (hardcoded)"` + `"READY"`.
- Press Vehicle A trigger → confirm `[BEACON]` lines appear on Node 1 within 1 second.
- Press trigger again → confirm `[CONFIRM]` sent and Node 1 shows `→ NORMAL`.

### Hour 2 (1:00–2:00) — ESP-NOW Multi-Sender Test
- Get **both** vehicle units broadcasting BEACON packets (with `vehicle_id` + `tier`) received correctly by Node 1's Serial Monitor.
- Confirm no packet collisions when both fire close together.

### Hour 3 (2:00–3:00) — Full Chain Relay (both vehicles)
- Extend relay to Node1→Node2→Node3 for **both** vehicle IDs independently.
- Confirm each node's Serial output distinguishes A vs B.
- **Verify PRECLEAR filtering:** Node 1 prints `PRECLEAR relay → target=Node2`; confirm Node 3's Serial does NOT show "Advance notice from Node1" (it should silently discard it).

### Hour 4 (3:00–4:00) — Traffic Light State Machine
- Normal R→G→Y→R cycle per node.
- Single-vehicle override still works exactly as before (regression check).

### Hour 5 (4:00–5:00) — Arbitration Engine (the key new hour)
- Implement the scoring function from Section 4 at each node.
- Test: trigger A then B in quick succession with different tiers — confirm the higher-scoring vehicle wins the green, and Serial Monitor prints the score breakdown (great for judges to see the "AI decision" happening).

### Hour 6 (5:00–6:00) — Dynamic Hold/Extend + Auto-Revert
- Implement the extend-vs-immediate-switch logic (rule 3/4 in Section 4).
- Wire passage-confirm buttons; test both the "close together" (extended green) and "far apart" (quick switch) scenarios.

### Hour 7 (6:00–7:00) — Integration Test, Polish & Backup
- Run the full 2-vehicle scenario 5–10 times, varying which vehicle is triggered first and which has higher tier, to make sure arbitration is visibly correct every time.
- Add Serial/OLED output showing live score values — judges should be able to see *why* the system chose what it chose.
- **Record a backup video** of a clean multi-vehicle run.

### Hour 8 (7:00–8:00) — Demo Rehearsal & Pitch Prep
- Rehearse the demo script (Section 7) twice, timed.
- Prep pitch notes mapped to judging criteria (Section 8).
- Battery test both vehicle units + all 3 nodes together, not just USB.

---

## 7. Live Demo Script (for Judges)

1. Lay out 3 nodes left-to-right; place Vehicle A and Vehicle B units at the "entry" side.
2. Set **Vehicle A** to tier 2 (standard) and **Vehicle B** to tier 1 (critical). Press A's trigger first, then B's trigger ~1s later.
3. Narrate: *"Even though Vehicle A signaled first, Vehicle B is a higher-priority critical transport — watch which one gets the green."*
4. **Node 1 grants green to B** — point to the Serial/OLED score readout showing B's higher score.
5. Trigger both again, this time close together in distance/timing — show the **dynamic extend**: the node holds green long enough for both to pass in one window instead of stopping and restarting.
6. Trigger both again, further apart — show the **fast immediate switch**: as soon as the first vehicle's passage is confirmed, the second gets green right away, no waiting for the normal fixed cycle.
7. **Closing line:** *"This isn't just a green light — it's a live arbitration decision, recomputed at every intersection, in real time, for every emergency vehicle in the zone."*

### Serial Monitor Prefix Key (show to judges on laptop)

Point judges to a live Serial Monitor window on Node 1 during the demo. Every line is prefixed so they can follow the logic in real time:

| Prefix | What it means |
|---|---|
| `[BEACON] Veh=B tier=1 dist=3200 wait_ticks=6` | Vehicle B broadcasting — tier 1, close, waited 6 cycles |
| `[NODE1 ARB] Vehicle A: ... → score=105` | Node 1 computing Vehicle A's score |
| `[NODE1 ARB] Vehicle B: ... → score=147` | Node 1 computing Vehicle B's score (wins) |
| `[NODE1 ARB] WINNER → Vehicle B` | Arbitration decision printed before LED changes |
| `[NODE1] → PRECLEAR  winner=B  queued=A` | Node 1 GREEN for B, A queued |
| `[NODE1] → EXTENDED GREEN (both vehicles close)` | Both vehicles close — one extended green window |
| `[NODE1] PRECLEAR relay → target=Node2  winner=B` | Node 1 sends advance notice to Node 2 |
| `[NODE2] Advance notice from Node1 → winner=B` | Node 2 receives upstream relay |
| `[NODE1] Passage confirmed: Veh B cleared` | B confirmed through, A getting green next |
| `[NODE1] → QUEUED: Immediate GREEN for Veh A` | No normal-cycle wait — A gets green right away |
| `[NODE1] → NORMAL (traffic cycle)` | All vehicles cleared, back to normal |
| `[NODE1] *** FAILSAFE *** No confirm after 15s` | Hard timer fired — safe auto-revert |

---

## 8. Judging Criteria Mapping

| Criterion | Talking Point |
|---|---|
| Feasibility | Live on stage, 5 off-the-shelf ESP32s, no custom PCBs |
| Practicality | Handles the real-world case of multiple simultaneous emergency calls, not just one ambulance — mirrors actual city traffic-ops needs |
| Innovation | Decentralized, per-node dynamic priority scoring with anti-starvation — not a fixed preemption rule |
| Completeness | Full live 2-vehicle cascade: dual beacon → arbitration → dynamic hold/switch → confirm → revert, all on stage |

---

## 9. Risk / Fallback Plan

- **ESP-NOW packet collisions** when both vehicles broadcast near-simultaneously: stagger the 500ms broadcast interval slightly per vehicle_id (e.g. A at t, B at t+250ms) to avoid clashes — already implemented in firmware (`BEACON_STAGGER_MS 250` for Vehicle B).
- **Arbitration looks wrong live**: keep the Serial/OLED score readout visible at all times — if judges can see the numbers, even an edge-case result reads as "working as designed" rather than "broken."
- **Timing flakiness**: recorded backup video from Hour 7.
- **Power issues**: test on battery in Hour 8, not just USB.
- **Out of time for full arbitration**: fallback to Hour 4 state (single-vehicle relay, as in the previous version of this plan) — still a complete, demoable system if Hours 5–6 don't converge.

### Troubleshooting Quick-Reference

| Symptom | Likely cause | Fix |
|---|---|---|
| Node doesn't react to beacon at all | Channel mismatch | Check Serial for `"WiFi channel: 1"` on both boards; reflash if any shows a different channel |
| `esp_now_send()` returns success but node gets nothing | Broadcast peer not registered | Confirm `esp_now_add_peer()` with `FF:FF:FF:FF:FF:FF` is in `setup()` — it is in v1.0 firmware |
| Both vehicles get same score, hard to distinguish | Dials/switches not set | Set tier switch visibly different (one UP, one DOWN) and spread pot dials apart before demo |
| Node stays GREEN forever | Confirm button missed or CONFIRM packet lost | 15s failsafe auto-reverts — or press node's confirm button manually |
| Node 2 reacts to Node 1's PRECLEAR | Old firmware (pre-fix) | Reflash — `target_node_id` filter in v1.0 prevents this |
| Buzzer no sound | Polarity or `tone()` issue | Swap piezo leads; confirm Core ≥ 2.0.0 for `tone()` support on ESP32 |
| Serial garbage / no output | Wrong baud rate | Set Serial Monitor to **115200** |

---

## 10. Post-Demo Talking Points (Future Scope)

- City-wide corridor mapping using live traffic data instead of a fixed node graph
- Hospital dispatch integration for optimal route pre-selection
- Extending priority-tiered preemption to fire trucks/police alongside ambulances
- Public dashboard showing average response-time improvement per zone, including multi-vehicle contention stats

---

## 11. Software Layer — Flutter + Firebase (Additive, Not a Dependency)

**Core principle:** the ESP-NOW mesh (Sections 3–7) is the safety-critical path and must keep working with zero changes if this entire layer is unplugged. Nothing here touches node firmware except the two items in Section 11.4.

### 11.1 Architecture

```
Node 1 (unchanged firmware, USB to laptop as already required for demo Serial Monitor)
        │ Serial (115200 baud) — existing [BEACON]/[NODE1 ARB]/etc. lines
        ▼
software/bridge/bridge.py  (~80 lines Python) — parses Serial, writes to Firebase
        │ Firebase REST API, over a phone hotspot (NOT venue WiFi)
        ▼
Firebase Realtime Database
        │
        ├──► Flutter Web dashboard  (live event log + node LED status)
        └──► Flutter mobile app    (same dashboard view + emergency trigger button)
```

### 11.2 Key Decisions

| Decision | Choice | Why |
|---|---|---|
| Database | Firebase **Realtime Database**, not Firestore | Simpler listener API for a live scrolling log under time pressure |
| Bridge location | **Laptop**, not Node 1 firmware | Running WiFi STA + ESP-NOW together risks a channel conflict — never worth it for a monitoring nice-to-have |
| Internet source | **Phone hotspot**, dedicated to bridge/dashboard/app | Venue WiFi is the least reliable thing in the room; also makes "ESP-NOW mesh never touches the internet" a demonstrably true claim |
| Trigger transport | Physical button (primary) + Flutter button (secondary failsafe path) | Both write the same Firebase event; bridge logs receipt |
| Auth | None for demo (Firebase rules: public read/write) | Known production step; state this proactively if judges ask |

### 11.3 Realtime Database Schema (mirrors existing Serial prefixes 1:1)

```
/events/{push_id}
  type:       "BEACON" | "ARBITRATION" | "WINNER_DECIDED" | "PRECLEAR" |
              "EXTENDED" | "PASSAGE_CONFIRM" | "QUEUED_GREEN" |
              "FAILSAFE_REVERT" | "NORMAL" | "ALLRED_BUFFER"
  node_id:    1 | 2 | 3
  vehicle_id: "A" | "B"
  tier:       1 | 2
  dist_dial:  <int>
  wait_ticks: <int>
  score:      <int>          // ARBITRATION events only
  timestamp:  <server timestamp>

/node_status/{node_id}
  state:           "RED" | "GREEN" | "YELLOW"
  last_heartbeat:  <server timestamp>

/trigger_requests/{push_id}
  vehicle_id: "A" | "B"
  tier:       1 | 2
  source:     "flutter_mobile"
  timestamp:  <server timestamp>

/bridge_status
  online:     true | false
  start_time: <server timestamp>
```

Nothing here requires new node firmware — every field already exists in Node 1's Serial output.

### 11.4 Two Firmware Add-Ons (make Judge Q&A answers true)

Both are in `firmware/intersection_node/intersection_node.ino`. Neither changes any packet structure.

**A — MAC Whitelist** (reject BEACON/CONFIRM from unknown MAC addresses)
- Array of 5 allowed MACs hardcoded near the top of the file
- Fill in from Hour 1 Step 0 MAC table
- In `onDataReceived()`: early-return with Serial log if MAC not in list

**B — 3-Second All-RED Buffer** (new `STATE_ALLRED_BUFFER` state)
- Before granting GREEN to any winner, hold all-red for 3 seconds
- Directly answers the "unsafe transition" judge question honestly
- Implemented as a proper non-blocking millis()-based state (not `delay()`)
- State machine: `NORMAL → ALLRED_BUFFER (3s) → PRECLEAR/EXTENDED`

### 11.5 Priority Stack (protect in this order if time runs short)

| Priority | Item |
|---|---|
| **P0** | Core hardware arbitration (Sections 3–7) — non-negotiable |
| **P1** | MAC whitelist + all-red buffer (cheap firmware-side, makes Q&A honest) |
| **P2** | Laptop bridge → Firebase → Flutter dashboard |
| **P3 stretch** | Flutter mobile trigger; fall back to Firebase write-only if local HTTP path isn't done |

### 11.6 Demo Script Addition

> *"Here's the failsafe in action — if the driver can't reach the physical button, the co-driver triggers the same request from their phone. Same event, same priority logic, no difference to the corridor."*
>
> *"And here's what a traffic control room would see in real time — the arbitration decision, with the score breakdown, logged and auditable. Note this dashboard runs entirely over a phone hotspot — the traffic-light mesh itself never touches the internet, so losing this connection never breaks the corridor."*

### 11.7 Generated Files

```
software/
  bridge/
    bridge.py           ← Serial→Firebase parser (~80 lines Python)
    requirements.txt    ← pyserial + requests
    README.md           ← setup + run instructions
  flutter_app/
    pubspec.yaml
    lib/
      main.dart                        ← app entry, routing, Firebase init
      firebase_options.dart            ← TEMPLATE — fill in from Firebase console
      services/
        firebase_service.dart          ← all Realtime Database operations
      screens/
        dashboard_screen.dart          ← node status cards + live event log
        trigger_screen.dart            ← vehicle trigger buttons
```

---
*Generated for INTIUM 2026 Round 1 hardware finale — AI-Powered Emergency Green Corridor System.*
