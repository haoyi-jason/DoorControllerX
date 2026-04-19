/* add user code begin Header */
/**
  **************************************************************************
  * @file     at32f413_int.c
  * @brief    main interrupt service routines.
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

/* includes ------------------------------------------------------------------*/
#include "at32f413_int.h"
#include "wk_system.h"
#include "freertos_app.h"


/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include "comm_task.h"
#include "at32f413_bpr.h"  /* bpr_data_write for HardFault sentinel */

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

/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */

/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */

/* add user code end 0 */

/* external variables ---------------------------------------------------------*/
/* add user code begin external variables */

/* add user code end external variables */

/**
  * @brief  this function handles nmi exception.
  * @param  none
  * @retval none
  */
void NMI_Handler(void)
{
  /* add user code begin NonMaskableInt_IRQ 0 */

  /* add user code end NonMaskableInt_IRQ 0 */

  /* add user code begin NonMaskableInt_IRQ 1 */

  /* add user code end NonMaskableInt_IRQ 1 */
}

/**
  * @brief  this function handles hard fault exception.
  * @param  none
  * @retval none
  */
void HardFault_Handler(void)
{
  /* add user code begin HardFault_IRQ 0 */
  /* Hard fault occurred — write sentinel to BPR_DATA2 so main can detect this after reset */
  extern volatile uint32_t g_dbg_startup_stage;
  extern volatile uint32_t g_dbg_hardfault_stage;
  g_dbg_hardfault_stage = g_dbg_startup_stage;  /* Snapshot stage when HardFault occurred */
  extern volatile uint32_t g_dbg_reset_reason;
  g_dbg_reset_reason = 99u;
  /* BPR write access should already be enabled by rtc_init(); write sentinel before reset */
  bpr_data_write(BPR_DATA2, 0xFAu);  /* HardFault sentinel — survives NVIC_SystemReset() */
  NVIC_SystemReset();  /* Reset so main can detect and report HardFault via LD_RESET_REASON */
  /* add user code end HardFault_IRQ 0 */
  /* go to infinite loop when hard fault exception occurs */
  while (1)
  {
    /* add user code begin W1_HardFault_IRQ 0 */

    /* add user code end W1_HardFault_IRQ 0 */
  }
}

/**
  * @brief  this function handles memory manage exception.
  * @param  none
  * @retval none
  */
void MemManage_Handler(void)
{
  /* add user code begin MemoryManagement_IRQ 0 */

  /* add user code end MemoryManagement_IRQ 0 */
  /* go to infinite loop when memory manage exception occurs */
  while (1)
  {
    /* add user code begin W1_MemoryManagement_IRQ 0 */

    /* add user code end W1_MemoryManagement_IRQ 0 */
  }
}

/**
  * @brief  this function handles bus fault exception.
  * @param  none
  * @retval none
  */
void BusFault_Handler(void)
{
  /* add user code begin BusFault_IRQ 0 */

  /* add user code end BusFault_IRQ 0 */
  /* go to infinite loop when bus fault exception occurs */
  while (1)
  {
    /* add user code begin W1_BusFault_IRQ 0 */

    /* add user code end W1_BusFault_IRQ 0 */
  }
}

/**
  * @brief  this function handles usage fault exception.
  * @param  none
  * @retval none
  */
void UsageFault_Handler(void)
{
  /* add user code begin UsageFault_IRQ 0 */

  /* add user code end UsageFault_IRQ 0 */
  /* go to infinite loop when usage fault exception occurs */
  while (1)
  {
    /* add user code begin W1_UsageFault_IRQ 0 */

    /* add user code end W1_UsageFault_IRQ 0 */
  }
}


/**
  * @brief  this function handles debug monitor exception.
  * @param  none
  * @retval none
  */
void DebugMon_Handler(void)
{
  /* add user code begin DebugMonitor_IRQ 0 */

  /* add user code end DebugMonitor_IRQ 0 */
  /* add user code begin DebugMonitor_IRQ 1 */

  /* add user code end DebugMonitor_IRQ 1 */
}

extern void xPortSysTickHandler(void);

/**
  * @brief  this function handles systick handler.
  * @param  none
  * @retval none
  */
void SysTick_Handler(void)
{
  /* add user code begin SysTick_IRQ 0 */

  /* add user code end SysTick_IRQ 0 */

  wk_timebase_handler();
#if (INCLUDE_xTaskGetSchedulerState == 1 )
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
#endif /* INCLUDE_xTaskGetSchedulerState */
  xPortSysTickHandler();
#if (INCLUDE_xTaskGetSchedulerState == 1 )
  }
#endif /* INCLUDE_xTaskGetSchedulerState */

  /* add user code begin SysTick_IRQ 1 */

  /* add user code end SysTick_IRQ 1 */
}

/**
  * @brief  this function handles ADC1 & ADC2 handler.
  * @param  none
  * @retval none
  */
void ADC1_2_IRQHandler(void)
{
  /* add user code begin ADC1_2_IRQ 0 */

  /* add user code end ADC1_2_IRQ 0 */

  if(adc_interrupt_flag_get(ADC1, ADC_CCE_FLAG) != RESET)
  {
    /* add user code begin ADC1_ADC_CCE_FLAG */
    /* clear flag */
    adc_flag_clear(ADC1, ADC_CCE_FLAG);
    /* add user code end ADC1_ADC_CCE_FLAG */ 
  }

  /* add user code begin ADC1_2_IRQ 1 */

  /* add user code end ADC1_2_IRQ 1 */
}

/**
  * @brief  this function handles USART1 global interrupt.
  * @param  none
  * @retval none
  */
void USART1_IRQHandler(void)
{
  comm_task_rx_isr_handler();
}

/* add user code begin 1 */

/* add user code end 1 */
