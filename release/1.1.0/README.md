# OpenWheel 1.1.0

Hardware update of the OpenWheel DIY filter wheel (4 × 2" filters): board V3.2 with a cheaper,
shared 3.3 V regulator. Firmware and mechanics are the same as in [1.0.0](../1.0.0/README.md).
Build it with the [build guide](../../docs/BUILD.md) and the [bill of materials](../../docs/BOM.md).

| Part | Version |
|---|---|
| Firmware | FW1.4.0, protocol 3 (identify first): unchanged, byte-identical to 1.0.0 |
| PCB | **V3.2** (11/2026), `hw/filter_wheel.kicad_pcb` |
| DIY mechanics | release 1 (`cad/diy/`, SolidCraft): unchanged |
| Compatible ASCOM driver | 0.4.0 (`driver/ASCOM/`) |

## Changes since 1.0.0

- **PCB V3.2** ([#30](https://github.com/xlazam01/OpenWheel/pull/30), [#31](https://github.com/xlazam01/OpenWheel/pull/31)):
  - U2 RT9193-33 (SOT-23-5, JLC Extended) → **ME6231A33M3G** (SOT-23, C2765181, 500 mA, 1.8 µA Iq), the same
    regulator as AstroUniBoard, so a joint order pays one setup fee less. C5 (bypass-pin cap) and the EN wiring
    are gone; the copper around U2 was re-routed.
  - C14, C15, C20, C22: 100 nF/50 V → 100 nF/16 V (C1525, Basic): at most 5 V across them.
  - Board outline gaps closed ([#26](https://github.com/xlazam01/OpenWheel/pull/26)), footprint copies and
    capacitor values unified ([#24](https://github.com/xlazam01/OpenWheel/pull/24)).
  - The KiCad project is self-contained: project symbol and footprint libraries, 3D models in `3dmodels/`
    ([#23](https://github.com/xlazam01/OpenWheel/pull/23)). New in the package: `pcb/kicad/`.
- **Shell STEP** re-exported: the 1.0.0 file has six zero-area faces that some importers drop (the solid then
  comes in open). Same geometry: volume 31 709.15 mm³, same bounding box.

> **Not yet verified on hardware:** no V3.2 board has been assembled yet; the new regulator
> passes ERC and DRC but has not run on a real board. The printed prototype checks (print settings, fits, friction
> drive) are still open in [#20](https://github.com/xlazam01/OpenWheel/issues/20).

## Contents

| Folder | Files |
|---|---|
| `firmware/` | `OpenWheel-FW1.4.0.bin`: flash at `0x08000000` over USB DFU (hold **−** while plugging in, or send `:31#`), e.g. `dfu-util -a 0 -s 0x08000000:leave -D OpenWheel-FW1.4.0.bin` or STM32CubeProgrammer |
| `pcb/` | `OpenWheel-gerbers.zip` (Gerbers, Excellon drill, drill maps), `OpenWheel-bom-jlc.csv` and `OpenWheel-cpl-jlc.csv` for JLCPCB assembly, `OpenWheel-schematic.pdf` |
| `pcb/kicad/hw/` | the KiCad 10 project, self-contained: opens on a clean KiCad without missing symbols, footprints or 3D models. Symbols in `openwheel.kicad_sym`, footprints in `pcb/kicad/hw/OpenWheel.pretty/`, 3D models in `pcb/kicad/hw/3dmodels/` |
| `cad/printed/` | shell, wheel, motor box and lid: 3MF for printing, STEP for editing |
| `cad/cnc/` | front and camera plate STEP for CNC (aluminium, 3 mm, matt black anodised) |
| `cad/assembly/` | complete assembly and the wheel assembly with filters, as STEP |
| `bom/` | electronics (LCSC) and mechanical BOM as CSV |
| `SHA256SUMS` | checksums of all files above (`sha256sum -c SHA256SUMS`) |

## Ordering the PCB (JLCPCB)

- Upload `OpenWheel-gerbers.zip`: 2 layers, 1.6 mm.
- Assembly (top side only) from `OpenWheel-bom-jlc.csv` / `OpenWheel-cpl-jlc.csv` (55 parts,
  all with LCSC numbers). J3 and J4 stay empty (DNP), mounting holes are not in the BOM.
- The placement file has KiCad's raw rotations: **check every part's orientation in JLCPCB's
  placement preview** (ICs, the USB-C receptacle, LEDs, diodes; U2 is a new SOT-23) before ordering.
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

- PCB: `kicad-cli` (KiCad 10) with the board's plot settings (`tools/kicad/fabexport.py`); DRC 0 on the
  released board. The BOM matches `docs/bom/electronics.csv` part for part. `pcb/kicad/` packed with
  `tools/kicad/libpack.py` and verified on a clean KiCad configuration.
- Firmware: `pio run -d fw -e STM32G0B1` from the release commit (81 488 bytes), byte-identical to the
  1.0.0 binary that was flashed and tested.
- CAD: the shell re-exported from `cad/diy/ofw_diy_shell.sc` (SolidCraft, zero feature errors or warnings,
  closed mesh, print check as above). The other parts' sources are unchanged since 1.0.0; their files are
  the 1.0.0 exports (SolidCraft 0.29.0).
