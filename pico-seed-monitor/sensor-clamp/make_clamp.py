"""Clamp-on piezo sensor holder for a 1.25" OD seed hose.

Two halves bolt around the hose with 2 x M4 bolts per side.
The top half has a flat, recessed seat for a 20 mm piezo disc,
with a thin floor between the disc and the hose.

Print both halves split-face DOWN on the bed, no supports:
the piezo seat is then the smooth top layer (turn on ironing if you have it).

Run:  pip install manifold3d trimesh  then  python3 make_clamp.py
"""
from manifold3d import Manifold, CrossSection
import numpy as np
import trimesh

# ---------------- dimensions (mm) ----------------
HOSE_OD = 31.75          # 1.25 in
WALL = 4.0               # shell thickness around the hose
LENGTH = 50.0            # along the hose
SPLIT_GAP = 0.5          # removed from each half so bolts squeeze the hose
FLAT_W = 10.0            # flat pad on top of bore under the piezo (easy bridge)

PIEZO_D = 20.0           # brass disc diameter
POCKET_D = PIEZO_D + 1.0 # seat diameter
FLOOR = 1.5              # plastic between disc and hose (thinner = more signal)
POCKET_DEPTH = 3.0       # room for disc + solder + potting
BOSS_W = POCKET_D + 6    # flat boss carrying the seat
BOSS_L = 30.0
CABLE_W = 4.5            # cable slot width

TAB_T = 6.0              # tab thickness (each half)
TAB_OUT = 13.0           # how far tabs stick out past the shell
BOLT_D = 4.5             # M4 clearance
NUT_AF = 7.2             # M4 nut across flats (+clearance)
NUT_DEPTH = 3.4
BOLT_Z = (11.0, LENGTH - 11.0)
SEG = 128
# --------------------------------------------------

R_IN = HOSE_OD / 2
R_OUT = R_IN + WALL
TAB_X = R_OUT + TAB_OUT / 2 - 1.0          # bolt hole centre


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def zcyl(r, z0, z1, x=0, y=0, n=SEG):
    return Manifold.cylinder(z1 - z0, r, r, n).translate([x, y, z0])


def ycyl(r, y0, y1, x, z, n=64):
    # cylinder along Y: build along Z then rotate
    c = Manifold.cylinder(y1 - y0, r, r, n)
    return c.rotate([-90, 0, 0]).translate([x, y0, z])


def half(top: bool):
    # Build as the TOP half (y >= 0), split plane at y=0, hose along Z.
    body = zcyl(R_OUT, 0, LENGTH)
    for s in (-1, 1):
        x0, x1 = sorted((s * (R_OUT - 3), s * (R_OUT + TAB_OUT)))
        body += box(x0, x1, 0, TAB_T, 0, LENGTH)

    flat_y = np.sqrt(R_IN**2 - (FLAT_W / 2) ** 2)   # height of the bore's flat pad
    zc = LENGTH / 2
    if top:
        seat_y = flat_y + FLOOR
        boss_top = max(seat_y + POCKET_DEPTH, R_OUT + 1.5)
        body += box(-BOSS_W / 2, BOSS_W / 2, 0, boss_top, zc - BOSS_L / 2, zc + BOSS_L / 2)

    # keep only y >= 0
    body = body ^ box(-100, 100, 0, 100, -1, LENGTH + 1)

    # bore with a small flat on top (under the piezo)
    bore = zcyl(R_IN, -1, LENGTH + 1)
    bore -= box(-50, 50, flat_y, 50, -2, LENGTH + 2)
    body -= bore

    if top:
        # piezo seat (pocket opens upward, axis along Y)
        body -= ycyl(POCKET_D / 2, seat_y, boss_top + 1, 0, zc, SEG)
        # cable slot from pocket out one end of the boss
        body -= box(-CABLE_W / 2, CABLE_W / 2, seat_y + 1.0, boss_top + 1,
                    zc, zc + BOSS_L / 2 + 1)

    # bolt holes (+ nut traps on the outside face of the bottom half)
    for s in (-1, 1):
        for z in BOLT_Z:
            body -= ycyl(BOLT_D / 2, -1, TAB_T + 1, s * TAB_X, z)
            if not top:
                hexagon = CrossSection.circle(NUT_AF / 2 / np.cos(np.pi / 6), 6)
                nut = Manifold.extrude(hexagon, NUT_DEPTH + 1).rotate([-90, 0, 0])
                body -= nut.translate([s * TAB_X, TAB_T - NUT_DEPTH, z])

    # take a little off the split face so the halves clamp before touching
    body = body ^ box(-100, 100, SPLIT_GAP, 100, -1, LENGTH + 1)
    return body.translate([0, -SPLIT_GAP, 0])


def save(m, path):
    mesh = m.to_mesh()
    t = trimesh.Trimesh(vertices=mesh.vert_properties[:, :3], faces=mesh.tri_verts)
    # rotate so the split face lies on the bed (Z up)
    t.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [1, 0, 0]))
    t.apply_translation([0, 0, -t.bounds[0][2]])
    t.export(path)
    print(path, "watertight:", t.is_watertight, "size mm:", np.round(t.extents, 1))
    return t


if __name__ == "__main__":
    a = save(half(True), "clamp_top_piezo.stl")
    b = save(half(False), "clamp_bottom_nuts.stl")
