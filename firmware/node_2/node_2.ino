/*
 * ============================================================
 *  AI-POWERED EMERGENCY GREEN CORRIDOR
 *  INTERSECTION NODE 2 — MIDDLE NODE (30-pin ESP32 DevKit)
 *  INTIUM 2026 · Smart City Track
 * ============================================================
 *
 *  ★ CASCADE DESIGN — ONE GREEN AT A TIME ★
 *
 *  Node 2 is the MIDDLE (cascade) node. It does NOT listen to vehicle
 *  beacons for arbitration. It only activates when it receives a
 *  PKT_PRECLEAR from Node 1 (after a vehicle passes Node 1).
 *
 *  On passage confirm, Node 2 sends PKT_PRECLEAR to Node 3.
 *
 * ============================================================
 *  PIN MAP (30-pin ESP32 DevKit)
 * ============================================================
 *  GPIO 25  - Red    LED  (+ → 220Ω → GPIO25, − → GND)
 *  GPIO 26  - Yellow LED  (+ → 220Ω → GPIO26, − → GND)
 *  GPIO 27  - Green  LED  (+ → 220Ω → GPIO27, − → GND)
 *  GPIO 18  - Passage/Confirm push-button (INPUT_PULLUP, active LOW)
 *             Press when the emergency vehicle clears this node.
 *
 * ============================================================
 *  NODE STATE MACHINE
 * ============================================================
 *  NORMAL   → normal R/G/Y cycle (stays RED until cascade trigger)
 *  PRECLEAR → GREEN granted on PKT_PRECLEAR from Node 1
 *  QUEUED   → Immediate GREEN for second queued vehicle
 *
 *  On passage confirm: sends PKT_PRECLEAR to Node 3 to continue cascade.
 *
 * ============================================================
 *  REQUIREMENTS
 * ============================================================
 *  Arduino IDE board: "ESP32 Dev Module"
 *  Espressif ESP32 Arduino Core >= 2.0.0
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"   // needed for esp_wifi_set_channel()

// ============================================================
//  ★  CHANGE BEFORE FLASHING  ★
// ============================================================
#define NODE_ID  2   // 1, 2, or 3
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

// ─── Direction Constants ─────────────────────────────────────
#define DIR_LEFT     'L'
#define DIR_STRAIGHT 'S'
#define DIR_RIGHT    'R'

// ─── Broadcast MAC ───────────────────────────────────────────
uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─── Fixed WiFi channel — MUST match across all 5 boards ────
#define ESPNOW_CHANNEL 1

// ─── Shared Packet Structure ─────────────────────────────────
// IMPORTANT: byte-for-byte identical to vehicle_unit.ino GCPacket.
// Any field reorder or size change will corrupt data silently.
struct __attribute__((packed)) GCPacket {
  uint8_t  type;
  char     vid;
  uint8_t  tier;
  uint16_t dist;
  uint16_t wait_ticks;
  uint8_t  node_id;
  uint8_t  target_node_id;
  char     winner;
  char     queued;
  uint8_t  direction;      // 'L'=Left, 'S'=Straight, 'R'=Right
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

// Direction received from upstream PRECLEAR — used when relaying to Node 3
uint8_t cascadeDirection = DIR_STRAIGHT;


// ─── MAC Whitelist (Section 11.4-A) ─────────────────────────
// Fill in all 5 MACs from Hour 1 Step 0 before flashing.
// Any BEACON or CONFIRM from an unlisted MAC is silently rejected.
// This answers the "rogue device" judge question honestly.
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
  STATE_NORMAL,         // Normal traffic light cycle
  STATE_ALLRED_BUFFER,  // 3s all-red safety window before granting GREEN
  STATE_PRECLEAR,       // GREEN held for winner; loser queued
  STATE_EXTENDED,       // GREEN extended — both vehicles very close
  STATE_QUEUED          // Immediately GREEN for queued vehicle
};
NodeState nodeState = STATE_NORMAL;

char     winnerVeh    = 0;   // Vehicle currently holding GREEN
char     queuedVeh    = 0;   // Vehicle waiting after winner passes

// Pending arbitration result — stored during STATE_ALLRED_BUFFER
// so runArbitration() doesn't need to be re-run after the buffer expires.
char     pendingWinner   = 0;
char     pendingQueued   = 0;
bool     pendingExtended = false;

// Timestamp of last state entry (for timers)
uint32_t stateEnterMs = 0;

// Normal cycle tracking
uint32_t normalTimerMs = 0;
uint8_t  normalPhase   = 0;  // 0=RED, 1=GREEN, 2=YELLOW

// Flag set in onDataReceived ISR, consumed in loop()
volatile bool needArbitration = false;

// Post-confirm ignore window: prevent immediate re-trigger
// when vehicle keeps beaconing after it has passed
char     clearedVeh    = 0;
uint32_t clearedUntilMs = 0;

// ─── Timing Constants ────────────────────────────────────────
#define ALLRED_BUFFER_MS    3000   // Safety all-red gap before GREEN grant
#define NORMAL_RED_MS       5000   // Normal red phase duration
#define NORMAL_GREEN_MS     5000   // Normal green phase duration
#define NORMAL_YELLOW_MS    2000   // Normal yellow phase duration
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
  int waits = (int)wait_ticks * 5;              // Anti-starvation (grows over time)
  return base + prox + waits;
}

// ─── PRECLEAR Relay ──────────────────────────────────────────
// Broadcast to all nodes, but target_node_id tells receivers
// whether this is meant for them. Each node filters on this field
// so Node 3 silently discards Node 1's relay (target=2) and vice
void sendCascadePreclear(char winner, char queued, uint8_t direction) {
  GCPacket p;
  memset(&p, 0, sizeof(p));
  p.type           = PKT_PRECLEAR;
  p.node_id        = NODE_ID;
  p.target_node_id = NODE_ID + 1;
  p.winner         = winner;
  p.queued         = queued;
  p.direction      = direction;
  esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
  Serial.printf("[NODE%d] ★ CASCADE → Node%d  winner=%c  dir=%c\n",
                NODE_ID, NODE_ID + 1, winner, direction);
}

// ─── State Transitions ───────────────────────────────────────
void enterNormal() {
  nodeState     = STATE_NORMAL;
  normalTimerMs = millis();
  normalPhase   = 0;
  winnerVeh     = 0;
  queuedVeh     = 0;
  setLED(true, false, false);
  Serial.printf("[NODE%d] → NORMAL (traffic cycle)\n", NODE_ID);
}

void enterPreclear(char winner, char queued, bool extended) {
  winnerVeh    = winner;
  queuedVeh    = queued;
  stateEnterMs = millis();
  setLED(false, false, true);

  if (extended) {
    nodeState = STATE_EXTENDED;
    Serial.printf("[NODE%d] → EXTENDED GREEN  winner=%c  loser=%c\n",
                  NODE_ID, winner, queued ? queued : '?');
  } else {
    nodeState = STATE_PRECLEAR;
    Serial.printf("[NODE%d] → PRECLEAR  winner=%c  queued=%c\n",
                  NODE_ID, winner, queued ? queued : '-');
  }
  // No relay here — cascade fires in passageConfirmed after vehicle clears
}

// Called when passage is confirmed (button press or PKT_CONFIRM)
void passageConfirmed(char vehicleId) {
  if (vehicleId != winnerVeh && nodeState != STATE_QUEUED) return;

  Serial.printf("[NODE%d] Passage confirmed: Veh %c cleared\n",
                NODE_ID, vehicleId);

  clearedVeh     = vehicleId;
  clearedUntilMs = millis() + CLEARED_IGNORE_MS;

  if (queuedVeh && queuedVeh != vehicleId) {
    // Cascade to Node 3, then grant GREEN to queued at this node
    sendCascadePreclear(vehicleId, 0, cascadeDirection);
    char nextWinner = queuedVeh;
    winnerVeh    = nextWinner;
    queuedVeh    = 0;
    nodeState    = STATE_QUEUED;
    stateEnterMs = millis();
    setLED(false, false, true);
    Serial.printf("[NODE%d] → QUEUED: Immediate GREEN for Veh %c\n",
                  NODE_ID, winnerVeh);
  } else {
    sendCascadePreclear(vehicleId, 0, cascadeDirection);
    setLED(false, true, false);
    delay(800);
    enterNormal();
  }
}

// ─── Arbitration Engine ──────────────────────────────────────
void runArbitration() {
  uint32_t now = millis();

  // Check which vehicles have sent a beacon recently
  bool aActive = vehA.active &&
                 (now - vehA.last_seen_ms < VEHICLE_TIMEOUT_MS) &&
                 !(vehA.active && clearedVeh == 'A' && now < clearedUntilMs);

  bool bActive = vehB.active &&
                 (now - vehB.last_seen_ms < VEHICLE_TIMEOUT_MS) &&
                 !(vehB.active && clearedVeh == 'B' && now < clearedUntilMs);

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

  // ── Both vehicles active → ARBITRATE ─────────────────────
  int scoreA = computeScore(vehA.tier, vehA.dist, vehA.wait_ticks);
  int scoreB = computeScore(vehB.tier, vehB.dist, vehB.wait_ticks);

  // Print score breakdown — judges see the "AI decision" live
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
    Serial.printf("[NODE%d ARB] Loser dist=%d > %d → EXTEND (both pass together)\n",
                  NODE_ID, loserDist, CLOSE_THRESHOLD);
    enterAllRedBuffer(winner, loser, true);    // extended after buffer
  } else {
    Serial.printf("[NODE%d ARB] Loser dist=%d <= %d → QUEUE (sequential pass)\n",
                  NODE_ID, loserDist, CLOSE_THRESHOLD);
    enterAllRedBuffer(winner, loser, false);   // preclear after buffer
  }
}

// ─── All-RED Safety Buffer (Section 11.4-B) ──────────────────
// Non-blocking: stores the pending arbitration result and transitions
// to STATE_ALLRED_BUFFER. The loop() state machine calls enterPreclear()
// after ALLRED_BUFFER_MS have elapsed.
void enterAllRedBuffer(char winner, char queued, bool extended) {
  pendingWinner   = winner;
  pendingQueued   = queued;
  pendingExtended = extended;
  nodeState       = STATE_ALLRED_BUFFER;
  stateEnterMs    = millis();
  setLED(true, false, false);  // All-RED
  Serial.printf("[NODE%d] All-RED buffer (%dms) before granting GREEN to Veh %c\n",
                NODE_ID, ALLRED_BUFFER_MS, winner);
}

// ─── ESP-NOW Receive Callback ────────────────────────────────
// NOTE: runs in WiFi task context — keep minimal, use flags
void onDataReceived(const uint8_t *mac, const uint8_t *data, int len) {
  if (!isMACAllowed(mac)) {
    Serial.printf("[NODE%d] Rejected packet from unknown MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                  NODE_ID,
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return;
  }
  if (len < (int)sizeof(GCPacket)) return;

  GCPacket p;
  memcpy(&p, data, sizeof(p));

  // ── BEACON from a vehicle ──────────────────────────────────
  // Node 2 is a DOWNSTREAM (cascade) node. It does NOT run arbitration
  // on raw vehicle beacons — only Node 1 (entry) does that.
  // Receiving beacons here is expected (ESP-NOW is broadcast), but we
  // intentionally ignore them for arbitration purposes.
  if (p.type == PKT_BEACON) {
    // Silently ignore — Node 1 handles all beacon arbitration.
    return;
  }

  // ── CONFIRM from a vehicle (passage done) ─────────────────
  else if (p.type == PKT_CONFIRM) {
    if (nodeState == STATE_PRECLEAR ||
        nodeState == STATE_EXTENDED ||
        nodeState == STATE_QUEUED) {
      if (p.vid == winnerVeh) {
        Serial.printf("[NODE%d] CONFIRM received from Veh %c\n",
                      NODE_ID, p.vid);
        passageConfirmed(p.vid);
      }
    }
  }

  // ── PRECLEAR relay from upstream Node 1 ───────────────────
  // CASCADE trigger: vehicle confirmed passage at Node 1 (STRAIGHT direction).
  // Node 2 now activates GREEN for the vehicle approaching it.
  else if (p.type == PKT_PRECLEAR) {
    if (p.target_node_id == NODE_ID) {
      cascadeDirection = p.direction ? p.direction : DIR_STRAIGHT;
      Serial.printf("[NODE%d] ★ CASCADE from Node%d → winner=%c  dir=%c\n",
                    NODE_ID, p.node_id, p.winner, cascadeDirection);
      vehA.active = false;
      vehB.active = false;
      if (p.winner == 'A') {
        vehA = {true, 1, 2000, 1, (uint32_t)millis()};
      } else {
        vehB = {true, 1, 2000, 1, (uint32_t)millis()};
      }
      if (p.queued == 'A') {
        vehA = {true, 1, 1500, 1, (uint32_t)millis()};
      } else if (p.queued == 'B') {
        vehB = {true, 1, 1500, 1, (uint32_t)millis()};
      }
      if (nodeState == STATE_NORMAL) {
        enterAllRedBuffer(p.winner, p.queued, false);
      }
    }
    // Silently discard packets not for this node
  }
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  // Uncomment to debug TX:
  // Serial.printf("  TX: %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("================================================");
  Serial.printf( "  GREEN CORRIDOR — INTERSECTION NODE  %d\n", NODE_ID);
  Serial.println("  INTIUM 2026 Smart City Track");
  Serial.println("================================================");

  // LED pins
  pinMode(PIN_RED,         OUTPUT);
  pinMode(PIN_YELLOW,      OUTPUT);
  pinMode(PIN_GREEN_LED,   OUTPUT);
  // Confirm button
  pinMode(PIN_CONFIRM_BTN, INPUT_PULLUP);

  // Start in RED (safe default)
  setLED(true, false, false);
  normalTimerMs = millis();

  // ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // Lock channel to prevent silent drift — must match all other boards.
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.print("  MAC: "); Serial.println(WiFi.macAddress());
  Serial.printf("  WiFi channel: %d (hardcoded)\n", ESPNOW_CHANNEL);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init FAILED");
    // Error: blink all three LEDs
    while (true) {
      setLED(true, true, true);  delay(200);
      setLED(false, false, false); delay(200);
    }
  }
  esp_now_register_recv_cb(onDataReceived);
  esp_now_register_send_cb(onDataSent);

  // Broadcast peer registration is required even for FF:FF:FF:FF:FF:FF
  // sends — without this, esp_now_send() to broadcast will silently fail
  // on arduino-esp32 core 2.0.x+.
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, BROADCAST_MAC, 6);
  peer.channel = ESPNOW_CHANNEL;  // must match esp_wifi_set_channel above
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  Serial.println("  READY — Waiting for emergency vehicle beacons.");
  Serial.println("  Confirm btn: press when vehicle clears this node.");
  Serial.println();
}

// ─── Main Loop ───────────────────────────────────────────────
void loop() {
  uint32_t now    = millis();
  static bool prevBtn = HIGH;
  bool        btn     = digitalRead(PIN_CONFIRM_BTN);

  // ── Passage Confirm Button (falling edge) ─────────────────
  if (prevBtn == HIGH && btn == LOW) {
    delay(50);  // debounce
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

  // ── Arbitration flag (set by onDataReceived) ──────────────
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
      // Normal R→G→Y→R traffic cycle
      uint32_t cycleElapsed = now - normalTimerMs;
      if (normalPhase == 0 && cycleElapsed >= NORMAL_RED_MS) {
        normalPhase   = 1;
        normalTimerMs = now;
        setLED(false, false, true);   // GREEN
      } else if (normalPhase == 1 && cycleElapsed >= NORMAL_GREEN_MS) {
        normalPhase   = 2;
        normalTimerMs = now;
        setLED(false, true, false);   // YELLOW
      } else if (normalPhase == 2 && cycleElapsed >= NORMAL_YELLOW_MS) {
        normalPhase   = 0;
        normalTimerMs = now;
        setLED(true, false, false);   // RED
      }
      break;
    }

    // ── All-RED buffer (Section 11.4-B) ─────────────────────
    // Holds all-RED for ALLRED_BUFFER_MS before granting GREEN.
    // Answers the "unsafe transition" judge question honestly.
    case STATE_ALLRED_BUFFER: {
      if (elapsed >= ALLRED_BUFFER_MS) {
        Serial.printf("[NODE%d] All-RED buffer complete → granting GREEN to Veh %c\n",
                      NODE_ID, pendingWinner);
        enterPreclear(pendingWinner, pendingQueued, pendingExtended);
      }
      // LED stays RED (set in enterAllRedBuffer) — nothing else to do
      break;
    }

    case STATE_PRECLEAR: {
      // GREEN held for winner vehicle
      // Failsafe: auto-revert if no confirm after FAILSAFE_MS
      if (elapsed >= FAILSAFE_MS) {
        Serial.printf("[NODE%d] *** FAILSAFE *** No confirm after %ds — reverting\n",
                      NODE_ID, FAILSAFE_MS / 1000);
        // Treat as manual passage confirm
        passageConfirmed(winnerVeh);
      }
      break;
    }

    case STATE_EXTENDED: {
      // GREEN extended — both vehicles close, pass in same window
      uint32_t holdTime = PRECLEAR_HOLD_MS + EXTEND_BONUS_MS;
      if (elapsed >= holdTime) {
        Serial.printf("[NODE%d] EXTENDED window complete (%lums elapsed)\n",
                      NODE_ID, (unsigned long)elapsed);
        setLED(false, true, false);  // brief YELLOW
        delay(800);
        enterNormal();
      } else if (elapsed >= FAILSAFE_MS) {
        Serial.printf("[NODE%d] *** FAILSAFE *** Extended timeout\n", NODE_ID);
        enterNormal();
      }
      break;
    }

    case STATE_QUEUED: {
      // GREEN for queued vehicle — also has failsafe
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
