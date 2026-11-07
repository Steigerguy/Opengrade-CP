# Seed blockage monitor - ONE TOWER - bench test version
# Raspberry Pi Pico H, MicroPython
#
# Piezo on each run -> LM339 comparator -> Pico GPIO.
# LM339 output is open-collector: it pulls the pin LOW on each seed hit,
# and a pull-up (to 3.3V, NOT 5V) brings it back HIGH.
#
# Every second it prints hits/second for each run and flags any run
# that is way below the tower average while the seeder is running.
# CAN bus to the cab ESP32 is not in this version (pins GP16-GP20
# are left free for an MCP2515 later).

from machine import Pin
import time
import array
import machine
from micropython import const

# ---------------- SETTINGS ----------------
TOWER_ID = 1

# Run 1..9 -> GP2..GP10. For an 8-run tower delete the last pin.
# While bench testing, list only the pins you have wired up.
RUN_PINS = [2, 3, 4, 5, 6, 7, 8, 9, 10]

USE_INTERNAL_PULLUP = True  # fine for testing; use 4.7k-10k to 3.3V on the real thing

# One seed hit makes the piezo ring, which can make the comparator
# chatter. Ignore extra edges on the same run for this long.
LOCKOUT_US = const(1000)    # 1 ms -> max ~1000 counts/s per run

REPORT_MS = 1000            # how often to print / check
MIN_TOWER_AVG = 5           # tower avg hits/s below this = seeder stopped, no alarms
BLOCK_PCT = 25              # run below this % of tower average = low
BLOCK_SECONDS = 3           # ...for this many reports in a row = BLOCKED
# ------------------------------------------

N = len(RUN_PINS)
counts = array.array('i', [0] * N)
last_us = array.array('i', [0] * N)
low_secs = [0] * N
led = Pin(25, Pin.OUT)      # onboard LED: on = something blocked

pins = []


def make_handler(i):
    # Runs inside the interrupt: keep it tiny, no memory allocation.
    def handler(pin):
        t = time.ticks_us()
        if time.ticks_diff(t, last_us[i]) >= LOCKOUT_US:
            last_us[i] = t
            counts[i] += 1
    return handler


for i, gp in enumerate(RUN_PINS):
    p = Pin(gp, Pin.IN, Pin.PULL_UP if USE_INTERNAL_PULLUP else None)
    p.irq(trigger=Pin.IRQ_FALLING, handler=make_handler(i), hard=True)
    pins.append(p)

print("Tower", TOWER_ID, "-", N, "runs on GP", RUN_PINS)
print("Tap a hose / piezo and watch that run's count go up.\n")

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
    for i in range(N):
        counts[i] = 0
    machine.enable_irq(state)

    rates = [c * 1000 // elapsed for c in snap]
    avg = sum(rates) / N if N else 0
    running = avg >= MIN_TOWER_AVG

    blocked = []
    for i in range(N):
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
