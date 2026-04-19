/**
  ******************************************************************************
  * @file     door_pid.c
  * @brief    PID motor control �� PWM, direction, ADC position reading
  ******************************************************************************
  */

#include "door_pid.h"
#include "board_io.h"
#include "database.h"
#include "at32f413_wk_config.h"

/* Private helpers -----------------------------------------------------------*/

#define ADC_FILTER_DEPTH  8u

typedef struct {
    uint16_t buf[ADC_FILTER_DEPTH];
    uint32_t sum;
    uint8_t  idx;
    uint8_t  count;
} adc_filter_t;

static adc_filter_t m1_filter;
static adc_filter_t m2_filter;
static uint8_t      adc2_ready = 0u;

static void adc_filter_reset(adc_filter_t *f)
{
    uint8_t i;

    f->sum = 0u;
    f->idx = 0u;
    f->count = 0u;
    for (i = 0u; i < ADC_FILTER_DEPTH; i++) {
        f->buf[i] = 0u;
    }
}

static void adc_filter_push(adc_filter_t *f, uint16_t sample)
{
    if (f->count < ADC_FILTER_DEPTH) {
        f->buf[f->idx] = sample;
        f->sum += sample;
        f->idx = (uint8_t)((f->idx + 1u) % ADC_FILTER_DEPTH);
        f->count++;
        return;
    }

    f->sum -= f->buf[f->idx];
    f->buf[f->idx] = sample;
    f->sum += sample;
    f->idx = (uint8_t)((f->idx + 1u) % ADC_FILTER_DEPTH);
}

static uint16_t adc_filter_avg(const adc_filter_t *f)
{
    uint8_t  i;
    uint16_t smin, smax;
    uint32_t sum;

    if (f->count == 0u) {
        return 0u;
    }

    /* Detect whether samples span the 0/4095 roll-over boundary.
     * This happens when the POT is near the electrical zero (home position)
     * and noise pushes readings to both ends of the 12-bit range. */
    smin = 4095u;
    smax = 0u;
    for (i = 0u; i < f->count; i++) {
        if (f->buf[i] < smin) { smin = f->buf[i]; }
        if (f->buf[i] > smax) { smax = f->buf[i]; }
    }

    if ((uint32_t)(smax - smin) > 2048u) {
        /* Circular mean: fold samples on the low side up by one full range
         * so all samples are contiguous before averaging.                    */
        sum = 0u;
        for (i = 0u; i < f->count; i++) {
            sum += (f->buf[i] < 2048u) ? ((uint32_t)f->buf[i] + 4096u)
                                       : (uint32_t)f->buf[i];
        }
        return (uint16_t)((sum / (uint32_t)f->count) & 0x0FFFu);
    }

    /* Normal linear mean (pre-computed running sum). */
    return (uint16_t)(f->sum / (uint32_t)f->count);
}

static void adc2_init_if_needed(void)
{
    adc_base_config_type adc_base_struct;

    if (adc2_ready != 0u) {
        return;
    }

    adc_reset(ADC2);
    adc_base_default_para_init(&adc_base_struct);
    adc_base_struct.sequence_mode = FALSE;
    adc_base_struct.repeat_mode = TRUE;   /* continuous sampling */
    adc_base_struct.data_align = ADC_RIGHT_ALIGNMENT;
    adc_base_struct.ordinary_channel_length = 1;
    adc_base_config(ADC2, &adc_base_struct);

    adc_ordinary_conversion_trigger_set(ADC2, ADC12_ORDINARY_TRIG_SOFTWARE, TRUE);
    adc_ordinary_part_mode_enable(ADC2, FALSE);

    adc_enable(ADC2, TRUE);
    adc_calibration_init(ADC2);
    while (adc_calibration_init_status_get(ADC2))
    {
    }
    adc_calibration_start(ADC2);
    while (adc_calibration_status_get(ADC2))
    {
    }

    adc2_ready = 1u;
}

/**
  * @brief  Capture 8 continuous samples on ADC2 and return filtered average.
  * @param  ch  ADC channel (ADC_CHANNEL_x)
  * @param  f   Per-channel moving average buffer
  * @retval Filtered 12-bit ADC value, or 0 on timeout
  */
static uint16_t adc2_poll_filtered(adc_channel_select_type ch, adc_filter_t *f)
{
    uint8_t  i;
    adc_ordinary_channel_set(ADC2, ch, 1, ADC_SAMPLETIME_28_5);
    adc_ordinary_software_trigger_enable(ADC2, TRUE);

    for (i = 0u; i < ADC_FILTER_DEPTH; i++) {
        uint32_t timeout = 100000u;
        while (!adc_flag_get(ADC2, ADC_CCE_FLAG) && (--timeout))
        {
        }
        if (timeout == 0u) {
            return adc_filter_avg(f);
        }
        adc_filter_push(f, (uint16_t)adc_ordinary_conversion_data_get(ADC2));
        adc_flag_clear(ADC2, ADC_CCE_FLAG);
    }

    return adc_filter_avg(f);
}

/* PID implementation --------------------------------------------------------*/

/**
  * @brief  Initialise a PID controller.
  */
void pid_init(_pid_t *pid, float kp, float ki, float kd)
{
    pid->kp         = kp;
    pid->ki         = ki;
    pid->kd         = kd;
    pid->integral   = 0.0f;
    pid->prev_error = 0.0f;
    pid->output     = 0.0f;
}

/**
  * @brief  Compute one PID step with anti-windup clamping.
  * @param  pid       PID instance
  * @param  setpoint  Desired value
  * @param  measured  Measured value
  * @param  dt        Time step (seconds)
  * @retval PID output (unclamped �� caller applies duty limits)
  */
float pid_compute(_pid_t *pid, float setpoint, float measured, float dt)
{
    float error;
    float derivative;

    if (dt <= 0.0f) dt = 0.001f;

    error = setpoint - measured;

    /* Integral with simple anti-windup (clamp) */
    pid->integral += error * dt;
    if (pid->integral >  1000.0f) pid->integral =  1000.0f;
    if (pid->integral < -1000.0f) pid->integral = -1000.0f;

    derivative = (error - pid->prev_error) / dt;
    pid->prev_error = error;

    pid->output = (pid->kp * error) + (pid->ki * pid->integral) + (pid->kd * derivative);
    return pid->output;
}

/* Motor PWM -----------------------------------------------------------------*/

/**
  * @brief  Set PWM duty cycle on the specified motor channel.
  *         Duty is clamped to [START_DUTY, MAX_DUTY] from the database,
  *         then written to the timer compare register.
  * @param  motor_id  MOTOR_M1 / MOTOR_M2 / MOTOR_M3
  * @param  duty_pct  Desired duty (0��100%)
  */
void motor_set_pwm(uint8_t motor_id, uint8_t duty_pct)
{
    uint8_t  start_duty, max_duty;
    uint32_t count;

    /* Retrieve limits from database */
    if (motor_id == MOTOR_M1) {
        start_duty = (uint8_t)db_get_param(DF_M1_START_DUTY);
        max_duty   = (uint8_t)db_get_param(DF_M1_MAX_DUTY);
    } else if (motor_id == MOTOR_M2) {
        start_duty = (uint8_t)db_get_param(DF_M2_START_DUTY);
        max_duty   = (uint8_t)db_get_param(DF_M2_MAX_DUTY);
    } else { /* MOTOR_M3 */
        start_duty = (uint8_t)db_get_param(DF_M3_START_DUTY);
        max_duty   = (uint8_t)db_get_param(DF_M3_MAX_DUTY);
    }

    if (duty_pct == 0u) {
        /* Explicit stop �� bypass start_duty clamp */
        count = 0u;
    } else {
        /* Clamp duty to [start_duty, max_duty] */
        if (duty_pct < start_duty) duty_pct = start_duty;
        if (duty_pct > max_duty)   duty_pct = max_duty;
        count = (uint32_t)duty_pct * (TMR_PERIOD_COUNTS / 100u);
    }

    if (motor_id == MOTOR_M1) {
        tmr_channel_value_set(TMR4, TMR_SELECT_CHANNEL_1, count);
        db_set_live(LD_M1_PWM, duty_pct);
    } else { /* M2 and M3 share D2 / TIM5 CH3 */
        tmr_channel_value_set(TMR5, TMR_SELECT_CHANNEL_3, count);
        db_set_live(LD_M2_PWM, duty_pct);
    }
}

/* Motor direction -----------------------------------------------------------*/

/**
  * @brief  Set motor direction.
  *         DIP_0=1 reverses the polarity for D1 and D2.
  * @param  motor_id  MOTOR_M1 / MOTOR_M2 / MOTOR_M3
  * @param  dir       MOTOR_DIR_FWD or MOTOR_DIR_REV
  */
void motor_set_direction(uint8_t motor_id, uint8_t dir)
{
    uint8_t dip  = board_get_dip();
    uint8_t ph;

    /*
     * DIP_0 polarity mapping (D1):
     *   DIP_0 = 0 -> FWD maps to D1 PH=0
     *   DIP_0 = 1 -> FWD maps to D1 PH=1
     * D2 PH is always inverse of D1 PH for the same logical direction:
     *   DIP_0 = 0 -> FWD maps to D2 PH=1
     *   DIP_0 = 1 -> FWD maps to D2 PH=0
     */
    if (motor_id == MOTOR_M1) {
        /* D1: DIP_0=0 -> FWD=PH0, DIP_0=1 -> FWD=PH1 */
        if (dip & 0x01u) {
            ph = (dir == MOTOR_DIR_FWD) ? 0u : 1u;
        } else {
            ph = (dir == MOTOR_DIR_FWD) ? 1u : 0u;
        }
        D1_SET_PH(ph);
    } else {
        /* D2: polarity inverted vs D1 �� DIP_0=0 -> FWD=PH1, DIP_0=1 -> FWD=PH0 */
        if (dip & 0x01u) {
            ph = (dir == MOTOR_DIR_FWD) ? 1u : 0u;
        } else {
            ph = (dir == MOTOR_DIR_FWD) ? 0u : 1u;
        }
        D2_SET_PH(ph);
    }
}

/* Motor enable / disable ----------------------------------------------------*/

/**
  * @brief  Enable a motor driver (deassert DrvOff, assert nSLEEP, enable PWM output).
  */
void motor_enable(uint8_t motor_id)
{
    tmr_output_config_type oc;

    oc.oc_mode         = TMR_OUTPUT_CONTROL_PWM_MODE_A;
    oc.oc_output_state = TRUE;
    oc.occ_output_state= FALSE;
    oc.oc_polarity     = TMR_OUTPUT_ACTIVE_HIGH;
    oc.occ_polarity    = TMR_OUTPUT_ACTIVE_HIGH;
    oc.oc_idle_state   = FALSE;
    oc.occ_idle_state  = FALSE;

    if (motor_id == MOTOR_M1) {
        D1_SET_DRVOFF(0);
        D1_SET_NSLEEP(1);
        tmr_output_channel_config(TMR4, TMR_SELECT_CHANNEL_1, &oc);
    } else {
        D2_SET_DRVOFF(0);
        D2_SET_NSLEEP(1);
        tmr_output_channel_config(TMR5, TMR_SELECT_CHANNEL_3, &oc);
    }
}

/**
  * @brief  Disable a motor driver (assert DrvOff, deassert nSLEEP, zero PWM).
  */
void motor_disable(uint8_t motor_id)
{
    tmr_output_config_type oc;

    oc.oc_mode         = TMR_OUTPUT_CONTROL_OFF;
    oc.oc_output_state = TRUE;
    oc.occ_output_state= FALSE;
    oc.oc_polarity     = TMR_OUTPUT_ACTIVE_HIGH;
    oc.occ_polarity    = TMR_OUTPUT_ACTIVE_HIGH;
    oc.oc_idle_state   = FALSE;
    oc.occ_idle_state  = FALSE;

    if (motor_id == MOTOR_M1) {
        tmr_channel_value_set(TMR4, TMR_SELECT_CHANNEL_1, 0);
        tmr_output_channel_config(TMR4, TMR_SELECT_CHANNEL_1, &oc);
        D1_SET_DRVOFF(1);
        D1_SET_NSLEEP(0);
        db_set_live(LD_M1_PWM, 0u);
    } else {
        tmr_channel_value_set(TMR5, TMR_SELECT_CHANNEL_3, 0);
        tmr_output_channel_config(TMR5, TMR_SELECT_CHANNEL_3, &oc);
        D2_SET_DRVOFF(1);
        D2_SET_NSLEEP(0);
        db_set_live(LD_M2_PWM, 0u);
    }
}

/* ADC position reading ------------------------------------------------------*/

/**
  * @brief  Convert a 12-bit ADC value to door angle in degrees.
  * @param  raw  12-bit ADC sample
  * @retval Angle in degrees (0 �� 360)
  */
float adc_to_position(uint16_t raw)
{
    return (float)raw * 360.0f / 4096.0f;
}

void adc_reset_pot_filter(uint8_t motor_id)
{
    if (motor_id == MOTOR_M1) {
        adc_filter_reset(&m1_filter);
    } else if (motor_id == MOTOR_M2) {
        adc_filter_reset(&m2_filter);
    }
}

/**
  * @brief  Read M1 potentiometer (PB0, ADC2_IN8).
  *         ADC2 is used to avoid conflict with the ADC1 CCE interrupt.
  * @retval 12-bit ADC raw value
  */
uint16_t adc_read_m1_pot(void)
{
    adc2_init_if_needed();
    return adc2_poll_filtered(ADC_CHANNEL_8, &m1_filter);
}

/**
  * @brief  Read M2 potentiometer (PB1, ADC2_IN9).
  * @retval 12-bit ADC raw value
  */
uint16_t adc_read_m2_pot(void)
{
    adc2_init_if_needed();
    return adc2_poll_filtered(ADC_CHANNEL_9, &m2_filter);
}
