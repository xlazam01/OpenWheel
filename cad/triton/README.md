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
  below with a 45° roof), four M3 slots with 4.9 mm travel (`slot_len`), and a 2.0 mm V recess
  underneath that continues the belt channel.
- **Belt**: GT2 closed **340 mm**, 120T sleeve / 12T pulley → centre distance 97.90 mm. The slots give
  93.8–98.7 mm (motor axis y = −55.8 … −60.7), so a 340 mm belt tensions near the far end of the slots.

Assembly: screw the housing on with four M3 through the front plate, put the pulley on the motor,
screw the motor to the plate from below (heads in the counterbores), lay the belt over the sleeve and
the pulley, then pull the plate away from the axis and tighten the four M3 plate screws.

## Deviations from the legacy parts

- Housing bottom sits on the DIY front plate (z = 17.9, `z_fp`); the legacy housing started at 17.5.
  The belt height above the front plate is unchanged.
- Plate: the R10 blends where the underside recess meets the Ø22 hole are left out (+21 mm³), and
  the R1 rounds at the recess/counterbore cusps are straight 0.4 mm cuts (the kernel refused the
  tangent R1 cut). Volume 7564 mm³ vs 7547 mm³ legacy.
- Housing: 65 132 mm³ vs 65 171 mm³ legacy (chamfers).

## Checks (2026-11-08)

| Design | Volume mm³ | Feature warnings | Mesh (`meshcheck`) | Print (`printcheck`) | Opens from saved model |
|---|---|---|---|---|---|
| `ofw_triton_shell` | 41 610.0 | none | 0 open edges | `--up=+z`: no supports, 4 insert-hole bridges 3.1 mm | yes |
| `ofw_triton_rot_housing` | 65 132.3 | none | 0 open edges | `--up=+z`: no supports, no bridges | yes |
| `ofw_triton_motor_plate` | 7 564.0 | none | 0 open edges | `--up=-z`: no supports, no thin walls | yes |
