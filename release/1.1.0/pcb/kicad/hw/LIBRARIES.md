# Project libraries

The KiCad project is self-contained: it opens without any global library or 3D-model path.

| Library | Content | Origin |
|---|---|---|
| `openwheel.kicad_sym` (nickname `OpenWheel`) | every symbol used in the schematic | KiCad standard symbol library (CC-BY-SA 4.0 with the KiCad library exception) and own symbols |
| `OpenWheel.pretty` (nickname `OpenWheel`) | every footprint used on the board; `_v2`… entries are older versions of a standard footprint that some parts were placed with | KiCad standard footprint library (same licence) and the WorksOnMySky logo (own) |
| `3dmodels/` | STEP models of the footprints | KiCad standard 3D model library (same licence) |

The KiCad libraries are © the KiCad library contributors, <https://www.kicad.org/libraries/license/>.
Their licence exception lets designs that use them be released under any licence (this board: CERN-OHL-S-2.0).

When you add a part, put its symbol, footprint and 3D model into these libraries as well, and reference models
as `${KIPRJMOD}/3dmodels/<file>.step`.
