# Seed blockage monitor: Pico tower node (test version)

Air seeder: 8 towers (4 × 9 runs, 4 × 8 runs). Each tower has a Pico H on the
`tower_node` rev B board (KiCad, 8-run and 9-run versions). A CAN bus
(SN65HVD230) links the towers to an ESP32 in the cab.

## Board → Pico pin map (from the KiCad files)

| Signal  | Pico pin | GPIO  |
|---------|----------|-------|
| OUT1    | 4        | GP2   |
| OUT2    | 5        | GP3   |
| OUT3    | 6        | GP4   |
| OUT4    | 7        | GP5   |
| OUT5    | 9        | GP6   |
| OUT6    | 10       | GP7   |
| OUT7    | 11       | GP8   |
| OUT8    | 12       | GP9   |
| CAN_RX  | 14       | GP10  |
| CAN_TX  | 15       | GP11  |
| OUT9    | 16       | GP12 (9-run board only) |
| ADC_TAP | 31       | GP26 / ADC0 (run 1 raw, JP1 fitted) |

Signal path for each run: piezo → 1M bleed + 1k series → clamp diodes → LM339 IN+.
IN− is VTH from RV1, about 0–0.58 V. The output is pulled up 10k to 3V3.
The pin sits **low**, and each seed hit is a **rising** edge.

## Flashing

1. Hold BOOTSEL while plugging in USB, then drag the MicroPython `.uf2` for the **Pico** (not Pico W) onto the drive.
2. In Thonny, pick the interpreter *MicroPython (Raspberry Pi Pico)*.
3. Open `main.py` and set `NUM_RUNS` (8 or 9). Then *File → Save as → Raspberry Pi Pico* as `main.py`.
4. Press Run.

## Setting the threshold (RV1)

1. Fit the JP1 jumper and set `TUNE_MODE = True`. Each second it prints the biggest spike on run 1.
2. Note the peak with only vibration or noise, then the peak with seed hitting or a finger tapping.
3. Put the meter on the RV1 wiper (LM339 pin 4) and set it between those two peaks.
4. Remove JP1, set `TUNE_MODE = False`, and run again.

## Counting mode output

```
T1 r1:  42 r2:  39 r3:   0 r4:  44 ...  avg:   31  BLOCKED: 3
```

- `LOCKOUT_US`: if one tap counts as several hits, raise it. The board has no hysteresis, so this setting handles chatter.
- `BLOCK_PCT` / `BLOCK_SECONDS`: these set how far below the tower average a run must fall, and for how long, before it's flagged.
- `MIN_TOWER_AVG`: below this average the seeder counts as stopped, so no alarms fire.

## Note for the CAN step

The SN65HVD230 is only a transceiver, and the RP2040 has no CAN controller built in.
CAN on the Pico therefore has to come from **can2040** (software CAN that runs on the PIO hardware).
can2040 is C code with no MicroPython version, so the final tower firmware will be
C++ (Arduino IDE with the arduino-pico core + ACAN2040 library). The cab ESP32 has CAN (TWAI) built in.
