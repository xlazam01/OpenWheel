# DIY notes

Everything a builder needs to know about the OpenWheel mechanics, collected while the DIY release
was modelled. It is the source for the bill of materials ([#6](https://github.com/xlazam01/OpenWheel/issues/6))
and the build instructions ([#7](https://github.com/xlazam01/OpenWheel/issues/7)); until those exist,
read this first. Items marked **open** are not decided or not tested yet.

State: 2026-11-02. Wheel, shell, both plates and the motor box are modelled (`cad/diy/`), and the
full assembly `cad/diy/ofw_diy_assembly.sc` / `.step` puts them together with the bought parts
([#5](https://github.com/xlazam01/OpenWheel/issues/5)). The assembly links the part designs
(`component.insert_design`), so it follows changes to them (`component.update_links`); the bought
parts (MF105ZZ, shims, shaft, black standoffs, black M3x8 plate screws, ISO 7380 M3x8/M3x16 and
M2x6 socket screws) are their own small designs in `cad/diy/`, and only the NEMA11 motor is a STEP
import. Threads are cosmetic everywhere (modelled threads make SolidCraft very slow). The wheel,
shaft and the two 5 × 7 shaft shims form one rigid group on a revolute joint `WheelRotation` about
the shaft axis; everything else is one rigid group, grounded on the camera plate (drive the joint
to turn the wheel). The motor drives the wheel rim by friction through a silicone tube on its shaft (the tube is not modelled).
- **Opening speed:** the assembly opens from its saved model (about 15 s, nearly all meshing). Keep it
  that way: a STEP import with open (non-closed) bodies stops SolidCraft from saving the model, so the
  whole assembly is rebuilt on every open (that was the KiCad switch models; the PCB now uses
  `ofw_diy_pcb_board.step` without them and two simple modelled switches). SolidCraft's
  `parts.insert` library screws also rebuild slowly (about 20 s each). Check with
  `solidcraft-cli bench-open <file.sc>`: `saved_model: true` and `recomputed: 0`.

## How it fits together

```text
 front plate   3 mm aluminium, sits in a 2.9 mm recess
 shell         printed ring, 14.9 mm chamber between the recess floors
   wheel       7 mm disc + 5 mm hub on a 5 mm shaft, 4 x 2" filters
   standoffs   12 x M3x15, set the plate spacing
 camera plate  3 mm aluminium, sits in a 2.9 mm recess
               ----------------------------------------------
               3 + 15 + 3 = 21 mm total stack
```

- The **standoffs, not the shell, set the stack height**. The plates rest on the standoff ends
  (15 mm) and stand 0.1 mm proud of each recess floor (14.9 mm apart), so the screws clamp the plates
  to the standoffs and the printed shell is only located, not squeezed.
- The wheel is turned by a NEMA11 stepper whose shaft reaches into the chamber next to the wheel rim
  and drives it by friction through a piece of silicone tube pushed onto the shaft (no pulley, no belt).
  Motor and PCB sit in the motor box on the flat (−Y) side.
- A **Hall sensor (A3144)** reads the magnets in the wheel rim; each filter position has its own
  magnet group, so the firmware knows where it is (see [Position coding](#position-coding)).

## Parts

Quantities are counted from the models where they exist. Sources are examples, not requirements.

| Part | Qty | Notes |
|---|---|---|
| Printed shell `cad/diy/ofw_diy_shell` | 1 | Prototype printed in Prusament Galaxy Black |
| Printed wheel `cad/diy/ofw_diy_wheel` | 1 | Black (light-tight) |
| Motor box and clip-in lid `cad/diy/ofw_diy_motorbox` | 1 each | Printed in red, see [Motor box](#motor-box) |
| M3x16 button head, ISO 7380 (box to standoffs) | 4 | Head on the box floor inside the box; through the 6.7 mm floor and the 3 mm camera plate, 6.3 mm into the standoff (the front M3x8 takes 6.3 mm from the other end, 12.6 of 15). **Check** that your standoffs are threaded at least 6.5 mm deep, else M3x12 (only 2.3 mm of thread) |
| M3x8 button head, ISO 7380 (motor ears) | 2 | From inside the box through the 3.3 mm floor and the tension slots: the full 3.2 mm of the ear thread, tip 1.3 mm into the camera-plate slot (1.7 mm short of the chamber). Not longer than 10 mm, or the tip reaches the wheel. Head 1.65 mm, 1.45 mm under the PCB (a DIN 912 head, 3 mm, would touch it) |
| M2x6 self-tapping screw (PCB) | 3 | Into the box bosses (Ø1.8 pilots, 6 mm deep; the screw ends 0.6 mm above the pilot bottom) |
| Front plate `cad/diy/ofw_diy_plate_front`, camera plate `cad/diy/ofw_diy_plate_camera` | 1 each | CNC aluminium 3 mm, matt black anodised (JLCPCB), see [Plates](#plates) |
| M3x15 standoff, round Ø5, female both ends | 12 | Filed to equal length, see below |
| M3x8 flat low-head screw, black, 12.9 (dk 5.8, k 1.0, hex 2) | 20 | Plates to standoffs: 12 front, 8 camera (the 4 motor-box positions on the camera side take the motor-box screws). Model `cad/diy/ofw_diy_screw_m3x8_lowhead` |
| M2.5 screws for camera / OAG | 6 + 6 | Through the camera's Ø2.9 flange holes into the plate threads; length per your camera and adapter |
| M2x6 self-tapping screw for plastic, socket cap (dk 3.6, k 1.8, hex 1.5) | 12 | Filter clamps, 3 per filter |
| O-ring 2 × 1 mm NBR | 12 | One under each clamp screw head |
| Magnet, cylinder 6 × 3 mm | 10 | Pressed in + super glue |
| 2" mounted filter cell, low profile (50.8 × 5 mm, M48 thread 2 mm) | 4 | Mounted vs. unmounted glass still **open** |
| Shaft Ø5 × 20 mm | 1 | `5mm_rod`; its ends stay 0.5 mm below both outer plate faces, see [Bearing stack](#bearing-stack) |
| Bearing MF105ZZ (5 × 10 × 4, flange Ø11.2 × 0.8, 2 metal shields) | 2 | One in each plate's Ø10 bore, **flange on the inner (chamber) side**; glue if loose. Model `cad/diy/ofw_diy_bearing_mf105zz` (rings, shields; balls not modelled) |
| Shim 10 × 14 × 0.3 | 2 | One under each bearing flange, so the bearing doesn't stick out of the plate |
| Shim 5 × 7 × 0.5 | 2 | On the shaft, one between each bearing's inner ring and the wheel |
| NEMA11 pancake stepper, STEPPERONLINE 28 × 28 × 9.5 mm, 0.5 A, 1 Ncm, Ø3 shaft (2 ears with M3 threads at ±17) | 1 | Model `cad/Nema11.asm` (has a Ø4 shaft) |
| Silicone tube 2 × 4 mm (ID × OD) on the motor shaft (friction roller) | 1 piece | Stretched over the Ø3 shaft (about Ø4.8), cut flush with the shaft end |
| A3144 Hall sensor (TO-92UA, 4 × 3 × 1.5 mm body, 15 mm leads; model `cad/A3144.par`) | 1 | Soldered straight into the J4 pads from the PCB **back**, pointing toward the wheel; it reaches through the box floor and the camera plate's Hall slot |
| PCB (current board, `hw/`, own BOM) | 1 | 1 × USB-C at 45°, 2 buttons with LEDs, 3 × M2 holes. The JST-PH footprints J3 (motor, 4-pin) and J4 (Hall, 3-pin) are **not fitted**: the motor wires and the Hall sensor are soldered to their pads |

**No heat-set inserts.** Earlier issue texts mention M3 3x4x5 inserts; the shell uses standoffs
in all 12 positions instead.

## Shell

`cad/diy/ofw_diy_shell.sc` (SolidCraft source), `.step`, `.3mf`. Rebuilt from `ofw4_shell_noOAG.par`
with the two obsolete horns at the top (+Y) removed ([#2](https://github.com/xlazam01/OpenWheel/issues/2)).

| | |
|---|---|
| Outer | Ø149 round part, 45° side faces, R5.1 corners, flat 85.5 mm from the centre (−Y) |
| Plate recesses | Ø145 (same outline offset 2 mm), 2.9 mm deep, both sides |
| Chamber | Ø141.8 (offset 3.6 mm), 14.9 mm high |
| Ring standoffs | 8 × Ø5.2 channels on PCD 138.78, at ±31.411°, ±1.411°, ±27.589°, ±56.589° from +X; each in a 1.51 mm pad |
| Motor-box standoffs | 4 × Ø5.4 holes in Ø8 tubes at (±17.758, −80.5) and (±34.063, −64.211) mm |
| Edges | 0.4 mm chamfer on the outer rims, 0.2 mm on lips, chamber edges, channels and tube holes |

- The ring channels are **half open to the chamber**: a standoff sitting in one reaches to R66.89,
  only **0.89 mm from the wheel rim** (R66). Keep the wheel centred on its shaft and check that it turns
  freely with all standoffs in place before closing the second plate.
- The standoff positions and angles are the plate interface. Don't move them without changing the
  plates (and the motor box for the four tubes).

## Standoffs: filing to length

The stack height is the standoff length, so all 12 must be **the same length**, not just "15 mm".
Commercial standoffs vary by a few hundredths to a tenth of a millimetre. File or sand the ends square
and check each one with a micrometer until they match. Unequal standoffs bend the plates and tilt the
filters against the optical axis.

## Motor box

`cad/diy/ofw_diy_motorbox.sc` (box and lid in one design) with `ofw_diy_motorbox.step/.3mf` and
`ofw_diy_motorbox_lid.step/.3mf`; the PCB for checking fits is `ofw_diy_pcb.sc/.step` (with the real USB-C model `GT-USB-7010ASV.step`, LCSC C2988369, from LCSC/EasyEDA, and the A3144 from `cad/A3144.par`)
([#4](https://github.com/xlazam01/OpenWheel/issues/4)). A simplified redesign of the original holder
and cap: same outline (flush with the shell's outer faces), same standoff, motor and PCB positions.

- **Height:** as low as the parts allow: 13.6 mm box + 1.6 mm lid = **15.2 mm** from the camera plate
  (was 19.4). The motor's flange rests flat on the camera plate's outer face; the plate is not
  screwed to it, the two ear screws through the box hold it. Its back face ends 0.3 mm short of the
  PCB. The lid's inner face is 0.2 mm above the button tops (13.4 mm), the highest parts after the
  USB-C.
- **Box:** 13.6 mm tall from the camera plate, 1.2 mm outline wall, 6.7 mm floor on the plate side. Motor
  pocket through the floor; ear bays 3.4 mm deep on the plate side, shaped like the motor ear grown by
  0.3 mm and swept over its travel (R2.3 at the tip, R1.7 at the ear root). The bays allow the full
  1.9 mm of travel away from the wheel (drive engagement) but only 0.6 mm toward it, so the Hall passage
  stays one uniform hole through the floor with a 0.63 mm wall to the left bay; M3 tension slots at x = ±17
  (3.8 mm travel, same as the camera plate); 4 × Ø3.3 screw holes to the motor-box standoffs;
  3 PCB bosses Ø4.5 (`d_boss`) with Ø1.8 × 6 mm pilots; the two next to the wall run into it with webs
  flush with the boss tops and R1.4 fillets tangent to the boss, drawn in the boss sketch itself
  (a separate web extrude touching the boss and wall faces left the mesh open); a 4.68 × 2.0 mm Hall-sensor
  passage over the plate's Hall slot; the motor-connector notch ends 0.9 mm inside the outer face
  with R0.5 inner corners and R0.8 edges. Above the floor the wall is one even 0.8 mm up to
  the 1.2 mm snap lip at the top (12.8 to 13.6 mm), so no thickness steps show through on the
  outside; the lip's underside is a 0.3 mm 45° ramp. On the bed face every inner edge (motor pocket,
  ear bays, Hall lobe, standoff holes) has a 0.3 mm chamfer against elephant foot; 0.4 mm on the outer bed edge,
  0.3 mm on the rim, which with the lid's 0.3 mm makes a V-groove at the seam.
- **Lid:** 1.6 mm, printed outer face down, 0.4 mm chamfer on the outer edge, 0.3 mm on the seam edge.
  Clipped in: a skirt inside the walls (0.1 mm clearance) with a 0.45 mm bead, 0.9 mm tall, that snaps under the lip.
  The bead has 0.3 mm 45° chamfers top (lead-in) and bottom (ramp against the lip's ramp, 0.2 mm
  play), so it prints without overhangs and the lid can be pulled off again (`bead_h`, `lid_clr`,
  `groove_d` in the model if the fit is too tight or loose). The skirt is 2 mm deep and ends 0.3 mm above the PCB.
  Skirt and bead are cut away over the two PCB screws next to the outline (the heads would touch
  them): two pockets with sides square to the bead, from 0.8 to 2.5 mm inside the outer face.
  The skirt root has no fillet: SolidCraft cannot end one on the pocket walls that interrupt the
  skirt. **Open:** check the skirt root on the first prints (it flexes on every snap).
  - **Buttons:** a round Ø9 finger pad over each button, cut free by a 0.5 mm slot and hinged on a
    4 mm neck toward the motor (pad centre 7.5 mm from the hinge; both pads sit 0.46 mm toward the
    hinge and 1.62 mm toward the flat edge from their button, so they look alike); the slot tails end
    in R0.25 rounds (stress relief at the hinge), and the slot edges on the outer face have a 0.3 mm
    chamfer on both sides. The
    pad is thinned to 1.0 mm from the underside, so it lies on the print bed with the rest of the
    outer face and needs no supports. A Ø2 nub under the pad, right over the button, hangs 0.2 mm
    above it, so a press moves the pad about 0.45 mm in total. The pads carry **+** (CW, SW3) and
    **−** (CCW / BOOT0, SW2), sunk 0.4 mm into the outer face. There is no LED window: the LEDs
    sit under the pad edge and only show through the slot. **Check on the print:** the pads must not
    rest on the buttons (stuck pressed); if a pad is too stiff, sand the neck thinner from inside.
  - **USB-C:** a 9.4 × 3.6 mm opening with R1 corners (`usb_w`, `usb_h`, `usb_r`, centre
    `usb_cz` 12.84 mm) through the 45° wall, across the box/lid seam. The receptacle (8.94 × 3.26,
    top 14.47 mm) reaches 0.9 mm into the lid; the lid has the same rounded profile cut 9.3 mm deep
    through its rim, skirt and underside (floor 14.64 mm, flush with the box opening, no steps), which
    leaves 0.56 mm of lid over the receptacle. The opening's bottom edge is 0.16 mm above the lid's edge chamfer.
- **Printing** (checked with `tools/printcheck.py`, no supports needed):
  - Box on its plate-side face. The two ear-bay ceilings are 12–14 mm bridges 3.4 mm above the bed:
    use bridge settings (cooling on, slower). The notch-end wall is 0.66–0.79 mm (2 perimeters).
  - Lid on its outer face. The **+** and **−** signs are 0.4 mm recesses bridged over (≤ 1.2 mm).
    The pads under the signs are 0.6 mm thick.
- **Assembly order** (from the prototype):
  1. Put the motor in, flange to the plate side, and fit the two M3x8 ear screws loosely through
     the tension slots.
  2. Screw the box to the 4 motor-box standoffs (screws from inside the box).
  3. Set the motor in the slots so the silicone tube grips the wheel rim, then tighten the two ear screws.
  4. Solder the motor wires to the J3 pads (the motor cable stays in the box).
  5. Lower the PCB onto the 3 bosses (components face the lid) so that the Hall sensor goes through
     the box passage and the plate slot, screw it down and clip the lid on.
- **No connectors at J3/J4.** The JST footprints are placeholders. The board sits only 0.3 mm above
  the motor's back, so solder the motor wires on the component side and cut the J3 pin ends flush on
  the back.
- **Hall sensor:** solder the A3144 into the J4 pads from the **back** of the board, **branded face**
  (the narrow face with the bevelled edges and the marking, not the wide flat back) toward the wheel
  centre (the magnets), leads straight along the board normal. Solder it so the **far end of the
  sensor is 17 mm from the board**: the body then sits in the chamber 1.2 to 4.2 mm past the camera
  plate's inner face (model `cad/A3144.par`, placed in `ofw_diy_pcb.sc`; corrected 2026-11-04, the
  model had the sensor turned 180°, which swaps pins 1 and 3, and 16 mm). It has to pass the plate's
  Ø2 × 2.67 slot (4.67 × 2 mm) with its 4 × 1.5 body, so keep the sensor straight and centred. The height against the magnets is checked in the full assembly
  ([#5](https://github.com/xlazam01/OpenWheel/issues/5)).
- **Hall pinout and polarity** (checked against the board): J4 pad 1 = +5V = A3144 pin 1 (VCC),
  pad 2 = GND = pin 2, pad 3 = `HALL_IN` (PA1, internal pull-up) = pin 3 (OUT). With the branded face
  toward the wheel centre the pins line up with pads 1-2-3 without crossing. The A3144 is unipolar and
  switches (output low, `HALLACTIVETYPE 2`) only on a **south pole at its branded face**: glue every
  magnet with its **south pole facing outward**. A firmware magnet check is planned in
  [#12](https://github.com/xlazam01/OpenWheel/issues/12).
- Printing: box and lid print without supports (box on its plate face, lid on its outer face).

## Plates

`cad/diy/ofw_diy_plate_front.sc` / `.step` and `cad/diy/ofw_diy_plate_camera.sc` / `.step`
([#3](https://github.com/xlazam01/OpenWheel/issues/3)). Both are flat 3 mm plates (2.5D), made for CNC
aluminium; the STEP files are what you upload to JLCPCB.

| | Front plate | Camera plate |
|---|---|---|
| Outline | Shell recess outline, line-on-line (Ø145, flat 83.5, R3.1 corners) | same |
| Shaft | Ø10 bore for an MF105 bearing | same |
| Aperture | **M48 × 0.75** thread at (0, 38) (Ø47.25 tap drill) | Ø50 at (0, 38) |
| Camera / OAG holes | 6 × M2.5 on Ø62 around the aperture (0°, ±60°, 180°) | same |
| Standoff screws | 12 × Ø3.2 with Ø6.5 × 1.3 counterbores | 8 × counterbored (ring), 4 × plain Ø3.2 (motor box) |
| Motor box | — | 2 × M3 slots at x = ±17 (3.8 mm travel to set the drive), Ø5 slot at x = 0 |
| Hall sensor | — | Ø2 × 2.67 slot at R66.95, −110° (just outside the wheel rim) |

- **Fit in the shell:** the plate outline is exactly the recess outline, with no clearance, so the
  stack is light-tight. Printers make holes a little small and outlines a little large, so fit the
  **print** to the plates, not the other way round: print the shell, try a plate, and scale the shell
  in X/Y in the slicer (typically 100.1–100.4 %, Z stays 100 %) until the plate drops in with no gap.
  The `fit` parameter in the plate models adds clearance per side if you prefer to change the plates.
- **Threads** are cosmetic thread features in the models (the STEP keeps the bores at tap-drill size).
  Order them threaded from JLCPCB (mark them in the order) or tap them yourself:
  - **M48 × 0.75** in the front-plate aperture (Ø47.25 bore, full 3 mm), for M48 adapters and filters
    on the telescope side.
  - **M2.5**, 6 holes per plate (Ø2.05). The Player One camera (Ø2.9 flange
  holes) and OAG have plain holes and rely on these threads, like Player One's own Phoenix wheel
  (21 mm thick, 6 × M2.5 on R31 on both sides).
- **Bearing bore:** Ø10 at JLCPCB's standard tolerance. If the MF105 sits loose, glue it (retaining
  compound such as Loctite 641) or fill the gap; JLCPCB also offers tighter tolerances for a price.
- **Edges:** as in the original plates, every edge has a 0.2 × 45° chamfer (outline, bore, aperture,
  all screw holes, counterbore rims, slots). The counterbores are Ø6.5 × 1.3 slots that run out through
  the plate edge with R0.5 corners, so the low screw heads (dk 5.8) never sit on a paper-thin wall.
  Only the short edge where a counterbore floor meets the plate outline stays sharp (the CAD kernel
  can't chamfer it); ask for "deburr all edges" with the order.
- **Which side is which:** the counterbores are on the outer face (−Z in the model). The camera plate
  carries the motor-box slots and the Hall sensor slot; the front plate is a plain cover.
- Camera reference (Player One, IMX571 cameras): 5 mm front flange, back focus 17.5 mm from the
  flange face. The 21 mm wheel stack counts toward your sensor spacing.

### Bearing stack

The wheel runs on a Ø5 shaft in two MF105 bearings, one in each plate. Nothing may stick out of the
outer plate faces: the camera and the OAG/adapter sit flat on them, and the camera flange reaches
almost to the shaft. Measured from the camera plate's inner face (the stack is the same from the
front plate):

| Part | Thickness | From | To |
|---|---|---|---|
| Shim 10 × 14 × 0.3, over the bearing body, under the flange | 0.3 | 0 | 0.3 |
| MF105 flange (the 3.2 mm body sits in the plate) | 0.8 | 0.3 | 1.1 |
| Shim 5 × 7 × 0.5 on the shaft, on the inner ring | 0.5 | 1.1 | 1.6 |
| Wheel: disc 7 + hub 5 = 12, **sanded to 11.8** | 11.8 | 1.6 | 13.4 |
| Shim 5 × 7 × 0.5 | 0.5 | 13.4 | 13.9 |
| MF105 flange (front plate) | 0.8 | 13.9 | 14.7 |
| Shim 10 × 14 × 0.3 | 0.3 | 14.7 | 15.0 |

- **Why this order:** the MF105 body is 3.2 mm long, so with the flange on the plate it would stand
  0.2 mm proud of a 3 mm plate. The 0.3 shim under the flange lifts it, and the body ends **0.1 mm
  below the outer face**. The flange on the inside also stops the bearing from being pushed out.
- **No play:** the wheel presses each bearing's inner ring outward through the 5 × 7 shims, and each
  bearing rests on its plate through the flange. With the standoffs bolted at 15 mm, the stack is
  locked. The 5 × 7 shims only touch the inner rings, and they keep the rotating wheel off the
  stationary bearing flanges.
- **Fit:** the printed wheel is 12 mm over disc and hub; the stack needs 11.8. Sand the **end of the
  hub** (the central peg) flat on fine paper on a flat surface until the wheel turns freely with the
  plates bolted, without axial play. The model shows the unsanded hub (0.2 mm into the front shim).
- **Wheel orientation:** disc toward the **camera plate**, hub toward the front plate. That keeps the
  magnets as close to the Hall sensor as possible (see the open item on the sensor height).
- **Shaft:** the 20 mm shaft is centred in the 21 mm stack, so each end sits 0.5 mm below its plate's
  outer face, with 3.4 mm of each bearing's 4 mm inner ring on the shaft.
- **Clearances in the assembly:** wheel disc to the camera plate 1.6 mm, filter clamp screw heads to the
  front plate 3.6 mm, wheel rim to the standoffs 0.89 mm. The interference check over plates, shell,
  wheel with filters, bearings, shims, shaft, standoffs, box, lid, Hall sensor and motor found overlaps
  only inside the wheel assembly itself (clamp screws in their pilot holes, O-ring on a cell). The
  check ran out of time on a few large pairs (plates against shell and box, motor shaft against the
  wheel); those were checked by their extents instead, which found the motor shaft open item below.

## Wheel

`cad/diy/ofw_diy_wheel.sc`, `.step`, `.3mf`; assembly with filters, O-rings and screws:
`ofw_diy_wheel_asm.sc` / `.step` ([#1](https://github.com/xlazam01/OpenWheel/issues/1)).

| | |
|---|---|
| Disc | Ø132 × 7 mm, hub Ø10 to 12 mm total height |
| Bore | Ø5.5 with 8 crush ribs (reach Ø4.9), 45° lead-in on both ends: press fit on the 5 mm shaft |
| Filter pockets | 4 on PCD 76, first at 22.5°; Ø51.2 × 4.5 seat, Ø48.7 aperture (clears the M48 cell thread) |
| Clamp holes | 12 × Ø1.8 × 6 pilot holes for M2x6 self-tapping screws, 0.2 countersink |
| Magnet pockets | 10 radial Ø6 pockets, 3.25 deep from the rim, 0.2 lead-in, 5.88° pitch |
| Other | Lightening windows, engraved numbers 1–4 |

- **Filters** drop into the seat; three M2x6 screws with a 2 × 1 O-ring under each head press on the
  cell rim. The O-ring spreads the load and keeps the cell from rattling; don't overtighten into the
  printed pilot holes.
- **Example filter set** (the prototype): 1 UV/IR cut, 2 Optolong L-Pro, 3 SVBony SV220 Ha/OIII,
  4 SVBony SV220 SII/OIII. Names and focus offsets are stored in the wheel (`:36`–`:39`), not in the
  print.
- **Shaft press fit:** the crush ribs deform on the first press. Press the shaft in straight
  (arbor press or vice), not with a hammer, and check the wheel runs true. 0.89 mm to the standoffs
  leaves little room for wobble.

## Position coding

- The rim carries **10 magnets in four groups of 4, 3, 2 and 1** at 5.88° pitch. As the wheel turns,
  each group gives a Hall pulse of a different length, which identifies the slot.
- The firmware **measures the pulse lengths during calibration**, counts the slots itself, and takes
  the **shortest pulse (the single magnet) as home**. Calibration, slot count and offsets are stored in
  the EEPROM.
- The group code and the pitch are tied to the firmware: changing the number of magnets or their
  spacing needs a firmware check too ([#10](https://github.com/xlazam01/OpenWheel/issues/10)).
- **Polarity:** the A3144 is a unipolar switch. It switches when a **south pole faces its marked
  (branded) side**, and the firmware expects an active-low output (`HALLACTIVETYPE 2`). Put **all 10
  magnets in the same orientation**. Mark the side that triggers the sensor before gluing: with the
  board powered, hold each magnet to the sensor and measure its output (it drops to 0 V when it
  switches); the side that triggers goes outward, toward the sensor. The documented way will be the
  magnet polarity check in the ASCOM setup dialog (firmware + driver, [#12](https://github.com/xlazam01/OpenWheel/issues/12)).
- The walls between neighbouring 6 mm magnets are only **0.47 mm** thick. Press the magnets in gently
  and glue them; an alternative with smaller magnets is in [#10](https://github.com/xlazam01/OpenWheel/issues/10).

## Electronics and first power-up

Details in the [README](../README.md); the parts that matter while building:

- Power and data come over **USB** (the board runs from USB 5 V behind a 0.5 A polyfuse). The motor
  current is set low on purpose: higher currents browned out the driver on the prototype.
- **Calibrate once after assembly:** long press **CW**, or hold CW while powering on, or the ASCOM
  setup dialog. Re-calibrate after taking the wheel off its shaft or moving magnets.
- **Buttons:** CW / CCW click = next / previous slot, double click = home.
- **Firmware update:** hold **CCW** while powering on (BOOT0, USB DFU), or send `:31#`.
- The wheel **turns one way only**; going back one slot takes a full lap (about 4.6 s instead of 2.1 s).
- Faults blink both LEDs alternately; the cause is readable with `:33#` / `:34#`. "Hall not seen"
  (error 6) after about 30 s of moving means no magnet was detected: check polarity and sensor distance.

## Printing

- The prototype shell was printed in **Prusament Galaxy Black**. Any opaque black filament works; the
  chamber must be light-tight.
- Orientation, layer height, walls and supports are **not recorded yet** ([#5](https://github.com/xlazam01/OpenWheel/issues/5)).
  Both the shell and the wheel are flat parts; print them on a face.
- Scale the shell in X/Y until the aluminium plates sit tight in the recesses (see [Plates](#plates)).
- Check after printing: plates drop into the recesses, standoffs slide into the channels and tubes
  (0.1–0.2 mm clearance on the Ø5 standoffs), the wheel bore takes the shaft.

## Design decisions and lessons

Why the model looks the way it does, so the next revision doesn't undo it by accident:

- **Release 1 keeps the original geometry and angles** (filter positions, magnet code, standoff
  positions), so existing shells, plates, firmware and calibration stay valid.
- **Standoffs instead of inserts:** a metal stack with filed standoffs gives a repeatable 21 mm and
  square plates; printed parts only locate.
- **Horns removed:** they held a part (OAG) that is no longer used.
- **M2x6 everywhere** for the filter clamps (one screw type to buy).
- Shell pad fillets are R0.8 instead of the original R2 (R2 doesn't fit a 1.51 mm pad); the
  side faces are exactly 45° (originally 45.03°, 0.03 mm difference at the corners).
- Wheel clamp holes have a 0.2 mm countersink: 0.4 mm overlapped the filter-seat chamfer and broke
  the printable mesh.
- Every printed part is checked before release: no CAD feature warnings, and the exported 3MF has
  0 open edges (`tools/meshcheck.py` in the Astro hub). Modelling problems found in SolidCraft and
  their workarounds are collected in [#11](https://github.com/xlazam01/OpenWheel/issues/11).

## Open items

- [ ] Hall sensor height: at 17 mm from the board the sensor body sits 1.2–4.2 mm past the camera plate's inner face; the magnets (Ø6, centres at 5.1 mm) span 2.1–8.1 mm, so the top 2.1 mm of the sensor is level with them. Confirm the switching on the prototype (live state `:45#`). If it is too weak: put the sensor on short wires (J4 pads) and glue it level with the magnets, or move the magnet pockets toward the camera side ([#5](https://github.com/xlazam01/OpenWheel/issues/5))
- [ ] Drive: the NEMA11 shaft (Ø4 model, centre 67.5 mm below the axis, mid travel) reaches 7 mm into the chamber and overlaps the wheel rim (R66) by 0.5 mm. It drives the rim through a silicone tube (not modelled): the 2 × 4 tube on the Ø3 shaft (about Ø4.8) touches the rim with the shaft at about 68.4 mm from the axis, inside the slot travel (66.9–69.4); confirm on the prototype ([#4](https://github.com/xlazam01/OpenWheel/issues/4))
- [ ] Plates: test order and fit; front aperture now M48 × 0.75 (M54 not planned); optional 4 × Ø2.7 holes on 53 × 40 that the Phoenix wheel has ([#3](https://github.com/xlazam01/OpenWheel/issues/3))
- [ ] Motor box: test print (snap fit of the lid, button pads, USB-C notch and lid pocket, box screw length, Hall sensor height), motor drive inside the chamber ([#4](https://github.com/xlazam01/OpenWheel/issues/4))
- [ ] Hall sensor position against the magnet radius and angles ([#2](https://github.com/xlazam01/OpenWheel/issues/2))
- [ ] Mounted 2" cells vs. unmounted 50.8 × 3 mm glass ([#1](https://github.com/xlazam01/OpenWheel/issues/1))
- [ ] Test print and fit check of wheel and shell; print settings ([#5](https://github.com/xlazam01/OpenWheel/issues/5))
- [x] Full assembly and clearance check (`cad/diy/ofw_diy_assembly`)
- [ ] Release exports ([#5](https://github.com/xlazam01/OpenWheel/issues/5))
