# Clamp-on piezo sensor: 1.25" OD hose

A two-piece clamp that bolts around the seed hose. The top half has a flat,
recessed seat for a **20 mm piezo disc**, with 1.5 mm of plastic between the
disc and the hose.

| File | What |
|------|------|
| `clamp_top_piezo.stl` | Top half with the piezo seat and cable slot |
| `clamp_bottom_nuts.stl` | Bottom half with M4 nut traps |
| `make_clamp.py` | Parametric source. Change sizes at the top and rerun |

## Printing

- Print **both halves split-face down** (flat side on the bed). That's how the STLs are oriented.
- **No supports.** The bore has a small 10 mm flat on top that bridges easily.
  The piezo seat is a top surface, so it comes out flat and smooth.
  Turn on **ironing** if your slicer has it.
- Use PETG or ABS/ASA rather than PLA, for heat and sun. Use 100% infill
  or lots of walls around the seat; stiffer plastic carries more signal.

## Assembly

1. Put an M4 nut in each trap on the bottom half.
2. Solder shielded 2-core cable to the disc: centre (ceramic) = **SIG**, brass = **GND**.
   Keep the solder blob small.
3. Glue the disc into the seat **brass side down** with a thin layer of epoxy.
   Press it down so there are no air bubbles. Route the cable out the slot.
4. Fill the pocket with epoxy or silicone to seal it and hold the cable.
5. Clamp it around the hose, ideally on the **outside of a bend**, with 4 × M4 × 16–20 mm bolts.
   The halves are designed with a 1 mm gap, so tightening squeezes the hose.

## Main dimensions (mm)

Bore 31.75 · length 50 · overall width about 66 · height about 40 assembled · bolt spacing 28 along the hose.

## Things to tweak in `make_clamp.py`

- `HOSE_OD`: if the clamp is loose or won't close, adjust ±0.3.
- `FLOOR`: a thinner floor (1.0) gives more signal, a thicker one more strength.
- `PIEZO_D`: set to 15 or 27 for other disc sizes (the boss grows to suit).
- `SPLIT_GAP`: more gap gives more squeeze.
