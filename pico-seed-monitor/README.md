# Seed blockage monitor: Pico tower node (test version)

Air seeder: 8 towers (4 × 9 runs, 4 × 8 runs). Each tower has a Pico H.
Piezo sensors go through LM339 comparators, and later a CAN bus links the
towers to an ESP32 in the cab.

## Wiring (per tower)

| Run | Pico pin | Pico GPIO |
|-----|----------|-----------|
| 1   | 4        | GP2       |
| 2   | 5        | GP3       |
| 3   | 6        | GP4       |
| 4   | 7        | GP5       |
| 5   | 9        | GP6       |
| 6   | 10       | GP7       |
| 7   | 11       | GP8       |
| 8   | 12       | GP9       |
| 9   | 14       | GP10      |

- Each LM339 output goes straight to its GPIO pin. Pull-ups go to **3.3V (Pico pin 36)**, never to 5V.
  The internal pull-up is fine on the bench. On the machine, use 4.7k–10k external pull-ups.
- The LM339 GND and Pico GND must be tied together.
- An LM339 has 4 comparators, so a 9-run tower needs 3 chips and an 8-run tower needs 2.
- GP16–GP20 are kept free for an MCP2515 CAN module later.
- The onboard LED (GP25) lights when any run is flagged as blocked.

## Flashing

1. Hold BOOTSEL while plugging in USB, then drag the MicroPython `.uf2` for the **Pico** (not Pico W) onto the drive.
2. In Thonny, pick the interpreter *MicroPython (Raspberry Pi Pico)*.
3. Open `main.py`, set `RUN_PINS` to the pins you have wired, then *File → Save as → Raspberry Pi Pico* as `main.py`.
4. Press Run. You'll see one line per second, like this:

```
T1 r1:  42 r2:  39 r3:   0 r4:  44  avg:   31  BLOCKED: 3
```

## Tuning

- `LOCKOUT_US`: if one tap counts as several hits, raise it. If a full stream of seed tops out at about 1000/s, lower it.
- `BLOCK_PCT` / `BLOCK_SECONDS`: these set how far below the tower average a run must fall, and for how long, before it's flagged.
- `MIN_TOWER_AVG`: below this average the seeder counts as stopped, so no alarms fire when you're lifted out or turning.
