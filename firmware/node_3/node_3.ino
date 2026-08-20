/*
 * ============================================================
 *  AI-POWERED EMERGENCY GREEN CORRIDOR
 *  INTERSECTION NODE FIRMWARE — 30-pin ESP32 DevKit
 *  INTIUM 2026 · Smart City Track
 * ============================================================
 *
 *  Flash this sketch to ALL THREE intersection nodes.
 *
 *  ★ BEFORE FLASHING — change the define below:
 *    Node 1 (entry):  NODE_ID  1
 *    Node 2 (middle): NODE_ID  2
 *    Node 3 (exit):   NODE_ID  3
 *
 * ============================================================
 *  PIN MAP (30-pin ESP32 DevKit)
 * ============================================================
 *  GPIO 25  - Red    LED  (+ → 220Ω → GPIO25, − → GND)
 *  GPIO 26  - Yellow LED  (+ → 220Ω → GPIO26, − → GND)
 *  GPIO 27  - Green  LED  (+ → 220Ω → GPIO27, − → GND)
 *  GPIO 18  - Passage/Confirm push-button (INPUT_PULLUP, active LOW)
 *
 * ============================================================
 *  NODE OPERATION:
 *  1. NORMAL MODE (Sequential Non-Synchronous Traffic Cycle):
 *     Node 1: GREEN (4s) → YELLOW (1.5s) → RED (11s)
 *     Node 2: RED (5.5s) → GREEN (4s) → YELLOW (1.5s) → RED (5.5s)
 *     Node 3: RED (11s)  → GREEN (4s) → YELLOW (1.5s)
 *     Total Cycle: 16.5s (Repeats in round-robin sequence)
 *
 *  2. EMERGENCY MODE (Preemption & Priority Arbitration):
 *     - Vehicle A (Tier 1 - Critical) vs Vehicle B (Tier 2 - Standard)
 *     - 3.0s All-RED safety buffer before granting GREEN
 *     - Preempts normal cycle and turns corridor GREEN for winning vehicle
 *     - Loser is queued and receives next immediate GREEN
 *     - Smooth return to sequential normal cycle after clearance
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"

// ============================================================
//  ★  CHANGE BEFORE FLASHING  ★
// ============================================================
#define NODE_ID  3   // 1, 2, or 3
// ============================================================

// ─── Pin Definitions ────────────────────────────────────────
#define PIN_RED         25
#define PIN_YELLOW      26
#define PIN_GREEN_LED   27
#define PIN_CONFIRM_BTN 18

// ─── Packet Types ────────────────────────────────────────────
#define PKT_BEACON   0x01
#define PKT_CONFIRM  0x02
#define PKT_PRECLEAR 0x03
#define PKT_SYNC     0x04

// ─── Broadcast MAC (Send to all nodes/vehicles) ─────────────
uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─── Fixed WiFi channel — MUST match across all 5 boards ────
#define ESPNOW_CHANNEL 1

// ─── Shared Packet Structure ─────────────────────────────────
struct __attribute__((packed)) GCPacket {
  uint8_t  type;
  char     vid;
  uint8_t  tier;
  uint16_t dist;
  uint16_t wait_ticks;
  uint8_t  node_id;        // sender's node (0 = vehicle)
  uint8_t  target_node_id; // PRECLEAR: intended recipient (0 = all nodes)
  char     winner;
  char     queued;
};

// ─── Per-vehicle data tracked by this node ───────────────────
struct VehicleData {
  bool     active;
  uint8_t  tier;
  uint16_t dist;
  uint16_t wait_ticks;
  uint32_t last_seen_ms;
};

VehicleData vehA = {};
VehicleData vehB = {};

// ─── MAC Whitelist (Section 11.4-A) ─────────────────────────
const uint8_t ALLOWED_MACS[][6] = {
  {0x1C, 0xC3, 0xAB, 0xBB, 0xE2, 0x30},  // Vehicle A
  {0x28, 0x05, 0xA5, 0xE2, 0xBF, 0xDC},  // Vehicle B
  {0x6C, 0xC8, 0x40, 0x05, 0x5A, 0x50},  // Node 1
  {0x08, 0xD1, 0xF9, 0xE1, 0x2B, 0xFC},  // Node 2
  {0x00, 0x70, 0x07, 0x3A, 0x38, 0x80},  // Node 3
};
#define ALLOWED_MAC_COUNT 5

bool isMACAllowed(const uint8_t* mac) {
  for (int i = 0; i < ALLOWED_MAC_COUNT; i++) {
    if (memcmp(mac, ALLOWED_MACS[i], 6) == 0) return true;
  }
  return false;
}

// ─── Node State Machine ──────────────────────────────────────
enum NodeState {
  STATE_NORMAL,         // Sequential round-robin traffic light cycle
  STATE_ALLRED_BUFFER,  // 3s all-red safety window before granting GREEN
  STATE_PRECLEAR,       // GREEN held for winner; loser queued
  STATE_EXTENDED,       // GREEN extended — both vehicles very close
  STATE_QUEUED          // Immediately GREEN for queued vehicle
};
NodeState nodeState = STATE_NORMAL;

char     winnerVeh    = 0;   // Vehicle currently holding GREEN
char     queuedVeh    = 0;   // Vehicle waiting after winner passes

// Pending arbitration result
char     pendingWinner   = 0;
char     pendingQueued   = 0;
bool     pendingExtended = false;

// Timestamp of last state entry (for timers)
uint32_t stateEnterMs = 0;

// Sequential Normal Cycle Tracking
uint32_t normalCycleStartMs = 0;
uint8_t  normalPhase = 255;  // 0=RED, 1=GREEN, 2=YELLOW

// Flag set in onDataReceived ISR, consumed in loop()
volatile bool needArbitration = false;

// Post-confirm ignore window
char     clearedVeh    = 0;
uint32_t clearedUntilMs = 0;

// ─── Timing Constants ────────────────────────────────────────
#define PHASE_GREEN_MS      4000   // 4.0s GREEN per node
#define PHASE_YELLOW_MS     1500   // 1.5s YELLOW per node
#define PHASE_TOTAL_MS      (PHASE_GREEN_MS + PHASE_YELLOW_MS) // 5500ms
#define CYCLE_TOTAL_MS      (3 * PHASE_TOTAL_MS)               // 16500ms

#define ALLRED_BUFFER_MS    3000   // Safety all-red gap before GREEN grant
#define PRECLEAR_HOLD_MS    8000   // Standard green-hold for one vehicle
#define EXTEND_BONUS_MS     5000   // Extra time added in EXTENDED mode
#define FAILSAFE_MS        15000   // Hard max — auto-revert if no confirm
#define VEHICLE_TIMEOUT_MS  3000   // Beacon gap before vehicle is considered gone
#define CLOSE_THRESHOLD     3000   // dist_dial above this = "vehicles very close"
#define CLEARED_IGNORE_MS   5000   // Ignore beacons from just-cleared vehicle

// ─── LED Helper ──────────────────────────────────────────────
void setLED(bool red, bool yellow, bool green) {
  digitalWrite(PIN_RED,       red   ? HIGH : LOW);
  digitalWrite(PIN_YELLOW,    yellow? HIGH : LOW);
  digitalWrite(PIN_GREEN_LED, green ? HIGH : LOW);
}

// ─── Score Function ──────────────────────────────────────────
int computeScore(uint8_t tier, uint16_t dist, uint16_t wait_ticks) {
  int base  = (tier == 1) ? 100 : 60;          // Tier 1 critical gets higher base
  int prox  = (int)map(dist, 0, 4095, 0, 40);  // Closer → more bonus (0-40)
  int waits = (int)wait_ticks * 5;              // Anti-starvation
  return base + prox + waits;
}

// ─── Packet Senders ──────────────────────────────────────────
void sendPreclear(char winner, char queued) {
  GCPacket p;
  memset(&p, 0, sizeof(p));
  p.type           = PKT_PRECLEAR;
  p.node_id        = NODE_ID;
  p.target_node_id = 0; // Broadcast corridor preclear to all nodes
  p.winner         = winner;
  p.queued         = queued;
  esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
  Serial.printf("[NODE%d] PRECLEAR relay → winner=%c  queued=%c\n",
                NODE_ID, winner, queued ? queued : '-');
}

void sendSync() {
  GCPacket p;
  memset(&p, 0, sizeof(p));
  p.type    = PKT_SYNC;
  p.node_id = NODE_ID;
  esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
}

// ─── State Transitions ───────────────────────────────────────
void enterNormal() {
  nodeState          = STATE_NORMAL;
  normalCycleStartMs = millis();
  normalPhase        = 255;
  winnerVeh          = 0;
  queuedVeh          = 0;
  vehA.active        = false;
  vehB.active        = false;
  Serial.printf("[NODE%d] → NORMAL (sequential cycle)\n", NODE_ID);
}

void enterAllRedBuffer(char winner, char queued, bool extended) {
  pendingWinner   = winner;
  pendingQueued   = queued;
  pendingExtended = extended;
  nodeState       = STATE_ALLRED_BUFFER;
  stateEnterMs    = millis();
  setLED(true, false, false);  // All-RED safety buffer
  Serial.printf("[NODE%d] All-RED buffer (%dms) before granting GREEN to Veh %c\n",
                NODE_ID, ALLRED_BUFFER_MS, winner);
}

void enterPreclear(char winner, char queued, bool extended) {
  winnerVeh    = winner;
  queuedVeh    = queued;
  stateEnterMs = millis();
  setLED(false, false, true);  // GREEN for emergency vehicle

  if (extended) {
    nodeState = STATE_EXTENDED;
    Serial.printf("[NODE%d] → EXTENDED GREEN (both vehicles close) winner=%c loser=%c\n",
                  NODE_ID, winner, queued ? queued : '?');
  } else {
    nodeState = STATE_PRECLEAR;
    Serial.printf("[NODE%d] → PRECLEAR winner=%c queued=%c\n",
                  NODE_ID, winner, queued ? queued : '-');
  }

  sendPreclear(winner, queued);
}

void passageConfirmed(char vehicleId) {
  if (vehicleId != winnerVeh && nodeState != STATE_QUEUED) return;

  Serial.printf("[NODE%d] Passage confirmed: Veh %c cleared\n",
                NODE_ID, vehicleId);

  clearedVeh     = vehicleId;
  clearedUntilMs = millis() + CLEARED_IGNORE_MS;

  if (queuedVeh && queuedVeh != vehicleId) {
    char nextWinner = queuedVeh;
    winnerVeh    = nextWinner;
    queuedVeh    = 0;
    nodeState    = STATE_QUEUED;
    stateEnterMs = millis();
    setLED(false, false, true);  // Immediate GREEN for queued vehicle
    Serial.printf("[NODE%d] → QUEUED: Immediate GREEN for Veh %c\n",
                  NODE_ID, winnerVeh);
    sendPreclear(winnerVeh, 0);
  } else {
    setLED(false, true, false);  // Yellow transition
    delay(800);
    enterNormal();
  }
}

// ─── Arbitration Engine ──────────────────────────────────────
void runArbitration() {
  uint32_t now = millis();

  bool aActive = vehA.active &&
                 (now - vehA.last_seen_ms < VEHICLE_TIMEOUT_MS) &&
                 !(clearedVeh == 'A' && now < clearedUntilMs);

  bool bActive = vehB.active &&
                 (now - vehB.last_seen_ms < VEHICLE_TIMEOUT_MS) &&
                 !(clearedVeh == 'B' && now < clearedUntilMs);

  if (!aActive && !bActive) return;

  if (aActive && !bActive) {
    Serial.printf("[NODE%d ARB] Only Veh A active → entering all-RED buffer\n", NODE_ID);
    enterAllRedBuffer('A', 0, false);
    return;
  }

  if (!aActive && bActive) {
    Serial.printf("[NODE%d ARB] Only Veh B active → entering all-RED buffer\n", NODE_ID);
    enterAllRedBuffer('B', 0, false);
    return;
  }

  // Both vehicles active → ARBITRATE
  int scoreA = computeScore(vehA.tier, vehA.dist, vehA.wait_ticks);
  int scoreB = computeScore(vehB.tier, vehB.dist, vehB.wait_ticks);

  Serial.println("--------------------------------------------");
  Serial.printf("[NODE%d ARB] Vehicle A: tier=%d dist=%4d wait=%3d → score=%3d\n",
                NODE_ID, vehA.tier, vehA.dist, vehA.wait_ticks, scoreA);
  Serial.printf("[NODE%d ARB] Vehicle B: tier=%d dist=%4d wait=%3d → score=%3d\n",
                NODE_ID, vehB.tier, vehB.dist, vehB.wait_ticks, scoreB);

  char winner = (scoreA >= scoreB) ? 'A' : 'B';
  char loser  = (winner == 'A')    ? 'B' : 'A';

  Serial.printf("[NODE%d ARB] WINNER → Vehicle %c\n", NODE_ID, winner);
  Serial.println("--------------------------------------------");

  uint16_t loserDist = (loser == 'A') ? vehA.dist : vehB.dist;

  if (loserDist > CLOSE_THRESHOLD) {
    Serial.printf("[NODE%d ARB] Loser dist=%d > %d → EXTEND\n",
                  NODE_ID, loserDist, CLOSE_THRESHOLD);
    enterAllRedBuffer(winner, loser, true);
  } else {
    Serial.printf("[NODE%d ARB] Loser dist=%d <= %d → QUEUE\n",
                  NODE_ID, loserDist, CLOSE_THRESHOLD);
    enterAllRedBuffer(winner, loser, false);
  }
}

// ─── ESP-NOW Receive Callback ────────────────────────────────
void onDataReceived(const uint8_t *mac, const uint8_t *data, int len) {
  if (!isMACAllowed(mac)) {
    return;
  }
  if (len < (int)sizeof(GCPacket)) return;

  GCPacket p;
  memcpy(&p, data, sizeof(p));
  uint32_t now = millis();

  // ── BEACON from a vehicle ──────────────────────────────────
  if (p.type == PKT_BEACON) {
    if (p.vid == 'A') {
      vehA = {true, p.tier, p.dist, p.wait_ticks, now};
    } else if (p.vid == 'B') {
      vehB = {true, p.tier, p.dist, p.wait_ticks, now};
    }
    if (nodeState == STATE_NORMAL) {
      needArbitration = true;
    }
  }

  // ── CONFIRM from a vehicle ─────────────────────────────────
  else if (p.type == PKT_CONFIRM) {
    if (nodeState == STATE_PRECLEAR ||
        nodeState == STATE_EXTENDED ||
        nodeState == STATE_QUEUED) {
      if (p.vid == winnerVeh) {
        Serial.printf("[NODE%d] CONFIRM received from Veh %c\n", NODE_ID, p.vid);
        passageConfirmed(p.vid);
      }
    }
  }

  // ── PRECLEAR relay from upstream node ─────────────────────
  else if (p.type == PKT_PRECLEAR) {
    if (nodeState == STATE_NORMAL) {
      Serial.printf("[NODE%d] PRECLEAR from Node%d → Preempting for Veh %c\n",
                    NODE_ID, p.node_id, p.winner);
      enterPreclear(p.winner, p.queued, false);
    }
  }

  // ── SYNC from Node 1 ───────────────────────────────────────
  else if (p.type == PKT_SYNC) {
    if (NODE_ID != 1 && nodeState == STATE_NORMAL) {
      normalCycleStartMs = millis();
    }
  }
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("================================================");
  Serial.printf( "  GREEN CORRIDOR — INTERSECTION NODE  %d\n", NODE_ID);
  Serial.println("  INTIUM 2026 Smart City Track");
  Serial.println("================================================");

  pinMode(PIN_RED,         OUTPUT);
  pinMode(PIN_YELLOW,      OUTPUT);
  pinMode(PIN_GREEN_LED,   OUTPUT);
  pinMode(PIN_CONFIRM_BTN, INPUT_PULLUP);

  setLED(true, false, false);
  normalCycleStartMs = millis();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.print("  MAC: "); Serial.println(WiFi.macAddress());
  Serial.printf("  WiFi channel: %d (hardcoded)\n", ESPNOW_CHANNEL);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init FAILED");
    while (true) {
      setLED(true, true, true);  delay(200);
      setLED(false, false, false); delay(200);
    }
  }
  esp_now_register_recv_cb(onDataReceived);
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, BROADCAST_MAC, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  Serial.println("  READY — Sequential Traffic Cycle active.");
  Serial.println("  Waiting for emergency vehicle beacons.");
  Serial.println();
}

// ─── Main Loop ───────────────────────────────────────────────
void loop() {
  uint32_t now    = millis();
  static bool prevBtn = HIGH;
  bool        btn     = digitalRead(PIN_CONFIRM_BTN);

  // ── Passage Confirm Button ────────────────────────────────
  if (prevBtn == HIGH && btn == LOW) {
    delay(50);
    if (digitalRead(PIN_CONFIRM_BTN) == LOW) {
      if (nodeState == STATE_PRECLEAR ||
          nodeState == STATE_EXTENDED ||
          nodeState == STATE_QUEUED) {
        Serial.printf("[NODE%d] Manual passage confirm pressed\n", NODE_ID);
        passageConfirmed(winnerVeh);
      }
    }
  }
  prevBtn = btn;

  // ── Arbitration flag ──────────────────────────────────────
  if (needArbitration) {
    needArbitration = false;
    if (nodeState == STATE_NORMAL) {
      runArbitration();
    }
  }

  // ── State Machine ─────────────────────────────────────────
  uint32_t elapsed = now - stateEnterMs;

  switch (nodeState) {

    case STATE_NORMAL: {
      uint32_t cyclePos = (now - normalCycleStartMs) % CYCLE_TOTAL_MS;

      // Node 1 broadcasts periodic sync tick to keep Node 2 & 3 aligned
      #if (NODE_ID == 1)
      static uint32_t lastSyncCycle = 0xFFFFFFFF;
      uint32_t curCycleIndex = (now - normalCycleStartMs) / CYCLE_TOTAL_MS;
      if (curCycleIndex != lastSyncCycle) {
        lastSyncCycle = curCycleIndex;
        sendSync();
      }
      #endif

      // Calculate this node's sequential position
      uint32_t myOffset = (NODE_ID - 1) * PHASE_TOTAL_MS;
      uint32_t myPos = (cyclePos + CYCLE_TOTAL_MS - myOffset) % CYCLE_TOTAL_MS;

      if (myPos < PHASE_GREEN_MS) {
        if (normalPhase != 1) {
          normalPhase = 1;
          setLED(false, false, true);   // GREEN
          Serial.printf("[NODE%d] SYNC: GREEN\n", NODE_ID);
        }
      } else if (myPos < PHASE_TOTAL_MS) {
        if (normalPhase != 2) {
          normalPhase = 2;
          setLED(false, true, false);   // YELLOW
          Serial.printf("[NODE%d] SYNC: YELLOW\n", NODE_ID);
        }
      } else {
        if (normalPhase != 0) {
          normalPhase = 0;
          setLED(true, false, false);   // RED
          Serial.printf("[NODE%d] SYNC: RED\n", NODE_ID);
        }
      }

      // Node 1 emits full corridor state for bridge/dashboard synchronization
      #if (NODE_ID == 1)
      static uint8_t lastReportedStage = 255;
      uint8_t stage = cyclePos / PHASE_TOTAL_MS; // 0=Node1, 1=Node2, 2=Node3
      bool inYellow = (cyclePos % PHASE_TOTAL_MS) >= PHASE_GREEN_MS;
      uint8_t subStage = (stage * 2) + (inYellow ? 1 : 0);
      if (subStage != lastReportedStage) {
        lastReportedStage = subStage;
        if (subStage == 0) Serial.println("[NODE1] CORRIDOR_SYNC: Node1=GREEN, Node2=RED, Node3=RED");
        else if (subStage == 1) Serial.println("[NODE1] CORRIDOR_SYNC: Node1=YELLOW, Node2=RED, Node3=RED");
        else if (subStage == 2) Serial.println("[NODE1] CORRIDOR_SYNC: Node1=RED, Node2=GREEN, Node3=RED");
        else if (subStage == 3) Serial.println("[NODE1] CORRIDOR_SYNC: Node1=RED, Node2=YELLOW, Node3=RED");
        else if (subStage == 4) Serial.println("[NODE1] CORRIDOR_SYNC: Node1=RED, Node2=RED, Node3=GREEN");
        else if (subStage == 5) Serial.println("[NODE1] CORRIDOR_SYNC: Node1=RED, Node2=RED, Node3=YELLOW");
      }
      #endif

      break;
    }

    case STATE_ALLRED_BUFFER: {
      if (elapsed >= ALLRED_BUFFER_MS) {
        Serial.printf("[NODE%d] All-RED buffer complete → granting GREEN to Veh %c\n",
                      NODE_ID, pendingWinner);
        enterPreclear(pendingWinner, pendingQueued, pendingExtended);
      }
      break;
    }

    case STATE_PRECLEAR: {
      if (elapsed >= FAILSAFE_MS) {
        Serial.printf("[NODE%d] *** FAILSAFE *** No confirm after %ds — reverting\n",
                      NODE_ID, FAILSAFE_MS / 1000);
        passageConfirmed(winnerVeh);
      }
      break;
    }

    case STATE_EXTENDED: {
      uint32_t holdTime = PRECLEAR_HOLD_MS + EXTEND_BONUS_MS;
      if (elapsed >= holdTime) {
        Serial.printf("[NODE%d] EXTENDED window complete (%lums elapsed)\n",
                      NODE_ID, (unsigned long)elapsed);
        setLED(false, true, false);
        delay(800);
        enterNormal();
      } else if (elapsed >= FAILSAFE_MS) {
        Serial.printf("[NODE%d] *** FAILSAFE *** Extended timeout\n", NODE_ID);
        enterNormal();
      }
      break;
    }

    case STATE_QUEUED: {
      if (elapsed >= FAILSAFE_MS) {
        Serial.printf("[NODE%d] *** FAILSAFE *** Queued vehicle timeout\n", NODE_ID);
        setLED(false, true, false);
        delay(800);
        enterNormal();
      }
      break;
    }
  }

  delay(10);
}
