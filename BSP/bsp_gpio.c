#include "bsp_gpio.h"

#include "at32f4xx.h"
#include "at32f4xx_gpio.h"
#include "at32f4xx_rcc.h"

static void gpio_out(GPIO_Type* port, uint16_t pins, bool initial_high)
{
  GPIO_InitType gi;
  GPIO_StructInit(&gi);
  gi.GPIO_Pins = pins;
  gi.GPIO_Mode = GPIO_Mode_OUT_PP;
  gi.GPIO_MaxSpeed = GPIO_MaxSpeed_50MHz;
  GPIO_Init(port, &gi);

  if (initial_high) GPIO_SetBits(port, pins);
  else GPIO_ResetBits(port, pins);
}

static void gpio_in_pu(GPIO_Type* port, uint16_t pins)
{
  GPIO_InitType gi;
  GPIO_StructInit(&gi);
  gi.GPIO_Pins = pins;
  gi.GPIO_Mode = GPIO_Mode_IN_PU;
  gi.GPIO_MaxSpeed = GPIO_MaxSpeed_2MHz;
  GPIO_Init(port, &gi);
}

static void gpio_analog(GPIO_Type* port, uint16_t pins)
{
  GPIO_InitType gi;
  GPIO_StructInit(&gi);
  gi.GPIO_Pins = pins;
  gi.GPIO_Mode = GPIO_Mode_IN_ANALOG;
  gi.GPIO_MaxSpeed = GPIO_MaxSpeed_2MHz;
  GPIO_Init(port, &gi);
}

static void gpio_af_pp(GPIO_Type* port, uint16_t pins)
{
  GPIO_InitType gi;
  GPIO_StructInit(&gi);
  gi.GPIO_Pins = pins;
  gi.GPIO_Mode = GPIO_Mode_AF_PP;
  gi.GPIO_MaxSpeed = GPIO_MaxSpeed_50MHz;
  GPIO_Init(port, &gi);
}

/* Raw reads */
bool bsp_read_pb10(void) { return GPIO_ReadInputDataBit(GPIOB, GPIO_Pins_10) ? true : false; }
bool bsp_read_pb3(void)  { return GPIO_ReadInputDataBit(GPIOB, GPIO_Pins_3) ? true : false; }
bool bsp_read_pc0(void)  { return GPIO_ReadInputDataBit(GPIOC, GPIO_Pins_0) ? true : false; }
bool bsp_read_pc1(void)  { return GPIO_ReadInputDataBit(GPIOC, GPIO_Pins_1) ? true : false; }

/* Driver enables */
void bsp_drv1_enable(bool on) { if (on) GPIO_SetBits(GPIOB, GPIO_Pins_4); else GPIO_ResetBits(GPIOB, GPIO_Pins_4); }
void bsp_drv2_enable(bool on) { if (on) GPIO_SetBits(GPIOA, GPIO_Pins_5); else GPIO_ResetBits(GPIOA, GPIO_Pins_5); }

/* Relays */
void bsp_relay_drv1_to_door1(bool on) { if (on) GPIO_SetBits(GPIOC, GPIO_Pins_7); else GPIO_ResetBits(GPIOC, GPIO_Pins_7); }
void bsp_relay_drv2_to_door2(bool on) { if (on) GPIO_SetBits(GPIOC, GPIO_Pins_8); else GPIO_ResetBits(GPIOC, GPIO_Pins_8); }
void bsp_relay_drv2_to_lock(bool on)  { if (on) GPIO_SetBits(GPIOC, GPIO_Pins_9); else GPIO_ResetBits(GPIOC, GPIO_Pins_9); }

void bsp_gpio_init(void)
{
  RCC_APB2PeriphClockCmd(RCC_APB2PERIPH_GPIOA |
                         RCC_APB2PERIPH_GPIOB |
                         RCC_APB2PERIPH_GPIOC |
                         RCC_APB2PERIPH_AFIO, ENABLE);

  gpio_in_pu(GPIOB, GPIO_Pins_10);
  gpio_in_pu(GPIOB, GPIO_Pins_3);
  gpio_in_pu(GPIOC, GPIO_Pins_0);
  gpio_in_pu(GPIOC, GPIO_Pins_1);

  gpio_analog(GPIOA, GPIO_Pins_0 | GPIO_Pins_1);
  gpio_analog(GPIOB, GPIO_Pins_0 | GPIO_Pins_1);

  gpio_out(GPIOB, GPIO_Pins_4, false);
  gpio_out(GPIOA, GPIO_Pins_5, false);

  gpio_out(GPIOC, GPIO_Pins_7, false);
  gpio_out(GPIOC, GPIO_Pins_8, false);
  gpio_out(GPIOC, GPIO_Pins_9, false);

  gpio_out(GPIOB, GPIO_Pins_7, false);
  gpio_out(GPIOA, GPIO_Pins_3, false);

  gpio_af_pp(GPIOB, GPIO_Pins_6);
  gpio_af_pp(GPIOA, GPIO_Pins_2);
}
