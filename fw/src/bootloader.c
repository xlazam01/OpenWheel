#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// System Memory Base Address for STM32G0
#define SYSTEM_MEMORY 0x1FFF0000

typedef void (*pFunction)(void);

void JumpToBootloader(void) {
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

#ifdef __cplusplus
}
#endif
