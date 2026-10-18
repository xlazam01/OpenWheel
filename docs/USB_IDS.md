# USB IDs (pid.codes)

All MartinsOpenAstroTools devices use VID **0x1209** (pid.codes, shared vendor ID for open-source
hardware). The PID identifies the product; the ASCOM driver finds the wheel by VID/PID and then
confirms it with the `:32#` identify command.

## Allocation (picked 2026-10-18 from the live pid.codes list: free at that date)

| PID    | Product                | Status |
|--------|------------------------|--------|
| 0x2180 | OpenWheel Filter Wheel | **to submit first** (draft: `docs/pidcodes/submit/`) |
| 0x2181 | UpBox                  | reserved, submit when its repo is public (`docs/pidcodes/later/`) |
| 0x2182 | AstroUniBoard          | reserved, submit when its repo is public (`docs/pidcodes/later/`) |
| 0x2183 | spare                  | not to be submitted yet |
| 0x2184 | spare                  | not to be submitted yet |

0x2171-0x21F6 was a free run, so the block is contiguous. **Nothing is reserved until the pull
request is merged**: pid.codes assigns PIDs in the order PRs arrive. Re-check the numbers against
<https://pid.codes/1209/> right before submitting; if one is taken, take the next free number and
update this table, `docs/pidcodes/` and `fw/platformio.ini`.

Until 0x2180 is merged, firmware builds keep the shared **test PID 0x0001** (not unique: do not
ship it, and do not run two different 0x0001 devices on one PC with the ASCOM driver).

## pid.codes requirements (checked against https://pid.codes/howto/ and /faq/)

- [x] Open-source licence for software: GPL-3.0 (`LICENSE`)
- [x] Open-hardware licence for hardware files: CERN-OHL-S-2.0 (`LICENSE-HARDWARE`)
- [ ] **Public** source repository <https://github.com/xlazam01/OpenWheel> (created, but still
      EMPTY as of 2026-10-18: push this project to it before opening the pid.codes PR)
- [x] Hardware design files are in the project (`hw/` KiCad, `cad/` Solid Edge + STEP), covered by CERN-OHL-S-2.0
- [x] Org page: `org/MartinsOpenAstroTools/index.md` (no spaces in the directory name)
- [x] One PID page per product at `1209/<PID>/index.md` with `layout, title, owner, license, site,
      source`; `title` must not contain the org name; `owner` must equal the org directory name
- [ ] Do **not** submit several unreleased products at once: the maintainers may ask for
      justification. Submit the filter wheel (it has working source), the others later. Spares are
      only notes here and are never submitted.
- Not allowed to request PIDs in the reserved 0x0000-0x1FFF range (ours are above it).

## How to submit

1. Push this project to the (public) GitHub repo. The `docs/pidcodes/submit/` pages already point to it.
2. Fork <https://github.com/pidcodes/pidcodes.github.com>.
3. Copy `docs/pidcodes/submit/org/` and `docs/pidcodes/submit/1209/` into the fork root.
4. Commit ("Add MartinsOpenAstroTools and OpenWheel Filter Wheel") and open a pull request.
5. After merge: set `USBD_PID=0x2180` in `fw/platformio.ini`, rebuild, reflash, and tell the ASCOM
   driver to match VID 0x1209 / PID 0x2180.
