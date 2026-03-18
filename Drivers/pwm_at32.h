#pragma once
#include <stdint.h>

void pwm_init_20k(void);
void pwm_set_tim4_ch1_pb6_duty_0_1000(uint16_t duty_0_1000);
void pwm_set_tim5_ch3_pa2_duty_0_1000(uint16_t duty_0_1000);
