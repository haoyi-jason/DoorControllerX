/* add user code begin Header */
/**
  **************************************************************************
  * @file     main.c
  * @brief    main program
  **************************************************************************
  * Copyright (c) 2025, Artery Technology, All rights reserved.
  *
  * The software Board Support Package (BSP) that is made available to
  * download from Artery official website is the copyrighted work of Artery.
  * Artery authorizes customers to use, copy, and distribute the BSP
  * software and its related documentation for the purpose of design and
  * development in conjunction with Artery microcontrollers. Use of the
  * software is governed by this copyright notice and the following disclaimer.
  *
  * THIS SOFTWARE IS PROVIDED ON "AS IS" BASIS WITHOUT WARRANTIES,
  * GUARANTEES OR REPRESENTATIONS OF ANY KIND. ARTERY EXPRESSLY DISCLAIMS,
  * TO THE FULLEST EXTENT PERMITTED BY LAW, ALL EXPRESS, IMPLIED OR
  * STATUTORY OR OTHER WARRANTIES, GUARANTEES OR REPRESENTATIONS,
  * INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
  *
  **************************************************************************
  */
/* add user code end Header */

/* Includes ------------------------------------------------------------------*/
#include "at32f413_wk_config.h"
#include "wk_system.h"
#include "freertos_app.h"
#include "arm_math.h"
#include "at32f413_wdt.h"  /* Watchdog support */

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include "board_io.h"
#include "database.h"
#include "door_ctrl.h"
/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */

/* add user code end private typedef */

/* private define ------------------------------------------------------------*/
/* add user code begin private define */

/* add user code end private define */

/* private macro -------------------------------------------------------------*/
/* add user code begin private macro */

/* add user code end private macro */

/* private variables ---------------------------------------------------------*/
/* add user code begin private variables */
volatile uint32_t g_dbg_ctrlsts_before_clear = 0u;
volatile uint32_t g_dbg_ctrlsts_after_clear = 0u;

/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */

/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */

/* add user code end 0 */

/**
  * @brief main function.
  * @param  none
  * @retval none
  */
int main(void)
{
  /* add user code begin 1 */
  /* CRITICAL: Feed watchdog IMMEDIATELY at startup to prevent timeout during peripheral init */
  wdt_counter_reload();
  
  /* Enable BPR clock early to check HardFault sentinel before reading CRM flags */
  crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_BPR_PERIPH_CLOCK, TRUE);
  pwc_battery_powered_domain_access(TRUE);

  /* Diagnose reset reason (AT32F413 uses CRM flags) */
  volatile uint8_t reset_reason = 0u;
  uint32_t csr = CRM->ctrlsts;
  g_dbg_ctrlsts_before_clear = csr;
  if (bpr_data_read(BPR_DATA2) == 0xFAu) {
    reset_reason = 99u; /* HardFault — written to BPR before NVIC_SystemReset() */
    bpr_data_write(BPR_DATA2, 0u);  /* Clear sentinel */
  } else if (csr & (1U << 29)) {           /* CRM_WDT_RESET_FLAG */
    reset_reason = 1u;  /* Independent Watchdog */
  } else if (csr & (1U << 30)) {    /* CRM_WWDT_RESET_FLAG */
    reset_reason = 2u;  /* Window Watchdog */
  } else if (csr & (1U << 28)) {    /* CRM_SW_RESET_FLAG */
    reset_reason = 3u;  /* Software reset */
  } else if (csr & (1U << 26)) {    /* CRM_NRST_RESET_FLAG */
    reset_reason = 4u;  /* PIN reset */
  } else if (csr & (1U << 27)) {    /* CRM_POR_RESET_FLAG */
    reset_reason = 5u;  /* Power-on reset */
  }
  crm_flag_clear(CRM_ALL_RESET_FLAG);  /* Clear reset flags using vendor API */
  g_dbg_ctrlsts_after_clear = CRM->ctrlsts;
  /* add user code end 1 */

  /* system clock config. */
  wk_system_clock_config();
  wdt_counter_reload();  /* Feed watchdog after clock init */

  /* config periph clock. */
  wk_periph_clock_config();
  wdt_counter_reload();  /* Feed watchdog */

  /* init debug function. */
  wk_debug_config();
  wdt_counter_reload();  /* Feed watchdog */

  /* nvic config. */
  wk_nvic_config();
  wdt_counter_reload();  /* Feed watchdog */

  /* timebase config for
     void wk_delay_ms(uint32_t delay); */
  wk_timebase_init();
  wdt_counter_reload();  /* Feed watchdog */

  /* init gpio function. */
  wk_gpio_config();
  wdt_counter_reload();  /* Feed watchdog */

  /* init adc1 function. */
  wk_adc1_init();
  wdt_counter_reload();  /* Feed watchdog after ADC (can be slow) */

  /* init usart1 function. */
  wk_usart1_init();
  wdt_counter_reload();  /* Feed watchdog */

  /* init i2c1 function. */
  wk_i2c1_init();
  wdt_counter_reload();  /* Feed watchdog */

  /* init tmr4 function. */
  wk_tmr4_init();
  wdt_counter_reload();  /* Feed watchdog */

  /* init tmr5 function. */
  wk_tmr5_init();
  wdt_counter_reload();  /* Feed watchdog */

  /* add user code begin 2 */
  board_io_init();
  wdt_counter_reload();  /* Feed watchdog after board IO */
  
  db_init();
  wdt_counter_reload();  /* Feed watchdog after database init */
  
  g_dbg_reset_reason = reset_reason;  /* Copy reset reason to debug watch variable */
  db_set_live(LD_RESET_REASON, (uint32_t)reset_reason);  /* Expose reset reason via live data */
  /* add user code end 2 */

  /* init freertos function. */
  wk_freertos_init();

  while(1)
  {
    /* add user code begin 3 */

    /* add user code end 3 */
  }
}

  /* add user code begin 4 */

  /* add user code end 4 */
