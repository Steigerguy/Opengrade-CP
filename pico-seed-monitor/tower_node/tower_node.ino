// Seed blockage monitor - tower node firmware
// Raspberry Pi Pico H on the tower_node rev B board (8-run or 9-run).
//
// Arduino IDE setup:
//   Board package: "Raspberry Pi Pico/RP2040" by Earle Philhower
//     (Boards Manager URL:
//      https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json)
//   Board: "Raspberry Pi Pico"
//   Library: ACAN2040 (Library Manager, or ZIP from github.com/obdevel/ACAN2040)
//
// Same firmware on every tower. Set the tower number and run count once over
// USB serial (115200) and they are saved in flash:
//   id 3        -> this is tower 3 (1..8)
//   runs 8      -> 8-run tower (or 9)
//   lockout 1000-> ignore re-triggers on a run for this many microseconds
//   tune        -> toggle tune mode: run 1 raw signal on GP26 (JP1 fitted)
//   quiet       -> toggle the once-a-second printout
//   stats       -> CAN statistics
//   help

#include <ACAN2040.h>
#include <EEPROM.h>
#include "protocol.h"

// ---------------- board pins (tower_node rev B) ----------------
const uint8_t RUN_PINS[SBM_MAX_RUNS] = {2, 3, 4, 5, 6, 7, 8, 9, 12};
const uint8_t CAN_RX_PIN = 10;
const uint8_t CAN_TX_PIN = 11;
const uint8_t ADC_TAP_PIN = 26;      // run 1 raw, via JP1
const uint8_t CAN_PIO = 0;

// ---------------- saved settings ----------------
struct Config {
  uint32_t magic;
  uint8_t towerId;
  uint8_t numRuns;
  uint16_t reserved;
  uint32_t lockoutUs;
};
const uint32_t CONFIG_MAGIC = 0x53424D31;  // "SBM1"
Config cfg;

// ---------------- counting ----------------
volatile uint32_t counts[SBM_MAX_RUNS];
volatile uint32_t lastHitUs[SBM_MAX_RUNS];
volatile uint32_t lockoutUs = 1000;

// One tiny interrupt handler per run (template makes 9 copies).
template <int I>
void runIsr() {
  uint32_t t = micros();
  if (t - lastHitUs[I] >= lockoutUs) {
    lastHitUs[I] = t;
    counts[I]++;
  }
}
void (*const RUN_ISRS[SBM_MAX_RUNS])() = {
  runIsr<0>, runIsr<1>, runIsr<2>, runIsr<3>, runIsr<4>,
  runIsr<5>, runIsr<6>, runIsr<7>, runIsr<8>,
};

// ---------------- CAN ----------------
void canCallback(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg) {
  // Towers only transmit; nothing to do with received frames.
  (void)cd; (void)notify; (void)msg;
}
ACAN2040 *can = nullptr;

// ---------------- state ----------------
bool tuneMode = false;
bool printOn = true;
uint8_t seq = 0;
uint32_t lastReportMs = 0;
uint32_t txFails = 0;
uint16_t rates[SBM_MAX_RUNS];
String cmdLine;

void loadConfig() {
  EEPROM.begin(256);
  EEPROM.get(0, cfg);
  if (cfg.magic != CONFIG_MAGIC || cfg.towerId < 1 || cfg.towerId > SBM_MAX_TOWERS ||
      (cfg.numRuns != 8 && cfg.numRuns != 9)) {
    cfg.magic = CONFIG_MAGIC;
    cfg.towerId = 1;
    cfg.numRuns = 9;
    cfg.reserved = 0;
    cfg.lockoutUs = 1000;
  }
  lockoutUs = cfg.lockoutUs;
}

void saveConfig() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
  Serial.println("saved");
}

void printConfig() {
  Serial.printf("Tower %u, %u runs, lockout %lu us, CAN %lu kbit/s on GP%u(TX)/GP%u(RX)\n",
                cfg.towerId, cfg.numRuns, (unsigned long)cfg.lockoutUs,
                (unsigned long)(SBM_CAN_BITRATE / 1000), CAN_TX_PIN, CAN_RX_PIN);
}

void printHelp() {
  Serial.println("Commands: id <1-8>, runs <8|9>, lockout <us>, tune, quiet, stats, help");
}

void handleCommand(String line) {
  line.trim();
  line.toLowerCase();
  if (line.length() == 0) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  long val = sp < 0 ? -1 : line.substring(sp + 1).toInt();

  if (cmd == "id" && val >= 1 && val <= SBM_MAX_TOWERS) {
    cfg.towerId = val; saveConfig();
  } else if (cmd == "runs" && (val == 8 || val == 9)) {
    cfg.numRuns = val; saveConfig();
  } else if (cmd == "lockout" && val >= 50 && val <= 100000) {
    cfg.lockoutUs = val; lockoutUs = val; saveConfig();
  } else if (cmd == "tune") {
    tuneMode = !tuneMode;
    Serial.println(tuneMode ? "TUNE MODE on: fit JP1, run 1 raw signal on GP26"
                            : "TUNE MODE off");
  } else if (cmd == "quiet") {
    printOn = !printOn;
  } else if (cmd == "stats") {
    struct can2040_stats st;
    can->get_statistics(&st);
    Serial.printf("CAN rx %lu tx %lu attempts %lu parse errors %lu, send fails %lu\n",
                  (unsigned long)st.rx_total, (unsigned long)st.tx_total,
                  (unsigned long)st.tx_attempt, (unsigned long)st.parse_error,
                  (unsigned long)txFails);
  } else {
    printHelp();
  }
  printConfig();
}

void sendFrames() {
  struct can2040_msg m;
  for (uint8_t part = 0; part < 3; part++) {
    memset(&m, 0, sizeof(m));
    m.id = sbm_can_id(cfg.towerId, part);
    m.dlc = 8;
    if (part < 2) {
      for (uint8_t k = 0; k < 4; k++) {
        uint16_t r = rates[part * 4 + k];
        m.data[k * 2] = r & 0xFF;
        m.data[k * 2 + 1] = r >> 8;
      }
    } else {
      uint16_t r9 = cfg.numRuns >= 9 ? rates[8] : 0;
      m.data[0] = r9 & 0xFF;
      m.data[1] = r9 >> 8;
      m.data[2] = cfg.numRuns;
      m.data[3] = seq;
      m.data[4] = tuneMode ? SBM_FLAG_TUNE : 0;
      m.data[5] = SBM_FW_VERSION;
    }
    if (!(can->ok_to_send() && can->send_message(&m))) txFails++;
  }
  seq++;
}

// Sample run 1's raw piezo signal flat out for ~1 s and print the peak.
void tuneReport() {
  uint16_t peak = 0;
  uint32_t total = 0, n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 1000) {
    uint16_t v = analogRead(ADC_TAP_PIN);
    if (v > peak) peak = v;
    total += v;
    n++;
  }
  float peakV = peak * 3.3f / 4095.0f;
  float avgV = (float)total / n * 3.3f / 4095.0f;
  Serial.printf("TUNE run1 peak %.3f V  avg %.3f V  (%lu samples)  ", peakV, avgV, (unsigned long)n);
  for (int i = 0; i < (int)(peakV * 20); i++) Serial.print('#');
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  analogReadResolution(12);
  loadConfig();

  for (uint8_t i = 0; i < SBM_MAX_RUNS; i++) {
    pinMode(RUN_PINS[i], INPUT);   // board has 10k pull-ups to 3V3
    attachInterrupt(digitalPinToInterrupt(RUN_PINS[i]), RUN_ISRS[i], RISING);
  }

  can = new ACAN2040(CAN_PIO, CAN_TX_PIN, CAN_RX_PIN, SBM_CAN_BITRATE, F_CPU, canCallback);
  can->begin();

  delay(1500);  // give USB serial a moment
  Serial.println("\nSeed blockage monitor - tower node");
  printConfig();
  printHelp();
  lastReportMs = millis();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      handleCommand(cmdLine);
      cmdLine = "";
    } else if (cmdLine.length() < 40) {
      cmdLine += c;
    }
  }

  uint32_t now = millis();
  uint32_t elapsed = now - lastReportMs;
  if (elapsed < 1000) return;
  lastReportMs = now;

  // Grab and clear the counts.
  uint32_t snap[SBM_MAX_RUNS];
  noInterrupts();
  for (uint8_t i = 0; i < SBM_MAX_RUNS; i++) {
    snap[i] = counts[i];
    counts[i] = 0;
  }
  interrupts();

  for (uint8_t i = 0; i < SBM_MAX_RUNS; i++) {
    uint32_t r = (i < cfg.numRuns) ? snap[i] * 1000UL / elapsed : 0;
    rates[i] = r > 65535 ? 65535 : r;
  }

  sendFrames();
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));  // heartbeat

  if (tuneMode) {
    tuneReport();   // takes ~1 s; counts keep accumulating meanwhile
    lastReportMs = millis() - 1000;  // send again right after
  } else if (printOn) {
    Serial.printf("T%u", cfg.towerId);
    for (uint8_t i = 0; i < cfg.numRuns; i++) Serial.printf(" r%u:%4u", i + 1, rates[i]);
    Serial.printf("  seq %u%s\n", seq, txFails ? "  (CAN send fails, check bus)" : "");
  }
}
