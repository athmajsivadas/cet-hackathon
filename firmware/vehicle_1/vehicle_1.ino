/*
 * ============================================================
 *  AI-POWERED EMERGENCY GREEN CORRIDOR
 *  VEHICLE 1 — Vehicle A (38-pin ESP32 DevKit)
 *  INTIUM 2026 · Smart City Track
 * ============================================================
 *
 *  Tier 1: Critical (Cardiac / Trauma)
 *
 *  ★ PROXIMITY POTENTIOMETER ★
 *  Physical pot on GPIO 34 controls the proximity value sent in
 *  every beacon. Turn RIGHT = closer = higher priority score.
 *  Turn LEFT = far away = lower score.
 *
 *  ★ DIRECTION ROUTING ★
 *  Three buttons select the turn direction BEFORE pressing trigger.
 *  Only signals on the chosen path go green.
 *
 * ============================================================
 *  PIN MAP (38-pin ESP32 DevKit)
 * ============================================================
 *  GPIO 18  - Trigger      : Press → START beaconing
 *                            Press again → STOP + send CONFIRM
 *  GPIO 34  - Potentiometer  middle pin  (ADC input, no pullup)
 *             Left pot pin → GND    |   Right pot pin → 3.3V
 *  GPIO 32  - Direction LEFT     button (INPUT_PULLUP, → GND)
 *  GPIO 33  - Direction STRAIGHT button (INPUT_PULLUP, → GND)
 *  GPIO 35  - Direction RIGHT    button (INPUT_PULLUP, → GND)
 *  GPIO  4  - Piezo buzzer positive leg
 *  GPIO 23  - Status LED   → 220Ω → LED+ → GPIO23
 *
 * ============================================================
 *  HOW TO USE
 * ============================================================
 *  1. Turn pot RIGHT to show proximity (or LEFT if far)
 *  2. Press direction button (LEFT / STRAIGHT / RIGHT)
 *  3. Press TRIGGER to start beaconing
 *  4. When vehicle clears the node, press TRIGGER again
 *
 * ============================================================
 *  REQUIREMENTS: Arduino IDE ESP32 Dev Module, Core >= 2.0.0
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"

// ============================================================
//  VEHICLE CONFIGURATION
// ============================================================
#define VEHICLE_ID          'A'
#define VEHICLE_TIER          1
#define BEACON_STAGGER_MS     0
// ============================================================

#define PIN_TRIGGER       18
#define PIN_PROX_POT      34
#define PIN_DIR_LEFT      32
#define PIN_DIR_STRAIGHT  33
#define PIN_DIR_RIGHT     35
#define PIN_BUZZER         4
#define PIN_STATUS_LED    23

uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#define ESPNOW_CHANNEL 1

#define PKT_BEACON   0x01
#define PKT_CONFIRM  0x02
#define PKT_PRECLEAR 0x03

#define DIR_LEFT     'L'
#define DIR_STRAIGHT 'S'
#define DIR_RIGHT    'R'

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
  uint8_t  direction;
};

bool     beaconOn         = false;
uint16_t waitTicks        = 0;
uint32_t lastBeaconMs     = 0;
bool     prevBtn          = HIGH;
bool     ledState         = false;
uint8_t  currentDirection = DIR_STRAIGHT;

void beep(uint16_t hz, uint16_t ms) { tone(PIN_BUZZER, hz, ms); delay(ms + 15); }
void sirenChirp()      { beep(1000,70);  beep(1600,70);  beep(2200,70); }
void clearTone()       { beep(900,120);  beep(500,120); }
void leftSelTone()     { beep(1200,60);  beep(800,60); }
void straightSelTone() { beep(1000,150); }
void rightSelTone()    { beep(800,60);   beep(1200,60); }

const char* dirLabel(uint8_t d) {
  if (d == DIR_LEFT)  return "LEFT";
  if (d == DIR_RIGHT) return "RIGHT";
  return "STRAIGHT";
}

void readDirectionButtons() {
  uint8_t newDir = currentDirection;
  if      (digitalRead(PIN_DIR_LEFT)     == LOW) newDir = DIR_LEFT;
  else if (digitalRead(PIN_DIR_STRAIGHT) == LOW) newDir = DIR_STRAIGHT;
  else if (digitalRead(PIN_DIR_RIGHT)    == LOW) newDir = DIR_RIGHT;
  if (newDir != currentDirection) {
    currentDirection = newDir;
    if (currentDirection == DIR_LEFT)       leftSelTone();
    else if (currentDirection == DIR_RIGHT) rightSelTone();
    else                                    straightSelTone();
    Serial.printf("[DIR] Direction set -> %s\n", dirLabel(currentDirection));
  }
}

void sendBeacon() {
  GCPacket p;
  memset(&p, 0, sizeof(p));
  p.type       = PKT_BEACON;
  p.vid        = VEHICLE_ID;
  p.tier       = VEHICLE_TIER;
  p.direction  = currentDirection;
  p.dist       = (uint16_t)analogRead(PIN_PROX_POT);
  p.wait_ticks = waitTicks;
  esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
  Serial.printf("[BEACON] Veh=%c  tier=%d  pot=%4d  wait=%3d  dir=%s\n",
                p.vid, p.tier, p.dist, p.wait_ticks, dirLabel(p.direction));
}

void sendConfirm() {
  GCPacket p;
  memset(&p, 0, sizeof(p));
  p.type      = PKT_CONFIRM;
  p.vid       = VEHICLE_ID;
  p.direction = currentDirection;
  esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));
  Serial.printf("[CONFIRM] Veh=%c  dir=%s -- passage complete\n",
                VEHICLE_ID, dirLabel(currentDirection));
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {}

void onDataReceived(const uint8_t *mac, const uint8_t *data, int len) {
  if (len < (int)sizeof(GCPacket)) return;
  GCPacket p; memcpy(&p, data, sizeof(p));
  if (p.type == PKT_PRECLEAR)
    Serial.printf("[RX] PRECLEAR from Node%d  winner=%c  queued=%c\n",
                  p.node_id, p.winner, p.queued ? p.queued : '-');
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("================================================");
  Serial.println("  GREEN CORRIDOR -- VEHICLE A  (Tier 1 Critical)");
  Serial.println("  INTIUM 2026 Smart City Track");
  Serial.println("================================================");

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

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
  Serial.println("  GPIO18 -- Trigger   : START / STOP+CONFIRM");
  Serial.println("  GPIO34 -- Pot       : Turn RIGHT=closer, LEFT=far");
  Serial.println("  GPIO32 -- Dir LEFT  : press before triggering");
  Serial.println("  GPIO33 -- Dir STR.  : press before triggering (default)");
  Serial.println("  GPIO35 -- Dir RIGHT : press before triggering");
  Serial.println();
}

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
        Serial.printf(">>>>> BEACON STARTED  Dir:%s  Pot:%d <<<<<\n",
                      dirLabel(currentDirection), analogRead(PIN_PROX_POT));
      } else {
        sendConfirm();
        waitTicks = 0;
        digitalWrite(PIN_STATUS_LED, LOW);
        clearTone();
        Serial.println(">>>>> BEACON STOPPED -- CONFIRM SENT <<<<<");
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
