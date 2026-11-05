"""Print the SolidCraft batch (JSON) that exports the build-guide views as DXF.

Usage: python views.py p03 p04 ... > batch.json, then run the steps with the SolidCraft MCP `batch`
tool on cad/diy/ofw_diy_assembly.sc. Occurrence ids are those of that assembly (component.list).
The exploded views (explode.create) are not saved in the assembly; create them first with the
moves listed in tools/manual/README.md. Pages p11 (filters) and the part_* drawings are exported
the same way; see the README.
"""
import json, sys

import os
# DXFs go to cad/_ref/manual (ignored); SolidCraft may only write inside its allowed folders
OUT = os.path.join(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")), "cad", "_ref", "manual", "")
STD = [56, 59, 62, 65, 68, 71, 74, 77, 80, 83, 86, 89]
CS = list(range(92, 114, 3))
FS = list(range(116, 150, 3))
BX = [158, 161, 164, 167]
PS = [170, 173, 176]
FILT = ["Cell", "Glass"] + [f"{n} ({i})" for i in range(2, 5) for n in ("Cell", "Glass")] + \
       ["ORing"] + [f"ORing (ORing_2x1:{i})" for i in range(2, 13)] + \
       ["Screw"] + [f"Screw (Screw_M2x6:{i})" for i in range(2, 13)]
UP, DOWN, TOP = 0.42, -0.42, 1.52

STACK = [2, 17, 23] + STD + CS + [8]
ALL_NO_FRONT = STACK + [29, 11, 14, 38, 41, 152, 155] + BX

PAGES = {
    "p03": (None, [11], UP, True),
    "p04": ("p04_shaft", [11, 14], UP, True),
    "p07": ("p07_standoffs", [2, 17, 23] + STD + CS, 0.3, False),
    "p08": ("p08_shell", STACK, UP, False),
    "p09": ("p09_wheel", STACK + [29, 11, 14, 32], UP, True),
    "p10": ("step23_front_plate", STACK + [29, 11, 14, 32, 5, 20, 26] + FS, UP, True),
    "p12": (None, [41], UP, False),
    "p13": ("p13_motor_box", [38, 41, 152, 155], DOWN, False, ["Lid"]),
    "p14": ("p14_box_on", STACK + [38, 41, 152, 155] + BX, DOWN, False, ["Lid"]),
    "p15": (None, ALL_NO_FRONT + [32], TOP, False, ["Lid"]),
    "p16": ("step23_front_plate", ALL_NO_FRONT + [32, 5, 20, 26] + FS, UP, False),
    "p17": ("p17_pcb", ALL_NO_FRONT + [32, 5, 20, 26] + FS + [35] + PS, DOWN, False, ["Lid"]),
    "p18a": (None, ALL_NO_FRONT + [32, 5, 20, 26] + FS + [35] + PS, DOWN, False, ["Lid"]),
    "p18b": (None, [38], DOWN, False, ["MotorBox"]),
    "p19a": (None, None, UP, False),
    "p19b": (None, None, DOWN, False),
}


def steps(name):
    view, occ, pitch, hide_filters, *rest = PAGES[name]
    hide_bodies = (rest[0] if rest else []) + (FILT if hide_filters else [])
    out = [{"command": "explode.show", "params": {"view": view} if view else {}}]
    if occ is None:
        out.append({"command": "browser.visibility", "params": {"visible": True, "folder": "all"}})
    else:
        out.append({"command": "browser.visibility",
                    "params": {"visible": True, "paths": [[o] for o in occ], "isolate": True}})
    if hide_bodies:
        out.append({"command": "browser.visibility", "params": {"visible": False, "items": hide_bodies}})
    out.append({"command": "file.export_view_dxf", "params": {
        "camera": {"distance": 600, "fov": 0, "pitch": pitch, "roll": 0, "target": [0, 0, 0],
                   "up": "z", "yaw": -0.7854},
        "tolerance": 0.02, "path": OUT + name + ".dxf"}})
    return out


print(json.dumps([s for n in sys.argv[1:] for s in steps(n)]))
