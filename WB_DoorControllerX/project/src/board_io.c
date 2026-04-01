/**
  ******************************************************************************
  * @file     board_io.c
  * @brief    Board I/O abstraction layer for DoorControllerX (AT32F413R)
  ******************************************************************************
  */

#include "board_io.h"

/* Private variables ---------------------------------------------------------*/
static uint8_t dip_value = 0;

/**
  * @brief  Read DIP switches and initialize all output pins to safe defaults.
  * @note   DIP switches are active-low: RESET state means the switch is ON (=1).
  * @retval none
  */
void board_io_init(void)
{
    /* Read DIP switches (active-low: pin RESET → switch ON → bit = 1) */
    dip_value = 0;
    if (DIP0_READ() == RESET) dip_value |= (1u << 0);
    if (DIP1_READ() == RESET) dip_value |= (1u << 1);
    if (DIP2_READ() == RESET) dip_value |= (1u << 2);
    if (DIP3_READ() == RESET) dip_value |= (1u << 3);

    /* Relays off */
    RELAY1_SET(0);
    RELAY2_SET(0);
    RELAY3_SET(0);

    /* Motor drivers: DrvOff=1 (disabled), nSLEEP=0 (sleep) */
    D1_SET_DRVOFF(1);
    D1_SET_NSLEEP(0);
    D2_SET_DRVOFF(1);
    D2_SET_NSLEEP(0);

    /* Motor direction: default forward */
    D1_SET_PH(0);
    D2_SET_PH(0);

    /* Output signals off */
    SET_OPEN_DONE(0);
    SET_CLOSE_DONE(0);

    /* Buzzer and LED off */
    BUZZER_SET(0);
    LED_SET(0);
}

/**
  * @brief  Return the 4-bit DIP switch value read at startup.
  * @retval 4-bit DIP value (bit0=DIP_0, bit1=DIP_1, bit2=DIP_2, bit3=DIP_3)
  */
uint8_t board_get_dip(void)
{
    return dip_value;
}
