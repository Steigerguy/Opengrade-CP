// Seed blockage monitor - cab unit
// Olimex ESP32-POE (Ethernet) + SN65HVD230 CAN module + 12 V buzzer.
//
// - Listens to AgOpenGPS on the module network (UDP 8888) for section 1 on/off
// - Collects per-run counts from the tower nodes over CAN
// - Decides which runs are blocked, drives the buzzer
// - Serves the monitor page:  http://192.168.5.140
//
// Arduino IDE setup:
//   Board package: "esp32" by Espressif, version 3.x
//   Board: "OLIMEX ESP32-PoE"
//   No extra libraries needed.

#include <ETH.h>
#include <AsyncUDP.h>
#include <WebServer.h>
#include <Preferences.h>
#include "driver/twai.h"
#include "protocol.h"
#include "page.h"

// ---------------- network ----------------
IPAddress MY_IP(192, 168, 5, 140);
IPAddress GATEWAY(192, 168, 5, 1);
IPAddress SUBNET(255, 255, 255, 0);
const uint16_t AOG_MODULE_PORT = 8888;   // AgIO broadcasts to modules here

// ---------------- pins (Olimex ESP32-POE) ----------------
const gpio_num_t CAN_TX_PIN = GPIO_NUM_4;   // UEXT pin 3 -> SN65HVD230 CTX
const gpio_num_t CAN_RX_PIN = GPIO_NUM_36;  // UEXT pin 4 -> SN65HVD230 CRX
const int BUZZER_PIN = 32;                  // -> transistor -> 12 V buzzer

// ---------------- settings (saved in flash) ----------------
struct Settings {
  uint8_t towers = 8;       // towers expected on the bus
  uint8_t blockPct = 25;    // run below this % of its tower average = blocked
  uint8_t warnPct = 60;     // below this % = low (yellow, no alarm)
  uint8_t blockSecs = 3;    // ...for this many seconds in a row
  uint8_t startDelay = 5;   // seconds after section on before checking
  uint16_t minAvg = 5;      // tower avg hits/s below this = tower not flowing
  bool always = false;      // monitor without AgOpenGPS section signal
  uint16_t ignore[SBM_MAX_TOWERS + 1] = {0};  // bit per run, per tower
  // Map on the page: tower numbers left to right, rows split by '/',
  // first row = front. 0 = empty spot.
  String layout = "1,2,3,4/5,6,7,8";
} cfg;
Preferences prefs;

// ---------------- tower data ----------------
enum RunState : uint8_t { RS_OK = 0, RS_LOW = 1, RS_BLOCKED = 2, RS_IGNORED = 3, RS_IDLE = 4 };

struct Tower {
  uint16_t rate[SBM_MAX_RUNS];
  uint8_t numRuns;
  uint8_t flags;
  uint32_t lastSeenMs;
  bool seen;
  uint16_t avg;
  uint8_t lowSecs[SBM_MAX_RUNS];
  uint8_t state[SBM_MAX_RUNS];
};
Tower towers[SBM_MAX_TOWERS + 1];  // index 1..15

// ---------------- AgOpenGPS state (written from UDP task) ----------------
volatile uint32_t aogLastMs = 0;
volatile bool aogSection = false;
volatile uint16_t aogSpeed10 = 0;   // km/h x10

// ---------------- monitor state ----------------
bool monitoring = false;     // section on (or always)
bool active = false;         // monitoring and start delay passed
uint32_t onSinceMs = 0;
bool alarmOn = false;
uint32_t muteUntilMs = 0;
uint32_t lastEvalMs = 0;
bool ethUp = false;

AsyncUDP udp;
WebServer server(80);

// ======================================================================
//  Settings
// ======================================================================
void loadSettings() {
  prefs.begin("sbm", true);
  cfg.towers = prefs.getUChar("towers", cfg.towers);
  cfg.blockPct = prefs.getUChar("block", cfg.blockPct);
  cfg.warnPct = prefs.getUChar("warn", cfg.warnPct);
  cfg.blockSecs = prefs.getUChar("secs", cfg.blockSecs);
  cfg.startDelay = prefs.getUChar("delay", cfg.startDelay);
  cfg.minAvg = prefs.getUShort("min", cfg.minAvg);
  cfg.always = prefs.getBool("always", cfg.always);
  prefs.getBytes("ignore", cfg.ignore, sizeof(cfg.ignore));
  cfg.layout = prefs.getString("layout", cfg.layout);
  prefs.end();
  if (cfg.towers < 1 || cfg.towers > SBM_MAX_TOWERS) cfg.towers = 8;
}

void saveSettings() {
  prefs.begin("sbm", false);
  prefs.putUChar("towers", cfg.towers);
  prefs.putUChar("block", cfg.blockPct);
  prefs.putUChar("warn", cfg.warnPct);
  prefs.putUChar("secs", cfg.blockSecs);
  prefs.putUChar("delay", cfg.startDelay);
  prefs.putUShort("min", cfg.minAvg);
  prefs.putBool("always", cfg.always);
  prefs.putBytes("ignore", cfg.ignore, sizeof(cfg.ignore));
  prefs.putString("layout", cfg.layout);
  prefs.end();
}

// ======================================================================
//  AgOpenGPS (UDP)
// ======================================================================
// PGN 239 (0xEF) machine data: 0x80 0x81 0x7F 0xEF len uturn speed hydLift
//   tram geoStop x sc1to8 sc9to16 crc
// PGN 229 (0xE5) 64 sections: 0x80 0x81 0x7F 0xE5 len sc1to8 ...
void onAogPacket(AsyncUDPPacket &p) {
  const uint8_t *d = p.data();
  size_t n = p.length();
  if (n < 6 || d[0] != 0x80 || d[1] != 0x81) return;
  if (d[3] == 0xEF && n >= 13) {
    aogSpeed10 = d[6];
    aogSection = d[11] & 0x01;
    aogLastMs = millis();
  } else if (d[3] == 0xE5 && n >= 6) {
    aogSection = d[5] & 0x01;
    aogLastMs = millis();
  }
}

bool aogConnected() { return millis() - aogLastMs < 3000; }

// ======================================================================
//  CAN
// ======================================================================
void canBegin() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  g.rx_queue_len = 64;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) == ESP_OK && twai_start() == ESP_OK) {
    Serial.println("CAN started (250 kbit/s)");
  } else {
    Serial.println("CAN start FAILED");
  }
}

void canPoll() {
  twai_message_t m;
  while (twai_receive(&m, 0) == ESP_OK) {
    if (m.extd || m.rtr || m.data_length_code < 8) continue;
    uint8_t tw, part;
    if (!sbm_parse_id(m.identifier, &tw, &part)) continue;
    Tower &T = towers[tw];
    if (part < 2) {
      for (uint8_t k = 0; k < 4; k++)
        T.rate[part * 4 + k] = m.data[k * 2] | (m.data[k * 2 + 1] << 8);
    } else {
      T.rate[8] = m.data[0] | (m.data[1] << 8);
      T.numRuns = (m.data[2] == 8 || m.data[2] == 9) ? m.data[2] : 9;
      T.flags = m.data[4];
      T.lastSeenMs = millis();
      T.seen = true;
    }
  }

  // Recover if the bus went off (e.g. cable unplugged with no other nodes).
  twai_status_info_t st;
  if (twai_get_status_info(&st) == ESP_OK) {
    if (st.state == TWAI_STATE_BUS_OFF) twai_initiate_recovery();
    else if (st.state == TWAI_STATE_STOPPED) twai_start();
  }
}

bool towerOnline(uint8_t t) {
  return towers[t].seen && millis() - towers[t].lastSeenMs < 3000;
}

// ======================================================================
//  Blockage logic (runs once a second)
// ======================================================================
void evaluate() {
  uint32_t now = millis();
  bool mon = cfg.always || (aogConnected() && aogSection);
  if (mon && !monitoring) onSinceMs = now;
  monitoring = mon;
  active = monitoring && (now - onSinceMs >= (uint32_t)cfg.startDelay * 1000UL);

  bool anyBlocked = false;
  bool anyOffline = false;

  for (uint8_t t = 1; t <= cfg.towers; t++) {
    Tower &T = towers[t];
    bool online = towerOnline(t);
    bool tuning = T.flags & SBM_FLAG_TUNE;
    uint8_t n = T.numRuns ? T.numRuns : 9;
    if (!online) anyOffline = true;

    // Tower average over runs that are not ignored.
    uint32_t sum = 0, cnt = 0;
    for (uint8_t r = 0; r < n; r++) {
      if (cfg.ignore[t] & (1 << r)) continue;
      sum += T.rate[r];
      cnt++;
    }
    T.avg = cnt ? sum / cnt : 0;
    bool towerDead = T.avg < cfg.minAvg;   // whole tower not flowing (primary hose plugged?)

    for (uint8_t r = 0; r < SBM_MAX_RUNS; r++) {
      if (r >= n) { T.state[r] = RS_IDLE; T.lowSecs[r] = 0; continue; }
      if (cfg.ignore[t] & (1 << r)) { T.state[r] = RS_IGNORED; T.lowSecs[r] = 0; continue; }
      if (!active || !online || tuning) { T.state[r] = RS_IDLE; T.lowSecs[r] = 0; continue; }

      uint32_t pct = T.avg ? (uint32_t)T.rate[r] * 100 / T.avg : 0;
      bool low = towerDead || pct < cfg.blockPct;
      if (low) { if (T.lowSecs[r] < 255) T.lowSecs[r]++; }
      else T.lowSecs[r] = 0;

      if (T.lowSecs[r] >= cfg.blockSecs) { T.state[r] = RS_BLOCKED; anyBlocked = true; }
      else if (pct < cfg.warnPct) T.state[r] = RS_LOW;
      else T.state[r] = RS_OK;
    }
  }

  alarmOn = active && (anyBlocked || anyOffline);
}

void driveBuzzer() {
  uint32_t now = millis();
  bool muted = (int32_t)(muteUntilMs - now) > 0;
  bool on = alarmOn && !muted && ((now / 300) % 2 == 0);   // beep-beep
  digitalWrite(BUZZER_PIN, on ? HIGH : LOW);
}

// ======================================================================
//  Web page + JSON
// ======================================================================
void handleRoot() { server.send(200, "text/html", PAGE_HTML); }

void handleData() {
  uint32_t now = millis();
  String j;
  j.reserve(2500);
  j += "{\"aog\":"; j += aogConnected() ? 1 : 0;
  j += ",\"sec\":"; j += aogSection ? 1 : 0;
  j += ",\"mon\":"; j += monitoring ? 1 : 0;
  j += ",\"act\":"; j += active ? 1 : 0;
  int wait = 0;
  if (monitoring && !active) wait = cfg.startDelay - (now - onSinceMs) / 1000;
  j += ",\"wait\":"; j += wait;
  j += ",\"spd\":"; j += String(aogSpeed10 / 10.0, 1);
  j += ",\"alarm\":"; j += alarmOn ? 1 : 0;
  int muteLeft = (int32_t)(muteUntilMs - now) > 0 ? (muteUntilMs - now) / 1000 : 0;
  j += ",\"mute\":"; j += muteLeft;
  j += ",\"eth\":"; j += ethUp ? 1 : 0;
  j += ",\"t\":[";
  for (uint8_t t = 1; t <= cfg.towers; t++) {
    Tower &T = towers[t];
    uint8_t n = T.numRuns ? T.numRuns : 9;
    if (t > 1) j += ',';
    j += "{\"id\":"; j += t;
    j += ",\"on\":"; j += towerOnline(t) ? 1 : 0;
    j += ",\"tune\":"; j += (T.flags & SBM_FLAG_TUNE) ? 1 : 0;
    j += ",\"avg\":"; j += T.avg;
    j += ",\"r\":[";
    for (uint8_t r = 0; r < n; r++) { if (r) j += ','; j += T.rate[r]; }
    j += "],\"s\":[";
    for (uint8_t r = 0; r < n; r++) { if (r) j += ','; j += T.state[r]; }
    j += "]}";
  }
  j += "],\"cfg\":{\"towers\":"; j += cfg.towers;
  j += ",\"block\":"; j += cfg.blockPct;
  j += ",\"warn\":"; j += cfg.warnPct;
  j += ",\"secs\":"; j += cfg.blockSecs;
  j += ",\"delay\":"; j += cfg.startDelay;
  j += ",\"min\":"; j += cfg.minAvg;
  j += ",\"always\":"; j += cfg.always ? 1 : 0;
  j += ",\"layout\":\""; j += cfg.layout; j += '"';
  j += "}}";
  server.send(200, "application/json", j);
}

long argOr(const char *name, long dflt, long lo, long hi) {
  if (!server.hasArg(name)) return dflt;
  long v = server.arg(name).toInt();
  return v < lo ? lo : (v > hi ? hi : v);
}

void handleSet() {
  cfg.towers = argOr("towers", cfg.towers, 1, SBM_MAX_TOWERS);
  cfg.blockPct = argOr("block", cfg.blockPct, 1, 95);
  cfg.warnPct = argOr("warn", cfg.warnPct, 1, 99);
  cfg.blockSecs = argOr("secs", cfg.blockSecs, 1, 60);
  cfg.startDelay = argOr("delay", cfg.startDelay, 0, 60);
  cfg.minAvg = argOr("min", cfg.minAvg, 0, 10000);
  cfg.always = argOr("always", cfg.always, 0, 1);
  if (server.hasArg("layout")) {
    // keep only digits, commas and slashes (it goes back out in JSON)
    String in = server.arg("layout"), out;
    for (size_t i = 0; i < in.length() && out.length() < 60; i++) {
      char c = in[i];
      if ((c >= '0' && c <= '9') || c == ',' || c == '/') out += c;
    }
    if (out.length()) cfg.layout = out;
  }
  saveSettings();
  server.send(200, "text/plain", "ok");
}

void handleIgnore() {
  long t = argOr("t", 0, 0, SBM_MAX_TOWERS);
  long r = argOr("r", 0, 0, SBM_MAX_RUNS);
  if (t >= 1 && r >= 1) {
    cfg.ignore[t] ^= (1 << (r - 1));
    towers[t].lowSecs[r - 1] = 0;
    saveSettings();
  }
  server.send(200, "text/plain", "ok");
}

void handleMute() {
  long secs = argOr("s", 120, 0, 3600);
  muteUntilMs = millis() + secs * 1000UL;
  server.send(200, "text/plain", "ok");
}

// ======================================================================
void onNetEvent(arduino_event_id_t event) {
  if (event == ARDUINO_EVENT_ETH_GOT_IP || event == ARDUINO_EVENT_ETH_CONNECTED) ethUp = true;
  if (event == ARDUINO_EVENT_ETH_DISCONNECTED || event == ARDUINO_EVENT_ETH_STOP) ethUp = false;
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  loadSettings();

  Network.onEvent(onNetEvent);
  // Olimex ESP32-POE: LAN8720, PHY addr 0, MDC 23, MDIO 18, power 12, clock out on GPIO17
  ETH.begin(ETH_PHY_LAN8720, 0, 23, 18, 12, ETH_CLOCK_GPIO17_OUT);
  ETH.config(MY_IP, GATEWAY, SUBNET);

  if (udp.listen(AOG_MODULE_PORT)) {
    udp.onPacket([](AsyncUDPPacket &packet) { onAogPacket(packet); });
  }

  canBegin();

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/set", handleSet);
  server.on("/ignore", handleIgnore);
  server.on("/mute", handleMute);
  server.begin();

  // short beep so you know it booted
  digitalWrite(BUZZER_PIN, HIGH); delay(150); digitalWrite(BUZZER_PIN, LOW);
  Serial.print("Seed blockage monitor cab unit at http://");
  Serial.println(MY_IP);
}

void loop() {
  canPoll();
  server.handleClient();

  uint32_t now = millis();
  if (now - lastEvalMs >= 1000) {
    lastEvalMs = now;
    evaluate();
  }
  driveBuzzer();
}
