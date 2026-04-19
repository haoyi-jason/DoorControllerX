/**
  ******************************************************************************
  * @file     door_pid.h
  * @brief    PID motor control layer for DoorControllerX (AT32F413R)
  ******************************************************************************
  */

#ifndef DOOR_PID_H
#define DOOR_PID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Motor IDs -----------------------------------------------------------------*/
#define MOTOR_M1    0   /*!< Main door motor — D1 (TIM4 CH1, PB6) */
#define MOTOR_M2    1   /*!< Secondary door motor — D2 (TIM5 CH3, PA2) */
#define MOTOR_M3    2   /*!< Electric lock motor — D2 (TIM5 CH3, PA2) + R3 */

/* Motor direction -----------------------------------------------------------*/
#define MOTOR_DIR_FWD   0   /*!< Logical forward direction (PH level mapped by DIP_0) */
#define MOTOR_DIR_REV   1   /*!< Logical reverse direction (PH level mapped by DIP_0) */

/* Timer period (counts) used during wk_tmr4/5 init: 20000 ticks = 100% duty */
#define TMR_PERIOD_COUNTS   20000u

/* PID controller structure --------------------------------------------------*/
typedef struct {
    float kp;
    float ki;
    float kd;
    float integral;
    float prev_error;
    float output;
} _pid_t;

/* Exported functions --------------------------------------------------------*/
void    pid_init(_pid_t *pid, float kp, float ki, float kd);
float   pid_compute(_pid_t *pid, float setpoint, float measured, float dt);

void    motor_set_pwm(uint8_t motor_id, uint8_t duty_pct);
void    motor_set_direction(uint8_t motor_id, uint8_t dir);
void    motor_enable(uint8_t motor_id);
void    motor_disable(uint8_t motor_id);

float   adc_to_position(uint16_t raw);
void    adc_reset_pot_filter(uint8_t motor_id);
uint16_t adc_read_m1_pot(void);
uint16_t adc_read_m2_pot(void);

#ifdef __cplusplus
}
#endif

#endif /* DOOR_PID_H */
