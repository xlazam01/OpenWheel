# Bill of materials

Everything needed to build one OpenWheel (DIY release 1, 4 × 2" filters). Quantities are counted from
the CAD model (`cad/diy/ofw_diy_assembly`). Sources are examples, not requirements:

- **Mechanical parts:** AliExpress. Where a link is given, it is the listing the prototype parts were
  bought from (pick the variant with the dimensions in the table); listings change, so every line also
  has a search term.
- **Electronics:** LCSC part numbers, so the board can be assembled by JLCPCB. Same table as CSV:
  [bom/electronics.csv](bom/electronics.csv) (JLCPCB format), mechanical: [bom/mechanical.csv](bom/mechanical.csv).

Lines marked **?** are not decided yet or not tested on the prototype.

## 1. Printed parts

| # | Part | Qty | File | Notes |
|---|---|---|---|---|
| P1 | Shell | 1 | `cad/diy/ofw_diy_shell.3mf` | Opaque black (prototype: Prusament Galaxy Black). Scale X/Y to fit the plates, see the build guide |
| P2 | Wheel | 1 | `cad/diy/ofw_diy_wheel.3mf` | Opaque black |
| P3 | Motor box | 1 | `cad/diy/ofw_diy_motorbox.3mf` | Any colour (prototype: red), on its plate-side face, no supports |
| P4 | Motor box lid | 1 | `cad/diy/ofw_diy_motorbox_lid.3mf` | Same colour as the box, outer face down, no supports |

Print settings (layer height, walls, infill, material): **?** recorded after the test print (#5).

## 2. CNC plates

| # | Part | Qty | File | Notes |
|---|---|---|---|---|
| M1 | Front plate | 1 | `cad/diy/ofw_diy_plate_front.step` | JLCPCB CNC, aluminium 6061, 3 mm, matt black anodised. Threads: 1 × M48 × 0.75 (aperture), 6 × M2.5. Ask for "deburr all edges" |
| M2 | Camera plate | 1 | `cad/diy/ofw_diy_plate_camera.step` | Same. Threads: 6 × M2.5 |

## 3. Bought mechanical parts

| # | Part | Qty | Specification | AliExpress | Notes |
|---|---|---|---|---|---|
| B1 | Standoff M3 × 15, round | 12 | Ø5, female–female, threaded ≥ 6.5 mm deep each end | [prototype listing](https://www.aliexpress.com/item/1005004063804402.html), `M3 round aluminium standoff female 15mm` | Aluminium, black. File all 12 to the same length |
| B2 | Screw M3 × 8, flat low head | 20 | Black, 12.9, hex 2, head Ø5.8 × 1.0 | `M3 ultra low head screw 12.9 black` | Plates to standoffs: 12 front, 8 camera |
| B3 | Screw M3 × 16, button head ISO 7380 | 4 | Hex 2 | `ISO7380 M3x16 button head` | Motor box to standoffs. Not shorter than 16 |
| B4 | Screw M3 × 8, button head ISO 7380 | 2 | Hex 2 | `ISO7380 M3x8 button head` | Motor ears. Not longer than 10 mm |
| B5 | Self-tapping screw M2 × 6, socket cap | 15 | For plastic, head Ø3.6 × 1.8, hex 1.5 | `M2x6 self tapping socket cap screw plastic` | 12 filter clamps + 3 PCB |
| B6 | O-ring 2 × 1 mm | 12 | NBR, ID 2, cross-section 1 | `O ring 2x1mm NBR` | One under each clamp screw head |
| B7 | Ball bearing MF105ZZ | 2 | 5 × 10 × 4, flange Ø11.2 × 0.8, shielded | `MF105ZZ flange bearing` | Flange on the chamber side |
| B8 | Shim washer 10 × 14 × 0.3 | 2 | DIN 988, stainless | [prototype listing](https://www.aliexpress.com/item/1005002301164770.html), `DIN988 shim 10x14x0.3` | Under each bearing flange |
| B9 | Shim washer 5 × 7 × 0.5 | 2 | DIN 988, stainless | [prototype listing](https://www.aliexpress.com/item/1005002301164770.html), `DIN988 shim 5x7x0.5` | On the shaft, between bearing and wheel |
| B10 | Shaft Ø5 × 20 mm | 1 | Stainless dowel pin, 304 | [prototype listing](https://www.aliexpress.com/item/4000473863693.html), `stainless dowel pin 5x20` | Press fit in the wheel |
| B11 | Magnet Ø6 × 3 mm | 10 | NdFeB N35 or stronger, axially magnetised | `neodymium magnet 6x3mm` | Buy a few spare |
| B12 | Stepper motor NEMA11 pancake | 1 | 28 × 28 × 9.5 mm, round, 1.8°, 0.5 A, 1 Ncm, bipolar 4 wires, **Ø3 mm shaft** | [STEPPERONLINE](https://www.aliexpress.com/item/4000182527052.html), `nema 11 pancake stepper 9.5mm` | Ears with M3 threads 34 mm apart |
| B13 | Silicone tube (friction drive) | 1 piece | ID 2 × OD 4 mm, stretched over the Ø3 shaft, cut flush with the shaft end | `silicone tube 2x4mm` | Pushed onto the motor shaft, runs on the wheel rim. No pulley, no belt |
| B14 | Hall sensor A3144 | 1 | Unipolar, TO-92UA | `A3144 hall sensor` | Soldered to the PCB, see the build guide |
| B15 | 2" filters, mounted | 4 | Ø50.8 × 5 mm cell, M48 × 0.75 thread ≤ 2 mm | — | Low-profile cells. Unmounted glass: **?** (#1) |
| B16 | Screws M2.5 for the camera | 6 + 6 | Length per camera / adapter | `M2.5 socket cap screw` | Camera side and telescope side |
| B17 | Thread-locking / retaining compound, super glue | — | e.g. Loctite 641 for loose bearings, CA glue for magnets | — | |

## 4. Electronics

The board (`hw/`) is assembled by JLCPCB from the files in `hw/`. J3 and J4 are not fitted: the motor
wires and the Hall sensor are soldered to their pads. "Basic" parts cost no feeder fee at JLCPCB.

| Ref | Value | Package | LCSC | Type |
|---|---|---|---|---|
| C1 | 4.7 nF | 0402 | C1538 | basic |
| C2, C3, C10 | 1 µF | 0402 | C52923 | basic |
| C4, C7, C8, C9, C13, C14, C15, C20, C22, C23, C25 | 100 nF | 0402 | C1525 | basic |
| C19 | 22 nF 50 V | 0402 | C1532 | basic |
| C6, C16, C17, C21 | 10 µF 10 V | 0402 | C315248 | extended (no 10 V basic part) |
| C11, C12 | 12 pF C0G | 0402 | C1547 | basic |
| C18 | 47 µF 10 V | 1206 | C96123 | basic |
| C24 | 1 nF | 0402 | C1523 | basic |
| D1, D2 | LED red | 0603 | C2286 | basic |
| F1 | Polyfuse 0.5 A, SMD0603P050TF | 0603 | C20789 | extended |
| J1 | USB-C receptacle GT-USB-7010ASV | SMD | C2988369 | extended |
| J3, J4 | JST PH 4-pin / 3-pin | — | — | **not fitted** |
| R1, R2, R10, R11, R12, R13 | 5.1 kΩ | 0402 | C25905 | basic |
| R3 | 1 MΩ | 0402 | C26083 | basic |
| R4, R7, R9, R14, R18, R20, R21 | 10 kΩ | 0402 | C25744 | basic |
| R8 | 510 Ω | 0402 | C25123 | basic |
| R15 | 1.5 kΩ | 0402 | C25867 | basic |
| R16, R17 | 0.11 Ω (motor current sense) | 0805 | C5126077 | extended |
| R19 | 100 Ω | 0402 | C25076 | basic |
| SW2, SW3 | Tactile switch TS-1088-AR02016 | SMD 4 × 3 | C720477 | basic |
| U1 | USBLC6-2SC6 (USB ESD) | SOT-23-6 | C2687116 | extended |
| U2 | ME6231A33M3G (3.3 V LDO, 500 mA; same part as AstroUniBoard U1) | SOT-23 | C2765181 | extended |
| U3 | STM32G0B1CBT6 | LQFP-48 | C2847904 | extended; was out of stock at JLCPCB on 2026-11-04. The chip is common elsewhere (LCSC, Mouser, AliExpress, e.g. the WeAct Studio store, which also sells an STM32G0B1 core board): buy it there and consign it to JLCPCB, or solder it yourself |
| U4 | 24AA64T-I/OT (EEPROM) | SOT-23-5 | C2061092 | extended |
| U5 | TMC2209-LA-T (stepper driver) | QFN-28 | C2150710 | extended |
| Y1 | 8 MHz crystal TAXM8M4RDBCCT2T | 3225 | C400090 | extended |

PCB: 2 layers, 1.6 mm. Board outline and fabrication settings: `hw/filter_wheel.kicad_pcb`.

## 5. Tools

- Hex keys 1.5, 2, 2.5 mm; small Phillips/flat screwdriver
- Micrometer or caliper (standoffs, hub height), flat file or sandpaper on a flat plate (P400–P800)
- Vice or arbor press (shaft into the wheel)
- Soldering iron (Hall sensor, motor wires), side cutters
- Multimeter (magnet polarity, see the build guide)
- USB-C cable, PC with the OpenWheel ASCOM driver
