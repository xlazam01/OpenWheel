# OpenWheel 1.0.0

First release of the OpenWheel DIY filter wheel (4 × 2" filters): mechanics, PCB and firmware.
Build it with the [build guide](../../docs/BUILD.md) and the [bill of materials](../../docs/BOM.md).

| Part | Version |
|---|---|
| Firmware | FW1.4.0, protocol 3 (identify first) |
| PCB | V3.1 (11/2026), `hw/filter_wheel.kicad_pcb` |
| DIY mechanics | release 1 (`cad/diy/`, SolidCraft) |
| Compatible ASCOM driver | 0.4.0 (`driver/ASCOM/`) |

> **Not yet verified on a printed prototype:** print settings, fits and the friction drive.
> Open checks are tracked in [#20](https://github.com/xlazam01/OpenWheel/issues/20).

## Contents

| Folder | Files |
|---|---|
| `firmware/` | `OpenWheel-FW1.4.0.bin`: flash at `0x08000000` over USB DFU (hold **−** while plugging in, or send `:31#`), e.g. `dfu-util -a 0 -s 0x08000000:leave -D OpenWheel-FW1.4.0.bin` or STM32CubeProgrammer |
| `pcb/` | `OpenWheel-gerbers.zip` (Gerbers, Excellon drill, drill maps), `OpenWheel-bom-jlc.csv` and `OpenWheel-cpl-jlc.csv` for JLCPCB assembly, `OpenWheel-schematic.pdf` |
| `cad/printed/` | shell, wheel, motor box and lid: 3MF for printing, STEP for editing |
| `cad/cnc/` | front and camera plate STEP for CNC (aluminium, 3 mm, matt black anodised) |
| `cad/assembly/` | complete assembly and the wheel assembly with filters, as STEP |
| `bom/` | electronics (LCSC) and mechanical BOM as CSV |
| `SHA256SUMS` | checksums of all files above (`sha256sum -c SHA256SUMS`) |

## Ordering the PCB (JLCPCB)

- Upload `OpenWheel-gerbers.zip`: 2 layers, 1.6 mm.
- Assembly (top side only) from `OpenWheel-bom-jlc.csv` / `OpenWheel-cpl-jlc.csv` (56 parts,
  all with LCSC numbers). J3 and J4 stay empty (DNP), mounting holes are not in the BOM.
- The placement file has KiCad's raw rotations: **check every part's orientation in JLCPCB's
  placement preview** (ICs, the USB-C receptacle, LEDs, diodes) before ordering.
- The STM32G0B1 may be out of stock at JLCPCB: buy it elsewhere and consign it.

## Ordering the plates

Upload the two STEP files in `cad/cnc/` as CNC aluminium, 3 mm, matt black anodised. Mark the
threads (front plate: M48 × 0.75 aperture and 6 × M2.5; camera plate: 6 × M2.5) and "deburr all edges".

## Printing

No part needs supports (checked with `tools/printcheck.py` in this orientation). Layer height,
walls and infill are not settled yet ([#20](https://github.com/xlazam01/OpenWheel/issues/20)).
Use opaque black filament for the shell and the wheel: the chamber must be light-tight.

| File | Orientation | Notes from the print check |
|---|---|---|
| `ofw_diy_shell.3mf` | flat, on a plate face (either side) | one 2.6 mm wide bridge 2.9 mm above the bed |
| `ofw_diy_wheel.3mf` | disc side down, hub up | walls between neighbouring magnet pockets are 0.39 mm (6 mm magnets at 5.88° pitch, see [#10](https://github.com/xlazam01/OpenWheel/issues/10)); thinner spots 0.1–0.3 mm at the magnet pocket lead-ins |
| `ofw_diy_motorbox.3mf` | on its plate-side face | the two ear-bay ceilings are bridges 3.4 mm above the bed (use bridge settings); slivers below 0.1 mm at chamfer edges |
| `ofw_diy_motorbox_lid.3mf` | outer face down | the + and − signs are 0.4 mm recesses bridged over; one 0.17 mm thin spot near a corner |

Scale the shell 100.1–100.4 % in X/Y if the plates don't drop into the recesses.

## How this release was made

- CAD: every part re-exported from `cad/diy/*.sc` with SolidCraft 0.29.0. Zero feature errors or
  warnings; all 3MF meshes are closed (0 open/non-manifold edges, `tools/meshcheck.py`).
- PCB: `kicad-cli` (KiCad 10) with the board's plot settings; DRC run on the released board. The BOM
  matches `docs/bom/electronics.csv` part for part.
- Firmware: `pio run -d fw -e STM32G0B1` from the release commit (81 488 bytes). A copy is in
  `A:\config\firmware\OpenWheel\1.0.0\`.
