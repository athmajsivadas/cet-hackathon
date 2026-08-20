/*
 * ============================================================
 *  AI-POWERED EMERGENCY GREEN CORRIDOR
 *  VEHICLE UNIT FIRMWARE — 38-pin ESP32 DevKit
 *  INTIUM 2026 · Smart City Track
 * ============================================================
 *
 *  Flash this sketch to BOTH ambulance/vehicle units.
 *
 *  ★ BEFORE FLASHING — change the two defines below:
 *    Vehicle A:  VEHICLE_ID 'A'  |  BEACON_STAGGER_MS   0
 *    Vehicle B:  VEHICLE_ID 'B'  |  BEACON_STAGGER_MS 250
 *
 * ============================================================
 *  PIN MAP (38-pin ESP32 DevKit)
 * ============================================================
 *  GPIO 32  - LEFT button     (INPUT_PULLUP) → Target Node 1
 *  GPIO 33  - STRAIGHT button (INPUT_PULLUP) → Target Node 2
 *  GPIO 35  - RIGHT button    (INPUT)        → Target Node 3 (ext pullup/active LOW)
 *  GPIO 18  - Main Trigger button (INPUT_PULLUP)
 *  GPIO 19  - Priority Tier switch (INPUT_PULLUP, HIGH=Tier1, LOW=Tier2)
 *  GPIO 34  - Potentiometer wiper (ADC, 0-4095)
 *  GPIO  4  - Piezo buzzer (+ → GPIO4, − → GND)
 *  GPIO 23  - Status LED   (+ → 220Ω → GPIO23, − → GND)
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"

// ============================================================
//  ★  CHANGE BEFORE FLASHING  ★
// ============================================================
#define VEHICLE_ID 'A'   // 'A' for Vehicle A, 'B' for Vehicle B
#define BEACON_STAGGER_MS   0   //  0 for Vehicle A, 250 for Vehicle B
// ============================================================

// ─── Pin Definitions ────────────────────────────────────────
#define PIN_BTN_LEFT     32   // Target Node 1
#define PIN_BTN_STRAIGHT 33   // Target Node 2
#define PIN_BTN_RIGHT    35   // Target Node 3 (Input only)
#define PIN_TRIGGER      18
#define PIN_TIER_SW      19
#define PIN_DIST_DIAL    34
#define PIN_BUZZER        4
#define PIN_STATUS_LED   23

// ─── ESP-NOW broadcast to all nodes simultaneously ──────────
uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#define ESPNOW_CHANNEL 1

// ─── Packet Types ────────────────────────────────────────────
#define PKT_BEACON   0x01
#define PKT_CONFIRM  0x02
#define PKT_PRECLEAR 0x03

// ─── Unified Packet Structure ────────────────────────────────
struct __attribute__((packed)) GCPacket {
  uint8_t  type;
  char     vid;
  uint8_t  tier;
  uint16_t dist;
  uint16_t wait_ticks;
  uint8_t  node_id;
  uint8_t  target_node_id; // 1 = Left (Node 1), 2 = Straight (Node 2), 3 = Right (Node 3)
  char     winner;
  char     queued;
};

// ─── Vehicle State ───────────────────────────────────────────
bool     beaconOn     = false;
uint8_t  targetNode   = 1;     // Default to Node 1 (Left)
uint16_t waitTicks    = 0;
uint32_t lastBeaconMs = 0;

bool prevBtnTrigger  = HIGH;
bool prevBtnLeft     = HIGH;
bool prevBtnStraight = HIGH;
bool prevBtnRight    = HIGH;
bool ledState        = false;

// ─── Buzzer ──────────────────────────────────────────────────
void beep(uint16_t hz, uint16_t ms) {
  tone(PIN_BUZZER, hz, ms);
  delay(ms + 15);
}

void sirenChirp() {
  beep(1000, 70);
  beep(1600, 70);
  beep(2200, 70);
}

void clearTone() {
  beep(900, 120);
  beep(500, 120);
}

// ─── Packet Senders ──────────────────────────────────────────
void sendBeacon() {
  GCPacket p;
  memset(&p, 0, sizeof(p));
  p.type           = PKT_BEACON;
  p.vid            = VEHICLE_ID;
  p.tier           = (digitalRead(PIN_TIER_SW) == HIGH) ? 1 : 2;
  p.dist           = (uint16_t)analogRead(PIN_DIST_DIAL);
  p.wait_ticks     = waitTicks;
  p.target_node_id = targetNode;

  esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));

  const char* dirName = (targetNode == 1) ? "LEFT (Node 1)" :
                        (targetNode == 2) ? "STRAIGHT (Node 2)" : "RIGHT (Node 3)";

  Serial.printf("[BEACON] Veh=%c target=%s tier=%d dist=%4d wait_ticks=%3d\n",
                p.vid, dirName, p.tier, p.dist, p.wait_ticks);
}

void sendConfirm() {
  GCPacket p;
  memset(&p, 0, sizeof(p));
  p.type           = PKT_CONFIRM;
  p.vid            = VEHICLE_ID;
  p.target_node_id = targetNode;

  esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
  Serial.printf("[CONFIRM] Veh=%c target=Node%d — passage complete\n",
                VEHICLE_ID, targetNode);
}

// ─── ESP-NOW Callbacks ───────────────────────────────────────
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {}

void onDataReceived(const uint8_t *mac, const uint8_t *data, int len) {
  if (len < (int)sizeof(GCPacket)) return;
  GCPacket p;
  memcpy(&p, data, sizeof(p));
  if (p.type == PKT_PRECLEAR) {
    Serial.printf("[RX] PRECLEAR from Node%d → winner=%c queued=%c\n",
                  p.node_id, p.winner, p.queued ? p.queued : '-');
  }
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("================================================");
  Serial.printf( "  GREEN CORRIDOR — VEHICLE UNIT  %c\n", VEHICLE_ID);
  Serial.println("  Directional Control: GPIO32=Left, 33=Straight, 35=Right");
  Serial.println("================================================");

  pinMode(PIN_TRIGGER,      INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT,     INPUT_PULLUP);
  pinMode(PIN_BTN_STRAIGHT, INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT,    INPUT_PULLUP); // GPIO 35 (or external pullup)
  pinMode(PIN_TIER_SW,      INPUT_PULLUP);
  pinMode(PIN_STATUS_LED,   OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.print("  MAC: "); Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init FAILED");
    while (true) {
      digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
      delay(150);
    }
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

  Serial.println("  READY.");
  Serial.println("  Press GPIO32 (Left), 33 (Straight), or 35 (Right) to trigger specific node!");
  Serial.println();
}

// Helper to start beaconing for a given target node
void triggerNode(uint8_t target) {
  if (beaconOn && targetNode == target) {
    // Stop beaconing
    beaconOn = false;
    sendConfirm();
    waitTicks = 0;
    digitalWrite(PIN_STATUS_LED, LOW);
    clearTone();
    Serial.printf(">>>>> BEACON STOPPED FOR NODE %d <<<<<\n", target);
  } else {
    // Start or switch target node
    targetNode   = target;
    beaconOn     = true;
    waitTicks    = 0;
    lastBeaconMs = millis() - 501;
    sirenChirp();
    const char* dir = (target == 1) ? "LEFT (Node 1)" :
                      (target == 2) ? "STRAIGHT (Node 2)" : "RIGHT (Node 3)";
    Serial.printf(">>>>> BEACON STARTED FOR %s <<<<<\n", dir);
  }
}

// ─── Main Loop ───────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  bool bLeft     = digitalRead(PIN_BTN_LEFT);
  bool bStraight = digitalRead(PIN_BTN_STRAIGHT);
  bool bRight    = digitalRead(PIN_BTN_RIGHT);
  bool bTrigger  = digitalRead(PIN_TRIGGER);

  // 1. Left Button (GPIO 32) → Target Node 1
  if (prevBtnLeft == HIGH && bLeft == LOW) {
    delay(50);
    if (digitalRead(PIN_BTN_LEFT) == LOW) {
      triggerNode(1);
    }
  }
  prevBtnLeft = bLeft;

  // 2. Straight Button (GPIO 33) → Target Node 2
  if (prevBtnStraight == HIGH && bStraight == LOW) {
    delay(50);
    if (digitalRead(PIN_BTN_STRAIGHT) == LOW) {
      triggerNode(2);
    }
  }
  prevBtnStraight = bStraight;

  // 3. Right Button (GPIO 35) → Target Node 3
  if (prevBtnRight == HIGH && bRight == LOW) {
    delay(50);
    if (digitalRead(PIN_BTN_RIGHT) == LOW) {
      triggerNode(3);
    }
  }
  prevBtnRight = bRight;

  // 4. Master Trigger Button (GPIO 18)
  if (prevBtnTrigger == HIGH && bTrigger == LOW) {
    delay(50);
    if (digitalRead(PIN_TRIGGER) == LOW) {
      triggerNode(targetNode);
    }
  }
  prevBtnTrigger = bTrigger;

  // Send BEACON every 500 ms while active
  if (beaconOn && (now - lastBeaconMs >= 500)) {
    lastBeaconMs = now;
    sendBeacon();
    waitTicks++;
    ledState = !ledState;
    digitalWrite(PIN_STATUS_LED, ledState ? HIGH : LOW);
  }

  delay(10);
}
