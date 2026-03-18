#include "pwm_at32.h"

#include "at32f4xx.h"
#include "at32f4xx_rcc.h"
#include "at32f4xx_tim.h"
#include "at32f4xx_gpio.h"

#define PWM_ARR  (3599U)
#define PWM_PSC  (0U)

static uint16_t duty_to_ccr(uint16_t duty_0_1000)
{
  if (duty_0_1000 > 1000) duty_0_1000 = 1000;
  uint32_t ccr = ((uint32_t)duty_0_1000 * (PWM_ARR + 1U) + 500U) / 1000U;
  if (ccr > PWM_ARR) ccr = PWM_ARR;
  return (uint16_t)ccr;
}

static void pwm_gpio_init(void)
{
  RCC_APB2PeriphClockCmd(RCC_APB2PERIPH_GPIOA |
                         RCC_APB2PERIPH_GPIOB |
                         RCC_APB2PERIPH_AFIO, ENABLE);

  GPIO_InitType gi;
  GPIO_StructInit(&gi);
  gi.GPIO_MaxSpeed = GPIO_MaxSpeed_50MHz;
  gi.GPIO_Mode = GPIO_Mode_AF_PP;

  gi.GPIO_Pins = GPIO_Pins_6;
  GPIO_Init(GPIOB, &gi);

  gi.GPIO_Pins = GPIO_Pins_2;
  GPIO_Init(GPIOA, &gi);
}

static void pwm_tim4_init(void)
{
  RCC_APB1PeriphClockCmd(RCC_APB1PERIPH_TMR4, ENABLE);

  TMR_TimerBaseInitType tb;
  TMR_TimeBaseStructInit(&tb);
  tb.TMR_DIV = PWM_PSC;
  tb.TMR_CounterMode = TMR_CounterDIR_Up;
  tb.TMR_Period = PWM_ARR;
  tb.TMR_ClockDivision = TMR_CKD_DIV1;
  tb.TMR_RepetitionCounter = 0;
  TMR_TimeBaseInit(TMR4, &tb);

  TMR_OCInitType oc;
  TMR_OCStructInit(&oc);
  oc.TMR_OCMode = TMR_OCMode_PWM1;
  oc.TMR_OutputState = TMR_OutputState_Enable;
  oc.TMR_Pulse = 0;
  oc.TMR_OCPolarity = TMR_OCPolarity_High;
  TMR_OC1Init(TMR4, &oc);

  TMR_OC1PreloadConfig(TMR4, TMR_OCPreload_Enable);
  TMR_ARPreloadConfig(TMR4, ENABLE);

  TMR_Cmd(TMR4, ENABLE);
}

static void pwm_tim5_init(void)
{
  RCC_APB1PeriphClockCmd(RCC_APB1PERIPH_TMR5, ENABLE);

  TMR_TimerBaseInitType tb;
  TMR_TimeBaseStructInit(&tb);
  tb.TMR_DIV = PWM_PSC;
  tb.TMR_CounterMode = TMR_CounterDIR_Up;
  tb.TMR_Period = PWM_ARR;
  tb.TMR_ClockDivision = TMR_CKD_DIV1;
  tb.TMR_RepetitionCounter = 0;
  TMR_TimeBaseInit(TMR5, &tb);

  TMR_OCInitType oc;
  TMR_OCStructInit(&oc);
  oc.TMR_OCMode = TMR_OCMode_PWM1;
  oc.TMR_OutputState = TMR_OutputState_Enable;
  oc.TMR_Pulse = 0;
  oc.TMR_OCPolarity = TMR_OCPolarity_High;
  TMR_OC3Init(TMR5, &oc);

  TMR_OC3PreloadConfig(TMR5, TMR_OCPreload_Enable);
  TMR_ARPreloadConfig(TMR5, ENABLE);

  TMR_Cmd(TMR5, ENABLE);
}

void pwm_init_20k(void)
{
  pwm_gpio_init();
  pwm_tim4_init();
  pwm_tim5_init();

  pwm_set_tim4_ch1_pb6_duty_0_1000(0);
  pwm_set_tim5_ch3_pa2_duty_0_1000(0);
}

void pwm_set_tim4_ch1_pb6_duty_0_1000(uint16_t duty_0_1000)
{
  TMR_SetCompare1(TMR4, duty_to_ccr(duty_0_1000));
}

void pwm_set_tim5_ch3_pa2_duty_0_1000(uint16_t duty_0_1000)
{
  TMR_SetCompare3(TMR5, duty_to_ccr(duty_0_1000));
}
