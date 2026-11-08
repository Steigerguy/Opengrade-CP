# Seed blockage monitor

Air seeder, one section: 8 towers (4 × 9 runs, 4 × 8 runs) = 68 runs.

```
piezo sensors -> tower node (Pico H) x8 --CAN 250k--> cab unit (ESP32-POE) --Ethernet--> switch
                                                        |  buzzer              AgOpenGPS tablet:
                                                        |                      - sends section on/off
                                                        +-------------------> - shows http://192.168.5.140
```

| Folder | What |
|--------|------|
| `main.py` | MicroPython **bench test**: counts and tune mode over USB, no CAN |
| `tower_node/` | **Tower firmware** (Arduino C++): counting + CAN |
| `cab_monitor/` | **Cab firmware** (Arduino, ESP32): AgOpenGPS section, blockage logic, buzzer, web page |
| `sensor-clamp/` | 3D-printable clamp-on piezo holder for 1.25" hose |

> Neither firmware has been compiled or run on hardware yet. Expect a round of fixes on first build.

---

## Bench test (MicroPython): `main.py`

The quickest way to check a board and set the RV1 threshold before the C++ firmware.
1. Flash MicroPython for the **Pico** (hold BOOTSEL, drag the `.uf2` on). In Thonny, save `main.py` to the Pico and press Run.
2. Set `NUM_RUNS` (8 or 9). Tap piezos and watch the counts.
3. **Threshold:** fit JP1 and set `TUNE_MODE = True`. Note the peak voltage with only noise, then with seed or a tap.
   Set RV1's wiper (LM339 pin 4) between those two values with a meter.

---

## Tower node: `tower_node/`

Board: `tower_node` rev B (KiCad). Pins:

| Signal | GPIO |
|--------|------|
| Runs 1–8 | GP2–GP9 |
| Run 9 (9-run board) | GP12 |
| CAN RX / TX (SN65HVD230) | GP10 / GP11 |
| ADC tap (run 1 raw, JP1) | GP26 |

Each seed hit is a rising edge (LM339 output idles low). Hits within `lockout` µs on the same run count once.

**Setup (Arduino IDE)**
1. *File → Preferences → Additional boards URLs*:
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
2. *Boards Manager*: install **Raspberry Pi Pico/RP2040** (Earle Philhower). Select board **Raspberry Pi Pico**.
3. *Library Manager*: install **ACAN2040**. If it isn't listed, download the ZIP from github.com/obdevel/ACAN2040 and use *Sketch → Include Library → Add .ZIP*.
4. Open `tower_node/tower_node.ino` and upload. For the first upload, hold BOOTSEL while plugging in.

**Per-tower settings.** Every tower runs the same firmware. Open *Serial Monitor* (115200, newline) and type:

| Command | Does |
|---------|------|
| `id 3` | This is tower 3 (number them 1–8) |
| `runs 8` | 8-run tower (default 9) |
| `lockout 1000` | Re-trigger lockout in µs |
| `tune` | Toggle tune mode: prints run 1's raw peak voltage (fit JP1) |
| `quiet` | Toggle the per-second printout |
| `stats` | CAN statistics |

Settings are saved in flash. The serial monitor also shows `T3 r1: 120 r2: 118 ...` every second, the same as the MicroPython test.

## CAN protocol: `protocol.h`

250 kbit/s, 11-bit IDs. Each tower sends 3 frames a second, with IDs `0x100 + tower×16 + part`:

- part 0: runs 1–4, uint16 hits/s each
- part 1: runs 5–8
- part 2: run 9, number of runs, sequence, flags (tune mode), firmware version

Fit 120 Ω termination **only at the cab unit and the last tower**.

## Cab unit: `cab_monitor/`

**Parts**
- **Olimex ESP32-POE** (Ethernet). Power it from a 12 V USB car adapter into its USB-C port. PoE isn't needed.
- **SN65HVD230** CAN module: 3V3, GND, CTX → **GPIO4**, CRX → **GPIO36** (UEXT pins 3 and 4). CANH/CANL go to the bus. Fit its 120 Ω resistor.
- **12 V buzzer**: + to 12 V, − to the collector of an NPN (2N2222 or similar). Emitter to GND, base through 1 kΩ from **GPIO32**.
  (Check GPIO32 on your board's pinout. Change `BUZZER_PIN` if needed.)
- Ethernet cable to a spare port on the AgOpenGPS switch.

**Setup (Arduino IDE)**
1. *Boards Manager*: install **esp32** by Espressif, **version 3.x**. Select board **OLIMEX ESP32-PoE**.
2. Open `cab_monitor/cab_monitor.ino` and upload. It beeps once on boot.
3. On the tablet, open **http://192.168.5.140** in a browser and put it beside AgOpenGPS.
   Tap **Sound on** once so the tablet can beep too.

**How it decides**
- **Monitoring** = AgOpenGPS section 1 is on. The unit hears this from AgIO's broadcasts on UDP 8888, so nothing needs changing in AgOpenGPS.
  After the section turns on, it waits the **start delay** (5 s) before checking. It stops instantly when the section goes off.
- Each run is compared to **its own tower's average**. Below **25 %** for **3 s** = **BLOCKED** (red, buzzer). Below 60 % = low (yellow, no alarm).
- If a whole tower's average drops below **min avg** while monitoring (primary hose plugged), all its runs go red.
- A tower that stops sending for 3 s shows **NO SIGNAL** and alarms while monitoring.
- **Machine map** at the top shows the towers where they sit on the seeder. Each tower is drawn like its outlet head:
  a ring of run segments, with run 1 at the top (front) going clockwise. Blocked runs go red and the tower centre flashes.
  Tap a tower to jump to its flow bars. Set the layout in Settings, e.g. `1,2,3,4/5,6,7,8`
  (tower numbers left to right, `/` starts the next row back, first row = front, `0` = empty spot).
- **Front / Back buttons** in the header switch a whole rank's monitoring on or off (first layout row = Front,
  second = Back). Towers in a rank that's off aren't checked, don't alarm, and show dimmed. The choice is saved.
- **Tap a run's bar** to ignore it (e.g. a broken sensor). Ignores are saved.
- **Mute** silences the buzzer for 2 minutes.
- All thresholds are in **Settings** at the bottom of the page.

**Bench testing with one tower:** set *Towers on the bus* to 1, and tick *Monitor without AgOpenGPS*
(or switch section 1 on in AgOpenGPS in simulator mode).
