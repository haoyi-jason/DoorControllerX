/**
  ******************************************************************************
  * @file     board_io.h
  * @brief    Board I/O abstraction layer for DoorControllerX (AT32F413R)
  ******************************************************************************
  */

#ifndef BOARD_IO_H
#define BOARD_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f413_wk_config.h"
#include <stdint.h>

/* Relay control (active high) -----------------------------------------------*/
/* R1 Relay → PC7 */
#define RELAY1_SET(x)     do { if(x) gpio_bits_set(GPIOC, GPIO_PINS_7);   \
                               else  gpio_bits_reset(GPIOC, GPIO_PINS_7); } while(0)
/* R2 Relay → PC8 */
#define RELAY2_SET(x)     do { if(x) gpio_bits_set(GPIOC, GPIO_PINS_8);   \
                               else  gpio_bits_reset(GPIOC, GPIO_PINS_8); } while(0)
/* R3 Relay → PC9 */
#define RELAY3_SET(x)     do { if(x) gpio_bits_set(GPIOC, GPIO_PINS_9);   \
                               else  gpio_bits_reset(GPIOC, GPIO_PINS_9); } while(0)

/* Motor direction (PH pin) --------------------------------------------------*/
/* D1 PH → PB7 */
#define D1_SET_PH(x)      do { if(x) gpio_bits_set(GPIOB, GPIO_PINS_7);   \
                               else  gpio_bits_reset(GPIOB, GPIO_PINS_7); } while(0)
/* D2 PH → PA3 */
#define D2_SET_PH(x)      do { if(x) gpio_bits_set(GPIOA, GPIO_PINS_3);   \
                               else  gpio_bits_reset(GPIOA, GPIO_PINS_3); } while(0)

/* Motor enable / sleep ------------------------------------------------------*/
/* D1 DrvOff (active high) → PB4 */
#define D1_SET_DRVOFF(x)  do { if(x) gpio_bits_set(GPIOB, GPIO_PINS_4);   \
                               else  gpio_bits_reset(GPIOB, GPIO_PINS_4); } while(0)
/* D1 nSLEEP (active low: 1=run, 0=sleep) → PB5 */
#define D1_SET_NSLEEP(x)  do { if(x) gpio_bits_set(GPIOB, GPIO_PINS_5);   \
                               else  gpio_bits_reset(GPIOB, GPIO_PINS_5); } while(0)
/* D2 DrvOff (active high) → PA5 */
#define D2_SET_DRVOFF(x)  do { if(x) gpio_bits_set(GPIOA, GPIO_PINS_5);   \
                               else  gpio_bits_reset(GPIOA, GPIO_PINS_5); } while(0)
/* D2 nSLEEP (active low: 1=run, 0=sleep) → PA7 */
#define D2_SET_NSLEEP(x)  do { if(x) gpio_bits_set(GPIOA, GPIO_PINS_7);   \
                               else  gpio_bits_reset(GPIOA, GPIO_PINS_7); } while(0)

/* Home sensor reads ---------------------------------------------------------*/
/* M1_HOME → PB10 (SET when at home) */
#define M1_HOME_READ()    gpio_input_data_bit_read(GPIOB, GPIO_PINS_10)
/* M2_HOME → PB3 */
#define M2_HOME_READ()    gpio_input_data_bit_read(GPIOB, GPIO_PINS_3)

/* Electric lock sensor reads ------------------------------------------------*/
/* M3_LL (lock sensor) → PC0 */
#define M3_LL_READ()      gpio_input_data_bit_read(GPIOC, GPIO_PINS_0)
/* M3_UL (unlock sensor) → PC1 */
#define M3_UL_READ()      gpio_input_data_bit_read(GPIOC, GPIO_PINS_1)

/* Trigger input reads -------------------------------------------------------*/
/* TG_OPEN → PC5 (active-low: RESET when trigger active) */
#define TG_OPEN_READ()    gpio_input_data_bit_read(GPIOC, GPIO_PINS_5)
/* TG_CLOSE → PC6 (active-low: RESET when trigger active) */
#define TG_CLOSE_READ()   gpio_input_data_bit_read(GPIOC, GPIO_PINS_6)

/* DIP switch reads (active low: RESET when ON) ------------------------------*/
/* DIP_0 → PA12 */
#define DIP0_READ()       gpio_input_data_bit_read(GPIOA, GPIO_PINS_12)
/* DIP_1 → PA11 */
#define DIP1_READ()       gpio_input_data_bit_read(GPIOA, GPIO_PINS_11)
/* DIP_2 → PB14 */
#define DIP2_READ()       gpio_input_data_bit_read(GPIOB, GPIO_PINS_14)
/* DIP_3 → PB15 */
#define DIP3_READ()       gpio_input_data_bit_read(GPIOB, GPIO_PINS_15)

/* Output signals ------------------------------------------------------------*/
/* OPEN_DONE (active high) → PC2 */
#define SET_OPEN_DONE(x)  do { if(x) gpio_bits_set(GPIOC, GPIO_PINS_2);   \
                               else  gpio_bits_reset(GPIOC, GPIO_PINS_2); } while(0)
/* CLOSE_DONE (active high) → PC3 */
#define SET_CLOSE_DONE(x) do { if(x) gpio_bits_set(GPIOC, GPIO_PINS_3);   \
                               else  gpio_bits_reset(GPIOC, GPIO_PINS_3); } while(0)

/* Buzzer (active high) → PA8 -----------------------------------------------*/
#define BUZZER_SET(x)     do { if(x) gpio_bits_set(GPIOA, GPIO_PINS_8);   \
                               else  gpio_bits_reset(GPIOA, GPIO_PINS_8); } while(0)

/* LED (active high) → PD2 --------------------------------------------------*/
#define LED_SET(x)        do { if(x) gpio_bits_set(GPIOD, GPIO_PINS_2);   \
                               else  gpio_bits_reset(GPIOD, GPIO_PINS_2); } while(0)

/* TEST button (active-low: RESET when pressed) → PB11 ----------------------*/
#define TEST_READ()       gpio_input_data_bit_read(GPIOB, GPIO_PINS_11)

/* Exported functions --------------------------------------------------------*/
void    board_io_init(void);
uint8_t board_get_dip(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_IO_H */
