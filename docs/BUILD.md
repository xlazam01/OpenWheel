# Build guide

How to build an OpenWheel (DIY release 1, 4 × 2" filters) from the parts in the
[bill of materials](BOM.md). The 20 step pages below are drawn from the CAD model
([how they are made](../tools/manual/README.md)); the same pages as one printable PDF:
**[OpenWheel-build-guide.pdf](build/OpenWheel-build-guide.pdf)**.

Part ids on the pages (P1, M2, B7, …) are the lines of the BOM. Background for every dimension and
design choice: [DIY notes](DIY_NOTES.md).

> **Not yet verified on a printed prototype:** print settings, the lid snap fit and button pads,
> the Hall sensor height against the magnets, and the friction-roller pressure. Report what you
> find in the [issues](https://github.com/xlazam01/OpenWheel/issues).

## Before you start

1. **Print** (no supports needed for any part):

   | Part | File | Orientation | Material |
   |---|---|---|---|
   | P1 shell | `cad/diy/ofw_diy_shell.3mf` | flat, on a plate face | opaque black (light-tight) |
   | P2 wheel | `cad/diy/ofw_diy_wheel.3mf` | flat, disc side down | opaque black |
   | P3 motor box | `cad/diy/ofw_diy_motorbox.3mf` | on its plate-side face; ear-bay ceilings are bridges | any |
   | P4 lid | `cad/diy/ofw_diy_motorbox_lid.3mf` | outer face down | same as the box |

   Layer height, walls and infill are not recorded yet. The prototype shell is Prusament Galaxy Black.
2. **Order the plates** (M1, M2) from the STEP files as CNC aluminium, 3 mm, matt black anodised,
   with the threads marked (front plate: M48 × 0.75 aperture and 6 × M2.5; camera plate: 6 × M2.5)
   and "deburr all edges".
3. **Order the PCB** assembled (JLCPCB, files in `hw/`, LCSC numbers in the BOM). J3 and J4 stay
   empty. The STM32G0B1 may be out of stock at JLCPCB: buy it elsewhere and consign it.
4. **Buy** the bought parts (B1–B17).
5. **Check the prints:** the plates drop into the shell recesses with no gap (else reprint the shell
   scaled 100.1–100.4 % in X/Y), the standoffs slide into the channels and tubes, the shaft enters
   the wheel bore, the lid clips onto the box.

**Tools:** hex keys 1.5 and 2 mm, micrometer or caliper, fine sandpaper (P400–P800) on a flat
plate, vice or arbor press, soldering iron, side cutters, USB-C cable, a PC with the OpenWheel
ASCOM driver (for the magnet check in step 3).

## Steps

| | |
|---|---|
| ![Step 1](build/page01.png) | ![Step 2](build/page02.png) |
| ![Step 3](build/page03.png) | ![Step 4](build/page04.png) |
| ![Step 5](build/page05.png) | ![Step 6](build/page06.png) |
| ![Step 7](build/page07.png) | ![Step 8](build/page08.png) |
| ![Step 9](build/page09.png) | ![Step 10](build/page10.png) |
| ![Step 11](build/page11.png) | ![Step 12](build/page12.png) |
| ![Step 13](build/page13.png) | ![Step 14](build/page14.png) |
| ![Step 15](build/page15.png) | ![Step 16](build/page16.png) |
| ![Step 17](build/page17.png) | ![Step 18](build/page18.png) |
| ![Step 19](build/page19.png) | ![Step 20](build/page20.png) |

In short:

1. File the 12 standoffs to one length.
2. Solder the Hall sensor to the PCB back (branded face toward the wheel centre, 17 mm from the board), flash the firmware.
3. Find each magnet's outward side (ASCOM setup dialog → Magnet polarity) and glue the 10 magnets in groups 4-3-2-1.
4. Press the shaft into the wheel.
5. – 6. Shim and bearing into each plate, flange on the inner side.
7. 12 standoffs on the camera plate (8 screws).
8. Shell over the standoffs.
9. – 10. Dry fit with the front plate; sand the hub until the wheel turns freely without play. Take it apart again.
11. Mount the filters (O-ring under every screw head).
12. Silicone tube on the motor shaft (friction roller).
13. – 14. Motor into the box, box onto the camera plate.
15. Wheel back in.
16. Press the motor against the wheel rim and tighten the two motor screws.
17. Front plate on.
18. Motor wires and PCB.
19. Lid.
20. Power up and calibrate.

Then continue with the [user manual](MANUAL.md): driver, filter names, focus offsets, buttons and LEDs.
