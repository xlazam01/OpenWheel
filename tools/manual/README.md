# Build-guide pages

The illustrated build guide (`docs/build/`, `docs/BUILD.md`) is drawn from the CAD assembly, so
it can be regenerated when the parts change.

1. **Views → DXF** (SolidCraft, `cad/diy/ofw_diy_assembly.sc`): for each page, show an exploded
   view, isolate the parts of that step and run `file.export_view_dxf` (hidden lines removed,
   orthographic, yaw −45°, pitch ±24°). `views.py` prints those batches for the assembly pages;
   the exploded views it names are created with `explode.create` first:

   | View | Moves (occurrence → mm in Z) |
   |---|---|
   | `p04_shaft` | shaft 14 → +40 |
   | `p05_cam_bearing` | shim 23 → +12, bearing 17 → +26 |
   | `p06_front_bearing` | shim 26 → −12, bearing 20 → −26 |
   | `p07_standoffs` | 12 standoffs (56…89) → +35, 8 camera screws (92…113) → −30 |
   | `p08_shell` | shell 8 → +45 |
   | `p09_wheel` | shim 29 → +25, wheel 11 + shaft 14 → +50, shim 32 → +85 |
   | `step23_front_plate` | front plate 5, bearing 20, shim 26 → +55; shim 32 → +22; 12 front screws (116…149) → +110 |
   | `p13_motor_box` | motor 41 → +30, ear screws 152, 155 → −28 |
   | `p14_box_on` | box 38, motor 41, ear screws → −40; box screws 158…167 → −80 |
   | `p17_pcb` | PCB 35 → −30, PCB screws 170…176 → −55 |

   Page 11 (filters) uses `cad/diy/ofw_diy_wheel_asm.sc`: filters → +22, O-rings → +40, screws → +55.
   The `part_*` drawings are single occurrences isolated with no explode.
   Output goes to `cad/_ref/manual/` (ignored, about 22 MB).
2. **DXF → pages:** `python tools/manual/page.py tools/manual/manual.json OUTDIR --pdf OUTDIR/OpenWheel-build-guide.pdf --png 110`
   (Inkscape for PDF/PNG, `pypdf` to merge). `manual.json` holds every page: step, parts box
   (BOM ids, quantities, `true_scale` for screws drawn 1:1), drawing, note and checks. Copy it next
   to the DXFs, or give the DXF paths relative to it.
3. Copy the PNGs and the PDF to `docs/build/`.

`dxf2svg.py` turns a single view DXF into an SVG for a quick look.
