// Entering the STM32G0 system (ROM) bootloader for USB DFU.
//
// Default: magic flag + reset (see docs/BOOTLOADER.md).
//   enterBootloader() writes a magic value into 8 bytes at the very top of SRAM that the linker
//   never hands out (platformio.ini: board_upload.maximum_ram_size = 144 KB - 8, so _estack, the
//   initial stack pointer, sits just below the flag) and resets the MCU. Early in the next boot,
//   before any clock, HAL or USB set-up, checkBootloaderRequest() sees the flag, clears it and
//   jumps to the ROM bootloader while every peripheral is still in its reset state.
//
// -D OPENWHEEL_DFU_DIRECT_JUMP: the old direct jump from the running firmware (de-init HAL and
//   clocks, reset USB, jump). Kept for rollback until the flag path has proven itself.

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_MEMORY 0x1FFF0000UL  // STM32G0 system memory (ROM bootloader) base

typedef void (*pFunction)(void);

#ifndef OPENWHEEL_DFU_DIRECT_JUMP

// Top 8 bytes of the 144 KB SRAM, kept out of the linker's RAM region (see platformio.ini).
#define DFU_FLAG_ADDR   0x20023FF8UL
#define DFU_MAGIC       0xB00710ADUL

static volatile uint32_t *const dfuFlag = (volatile uint32_t *)DFU_FLAG_ADDR;
extern uint32_t _estack;  // linker script: ORIGIN(RAM) + LENGTH(RAM)

// True when the linker RAM region really ends at or below the flag, i.e. nothing (data, bss,
// heap, stack) can overwrite it. If the reservation in platformio.ini is missing, DFU via the
// flag is disabled instead of corrupting the stack.
static inline int flagReserved(void) {
    return (uint32_t)&_estack <= DFU_FLAG_ADDR;
}

// Runs from __libc_init_array via .preinit_array: after the C runtime set up .data/.bss, before
// any C++ constructor, including STM32duino's premain() (constructor 101) that calls HAL_Init
// and the clock set-up. Only SystemInit() has run before, which leaves clocks at reset state.
static void checkBootloaderRequest(void) {
    if (!flagReserved()) return;
    // Two words: after power-on the SRAM holds random data, so a 64-bit match is required.
    if (dfuFlag[0] != DFU_MAGIC || dfuFlag[1] != ~DFU_MAGIC) return;

    // Clear first: when the ROM bootloader leaves DFU, the firmware must boot normally.
    dfuFlag[0] = 0;
    dfuFlag[1] = 0;

    // Map system memory at 0x00000000 (SYSCFG_CFGR1.MEM_MODE = 01), as BOOT0 would.
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void)RCC->APBENR2;
    SYSCFG->CFGR1 = (SYSCFG->CFGR1 & ~SYSCFG_CFGR1_MEM_MODE) | (0x01UL << SYSCFG_CFGR1_MEM_MODE_Pos);
    SCB->VTOR = SYSTEM_MEMORY;
    __DSB();
    __ISB();

    uint32_t sp = *(volatile uint32_t *)SYSTEM_MEMORY;
    pFunction bootloader = (pFunction)(*(volatile uint32_t *)(SYSTEM_MEMORY + 4));
    __set_MSP(sp);
    bootloader();
    while (1) {}
}

__attribute__((used, section(".preinit_array")))
static void (*const checkBootloaderRequestEntry)(void) = checkBootloaderRequest;

void enterBootloader(void) {
    if (!flagReserved()) return;  // build misconfigured: stay in the firmware
    dfuFlag[0] = DFU_MAGIC;
    dfuFlag[1] = ~DFU_MAGIC;
    __DSB();
    NVIC_SystemReset();
}

#else  // OPENWHEEL_DFU_DIRECT_JUMP

void enterBootloader(void) {
    // 1. Disable all interrupts
    __disable_irq();

    // 2. Disable SysTick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    // 3. Force Reset USB to force re-enumeration
    // This allows the host to detect a "new" device (the bootloader)
    // STM32G0 specific macros
    #ifdef __HAL_RCC_USB_FORCE_RESET
    __HAL_RCC_USB_FORCE_RESET();
    __HAL_RCC_USB_RELEASE_RESET();
    #endif

    // 4. DeInit HAL to reset clocks to HSI (Critical for Bootloader)
    HAL_DeInit();
    HAL_RCC_DeInit();

    // 5. Clear Interrupts
    for (int i=0;i<8;i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    // 6. Barrier
    __DSB();
    __ISB();

    // 7. Remap System Memory to 0x00000000
    // STM32G0: SYSCFG_CFGR1 -> MEM_MODE[1:0] = 01 (System Flash)
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    SYSCFG->CFGR1 &= ~(SYSCFG_CFGR1_MEM_MODE);
    SYSCFG->CFGR1 |= (0x01 << SYSCFG_CFGR1_MEM_MODE_Pos);

    // 8. Set the stack pointer
    uint32_t jumpAddress = *(__IO uint32_t*) (SYSTEM_MEMORY + 4);
    pFunction JumpToApplication = (pFunction) jumpAddress;

    __set_MSP(*(__IO uint32_t*) SYSTEM_MEMORY);

    // 9. Jump to bootloader
    JumpToApplication();
}

#endif  // OPENWHEEL_DFU_DIRECT_JUMP

#ifdef __cplusplus
}
#endif
