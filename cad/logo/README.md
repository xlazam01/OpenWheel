# OpenWheel logo

The filter wheel seen from the front: the rim ring with five filter apertures around the hub. The filter at
the top is solid: it is the one in the light path, and the light leaves through a break in the rim as an arrow.
Same drawing rules as the Triton logo (`Triton/cad/logo`): ring Ø20/Ø17.2, 0.7 mm knockouts, 1.1 mm lines,
an arrowhead like the trident's centre tine, with its tip at the same height (11.4 mm).

| File | What |
|---|---|
| `openwheel_badge.svg` | Badge only, 20 × 21.4 mm (ring Ø20) |
| `openwheel_lockup.svg` | Badge + "OPENWHEEL" (Arial Bold, 6 mm caps), 75 × 21.4 mm |
| `make_logo.py` | Generator (shapely + matplotlib): `python -I make_logo.py` rewrites the SVGs and `preview.png` |
| `lid_logo.png` | The badge on the motor box lid (outer face, seen from below) |

The SVGs hold only filled closed outlines in mm (no strokes, no text objects).

## Embossing in SolidCraft

1. Sketch on the face (`project_edges: false`): `sketch.insert_svg {path, at: [x, y], align: "center", width: <mm>, angle}`,
   then `sketch.constraint.fix` on all its points (dof 0).
2. Extrude **join** or **cut** 0.6 mm for a two-colour print (0.4 shows the second colour poorly); 0.4 mm
   is enough for a single-colour relief. The default profile pick ("all") takes the
   filter counters as holes.

The badge gives 11 profiles (7 islands, 4 counters). Extruded, the volume is area × depth: 151.5 mm² at ×1.

## Where it is used

| Part | Placement |
|---|---|
| `cad/diy/ofw_diy_motorbox` lid | outer face (bed face), ×1.1 (ring Ø22), centre (17, −66) next to the button pads, recessed 0.6 mm (two-colour print; 0.4 until 2026-11-11), arrow toward the wheel centre (+Y); sketch `LogoSketch`, cut `Logo` |

## Size limits (0.4 mm nozzle)

At the drawn size, lines are 1.1 mm wide or more and gaps at least 0.7 mm. Only the arrow tip is thinner.
- Badge: no smaller than Ø16 mm.
- Lockup: no smaller than 60 mm wide.
- On a face that prints on the bed, deboss it. The recess roofs are bridges 0.6 mm above the bed, at most
  4 mm wide (the solid filter at ×1.1).
