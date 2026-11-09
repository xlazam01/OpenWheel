# cad/triton — OpenWheel variant for Triton

Parts that turn the DIY filter wheel into the Triton variant (the Esprit 100ED imaging train,
[xlazam01/Triton#6](https://github.com/xlazam01/Triton/issues/6), [#25](https://github.com/xlazam01/OpenWheel/issues/25)).
All parts are in the DIY shell's frame: wheel centre at the origin, optical axis at (0, 38),
telescope side +Z, shell from z = −2.9 to 17.8, front plate outer face at z = 17.9.

| Design | Make | What it is |
|---|---|---|
| `ofw_triton_shell` | printed, flat, no supports | The DIY shell (linked from `../diy/ofw_diy_shell.sc`, not copied) plus two horns on top for the OAG guide-camera focuser drive |
| `ofw_triton_rot_housing` | printed, bottom on the bed, no supports | Rotator motor housing on the telescope side of the front plate, over the motor box |
| `ofw_triton_motor_plate` | printed, **top face on the bed**, no supports | Slotted plate that carries the rotator motor and slides on the housing to tension the belt |

Each design has its `.sc` (editable source), `.step` and `.3mf`.

`ofw_triton_assembly.sc` is the Triton variant of the whole wheel: `../diy/ofw_diy_assembly.sc` with the shell
relinked to `ofw_triton_shell`, plus the rotator housing and motor plate. It uses the DIY assembly frame (camera
plate outer face z = 0, front plate outer face z = 21.0). The shell sits there at +3.05; housing and plate at
**+3.10**, because their `z_fp` = 17.9 is 0.05 mm short of the real front plate face (17.95 in the shell frame).
The motor plate sits in its **belt-tensioned** position: moved 3.65 mm along the slots (motor axis 97.90 mm from
the optical axis, for the 340 mm belt). Triton's system assembly (`Triton/cad/triton_assembly.sc`) links this file.
In that assembly the belt clears the plate's underside recess by 0.35 mm (recess 2.6 mm since 2026-11-09;
the legacy 2.0 mm rubbed the belt by 0.25 mm).

## Shell horns

Copied from the legacy Solid Edge `ofw4_shell`: two lugs at x = ±33 mm (`horn_x`), 10 mm wide
(`horn_w`), top face at y = 84.5 mm (`horn_top`), full shell height, R10 / R30 blends into the shell
(`r_horn_in`, `r_horn_out`) and 0.4 mm chamfers that continue the shell's rim chamfer. Each horn has
two radial Ø4.4 × 6 mm holes for M3 heat-set inserts at z = 2.1 and 12.8 (`ins_*`), 0.4 mm lead-in.
Printed flat, the insert-hole roofs are 3.1 mm bridges.

Changes to the shell itself go to `../diy/ofw_diy_shell.sc`; the Triton shell follows on the next
open (linked design). Keep `R_sh` = `D_out/2` of the DIY shell.

## Rotator drive (OpenRoto)

Rebuilt from the legacy `ofw4_rotator_nema17.stp` (housing) and `ofw4_rotator_plate.stp` (plate),
the parts of the working prototype:

- **Housing**: motor-box outline, 23 mm tall (`h_hous`), spans the OAG up to OpenRoto's belt.
  Four counterbored M3 holes on the shell's motor-box screw positions (screws through the front
  plate into the shell tubes), four Ø2.8 × 15 pilot holes for the plate screws, a pulley pocket
  (R7 stadium, 5 mm travel) and a V belt channel 6.1 mm deep that opens towards the optical axis.
- **Plate**: 57 × 42 × 5 mm, NEMA17 boss hole Ø22 and 31 mm screw square (Ø6.5 counterbores from
  below with a 45° roof), four M3 slots with 4.9 mm travel (`slot_len`), and a 2.6 mm V recess
  underneath that continues the belt channel (`rec_pl`).
- **Belt**: GT2 closed **340 mm**, 120T sleeve / 12T pulley → centre distance 97.90 mm. The slots give
  93.8–98.7 mm (motor axis y = −55.8 … −60.7), so a 340 mm belt tensions near the far end of the slots.

Assembly: screw the housing on with four M3 through the front plate, put the pulley on the motor,
screw the motor to the plate from below (heads in the counterbores), lay the belt over the sleeve and
the pulley, then pull the plate away from the axis and tighten the four M3 plate screws.

### Fasteners (bought, linked designs in this folder, placed in `ofw_triton_assembly.sc`)

| Qty | Design | Part | Where | Fit |
|---|---|---|---|---|
| 4 | `ofw_triton_screw_din912_m3x30` | DIN 912 / ISO 4762 M3×30, stainless A2 | housing counterbores (floor 3 mm below the top) → front plate → M3×15 standoffs | **replaces** the DIY front-plate screws M3×8 at those four holes; 7 mm in the standoff, tip 1.75 mm short of the camera-side ISO 7380 M3×16 in the same standoff |
| 4 | `ofw_triton_screw_iso7380_m3x16` | ISO 7380-1 M3×16 button head, stainless A2 | plate slots → Ø2.8 pilots in the housing (self-tapping) | 10.5 mm in the pilot (15 deep); head Ø5.7 clears the motor flange by 0.5 mm |
| 4 | `ofw_triton_washer_din433_m3` | DIN 433 M3 washer 3.2/6 × 0.5, A2 | under the slot screws | Ø6, not DIN 125 (Ø7): Ø7 would touch the motor flange next to the slots |
| 4 | `ofw_triton_screw_iso7380_m3x5` | ISO 7380-1 M3×5 button head, stainless A2 | motor screws from below, heads in the plate counterbores (seat on the 45° cone, 2.6 mm above the underside) | 2.6 mm into the motor; the 17HS08 drawing gives 2.5 mm min thread depth. If it bottoms, add a 0.3–0.5 mm shim washer under the head |

Dimensions: ISO 4762 / ISO 7380-1 / DIN 433 nominal values (head Ø, height, socket), threads cosmetic.
The housing and plate use OpenWheel's own "PETG red" look (`#c8201e`, as the motor box and lid); screws and
washers are stainless A2 (satin stainless look).

## Deviations from the legacy parts

- Housing bottom sits on the DIY front plate (z = 17.9, `z_fp`); the legacy housing started at 17.5.
  The belt height above the front plate is unchanged.
- Plate: the R10 blends where the underside recess meets the Ø22 hole are left out (+21 mm³), and
  the R1 rounds at the recess/counterbore cusps are straight 0.4 mm cuts (the kernel refused the
  tangent R1 cut). Volume 7564 mm³ vs 7547 mm³ legacy.
- Plate recess **2.6 mm instead of the legacy 2.0** (2026-11-09): with the OAG between, the 340 mm belt
  runs 0.25 mm higher than the 2.0 mm recess roof (Triton assembly). The cusp cuts stay at counterbore
  depth (`min(rec_pl, mcb_d)`): at recess depth they left 0.01 mm slivers. Volume now 7363 mm³.
- Housing: 65 132 mm³ vs 65 171 mm³ legacy (chamfers).

## Checks (2026-11-08)

| Design | Volume mm³ | Feature warnings | Mesh (`meshcheck`) | Print (`printcheck`) | Opens from saved model |
|---|---|---|---|---|---|
| `ofw_triton_shell` | 41 610.0 | none | 0 open edges | `--up=+z`: no supports, 4 insert-hole bridges 3.1 mm | yes |
| `ofw_triton_rot_housing` | 65 132.3 | none | 0 open edges | `--up=+z`: no supports, no bridges | yes |
| `ofw_triton_motor_plate` (2026-11-09, recess 2.6) | 7 363.0 | none | 0 open edges | `--up=-z`: no supports, no thin walls | yes |
