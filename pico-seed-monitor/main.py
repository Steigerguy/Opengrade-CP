# Seed blockage monitor - tower node - bench test version
# Raspberry Pi Pico H, MicroPython
# Matches KiCad "tower_node" rev B (8-run and 9-run boards).
#
# Per run on the board:
#   piezo -> 1M bleed + 1k series -> 1N4148 clamps -> LM339 IN+
#   LM339 IN- = VTH from trimpot RV1 (about 0 - 0.58 V)
#   LM339 output pulled up 10k to 3V3 on the board
# So the pin idles LOW and goes HIGH while the piezo spike is above VTH.
# Each seed hit = a RISING edge.
#
# Pico pin map (from the board):
#   OUT1-OUT4 -> GP2-GP5   (Pico pins 4-7)
#   OUT5-OUT8 -> GP6-GP9   (Pico pins 9-12)
#   OUT9      -> GP12      (Pico pin 16, 9-run board only)
#   CAN_RX    -> GP10, CAN_TX -> GP11 (SN65HVD230, not used in this test)
#   ADC_TAP   -> GP26/ADC0 (run 1 raw signal when JP1 jumper is fitted)

from machine import Pin, ADC
import time
import array
import machine
from micropython import const

# ---------------- SETTINGS ----------------
TOWER_ID = 1
NUM_RUNS = 9                # 9 or 8 for the board you are testing

# Set True with the JP1 jumper fitted to see run 1's raw piezo voltage
# (for setting the RV1 threshold). False = normal counting mode.
TUNE_MODE = False

# A hit makes the piezo ring, which can make the comparator chatter
# (there is no hysteresis on the board). Ignore extra edges on the
# same run for this long.
LOCKOUT_US = const(1000)    # 1 ms -> max ~1000 counts/s per run

REPORT_MS = 1000            # how often to print / check
MIN_TOWER_AVG = 5           # tower avg hits/s below this = seeder stopped, no alarms
BLOCK_PCT = 25              # run below this % of tower average = low
BLOCK_SECONDS = 3           # ...for this many reports in a row = BLOCKED
# ------------------------------------------

ALL_RUN_PINS = [2, 3, 4, 5, 6, 7, 8, 9, 12]
RUN_PINS = ALL_RUN_PINS[:NUM_RUNS]
led = Pin(25, Pin.OUT)      # onboard LED


def tune_mode():
    # Run 1 raw signal on GP26 via JP1. Sample flat out for one second
    # and print the biggest spike, so RV1 can be set below the seed hits
    # but above the vibration noise. Measure VTH with a meter on the
    # RV1 wiper (or LM339 pin 4) and compare.
    adc = ADC(26)
    print("TUNE MODE: run 1 raw signal on GP26 (JP1 fitted)")
    print("Set VTH (RV1 wiper) between the 'noise' peak and the 'seed' peak.\n")
    while True:
        peak = 0
        total = 0
        n = 0
        t0 = time.ticks_ms()
        while time.ticks_diff(time.ticks_ms(), t0) < 1000:
            v = adc.read_u16()
            if v > peak:
                peak = v
            total += v
            n += 1
        peak_v = peak * 3.3 / 65535
        avg_v = total / n * 3.3 / 65535
        bar = "#" * int(peak_v * 20)
        print("peak %.3f V  avg %.3f V  (%d samples)  %s" % (peak_v, avg_v, n, bar))
        led.toggle()


def count_mode():
    n_runs = len(RUN_PINS)
    counts = array.array('i', [0] * n_runs)
    last_us = array.array('i', [0] * n_runs)
    low_secs = [0] * n_runs

    def make_handler(i):
        # Runs inside the interrupt: keep it tiny, no memory allocation.
        def handler(pin):
            t = time.ticks_us()
            if time.ticks_diff(t, last_us[i]) >= LOCKOUT_US:
                last_us[i] = t
                counts[i] += 1
        return handler

    pins = []
    for i, gp in enumerate(RUN_PINS):
        p = Pin(gp, Pin.IN)  # board has its own 10k pull-ups
        p.irq(trigger=Pin.IRQ_RISING, handler=make_handler(i), hard=True)
        pins.append(p)

    print("Tower", TOWER_ID, "-", n_runs, "runs on GP", RUN_PINS)
    print("Tap a piezo and watch that run's count go up.")
    print("A run stuck at a big number with nothing touching it = threshold too low.\n")

    last_report = time.ticks_ms()
    while True:
        time.sleep_ms(10)
        now = time.ticks_ms()
        elapsed = time.ticks_diff(now, last_report)
        if elapsed < REPORT_MS:
            continue
        last_report = now

        # Grab and clear the counts without an interrupt sneaking in.
        state = machine.disable_irq()
        snap = list(counts)
        for i in range(n_runs):
            counts[i] = 0
        machine.enable_irq(state)

        rates = [c * 1000 // elapsed for c in snap]
        avg = sum(rates) / n_runs
        running = avg >= MIN_TOWER_AVG

        blocked = []
        for i in range(n_runs):
            if running and rates[i] * 100 < avg * BLOCK_PCT:
                low_secs[i] += 1
            else:
                low_secs[i] = 0
            if low_secs[i] >= BLOCK_SECONDS:
                blocked.append(i + 1)

        led.value(1 if blocked else 0)

        line = "T%d " % TOWER_ID
        line += " ".join("r%d:%4d" % (i + 1, r) for i, r in enumerate(rates))
        line += "  avg:%5.0f" % avg
        if not running:
            line += "  (stopped)"
        elif blocked:
            line += "  BLOCKED: " + ",".join(str(r) for r in blocked)
        print(line)


if TUNE_MODE:
    tune_mode()
else:
    count_mode()
