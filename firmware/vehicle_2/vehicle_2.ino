/*
 * ============================================================
 *  AI-POWERED EMERGENCY GREEN CORRIDOR
 *  VEHICLE UNIT FIRMWARE — 38-pin ESP32 DevKit
 *  INTIUM 2026 · Smart City Track
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"

// ============================================================
//  ★ CONFIGURATION PER VEHICLE ★
// ============================================================
#define VEHICLE_ID          'B'   // 'A' or 'B'
#define VEHICLE_TIER          2   // 1 = Critical, 2 = Standard
#define BEACON_STAGGER_MS     250   // 0 for A, 250 for B
// ============================================================

#define PIN_TRIGGER     18
#define PIN_BUZZER       4
#define PIN_STATUS_LED  23

#define PKT_BEACON   0x01
#define PKT_CONFIRM  0x02
#define PKT_PRECLEAR 0x03

uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#define ESPNOW_CHANNEL 1

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
};

bool     beaconOn     = false;
uint16_t waitTicks    = 0;
uint32_t lastBeaconMs = 0;
bool     prevBtn      = HIGH;
bool     ledState     = false;

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

void sendBeacon() {
  GCPacket p;
  memset(&p, 0, sizeof(p));
  p.type       = PKT_BEACON;
  p.vid        = VEHICLE_ID;
  p.tier       = VEHICLE_TIER;
  
  uint16_t simDist = 1200 + (waitTicks * 300);
  p.dist       = (simDist > 3800) ? 3800 : simDist;
  p.wait_ticks = waitTicks;

  esp_now_send(BROADCAST_MAC, (uint8_t*)&p, sizeof(p));

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

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("================================================");
  Serial.printf( "  GREEN CORRIDOR — VEHICLE UNIT  %c\n", VEHICLE_ID);
  Serial.println("  INTIUM 2026 Smart City Track");
  Serial.println("================================================");

  pinMode(PIN_TRIGGER,    INPUT_PULLUP);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

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

  Serial.println();
  Serial.println("  READY.");
  Serial.println("  Trigger btn  : press to START beaconing, press again to STOP+CONFIRM");
  Serial.println("  (Proximity and Tier are now auto-simulated in software!)");
  Serial.println();
}

void loop() {
  uint32_t now    = millis();
  bool     btnNow = digitalRead(PIN_TRIGGER);

  if (prevBtn == HIGH && btnNow == LOW) {
    delay(50);
    if (digitalRead(PIN_TRIGGER) == LOW) {
      beaconOn = !beaconOn;

      if (beaconOn) {
        waitTicks    = 0;
        lastBeaconMs = now - 501; 
        sirenChirp();
        Serial.println(">>>>> BEACON STARTED <<<<<");
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
