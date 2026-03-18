#include "bsp_clock.h"

#include "at32f4xx.h"
#include "at32f4xx_rcc.h"
#include "at32f4xx_flash.h" /* Adjust if your SPL flash header differs */

void bsp_clock_init(void)
{
  RCC_Reset();

  RCC_HSEConfig(RCC_HSE_ENABLE);
  if (RCC_WaitForHSEStable() != SUCCESS)
  {
    while (1) {}
  }

  /* Flash wait states for 72MHz (function/enum names may differ by SPL pack) */
  FLASH_PrefetchBufferCmd(ENABLE);
  FLASH_SetLatency(FLASH_Latency_2);

  RCC_AHBCLKConfig(RCC_SYSCLK_Div1);
  RCC_APB2CLKConfig(RCC_AHBCLK_Div1);
  RCC_APB1CLKConfig(RCC_AHBCLK_Div2);

  /* Safe ADC clock example: PCLK2/6 = 12MHz */
  RCC_ADCCLKConfig(RCC_APB2CLK_Div6);

  RCC_PLLConfig(RCC_PLLRefClk_HSE_Div1, RCC_PLLMult_9, RCC_Range_LessEqual_72Mhz);
  RCC_PLLCmd(ENABLE);
  while (RCC_GetFlagStatus(RCC_FLAG_PLLSTBL) == RESET) { }

  RCC_SYSCLKConfig(RCC_SYSCLKSelction_PLL);
  while (RCC_GetSYSCLKSelction() != RCC_SYSCLKSelction_PLL) { }

  SystemCoreClockUpdate();
}

uint32_t bsp_sysclk_hz(void)
{
  return SystemCoreClock;
}
