# Entering the DFU bootloader from firmware

`:31#` starts the STM32G0's built-in USB DFU bootloader (ROM, `0x1FFF0000`) without touching the
BOOT0 button. Since November 2026 the firmware does this with a **flag in RAM and a reset**, not by
jumping into the ROM from the running firmware.

Code: [`fw/src/bootloader.c`](../fw/src/bootloader.c), RAM reservation in
[`fw/platformio.ini`](../fw/platformio.ini).

## How it works

1. `:31#` replies `OK#`, waits 100 ms for the reply to leave, then `enterBootloader()` writes
   `0xB00710AD` and its complement `0x4FF8EF52` to the flag (`0x20023FF8`, `0x20023FFC`) and calls
   `NVIC_SystemReset()`.
2. The reset puts every peripheral (clocks, USB, timers, GPIO) back into its reset state. USB drops
   off the bus.
3. Next boot: `Reset_Handler` sets the stack, calls `SystemInit()` (G0: nothing to do), copies
   `.data`, zeroes `.bss`, then calls `__libc_init_array()`. Its first entry is the
   `.preinit_array` hook `checkBootloaderRequest()`, which runs **before any C++ constructor**,
   including STM32duino's `premain()` (constructor priority 101) that calls `HAL_Init()` and sets up
   the clocks.
4. If both flag words match, the hook clears them, maps system memory at `0x00000000`
   (`SYSCFG_CFGR1.MEM_MODE = 01`, the same as BOOT0), sets `VTOR` to `0x1FFF0000`, loads `MSP` from
   `0x1FFF0000` and jumps to the reset vector at `0x1FFF0004`. Nothing needs de-initialising: no
   clock, PLL, interrupt or USB has been started yet.
5. The ROM bootloader enumerates as `0483:df11`. When dfu-util leaves DFU (`:leave`), the chip resets;
   the flag is already cleared, so the firmware starts normally.

## Where the flag lives and why it survives the reset

- **SRAM keeps its content over a system reset** (`NVIC_SystemReset`, NRST pin, watchdog). Only a
  power-off loses it.
- The flag is the **top 8 bytes of the 144 KB SRAM**. `board_upload.maximum_ram_size = 147448`
  (144 KB − 8) in `platformio.ini` shrinks the linker's RAM region (STM32duino passes it as
  `LD_MAX_DATA_SIZE`), so `_estack = ORIGIN(RAM) + LENGTH(RAM) = 0x20023FF8`. The stack starts below
  the flag and grows down; `.data`, `.bss` and the heap (`_sbrk` stops at `_estack - _Min_Stack_Size`)
  are lower still. **Nothing the C runtime or the program does can write those 8 bytes.**
- No custom linker script and no `.noinit` section are needed: STM32duino's G0B1 linker script has
  no `.noinit`, and an orphan section would be placed by `ld` next to `.bss`, where the heap starts.
- `enterBootloader()` and the boot check both test `&_estack <= 0x20023FF8`. If the reservation is
  missing from `platformio.ini`, `:31#` stays in the firmware (debug log says so) instead of
  writing into the stack.
- After power-on the SRAM holds random data. Two words (magic + complement) make a false match
  practically impossible (2⁻⁶⁴); a normal reset never sets the flag, and the hook clears it before
  jumping.
- The G0B1 has 144 KB SRAM only while the SRAM parity check is **off** (option bit
  `RAM_PARITY_CHECK = 1`, the factory default). With parity on, the top 16 KB do not exist and the
  flag address must move below `0x20020000`.

Cost: −180 bytes of flash against the old direct jump (no `HAL_DeInit`/`HAL_RCC_DeInit`), 8 bytes of
RAM.

## Why not the old direct jump

The old code jumped from the running firmware: disable IRQs and SysTick, force-reset USB,
`HAL_DeInit()`/`HAL_RCC_DeInit()`, clear the NVIC, remap, jump. It worked, but it depends on getting
every running peripheral back to its reset state by hand (clocks, PLL, timers with DMA, pending
interrupts, the USB pull-up); anything missed is inherited by the ROM bootloader. A hardware reset
does all of that, and the jump then happens from a clean state.

The old path is still in `bootloader.c` for rollback: add `-D OPENWHEEL_DFU_DIRECT_JUMP` to
`build_flags` (commented-out line in `platformio.ini`). Remove it once the flag path has been in use
for a while.

## Hardware test (2026-11-06, rev 1 board, FW1.3.0)

- `:31#` → `OK#`, board re-enumerates as `0483:df11` (`dfu-util -l` lists it) after ~1.9 s.
- Flashed through `pio run -t upload` and through plain `dfu-util -a 0 -s 0x08000000:leave -D` —
  both fine (the `get_status` error after "Download done" and the leave request is normal on this
  chip), firmware boots and answers `:32#`.
- Two `:31#` → DFU → flash cycles in a row.
- `:22#` (software reset, `NVIC_SystemReset`) and a power cycle start the firmware, not DFU.

## Porting to another STM32G0 board (AstroUniBoard V2)

1. Copy the flag part of `bootloader.c` (`DFU_FLAG_ADDR`, `flagReserved()`,
   `checkBootloaderRequest()` with its `.preinit_array` entry, `enterBootloader()`).
2. Reserve the top 8 bytes of SRAM: `board_upload.maximum_ram_size = <RAM size - 8>` in
   `platformio.ini`, and set `DFU_FLAG_ADDR` to `0x20000000 + <RAM size> - 8`. Check after the build:
   `arm-none-eabi-nm firmware.elf | grep _estack` must print the flag address.
3. Check that the `.preinit_array` entry made it into the ELF
   (`arm-none-eabi-objdump -s -j .preinit_array firmware.elf` points at `checkBootloaderRequest`).
4. Call `enterBootloader()` after the reply to the DFU command has been flushed.
5. Test as above, with BOOT0 ready as the way back.
