# OpenWheel

Filter wheel: STM32G0B1 firmware (`fw/`), KiCad hardware (`hw/`), ASCOM driver (`driver/ASCOM/`).
See README.md for the layout and `driver/ASCOM/SPEC.md` for the driver/protocol design.

## Conventions
- PlatformIO project is `fw/` (`pio run -d fw -e STM32G0B1`); `pio` is not on PATH, use
  `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe`.
- Serial protocol `:NN[params]#`, 115200; every command replies (`value#`, `OK#` or `ERR,<code>#`).
  Error codes live in `fw/src/error_codes.h`; keep `docs/PROTOCOL.md` and the spec in sync.
- Keep ISRs minimal; no dynamic allocation in ISR context. Conventional commits.
- `/_private/` is a separate git repo (local Gitea only). Never copy its content into this repo.
- Skills: `/flash-fw <COM>`, `/test-fw <COM>`.
