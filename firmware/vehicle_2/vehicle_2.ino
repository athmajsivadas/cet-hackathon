/*
 * ============================================================
 *  AI-POWERED EMERGENCY GREEN CORRIDOR
 *  VEHICLE 2 — Vehicle B (38-pin ESP32 DevKit)
 *  INTIUM 2026 · Smart City Track
 * ============================================================
 *
 *  Tier 2: Standard Transport
 *
 *  ★ DIRECTION ROUTING ★
 *  Three buttons let the driver choose the intended turn direction
 *  BEFORE pressing the trigger. Only the signals on the chosen
 *  path go green — other intersections stay in normal cycle.
 *
 * ============================================================
 *  PIN MAP
 * ============================================================
 *  GPIO 18  - Trigger     : Press → START beaconing
 *                           Press again → STOP + send CONFIRM
 *  GPIO 32  - Direction LEFT     button (INPUT_PULLUP)
 *  GPIO 33  - Direction STRAIGHT button (INPUT_PULLUP) ← default
 *  GPIO 35  - Direction RIGHT    button (INPUT_PULLUP)
 *  GPIO  4  - Piezo buzzer
 *  GPIO 23  - Status LED (blinks while beaconing)
 *
 * ============================================================
 *  REQUIREMENTS
 * ============================================================
 *  Arduino IDE: ESP32 Dev Module, Core >= 2.0.0
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"

// ============================================================
//  VEHICLE CONFIGURATION
// ============================================================
#define VEHICLE_ID          'B'
#define VEHICLE_TIER          2     // 2 = Standard Transport
#define BEACON_STAGGER_MS   250     // 250ms for Vehicle B (avoid collision with A)
// ============================================================

// ─── Pin Definitions ────────────────────────────────────────
#define PIN_TRIGGER       18
#define PIN_DIR_LEFT      32   // Press = turn LEFT at intersection
#define PIN_DIR_STRAIGHT  33   // Press = go STRAIGHT (default)
#define PIN_DIR_RIGHT     35   // Press = turn RIGHT at intersection
#define PIN_BUZZER         4
#define PIN_STATUS_LED    23

// ─── ESP-NOW ─────────────────────────────────────────────────
uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#define ESPNOW_CHANNEL 1

// ─── Packet Types ────────────────────────────────────────────
#define PKT_BEACON   0x01
#define PKT_CONFIRM  0x02
#define PKT_PRECLEAR 0x03

// ─── Direction Constants ─────────────────────────────────────
#define DIR_LEFT     'L'
#define DIR_STRAIGHT 'S'
#define DIR_RIGHT    'R'

// ─── Packet Structure ─────────────────────────────────────────
// MUST be byte-for-byte identical across ALL 5 boards.
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
  uint8_t  direction;   // DIR_LEFT / DIR_STRAIGHT / DIR_RIGHT
};

// ─── State ───────────────────────────────────────────────────
bool     beaconOn         = false;
uint16_t waitTicks        = 0;
uint32_t lastBeaconMs     = 0;
bool     prevBtn          = HIGH;
bool     ledState         = false;
uint8_t  currentDirection = DIR_STRAIGHT;  // Default: go straight

// ─── Buzzer ──────────────────────────────────────────────────
void beep(uint16_t hz, uint16_t ms) {
  tone(PIN_BUZZER, hz, ms);
  delay(ms + 15);
}
void sirenChirp()      { beep(1000,70);  beep(1600,70);  beep(2200,70); }
void clearTone()       { beep(900,120);  beep(500,120); }
void leftSelTone()     { beep(1200,60);  beep(800,60); }
void straightSelTone() { beep(1000,150); }
void rightSelTone()    { beep(800,60);   beep(1200,60); }

// ─── Direction Label Helper ──────────────────────────────────
const char* dirLabel(uint8_t d) {
  if (d == DIR_LEFT)  return "LEFT";
  if (d == DIR_RIGHT) return "RIGHT";
  return "STRAIGHT";
}

// ─── Read Direction Buttons ──────────────────────────────────
void readDirectionButtons() {
  uint8_t newDir = currentDirection;
  if      (digitalRead(PIN_DIR_LEFT)     == LOW) newDir = DIR_LEFT;
  else if (digitalRead(PIN_DIR_STRAIGHT) == LOW) newDir = DIR_STRAIGHT;
  else if (digitalRead(PIN_DIR_RIGHT)    == LOW) newDir = DIR_RIGHT;

  if (newDir != currentDirection) {
    currentDirection = newDir;
    if (currentDirection == DIR_LEFT)      leftSelTone();
    else if (currentDirection == DIR_RIGHT) rightSelTone();
    else                                    straightSelTone();
    Serial.printf("[DIR] Direction set → %s\n", dirLabel(currentDirection));
  }
}

// ─── Beacon / Confirm ─────────────────────────────────────────
void sendBeacon() {
  GCPacket p;
  memset(&p, 0, sizeof(p));
  p.type       = PKT_BEACON;
  p.vid        = VEHICLE_ID;
  p.tier       = VEHICLE_TIER;
  p.direction  = currentDirection;
  uint16_t d   = 1200 + (waitTicks * 300);
  p.dist       = (d > 3800) ? 3800 : d;
  p.wait_ticks = waitTicks;
  esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
  Serial.printf("[BEACON] Veh=%c  tier=%d  dist=%4d  wait=%3d  dir=%s\n",
                p.vid, p.tier, p.dist, p.wait_ticks, dirLabel(p.direction));
}

void sendConfirm() {
  GCPacket p;
  memset(&p, 0, sizeof(p));
  p.type      = PKT_CONFIRM;
  p.vid       = VEHICLE_ID;
  p.direction = currentDirection;
  esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
  Serial.printf("[CONFIRM] Veh=%c  dir=%s — passage complete\n",
                VEHICLE_ID, dirLabel(currentDirection));
}

// ─── ESP-NOW Callbacks ───────────────────────────────────────
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {}

void onDataReceived(const uint8_t *mac, const uint8_t *data, int len) {
  if (len < (int)sizeof(GCPacket)) return;
  GCPacket p; memcpy(&p, data, sizeof(p));
  if (p.type == PKT_PRECLEAR) {
    Serial.printf("[RX] PRECLEAR from Node%d → winner=%c  queued=%c\n",
                  p.node_id, p.winner, p.queued ? p.queued : '-');
  }
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("================================================");
  Serial.println("  GREEN CORRIDOR — VEHICLE B  (Tier 2 Standard)");
  Serial.println("  INTIUM 2026 Smart City Track");
  Serial.println("================================================");

  pinMode(PIN_TRIGGER,      INPUT_PULLUP);
  pinMode(PIN_DIR_LEFT,     INPUT_PULLUP);
  pinMode(PIN_DIR_STRAIGHT, INPUT_PULLUP);
  pinMode(PIN_DIR_RIGHT,    INPUT_PULLUP);
  pinMode(PIN_STATUS_LED,   OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.print("  MAC: "); Serial.println(WiFi.macAddress());
  Serial.printf("  WiFi channel: %d\n", ESPNOW_CHANNEL);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init FAILED");
    while (true) { digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED)); delay(150); }
  }
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);

  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, BROADCAST_MAC, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  delay(BEACON_STAGGER_MS);
  beep(700, 120);

  Serial.println();
  Serial.println("  READY.");
  Serial.println("  GPIO18 — Trigger : START beaconing / STOP + CONFIRM");
  Serial.println("  GPIO32 — Direction: LEFT    (press before triggering)");
  Serial.println("  GPIO33 — Direction: STRAIGHT (default)");
  Serial.println("  GPIO35 — Direction: RIGHT   (press before triggering)");
  Serial.println();
}

// ─── Loop ────────────────────────────────────────────────────
void loop() {
  uint32_t now    = millis();
  bool     btnNow = digitalRead(PIN_TRIGGER);

  readDirectionButtons();

  if (prevBtn == HIGH && btnNow == LOW) {
    delay(50);
    if (digitalRead(PIN_TRIGGER) == LOW) {
      beaconOn = !beaconOn;
      if (beaconOn) {
        waitTicks    = 0;
        lastBeaconMs = now - 501;
        sirenChirp();
        Serial.printf(">>>>> BEACON STARTED — Direction: %s <<<<<\n",
                      dirLabel(currentDirection));
      } else {
        sendConfirm();
        waitTicks = 0;
        digitalWrite(PIN_STATUS_LED, LOW);
        clearTone();
        Serial.println(">>>>> BEACON STOPPED — CONFIRM SENT <<<<<");
      }
    }
  }
  prevBtn = btnNow;

  if (beaconOn && (now - lastBeaconMs >= 500)) {
    lastBeaconMs = now;
    sendBeacon();
    waitTicks++;
    ledState = !ledState;
    digitalWrite(PIN_STATUS_LED, ledState);
  }

  delay(5);
}