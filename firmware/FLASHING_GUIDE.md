# Green Corridor Firmware — Flashing Guide
## INTIUM 2026 · Smart City Track

---

## Folder Structure

```
firmware/
  vehicle_unit/
    vehicle_unit.ino      ← flash to both 38-pin ESP32 boards
  intersection_node/
    intersection_node.ino ← flash to all three 30-pin ESP32 boards
  FLASHING_GUIDE.md       ← this file
```

---

## Arduino IDE Setup (do once)

1. Open Arduino IDE (≥ 1.8.x or IDE 2.x)
2. Go to **File → Preferences → Additional Board Manager URLs** and add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools → Board → Boards Manager**, search `esp32`, install **esp32 by Espressif Systems** (≥ 2.0.0)
4. Select board: **Tools → Board → ESP32 Arduino → ESP32 Dev Module**
5. Set: **Upload Speed 921600**, **Flash Size 4MB**, **Partition Scheme Default**

---

## Step 1 — Record MAC Addresses (CRITICAL FIRST STEP)

Before flashing production code, flash this tiny MAC-printer sketch to each board
to record its MAC address. You will need them for the HOUR 1 setup log.

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

Open Serial Monitor at **115200 baud** and note each board's MAC.
Label the board with tape.

| Board Label       | Type    | GPIO used    | MAC Address |
|-------------------|---------|--------------|-------------|
| Vehicle A (38-pin)| 38-pin  | 4,18,19,23,34| ___:___:___ |
| Vehicle B (38-pin)| 38-pin  | 4,18,19,23,34| ___:___:___ |
| Node 1   (30-pin) | 30-pin  | 18,25,26,27  | ___:___:___ |
| Node 2   (30-pin) | 30-pin  | 18,25,26,27  | ___:___:___ |
| Node 3   (30-pin) | 30-pin  | 18,25,26,27  | ___:___:___ |

> NOTE: This firmware uses ESP-NOW **broadcast** — no MAC configuration
> is required in the code. Broadcast works out-of-the-box.
> MACs are only needed for your own records.

---

## Step 2 — Flash Vehicle Units (38-pin boards)

Open `vehicle_unit/vehicle_unit.ino` in Arduino IDE.

### For Vehicle A (38-pin board #1):
```cpp
#define VEHICLE_ID        'A'
#define BEACON_STAGGER_MS   0
```
Flash → done.

### For Vehicle B (38-pin board #2):
Change the two defines to:
```cpp
#define VEHICLE_ID        'B'
#define BEACON_STAGGER_MS 250
```
Flash → done.

---

## Step 3 — Flash Intersection Nodes (30-pin boards)

Open `intersection_node/intersection_node.ino` in Arduino IDE.

### For Node 1 (30-pin board #1 — entry):
```cpp
#define NODE_ID  1
```
Flash → done.

### For Node 2 (30-pin board #2 — middle):
```cpp
#define NODE_ID  2
```
Flash → done.

### For Node 3 (30-pin board #3 — exit):
```cpp
#define NODE_ID  3
```
Flash → done.

---

## Step 4 — Wiring Reference

### Intersection Node (30-pin) — wire ×3
```
ESP32 GPIO25  ──[220Ω]── Red LED (+)    ── GND (−)
ESP32 GPIO26  ──[220Ω]── Yellow LED (+) ── GND (−)
ESP32 GPIO27  ──[220Ω]── Green LED (+)  ── GND (−)
ESP32 GPIO18  ── Confirm button ── GND
               (INPUT_PULLUP, press = active LOW)
ESP32 3.3V/5V via USB or power bank
```

### Vehicle Unit (38-pin) — wire ×2
```
ESP32 GPIO18 ── Trigger push-button ── GND
               (INPUT_PULLUP, press = active LOW)
ESP32 GPIO19 ── 2-way tier switch    ── GND
               (open/HIGH = Tier1, shorted/LOW = Tier2)
ESP32 GPIO34 ── Potentiometer WIPER
ESP32 3.3V   ── Potentiometer VCC (one end)
ESP32 GND    ── Potentiometer GND (other end)
               (CW rotation = high value = closer to node)
ESP32 GPIO4  ── Piezo buzzer (+), Buzzer (−) → GND
ESP32 GPIO23 ──[220Ω]── Status LED (+) ── GND (−)
ESP32 powered via 18650 power bank (USB)
```

---

## Step 5 — Verify Wiring (Hour 1 Bench Test)

1. Power all 5 boards via USB.
2. Open Serial Monitor on each (115200 baud).
3. Confirm all 5 print their MAC and "READY" message.
4. On Vehicle A: press trigger → hear siren chirp → Serial shows [BEACON].
5. On Node 1: Serial Monitor should show "[NODE1 ARB]" within 1-2 seconds.
6. Node 1 green LED should light up.
7. Press trigger again on Vehicle A → hear clear tone → [CONFIRM] sent.
8. Node 1 should show "[NODE1] Passage confirmed" and revert to RED.

---

## Demo Quick Reference (for Stage)

### Setup
- Lay nodes 1→2→3 left-to-right.
- Place Vehicle A and B units at the left (entry) side.
- Keep a laptop with Serial Monitor open on Node 1 for judges.

### Scenario 1 — Priority Arbitration
1. Set Vehicle A → Tier 2 (switch DOWN), pot ~50%
2. Set Vehicle B → Tier 1 (switch UP), pot ~50%
3. Press A trigger first, then B ~1 second later.
4. Node 1 grants GREEN to B (higher tier wins).
5. Point to Serial Monitor score breakdown.
6. Press B trigger again → CONFIRM → Node reverts.

### Scenario 2 — Dynamic Extend (Close Together)
1. Both vehicles Tier 2 (or any tier).
2. Turn both dist dials CW to ~75-100% (dist > 3000).
3. Press both triggers nearly simultaneously.
4. Node shows EXTENDED — green stays on longer for both to pass.

### Scenario 3 — Fast Sequential Switch (Far Apart)
1. Winner's dial at 75%, loser's dial at 25% (below threshold).
2. Press both triggers.
3. Node shows PRECLEAR → winner passes → immediately QUEUED for loser.
4. No normal-cycle wait between the two.

### Closing Demo Line
*"This isn't just a green light — it's a live arbitration decision,
recomputed at every intersection, in real time, for every emergency
vehicle in the zone."*

---

## Serial Monitor Output Key

| Prefix               | Meaning                                    |
|----------------------|--------------------------------------------|
| `[BEACON]`           | Vehicle transmitting beacon packet         |
| `[CONFIRM]`          | Vehicle signaling passage complete         |
| `[NODE1 ARB]`        | Node running arbitration engine            |
| `[NODE1] → PRECLEAR` | Node granted GREEN to winner               |
| `[NODE1] → EXTENDED` | Node extended green (both vehicles close)  |
| `[NODE1] → QUEUED`   | Node granted immediate GREEN to queued veh |
| `[NODE1] → NORMAL`   | Node reverted to normal traffic cycle      |
| `[NODE1] *** FAILSAFE ***` | 15s hard timer fired, auto-revert    |
| `[NODE1] PRECLEAR relay →` | PRECLEAR sent to downstream nodes    |
| `[NODE1] Advance notice`   | Upstream node's PRECLEAR received    |

---

## Troubleshooting

| Problem | Fix |
|---|---|
| Node doesn't react to beacon | Check both devices on same WiFi channel (default=0). Restart both. |
| ESP-NOW init FAILED | Status LED blinks fast. Re-flash board. Check board type = ESP32 Dev Module. |
| Both vehicles get same score | Adjust pot dials or tier switches to create clear score difference. |
| Node stays GREEN forever | Failsafe fires at 15s automatically. Or press node's confirm button. |
| Buzzer no sound | Check piezo polarity. GPIO4 → (+) lead. Ensure `tone()` is supported on your core version. |
| Packet collisions (beacons drop) | Vehicle B already staggered by 250ms. If still dropping, increase to 300ms in `BEACON_STAGGER_MS`. |

---
*INTIUM 2026 · AI-Powered Emergency Green Corridor · Firmware v1.0*
