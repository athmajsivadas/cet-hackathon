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
 *  GPIO 18  - Trigger push-button  (INPUT_PULLUP, active LOW)
 *             Press once  → start beaconing
 *             Press again → stop beaconing + send CONFIRM
 *  GPIO 19  - Priority-tier 2-way switch
 *             HIGH / open  → Tier 1  (Critical: cardiac/trauma/stroke)
 *             LOW / closed → Tier 2  (Standard transport)
 *  GPIO 34  - Potentiometer wiper   (ADC, 0–4095)
 *             Turn CW (max) = very close to node → large proximity bonus
 *             Turn CCW(min) = far from node → no bonus
 *  GPIO  4  - Piezo buzzer (+ → GPIO4, − → GND)
 *  GPIO 23  - Status LED   (+ → 220Ω → GPIO23, − → GND)
 *             Blinks at 1 Hz while beacon is active
 *
 * ============================================================
 *  REQUIREMENTS
 * ============================================================
 *  Arduino IDE board: "ESP32 Dev Module"
 *  Espressif ESP32 Arduino Core >= 2.0.0  (ESP-NOW built-in)
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"   // needed for esp_wifi_set_channel()

// ============================================================
//  ★  CHANGE BEFORE FLASHING  ★
// ============================================================
#define VEHICLE_ID        'A'   // 'A' for Vehicle A, 'B' for Vehicle B
#define BEACON_STAGGER_MS   0   //  0 for Vehicle A, 250 for Vehicle B
// ============================================================

// ─── Pin Definitions ────────────────────────────────────────
#define PIN_TRIGGER     18
#define PIN_TIER_SW     19
#define PIN_DIST_DIAL   34      // Input-only pin — ADC only, no pullup
#define PIN_BUZZER       4
#define PIN_STATUS_LED  23

// ─── ESP-NOW broadcast to all nodes simultaneously ──────────
uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─── Fixed WiFi channel — MUST match across all 5 boards ────
// Channel 1 is hardcoded to prevent silent comm failures if any
// board happens to drift (e.g. after a brief router association).
#define ESPNOW_CHANNEL 1

// ─── Packet Type Constants ───────────────────────────────────
#define PKT_BEACON   0x01   // Vehicle → nodes   (every 500 ms)
#define PKT_CONFIRM  0x02   // Vehicle → nodes   (passage done)
#define PKT_PRECLEAR 0x03   // Node    → nodes   (relay / advance notice)

// ─── Unified packet structure (packed, no padding) ──────────
// IMPORTANT: this struct MUST be byte-for-byte identical in both
// vehicle_unit.ino and intersection_node.ino — any mismatch will
// cause silent data corruption across the broadcast.
struct __attribute__((packed)) GCPacket {
  uint8_t  type;           // PKT_BEACON / PKT_CONFIRM / PKT_PRECLEAR
  char     vid;            // Vehicle ID 'A' or 'B'  (0 for node packets)
  uint8_t  tier;           // 1 = critical, 2 = standard
  uint16_t dist;           // Potentiometer 0-4095 (proximity)
  uint16_t wait_ticks;     // Beacon cycles sent — anti-starvation counter
  uint8_t  node_id;        // Originating node (0 = vehicle packet)
  uint8_t  target_node_id; // PRECLEAR: intended recipient node (0 = broadcast all)
  char     winner;         // PRECLEAR only: vehicle that got GREEN
  char     queued;         // PRECLEAR only: vehicle queued (0 = none)
};

// ─── Vehicle State ───────────────────────────────────────────
bool     beaconOn     = false;
uint16_t waitTicks    = 0;
uint32_t lastBeaconMs = 0;
bool     prevBtn      = HIGH;
bool     ledState     = false;

// ─── Buzzer ──────────────────────────────────────────────────
void beep(uint16_t hz, uint16_t ms) {
  tone(PIN_BUZZER, hz, ms);
  delay(ms + 15);
}

// Ascending siren chirp — played when beaconing starts
void sirenChirp() {
  beep(1000, 70);
  beep(1600, 70);
  beep(2200, 70);
}

// Descending clear tone — played when beaconing stops
void clearTone() {
  beep(900, 120);
  beep(500, 120);
}

// ─── Packet Senders ──────────────────────────────────────────
void sendBeacon() {
  GCPacket p;
  memset(&p, 0, sizeof(p));
  p.type       = PKT_BEACON;
  p.vid        = VEHICLE_ID;
  p.tier       = (digitalRead(PIN_TIER_SW) == HIGH) ? 1 : 2;
  p.dist       = (uint16_t)analogRead(PIN_DIST_DIAL);
  p.wait_ticks = waitTicks;

  esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));

  // Judges can see this in Serial Monitor
  Serial.printf("[BEACON] Veh=%c  tier=%d  dist=%4d  wait_ticks=%3d\n",
                p.vid, p.tier, p.dist, p.wait_ticks);
}

void sendConfirm() {
  GCPacket p;
  memset(&p, 0, sizeof(p));
  p.type = PKT_CONFIRM;
  p.vid  = VEHICLE_ID;

  esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
  Serial.printf("[CONFIRM] Veh=%c — passage complete\n", VEHICLE_ID);
}

// ─── ESP-NOW Callbacks ───────────────────────────────────────
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  // Uncomment to debug TX delivery:
  // Serial.printf("  TX: %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void onDataReceived(const uint8_t *mac, const uint8_t *data, int len) {
  if (len < (int)sizeof(GCPacket)) return;
  GCPacket p;
  memcpy(&p, data, sizeof(p));
  // Vehicle logs PRECLEAR advance-notices from nodes (future OLED display use)
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
  Serial.println("  INTIUM 2026 Smart City Track");
  Serial.println("================================================");

  pinMode(PIN_TRIGGER,    INPUT_PULLUP);
  pinMode(PIN_TIER_SW,    INPUT_PULLUP);  // open = HIGH = Tier 1
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  // ADC config for potentiometer on GPIO34
  analogReadResolution(12);        // 12-bit → 0–4095
  analogSetAttenuation(ADC_11db); // 0–3.3V input range

  // WiFi must be STA for ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // Lock to channel 1 — prevents silent RX failures if any board
  // drifts to a different channel (e.g. after touching a router).
  // ALL 5 boards must use the same channel.
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.print("  MAC: "); Serial.println(WiFi.macAddress());
  Serial.printf("  WiFi channel: %d (hardcoded)\n", ESPNOW_CHANNEL);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init FAILED");
    // Rapid-blink status LED as error indicator
    while (true) {
      digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
      delay(150);
    }
  }
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);

  // Add broadcast peer — required even for broadcast sends on most
  // arduino-esp32 core versions (2.0.x+); without this esp_now_send()
  // to FF:FF:FF:FF:FF:FF will silently fail.
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, BROADCAST_MAC, 6);
  peer.channel = ESPNOW_CHANNEL;  // must match esp_wifi_set_channel above
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  delay(BEACON_STAGGER_MS);  // stagger B by 250 ms vs A
  beep(700, 120);             // startup ready tone

  Serial.println();
  Serial.println("  READY.");
  Serial.println("  Trigger btn  : press to START beaconing, press again to STOP+CONFIRM");
  Serial.println("  Tier switch  : UP/open = Tier1 Critical | DOWN = Tier2 Standard");
  Serial.println("  Dist dial    : CW (4095) = very close   | CCW (0) = far away");
  Serial.println();
}

// ─── Main Loop ───────────────────────────────────────────────
void loop() {
  uint32_t now    = millis();
  bool     btnNow = digitalRead(PIN_TRIGGER);

  // Detect falling edge (HIGH → LOW = button pressed)
  if (prevBtn == HIGH && btnNow == LOW) {
    delay(50);                          // debounce wait
    if (digitalRead(PIN_TRIGGER) == LOW) {
      beaconOn = !beaconOn;

      if (beaconOn) {
        // ── BEACON ON ──────────────────────────────────────
        waitTicks    = 0;
        lastBeaconMs = now - 501;  // trigger first beacon immediately
        sirenChirp();
        Serial.println(">>>>> BEACON STARTED <<<<<");
      } else {
        // ── BEACON OFF ─────────────────────────────────────
        sendConfirm();
        waitTicks = 0;
        digitalWrite(PIN_STATUS_LED, LOW);
        clearTone();
        Serial.println(">>>>> BEACON STOPPED — CONFIRM SENT <<<<<");
      }
    }
  }
  prevBtn = btnNow;

  // Send BEACON every 500 ms while active
  if (beaconOn && (now - lastBeaconMs >= 500)) {
    lastBeaconMs = now;
    sendBeacon();
    waitTicks++;
    ledState = !ledState;
    digitalWrite(PIN_STATUS_LED, ledState);
  }

  delay(5);
}
