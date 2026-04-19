/**
  ******************************************************************************
  * @file     door_ctrl.c
  * @brief    Main door control state machine (FreeRTOS task, AT32F413R)
  ******************************************************************************
  *
  * State machine overview:
  *   INIT ��� WAIT ��� OPENING ��� OPEN_DONE ��� CLOSING ��� CLOSE_DONE ��� WAIT
  *                         ��� BLOCKED ��� (obstruction retry)
  *                         ��� ERROR   (after 3 failures or timeout)
  *
  * DIP_0: 0=normal rotation,  1=reverse rotation for D1/D2
  * DIP_1: 0=no electric lock, 1=electric lock enabled
  * DIP_2: 0=M1 only,          1=M1+M2 dual-door
  * DIP_3: unused
  ******************************************************************************
  */

#include "door_ctrl.h"
#include "board_io.h"
#include "database.h"
#include "door_pid.h"
#include "wk_system.h"
#include "at32f413_wdt.h"
#include <math.h>

/* Private constants ---------------------------------------------------------*/
#define MAX_BLOCK_RETRIES   3u
#define MAX_LOCK_RETRIES    3u

#define STARTUP_TEST_PULSE_MS     2000u
#define STARTUP_HOME_TIMEOUT_MS   8000u
#define STARTUP_POLL_MS             20u
#define STARTUP_MIN_DELTA_DEG      1.5f
#define STOP_SETTLE_MS             30u

#define ERR_STARTUP_LOCK_UNLOCK    10u
#define ERR_STARTUP_M2_DIR         11u
#define ERR_STARTUP_M2_HOME        12u
#define ERR_STARTUP_M2_HOME_SW     13u
#define ERR_STARTUP_M1_DIR         14u
#define ERR_STARTUP_M1_HOME        15u
#define ERR_STARTUP_M1_HOME_SW     16u
#define ERR_STARTUP_LOCK_LOCK      17u
#define ERR_POT_FAULT_M1           18u
#define ERR_POT_FAULT_M2           19u

#define ERR_TIMEOUT                2u   /* operation exceeded MAX_OPEN_OPERATION_TIME */
#define ERR_RUNTIME_UNLOCK         3u   /* unlock failed retries during open */
#define ERR_RUNTIME_LOCK           4u   /* lock failed retries during close */

#define REMOTE_CMD_NONE            0u
#define REMOTE_CMD_OPEN            1u
#define REMOTE_CMD_CLOSE           2u
#define REMOTE_CMD_LOCK            3u
#define REMOTE_CMD_UNLOCK          4u
#define REMOTE_CMD_CLEAR_ERROR     5u

#define TEST_LONG_PRESS_CLEAR_MS   3000u

#define CLOSE_STAGE_IDLE           0u
#define CLOSE_STAGE_M2_PRE         1u
#define CLOSE_STAGE_M1_MAIN        2u
#define CLOSE_STAGE_M2_FINAL       3u

/* Alarm timing */
#define BEEP_SHORT_MS             200u

/* Feed watchdog periodically during long startup sequences */
#define FEED_WDT()  do { wdt_counter_reload(); } while(0)
#define BEEP_LONG_MS              600u
#define BEEP_GAP_MS               200u
#define ALARM_CYCLE_GAP_MS       1000u
#define ALARM_CYCLES                2u
#define STARTUP_AUTO_OPEN_GUARD_MS 3000u

/* Private variables ---------------------------------------------------------*/
static sys_state_t  sys_state   = SYS_STATE_INIT;
static door_state_t m1_state    = DOOR_IDLE;
static door_state_t m2_state    = DOOR_IDLE;

static _pid_t pid_m1;
static _pid_t pid_m2;

static uint32_t operation_start_tick = 0u;  /* used for timeout check */
static sys_state_t blocked_from_state = SYS_STATE_WAIT;
static uint32_t blocked_retry_count = 0u;
static float m1_zero_offset_deg = 0.0f;
static float m2_zero_offset_deg = 0.0f;
static uint8_t     alarm_played  = 0u;        /* 1 once alarm fires in ERROR state */
static flag_status test_btn_prev = SET;        /* last TEST button state */

/* Non-blocking state timing helpers (advanced by door_ctrl_run each TIME_WINDOW). */
static uint8_t  close_done_phase = 0u;
static uint8_t  close_done_pot_bad = 0u;
static uint32_t close_done_tick_ms = 0u;
static uint8_t  blocked_phase = 0u;
static uint32_t blocked_tick_ms = 0u;

/* Non-blocking close flow runtime states. */
typedef enum {
    M1_CLOSE_STEP_IDLE = 0,
    M1_CLOSE_STEP_RUN,
    M1_CLOSE_STEP_HOLD_WAIT,
    M1_CLOSE_STEP_RETRY_WAIT,
    M1_CLOSE_STEP_LOCK_PUSH,
    M1_CLOSE_STEP_LOCK_WAIT,
    M1_CLOSE_STEP_LOCK_RETRY_WAIT
} m1_close_step_t;

typedef enum {
    M2_CLOSE_STEP_IDLE = 0,
    M2_CLOSE_STEP_RUN,
    M2_CLOSE_STEP_RETRY_WAIT
} m2_close_step_t;

static uint8_t        closing_flow_phase = 0u;   /* dual: 0=M2 pre, 1=M1 main, 2=M2 final */
static uint8_t        closing_started = 0u;      /* latch close-count increment per close cycle */

static m1_close_step_t m1_close_step = M1_CLOSE_STEP_IDLE;
static uint32_t        m1_close_hold_until_ms = 0u;
static uint32_t        m1_close_retry_until_ms = 0u;
static uint32_t        m1_close_lock_request_at_ms = 0u;
static uint32_t        m1_close_lock_wait_until_ms = 0u;
static uint32_t        m1_close_block_count = 0u;
static uint32_t        m1_close_lock_retry = 0u;
static uint32_t        m1_close_check_cnt = 0u;
static uint32_t        m1_close_checks_per_window = 1u;
static float           m1_close_prev_pos = 0.0f;
static uint8_t         m1_close_current_rev_duty = 0u;
static uint8_t         m1_close_home_confirm_cnt = 0u;
static uint8_t         m1_close_completed = 0u;

/* WAIT auto-open uses edge trigger with arm/disarm to avoid level retrigger after close. */
static uint8_t         wait_auto_open_armed = 1u;
static uint32_t        wait_auto_open_guard_until_ms = 0u;
static uint32_t        test_press_start_ms = 0u;

/* Auto test cycle control: each cycle = OPENING -> CLOSING -> CLOSE_DONE. */
static uint32_t        auto_test_target_cycles = 0u;
static uint32_t        auto_test_done_cycles = 0u;
static uint8_t         auto_test_active = 0u;
static uint32_t        auto_test_open_hold_until_ms = 0u;

static m2_close_step_t m2_close_step = M2_CLOSE_STEP_IDLE;
static uint8_t         m2_close_mode_preclose = 0u;
static uint8_t         m2_close_hold_active = 0u;
static uint32_t        m2_close_pre_target = 0u;
static uint32_t        m2_close_retry_until_ms = 0u;
static uint32_t        m2_close_block_count = 0u;
static uint32_t        m2_close_check_cnt = 0u;
static uint32_t        m2_close_checks_per_window = 1u;
static float           m2_close_prev_pos = 0.0f;
static uint8_t         m2_close_pre_reached = 0u;
static uint8_t         m2_close_completed = 0u;

static void closing_flow_reset(void)
{
    closing_flow_phase = 0u;
    closing_started = 0u;

    m1_close_step = M1_CLOSE_STEP_IDLE;
    m1_close_hold_until_ms = 0u;
    m1_close_retry_until_ms = 0u;
    m1_close_lock_request_at_ms = 0u;
    m1_close_lock_wait_until_ms = 0u;
    m1_close_block_count = 0u;
    m1_close_lock_retry = 0u;
    m1_close_check_cnt = 0u;
    m1_close_checks_per_window = 1u;
    m1_close_prev_pos = 0.0f;
    m1_close_home_confirm_cnt = 0u;
    m1_close_completed = 0u;

    m2_close_step = M2_CLOSE_STEP_IDLE;
    m2_close_mode_preclose = 0u;
    m2_close_hold_active = 0u;
    m2_close_pre_target = 0u;
    m2_close_retry_until_ms = 0u;
    m2_close_block_count = 0u;
    m2_close_check_cnt = 0u;
    m2_close_checks_per_window = 1u;
    m2_close_prev_pos = 0.0f;
    m2_close_pre_reached = 0u;
    m2_close_completed = 0u;
}

/* Debug watch variables (inspect in debugger Watch window) */
volatile uint32_t g_dbg_startup_stage = 0u;
volatile uint32_t g_dbg_startup_error = 0u;
volatile uint32_t g_dbg_reset_reason = 0u;   /* 0=normal, 1=IWDG, 2=WWDG, 3=software, 4=PIN, 5=POR, 99=HardFault */
volatile uint32_t g_dbg_hardfault_stage = 0u;  /* Startup stage when HardFault occurred */
volatile uint32_t g_dbg_m2_run_for_count = 0u;
volatile uint32_t g_dbg_m2_last_dir = 0u;
volatile uint32_t g_dbg_m2_last_duty = 0u;
volatile float    g_dbg_m2_p0 = 0.0f;
volatile float    g_dbg_m2_p1 = 0.0f;
volatile float    g_dbg_m2_p2 = 0.0f;
volatile float    g_dbg_m2_d01 = 0.0f;
volatile float    g_dbg_m2_d12 = 0.0f;

/* Private helpers -----------------------------------------------------------*/

/** Return current FreeRTOS tick in ms */
static uint32_t get_tick_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/** Convert PID output (possibly negative) to a clamped duty percentage (0-100) */
static uint8_t pid_duty_pct(float pid_out)
{
    float abs_pid_out = (pid_out > 0.0f) ? pid_out : -pid_out;
    if (abs_pid_out > 100.0f) abs_pid_out = 100.0f;
    return (uint8_t)abs_pid_out;
}

/** Read M1 angle in degrees */
static float m1_pos(void)
{
    float raw = adc_to_position(adc_read_m1_pot());
    float pos = raw - m1_zero_offset_deg;
    if (pos < 0.0f) {
        pos = 0.0f;
    }
    db_set_live(LD_M1_RAW_ANGLE, (uint32_t)(raw * 100.0f));
    db_set_live(LD_M1_POS, (uint32_t)(pos * 100.0f));
    return pos;
}

/** Read M2 angle in degrees.
 *  M2 POT direction is opposite to M1 (DIP_0=0: FWD decreases POT).
 *  Invert so that m2_pos() always increases as M2 opens (FWD direction).
 */
static float m2_pos(void)
{
    float raw = adc_to_position(adc_read_m2_pot());
    float pos;
    uint8_t dip = board_get_dip();
    if (dip & 0x01u) {
        /* DIP_0=1: M2 FWD increases POT — same polarity as M1 */
        pos = raw - m2_zero_offset_deg;
    } else {
        /* DIP_0=0: M2 FWD decreases POT — invert to get positive opening angle */
        pos = m2_zero_offset_deg - raw;
    }
    if (pos < 0.0f) {
        pos = 0.0f;
    }
    db_set_live(LD_M2_RAW_ANGLE, (uint32_t)(raw * 100.0f));
    db_set_live(LD_M2_POS, (uint32_t)(pos * 100.0f));
    return pos;
}

static float m1_raw_pos(void)
{
    return adc_to_position(adc_read_m1_pot());
}

static void delay_ms_feed_wdt(uint32_t delay_ms)
{
    while (delay_ms > 0u) {
        uint32_t step = (delay_ms > 20u) ? 20u : delay_ms;
        wk_delay_ms(step);
        FEED_WDT();
        delay_ms -= step;
    }
}

static void delay_rtos_ms_feed_wdt(uint32_t delay_ms)
{
    while (delay_ms > 0u) {
        uint32_t step = (delay_ms > 50u) ? 50u : delay_ms;
        vTaskDelay(pdMS_TO_TICKS(step));
        FEED_WDT();
        delay_ms -= step;
    }
}

static float m2_raw_pos(void)
{
    return adc_to_position(adc_read_m2_pot());
}

static void clear_error_state(void)
{
    alarm_played = 0u;
    LED_SET(0);
    sys_state = SYS_STATE_WAIT;
    m1_state  = DOOR_IDLE;
    m2_state  = DOOR_IDLE;
    blocked_from_state = SYS_STATE_WAIT;
    blocked_retry_count = 0u;
    blocked_phase = 0u;
    close_done_phase = 0u;
    close_done_pot_bad = 0u;
    test_press_start_ms = 0u;
    db_set_live(LD_ERROR_CODE, 0u);
    db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
    db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);
    db_set_live(LD_M1_STATE,  (uint32_t)m1_state);
    db_set_live(LD_M2_STATE,  (uint32_t)m2_state);
    db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
}

static void m1_set_zero_reference(void)
{
    m1_zero_offset_deg = m1_raw_pos();
    db_set_live(LD_M1_POS, 0u);
}

static void m2_set_zero_reference(void)
{
    m2_zero_offset_deg = m2_raw_pos();
    db_set_live(LD_M2_POS, 0u);
}

static void refresh_stopped_position(uint8_t motor_id)
{
    wk_delay_ms(STOP_SETTLE_MS);
    adc_reset_pot_filter(motor_id);

    if (motor_id == MOTOR_M1) {
        (void)m1_pos();
    } else if (motor_id == MOTOR_M2) {
        (void)m2_pos();
    }
}

static void m1_run_for(uint8_t dir, uint8_t duty, uint32_t run_ms)
{
    RELAY1_SET(1);
    motor_set_direction(MOTOR_M1, dir);
    motor_enable(MOTOR_M1);
    motor_set_pwm(MOTOR_M1, duty);
    delay_ms_feed_wdt(run_ms);
    motor_set_pwm(MOTOR_M1, 0u);
    motor_disable(MOTOR_M1);
    RELAY1_SET(0);
}

static void m2_run_for(uint8_t dir, uint8_t duty, uint32_t run_ms)
{
    g_dbg_m2_run_for_count++;
    g_dbg_m2_last_dir = dir;
    g_dbg_m2_last_duty = duty;

    RELAY3_SET(0);
    RELAY2_SET(1);

    /* Give relay contacts time to settle before driving D2. */
    wk_delay_ms(3u);
    FEED_WDT();

    motor_set_direction(MOTOR_M2, dir);

    /* Ensure PH is stable before waking DRV8242. */
    wk_delay_ms(2u);
    FEED_WDT();

    motor_enable(MOTOR_M2);

    /* DRV8242 wake-up guard time after nSLEEP release. */
    wk_delay_ms(3u);
    FEED_WDT();

    motor_set_pwm(MOTOR_M2, duty);
    delay_ms_feed_wdt(run_ms);
    motor_set_pwm(MOTOR_M2, 0u);
    motor_disable(MOTOR_M2);
    RELAY2_SET(0);
}

static uint8_t pot_raw_is_fault(float raw_deg)
{
    return (raw_deg < 30.0f || raw_deg > 330.0f) ? 1u : 0u;
}

/**
  * @brief  Compute absolute angular distance between two positions (0–360°).
  *         Handles wrap-around at 0/360° boundary, returns shortest path.
  * @param  from  Starting angle (degrees)
  * @param  to    Ending angle (degrees)
  * @retval Absolute distance [0, 180]
  */
static float angle_distance(float from, float to)
{
    float d = fabsf(to - from);
    if (d > 180.0f) {
        d = 360.0f - d;  /* Take shorter path around circle */
    }
    return d;
}

/**
  * @brief  Signed angular difference from → to, range (-180, +180].
  *         Positive = POT increasing, Negative = POT decreasing.
  */
static float angle_signed_diff(float from, float to)
{
    float d = to - from;
    if (d >  180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

static int startup_check_m1_direction(uint8_t duty)
{
    uint8_t dip  = board_get_dip();
    float p0 = m1_pos();
    float p1;
    float p2;

    if (pot_raw_is_fault(m1_raw_pos())) {
        return -2;
    }

    m1_run_for(MOTOR_DIR_FWD, duty, STARTUP_TEST_PULSE_MS);
    p1 = m1_pos();
    if (pot_raw_is_fault(m1_raw_pos())) {
        return -2;
    }
    m1_run_for(MOTOR_DIR_REV, duty, STARTUP_TEST_PULSE_MS);
    p2 = m1_pos();
    if (pot_raw_is_fault(m1_raw_pos())) {
        return -2;
    }

    /* M1 FWD: DIP_0=0 → POT increases; DIP_0=1 → POT decreases */
    float d01 = angle_signed_diff(p0, p1);
    if (dip & 0x01u) {
        /* DIP_0=1: FWD should decrease POT */
        if (d01 > -STARTUP_MIN_DELTA_DEG) return -1;
    } else {
        /* DIP_0=0: FWD should increase POT */
        if (d01 < STARTUP_MIN_DELTA_DEG) return -1;
    }
    /* REV should move back (opposite direction) */
    if (angle_distance(p1, p2) < STARTUP_MIN_DELTA_DEG) {
        return -1;
    }
    return 0;
}

static int startup_check_m2_direction(uint8_t duty)
{
    g_dbg_startup_stage = 120u;

    /* Use raw POT readings: m2_zero_offset not set yet at this stage */
    float p0 = m2_raw_pos();
    float p1;
    float p2;

    if (pot_raw_is_fault(p0)) {
        g_dbg_startup_stage = 126u;
        return -2;
    }

    m2_run_for(MOTOR_DIR_FWD, duty, STARTUP_TEST_PULSE_MS);
    g_dbg_startup_stage = 121u;
    p1 = m2_raw_pos();
    if (pot_raw_is_fault(p1)) {
        g_dbg_startup_stage = 126u;
        return -2;
    }
    m2_run_for(MOTOR_DIR_REV, duty, STARTUP_TEST_PULSE_MS);
    g_dbg_startup_stage = 122u;
    p2 = m2_raw_pos();
    if (pot_raw_is_fault(p2)) {
        g_dbg_startup_stage = 126u;
        return -2;
    }

    g_dbg_m2_p0 = p0;
    g_dbg_m2_p1 = p1;
    g_dbg_m2_p2 = p2;
    g_dbg_m2_d01 = angle_signed_diff(p0, p1);
    g_dbg_m2_d12 = angle_distance(p1, p2);

    /* M2 FWD: DIP_0=0 → POT decreases; DIP_0=1 → POT increases */
    uint8_t dip = board_get_dip();
    if (dip & 0x01u) {
        /* DIP_0=1: FWD should increase POT */
        if (g_dbg_m2_d01 < STARTUP_MIN_DELTA_DEG) {
            g_dbg_startup_stage = 123u;
            return -1;
        }
    } else {
        /* DIP_0=0: FWD should decrease POT */
        if (g_dbg_m2_d01 > -STARTUP_MIN_DELTA_DEG) {
            g_dbg_startup_stage = 123u;
            return -1;
        }
    }
    /* REV should move back (opposite direction) */
    if (g_dbg_m2_d12 < STARTUP_MIN_DELTA_DEG) {
        g_dbg_startup_stage = 124u;
        return -1;
    }

    g_dbg_startup_stage = 125u;
    return 0;
}

static int startup_home_m1(uint8_t duty)
{
    uint32_t elapsed = 0u;
    uint32_t home_zero_sample_ms = db_get_param(DF_HOME_ZERO_SAMPLE_TIME) * 1000u;
    float    pos;
    float    zero_min = (float)db_get_param(DF_M1_ZERO_MIN);
    float    zero_max = (float)db_get_param(DF_M1_ZERO_MAX);

    RELAY1_SET(1);
    motor_set_direction(MOTOR_M1, MOTOR_DIR_REV);
    motor_enable(MOTOR_M1);
    motor_set_pwm(MOTOR_M1, duty);

    while (elapsed < STARTUP_HOME_TIMEOUT_MS) {
        FEED_WDT();  /* Feed watchdog during long home wait */
        pos = m1_raw_pos();
        if (M1_HOME_READ() == RESET) {
            break;
        }
        wk_delay_ms(STARTUP_POLL_MS);  /* Use blocking delay (scheduler may not be running yet) */
        elapsed += STARTUP_POLL_MS;
    }

    if (elapsed < STARTUP_HOME_TIMEOUT_MS && M1_HOME_READ() == RESET) {
        /* Keep pressure on HOME briefly so the sampled zero reference is stable. */
        delay_ms_feed_wdt(home_zero_sample_ms);
    }

    motor_set_pwm(MOTOR_M1, 0u);
    motor_disable(MOTOR_M1);
    RELAY1_SET(0);

    pos = m1_raw_pos();
    if (pot_raw_is_fault(pos)) {
        return 3;
    }
    if (pos >= zero_min && pos <= zero_max) {
        m1_set_zero_reference();
        return 0;
    }

    if (elapsed >= STARTUP_HOME_TIMEOUT_MS) {
        return 1;
    }
    return 2;
}

static int startup_home_m2(uint8_t duty)
{
    uint32_t elapsed = 0u;
    uint32_t home_zero_sample_ms = db_get_param(DF_HOME_ZERO_SAMPLE_TIME) * 1000u;
    float    pos;
    float    zero_min = (float)db_get_param(DF_M2_ZERO_MIN);
    float    zero_max = (float)db_get_param(DF_M2_ZERO_MAX);

    RELAY2_SET(1);
    motor_set_direction(MOTOR_M2, MOTOR_DIR_REV);
    motor_enable(MOTOR_M2);
    motor_set_pwm(MOTOR_M2, duty);

    while (elapsed < STARTUP_HOME_TIMEOUT_MS) {
        FEED_WDT();  /* Feed watchdog during long home wait */
        pos = m2_raw_pos();
        if (M2_HOME_READ() == RESET) {
            break;
        }
        wk_delay_ms(STARTUP_POLL_MS);  /* Use blocking delay (scheduler may not be running yet) */
        elapsed += STARTUP_POLL_MS;
    }

    if (elapsed < STARTUP_HOME_TIMEOUT_MS && M2_HOME_READ() == RESET) {
        /* Keep pressure on HOME briefly so the sampled zero reference is stable. */
        delay_ms_feed_wdt(home_zero_sample_ms);
    }

    motor_set_pwm(MOTOR_M2, 0u);
    motor_disable(MOTOR_M2);
    RELAY2_SET(0);

    pos = m2_raw_pos();
    if (pot_raw_is_fault(pos)) {
        return 3;
    }
    if (pos >= zero_min && pos <= zero_max) {
        m2_set_zero_reference();
        return 0;
    }

    if (elapsed >= STARTUP_HOME_TIMEOUT_MS) {
        return 1;
    }
    return 2;
}

static int startup_check_lock_unlock(void)
{
    door_unlock();
    if (M3_UL_READ() == RESET && M3_LL_READ() != RESET) {
        return 0;
    }
    return -1;
}

static int startup_check_lock_lock(void)
{
    door_lock();
    if (M3_LL_READ() == RESET && M3_UL_READ() != RESET) {
        return 0;
    }
    return -1;
}

static uint32_t door_startup_sequence(void)
{
    g_dbg_startup_stage = 100u;

    uint8_t dip      = board_get_dip();
    uint8_t has_lock = (dip & 0x02u) ? 1u : 0u;
    uint8_t dual_door= (dip & 0x04u) ? 1u : 0u;
    uint8_t m1_duty  = (uint8_t)db_get_param(DF_M1_START_DUTY);
    uint8_t m2_duty  = (uint8_t)db_get_param(DF_M2_START_DUTY);
    int ret;

    if (pot_raw_is_fault(m1_raw_pos())) {
        return ERR_POT_FAULT_M1;
    }
    if (dual_door && pot_raw_is_fault(m2_raw_pos())) {
        return ERR_POT_FAULT_M2;
    }

    if (has_lock) {
        g_dbg_startup_stage = 110u;
        if (startup_check_lock_unlock() != 0) {
            return ERR_STARTUP_LOCK_UNLOCK;
        }
    }

    if (dual_door) {
        /* Mechanically relieve M2 by briefly running M1 FWD before M2 direction test. */
        g_dbg_startup_stage = 118u;
        m1_run_for(MOTOR_DIR_FWD, m1_duty, (uint32_t)db_get_param(DF_M1_STARTUP_RELIEF_MS));

        g_dbg_startup_stage = 120u;
        ret = startup_check_m2_direction(m2_duty);
        if (ret == -2) {
            return ERR_POT_FAULT_M2;
        }
        if (ret != 0) {
            return ERR_STARTUP_M2_DIR;
        }

        g_dbg_startup_stage = 130u;
        ret = startup_home_m2(m2_duty);
        if (ret == 1) {
            return ERR_STARTUP_M2_HOME;
        }
        if (ret == 2) {
            return ERR_STARTUP_M2_HOME_SW;
        }
        if (ret == 3) {
            return ERR_POT_FAULT_M2;
        }
    }

    ret = startup_check_m1_direction(m1_duty);
    g_dbg_startup_stage = 140u;
    if (ret == -2) {
        return ERR_POT_FAULT_M1;
    }
    if (ret != 0) {
        return ERR_STARTUP_M1_DIR;
    }

    ret = startup_home_m1(m1_duty);
    g_dbg_startup_stage = 150u;
    if (ret == 1) {
        return ERR_STARTUP_M1_HOME;
    }
    if (ret == 2) {
        return ERR_STARTUP_M1_HOME_SW;
    }
    if (ret == 3) {
        return ERR_POT_FAULT_M1;
    }

    if (has_lock) {
        g_dbg_startup_stage = 160u;
        if (startup_check_lock_lock() != 0) {
            return ERR_STARTUP_LOCK_LOCK;
        }
    }

    g_dbg_startup_stage = 199u;
    return 0u;
}

/* ============================================================================
 * Public API
 * ========================================================================== */

/**
  * @brief  Initialise the door control module.
  *         Called once before the FreeRTOS task starts.
  *         db_init() is called by main() before the scheduler starts.
  */
void door_ctrl_init(void)
{
    uint8_t dip = board_get_dip();
    uint32_t startup_error;

    g_dbg_startup_stage = 1u;
    g_dbg_startup_error = 0u;
    g_dbg_m2_run_for_count = 0u;

    db_set_live(LD_DIP_VALUE, dip);

//    pid_init(&pid_m1, 1.0f, 0.05f, 0.1f);
    pid_init(&pid_m1, 0.092f, 0.014f, 0.0f);
    pid_init(&pid_m2, 0.092f, 0.014f, 0.0f);
//    pid_init(&pid_m2, 1.0f, 0.05f, 0.1f);

    sys_state = SYS_STATE_WAIT;
    m1_state  = DOOR_IDLE;
    m2_state  = DOOR_IDLE;
    blocked_from_state = SYS_STATE_WAIT;
    blocked_retry_count = 0u;
    m1_zero_offset_deg = 0.0f;
    m2_zero_offset_deg = 0.0f;

    db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
    db_set_live(LD_M1_STATE,  (uint32_t)m1_state);
    db_set_live(LD_M2_STATE,  (uint32_t)m2_state);
    db_set_live(LD_BLOCK_COUNT, 0u);
    db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
    db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);
    db_set_live(LD_REMOTE_CMD, REMOTE_CMD_NONE);
    db_set_live(LD_CLOSE_STAGE, CLOSE_STAGE_IDLE);
    db_set_live(LD_AUTO_TEST_TARGET, 0u);
    db_set_live(LD_AUTO_TEST_DONE, 0u);

    g_dbg_startup_stage = 2u;
    startup_error = door_startup_sequence();
    g_dbg_startup_error = startup_error;
    if (startup_error != 0u) {
        g_dbg_startup_stage = 3u;
        m1_state  = DOOR_ERROR;
        m2_state  = DOOR_ERROR;
        sys_state = SYS_STATE_ERROR;
        db_set_live(LD_M1_STATE,  (uint32_t)m1_state);
        db_set_live(LD_M2_STATE,  (uint32_t)m2_state);
        db_set_live(LD_ERROR_CODE, startup_error);
        db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
        return;
    }

    g_dbg_startup_stage = 4u;
    db_set_live(LD_ERROR_CODE, 0u);
    wait_auto_open_armed = 0u;
    wait_auto_open_guard_until_ms = get_tick_ms() + STARTUP_AUTO_OPEN_GUARD_MS;
}

/**
  * @brief  FreeRTOS task entry point.
  *         Calls door_ctrl_run() every TIME_WINDOW ms.
  */
void door_ctrl_task(void *pvParameters)
{
    (void)pvParameters;
    door_ctrl_init();

    while (1) {
        door_ctrl_run();
        delay_rtos_ms_feed_wdt((uint32_t)db_get_param(DF_TIME_WINDOW));
    }
}

/* ============================================================================
 * Buzzer / beep
 * ========================================================================== */

/**
  * @brief  Activate buzzer for duration_ms milliseconds.
  */
void door_beep(uint16_t duration_ms)
{
    BUZZER_SET(1);
    delay_rtos_ms_feed_wdt((uint32_t)duration_ms);
    BUZZER_SET(0);
}

/* ============================================================================
 * Structured alarm  (short = 200 ms, long = 600 ms)
 *
 * Pattern table:
 *   Code  1  - S                   阻擋超限
 *   Code  2  - S S                 操作逾時
 *   Code  3  - S S S               執行中解鎖失敗
 *   Code  4  - S S S S             執行中上鎖失敗
 *   Code 10  - L S                 上電解鎖失敗
 *   Code 11  - L S S               副門方向不一致
 *   Code 12  - L S S S             副門回原點逾時
 *   Code 13  - L S S S S           副門原點開關異常
 *   Code 14  - L L S               主門方向不一致
 *   Code 15  - L L S S             主門回原點逾時
 *   Code 16  - L L S S S           主門原點開關異常
 *   Code 17  - L L L               上電上鎖失敗
 *   Code 18  - L L L S             主門POT角度異常
 *   Code 19  - L L L S S           副門POT角度異常
 * ========================================================================== */

#define BP_END  0u
#define BP_S    1u
#define BP_L    2u

typedef struct { uint32_t code; uint8_t seq[8]; } alarm_pat_t;

static const alarm_pat_t s_alarm_pats[] = {
    {  1u, { BP_S,                               BP_END, 0, 0, 0, 0, 0, 0 } },
    {  2u, { BP_S, BP_S,                         BP_END, 0, 0, 0, 0, 0    } },
    {  3u, { BP_S, BP_S, BP_S,                   BP_END, 0, 0, 0, 0       } },
    {  4u, { BP_S, BP_S, BP_S, BP_S,             BP_END, 0, 0, 0          } },
    { 10u, { BP_L, BP_S,                         BP_END, 0, 0, 0, 0, 0    } },
    { 11u, { BP_L, BP_S, BP_S,                   BP_END, 0, 0, 0, 0       } },
    { 12u, { BP_L, BP_S, BP_S, BP_S,             BP_END, 0, 0, 0          } },
    { 13u, { BP_L, BP_S, BP_S, BP_S, BP_S,       BP_END, 0, 0             } },
    { 14u, { BP_L, BP_L, BP_S,                   BP_END, 0, 0, 0, 0       } },
    { 15u, { BP_L, BP_L, BP_S, BP_S,             BP_END, 0, 0, 0          } },
    { 16u, { BP_L, BP_L, BP_S, BP_S, BP_S,       BP_END, 0, 0             } },
    { 17u, { BP_L, BP_L, BP_L,                   BP_END, 0, 0, 0, 0       } },
    { 18u, { BP_L, BP_L, BP_L, BP_S,             BP_END, 0, 0, 0          } },
    { 19u, { BP_L, BP_L, BP_L, BP_S, BP_S,       BP_END, 0, 0             } },
};

/**
  * @brief  Play structured error alarm: 2 cycles of the code's short/long pattern.
  *         LED is NOT controlled here; caller manages LED state.
  * @param  error_code  LD_ERROR_CODE value (0 → fallback S-S).
  */
void door_alarm_play(uint32_t error_code)
{
    static const uint8_t s_fallback[] = { BP_S, BP_S, BP_END, 0, 0, 0, 0, 0 };
    const uint8_t *seq = s_fallback;
    uint32_t i, j;

    for (i = 0u; i < (uint32_t)(sizeof(s_alarm_pats) / sizeof(s_alarm_pats[0])); i++) {
        if (s_alarm_pats[i].code == error_code) {
            seq = s_alarm_pats[i].seq;
            break;
        }
    }

    for (i = 0u; i < ALARM_CYCLES; i++) {
        for (j = 0u; seq[j] != BP_END; j++) {
            if (j > 0u) { delay_rtos_ms_feed_wdt(BEEP_GAP_MS); }
            door_beep((seq[j] == BP_L) ? BEEP_LONG_MS : BEEP_SHORT_MS);
        }
        if (i < (ALARM_CYCLES - 1u)) { delay_rtos_ms_feed_wdt(ALARM_CYCLE_GAP_MS); }
    }
}

/* ============================================================================
 * Electric lock
 * ========================================================================== */

/**
  * @brief  Unlock M3: PH=0, enable R3, run at M3_START_DUTY for LOCK_ACTIVE_TIME.
  */
void door_unlock(void)
{
    uint32_t active_time_ms;

    active_time_ms = db_get_param(DF_LOCK_ACTIVE_TIME) * 100u; /* param unit = 0.1s, ~~100 converts to ms */

    /* M3 lock motor direction is opposite to current D2 logical mapping. */
    motor_set_direction(MOTOR_M3, MOTOR_DIR_FWD);   /* unlock direction */
    RELAY3_SET(1);
    motor_enable(MOTOR_M3);
    motor_set_pwm(MOTOR_M3, (uint8_t)db_get_param(DF_M3_START_DUTY));

    delay_ms_feed_wdt(active_time_ms);

    motor_set_pwm(MOTOR_M3, 0u);
    motor_disable(MOTOR_M3);
    RELAY3_SET(0);
}

/**
  * @brief  Lock M3: PH=1, enable R3, run at M3_START_DUTY for LOCK_ACTIVE_TIME.
  */
void door_lock(void)
{
    uint32_t active_time_ms;

    active_time_ms = db_get_param(DF_LOCK_ACTIVE_TIME) * 100u; /* param unit = 0.1s, ~~100 converts to ms */

    /* M3 lock motor direction is opposite to current D2 logical mapping. */
    motor_set_direction(MOTOR_M3, MOTOR_DIR_REV);   /* lock direction */
    RELAY3_SET(1);
    motor_enable(MOTOR_M3);
    motor_set_pwm(MOTOR_M3, (uint8_t)db_get_param(DF_M3_START_DUTY));

    delay_ms_feed_wdt(active_time_ms);

    motor_set_pwm(MOTOR_M3, 0u);
    motor_disable(MOTOR_M3);
    RELAY3_SET(0);
}

/**
  * @brief  Check lock/unlock sensor feedback.
  * @retval 0 = OK (one sensor properly triggered),
  *         1 = no feedback (neither sensor triggered),
  *         2 = sensor error (both triggered simultaneously)
  */
int door_check_lock_error(void)
{
    flag_status ul, ll;

    /* Allow short settle time before reading sensors */
    vTaskDelay(pdMS_TO_TICKS(50));

    ul = M3_UL_READ();
    ll = M3_LL_READ();

    if (ul != RESET && ll != RESET) {
        /* Neither sensor triggered �� lock/unlock did not complete */
        return 1;
    }
    if (ul == RESET && ll == RESET) {
        /* Both triggered simultaneously �� sensor/wiring error */
        return 2;
    }
    return 0;   /* Exactly one sensor triggered �� consistent state */
}

/* ============================================================================
 * Timeout check
 * ========================================================================== */

/**
  * @brief  If current operation exceeds MAX_OPEN_OPERATION_TIME, beep + error.
  */
void door_check_timeout(void)
{
    uint32_t elapsed_ms = get_tick_ms() - operation_start_tick;
    uint32_t max_ms     = db_get_param(DF_MAX_OPEN_OPERATION_TIME) * 1000u;

    db_set_live(LD_OPERATION_TIME_MS, elapsed_ms);

    if (elapsed_ms >= max_ms) {
        db_set_live(LD_ERROR_CODE, ERR_TIMEOUT);
        door_alarm_play(ERR_TIMEOUT);
        alarm_played = 1u;
        motor_disable(MOTOR_M1);
        motor_disable(MOTOR_M2);
        sys_state = SYS_STATE_ERROR;
    }
}

/* ============================================================================
 * Obstruction detection
 * ========================================================================== */

/**
  * @brief  Check for motor obstruction during movement.
  *         If position increment < BLOCK_DETECT_ANGLE within BLOCK_DETECT_TIME,
  *         the door is considered blocked.
  * @note   Call periodically during open/close sequences.
  * @retval 1 if blocked, 0 if moving normally
  */
static int check_obstruction(float pos_now, float pos_prev)
{
    uint32_t detect_angle = db_get_param(DF_BLOCK_DETECT_ANGLE);
    float    delta;

    delta = pos_now - pos_prev;
    if (delta < 0.0f) delta = -delta;   /* absolute change */

    return (delta < (float)detect_angle) ? 1 : 0;
}

/* ============================================================================
 * Main door (M1) open / close sequences
 * ========================================================================== */

/**
  * @brief  Main door (M1) open sequence.
  *
  *  No lock (DIP_1=0): PID forward to M1_OPEN_ANGLE.
  *  With lock (DIP_1=1):
  *    1. Reverse M1 at M1_OPEN_REV_DUTY
  *    2. Unlock M3 ��� check M3_UL; retry up to 3��
  *    3. On success: PID forward to M1_OPEN_ANGLE
  *    4. On 3 failures: beep + error state
  */
void door_main_open(void)
{
    uint8_t  dip           = board_get_dip();
    uint8_t  has_lock      = (dip & 0x02u) ? 1u : 0u;
    uint8_t  dual_door     = (dip & 0x04u) ? 1u : 0u;
    uint32_t open_angle    = db_get_param(DF_M1_OPEN_ANGLE);
    uint8_t  rev_duty      = (uint8_t)db_get_param(DF_M1_OPEN_REV_DUTY);
    uint32_t retry_delay_ms= db_get_param(DF_BLOCK_RETRY_DELAY_SEC) * 1000u;
    uint32_t lock_retry    = 0u;
    float    pos, prev_pos;
    uint32_t block_count   = 0u;
    uint32_t detect_time   = db_get_param(DF_BLOCK_DETECT_TIME);
    uint32_t check_interval= (uint32_t)db_get_param(DF_TIME_WINDOW);
    uint32_t checks_per_window;
    uint32_t check_cnt;

    checks_per_window = (detect_time > check_interval) ? (detect_time / check_interval) : 1u;

    m1_state = DOOR_OPENING;
    db_set_live(LD_M1_STATE, (uint32_t)m1_state);

    /* --- Step 1: reverse-unlock M1 if electric lock is enabled --- */
    if (has_lock) {
        for (lock_retry = 0u; lock_retry < MAX_LOCK_RETRIES; lock_retry++) {

            /* Brief reverse to relieve mechanical pressure on the lock */
            motor_enable(MOTOR_M1);
            motor_set_direction(MOTOR_M1, MOTOR_DIR_REV);
            motor_set_pwm(MOTOR_M1, rev_duty);
            delay_rtos_ms_feed_wdt(200u);
            motor_set_pwm(MOTOR_M1, 0u);
            motor_disable(MOTOR_M1);

            /* Activate lock motor to unlock */
            door_unlock();

            /* Check unlock sensor (active-low): UL asserted, LL deasserted. */
            if (M3_UL_READ() == RESET && M3_LL_READ() != RESET) {
                break;  /* unlock confirmed */
            }

            if (lock_retry < (MAX_LOCK_RETRIES - 1u)) {
                /* Increase reverse pressure on each retry */
                rev_duty += (uint8_t)db_get_param(DF_M1_OPEN_REV_DUTY_DELTA);
                delay_rtos_ms_feed_wdt(retry_delay_ms);
            }
        }

        if (lock_retry >= MAX_LOCK_RETRIES) {
            /* Failed to unlock after 3 attempts */
            db_set_live(LD_LOCK_RETRY_COUNT, lock_retry);
            db_set_live(LD_ERROR_CODE, ERR_RUNTIME_UNLOCK);
            door_alarm_play(ERR_RUNTIME_UNLOCK);
            alarm_played = 1u;
            m1_state  = DOOR_ERROR;
            sys_state = SYS_STATE_ERROR;
            db_set_live(LD_M1_STATE, (uint32_t)m1_state);
            db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
            return;
        }
    }

    /* --- Step 2: PID forward to open angle --- */
    pid_init(&pid_m1, 1.0f, 0.05f, 0.1f);

    motor_set_direction(MOTOR_M1, MOTOR_DIR_FWD);
    motor_enable(MOTOR_M1);
    RELAY1_SET(1);

    block_count = 0u;
    prev_pos    = m1_pos();
    check_cnt   = 0u;

    while (1) {
        pos = m1_pos();

        if (pos >= (float)open_angle) {
            break;  /* reached target */
        }

        /* PID compute */
        float pid_out = pid_compute(&pid_m1, (float)open_angle, pos, (float)check_interval / 1000.0f);
        uint8_t duty = pid_duty_pct(pid_out);
        motor_set_pwm(MOTOR_M1, duty);

        vTaskDelay(pdMS_TO_TICKS(check_interval));
        FEED_WDT();
        door_check_timeout();
        if (sys_state == SYS_STATE_ERROR) {
            motor_disable(MOTOR_M1);
            RELAY1_SET(0);
            return;
        }

        /* Obstruction detection: sample every check_interval, check every detect_time */
        check_cnt++;
        if (check_cnt >= checks_per_window) {
            /* In dual-door mode, when M2 has started opening, avoid transient false block on M1. */
            if (dual_door && m2_state == DOOR_OPENING) {
                prev_pos = pos;
                check_cnt = 0u;
                block_count = 0u;
                continue;
            }

            if (check_obstruction(pos, prev_pos)) {
                block_count++;
                db_set_live(LD_BLOCK_COUNT, block_count);
                if (block_count >= MAX_BLOCK_RETRIES) {
                    motor_set_pwm(MOTOR_M1, 0u);
                    motor_disable(MOTOR_M1);
                    RELAY1_SET(0);
                    door_beep(BEEP_SHORT_MS);   /* single alert: entering blocked-retry */
                    m1_state  = DOOR_BLOCKED;
                    blocked_from_state = SYS_STATE_OPENING;
                    sys_state = SYS_STATE_BLOCKED;
                    db_set_live(LD_M1_STATE,  (uint32_t)m1_state);
                    db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);
                    db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
                    return;
                }
                /* Wait and retry */
                motor_set_pwm(MOTOR_M1, 0u);
                delay_rtos_ms_feed_wdt(retry_delay_ms);
                motor_set_pwm(MOTOR_M1, (uint8_t)db_get_param(DF_M1_START_DUTY));
            } else {
                block_count = 0u;  /* moving normally �� reset consecutive counter */
            }
            prev_pos  = pos;
            check_cnt = 0u;
        }
    }

    motor_set_pwm(MOTOR_M1, 0u);
    motor_disable(MOTOR_M1);
    RELAY1_SET(0);
    refresh_stopped_position(MOTOR_M1);

    m1_state = DOOR_OPEN;
    db_set_live(LD_M1_STATE, (uint32_t)m1_state);
}

/**
  * @brief  Main door (M1) close sequence.
  *
    *  No lock (DIP_1=0): PID reverse until HOME is active or position is within DF_M1_ZERO_ERROR, then hold reverse duty and finish.
  *  With lock (DIP_1=1):
    *    1. PID reverse to close condition
    *    2. Hold reverse duty for DF_M1_CLOSE_HOLD_TIME
    *    3. Lock M3 and check M3_LL; retry up to MAX_LOCK_RETRIES
  */
void door_main_close(void)
{
    uint8_t  dip            = board_get_dip();
    uint8_t  has_lock       = (dip & 0x02u) ? 1u : 0u;
    uint32_t zero_error     = db_get_param(DF_M1_ZERO_ERROR);
    uint32_t close_hold_ms  = db_get_param(DF_M1_CLOSE_HOLD_TIME) * 1000u;
    uint32_t retry_delay_ms = db_get_param(DF_BLOCK_RETRY_DELAY_SEC) * 1000u;
    uint32_t detect_time    = db_get_param(DF_BLOCK_DETECT_TIME);
    uint32_t check_interval = (uint32_t)db_get_param(DF_TIME_WINDOW);
    uint32_t close_lock_delay_ms = 0u;
    float    pos;
    float    raw_pos;

    if (has_lock && close_hold_ms > 0u) {
        close_lock_delay_ms = close_hold_ms / 2u;
        if (close_lock_delay_ms < check_interval) {
            close_lock_delay_ms = check_interval;
        }
        if (close_lock_delay_ms >= close_hold_ms) {
            close_lock_delay_ms = close_hold_ms - 1u;
        }
    }

    if (m1_close_step == M1_CLOSE_STEP_IDLE) {
        m1_close_completed = 0u;
        m1_close_block_count = 0u;
        m1_close_lock_retry = 0u;
        m1_close_check_cnt = 0u;
        m1_close_hold_until_ms = 0u;
        m1_close_lock_request_at_ms = 0u;
        m1_close_lock_wait_until_ms = 0u;
        m1_close_checks_per_window = (detect_time > check_interval) ? (detect_time / check_interval) : 1u;
        m1_close_prev_pos = m1_pos();
        m1_close_home_confirm_cnt = 0u;
        m1_close_current_rev_duty = (uint8_t)db_get_param(DF_M1_CLOSE_REV_DUTY);

        m1_state = DOOR_CLOSING;
        db_set_live(LD_M1_STATE, (uint32_t)m1_state);

        pid_init(&pid_m1, 1.0f, 0.05f, 0.1f);
        motor_set_direction(MOTOR_M1, MOTOR_DIR_REV);
        motor_enable(MOTOR_M1);
        RELAY1_SET(1);
        m1_close_step = M1_CLOSE_STEP_RUN;
        return;
    }

    if (m1_close_step == M1_CLOSE_STEP_HOLD_WAIT) {
        door_check_timeout();
        if (sys_state == SYS_STATE_ERROR) {
            motor_set_pwm(MOTOR_M1, 0u);
            motor_disable(MOTOR_M1);
            RELAY1_SET(0);
            m1_close_step = M1_CLOSE_STEP_IDLE;
            return;
        }

        if ((int32_t)(get_tick_ms() - m1_close_hold_until_ms) < 0) {
            return;
        }

        motor_set_pwm(MOTOR_M1, 0u);
        motor_disable(MOTOR_M1);
        RELAY1_SET(0);
        adc_reset_pot_filter(MOTOR_M1);
        (void)m1_pos();

        m1_state = DOOR_CLOSED;
        db_set_live(LD_M1_STATE, (uint32_t)m1_state);
        m1_close_completed = 1u;
        m1_close_step = M1_CLOSE_STEP_IDLE;
        return;
    }

    if (m1_close_step == M1_CLOSE_STEP_RETRY_WAIT) {
        if ((int32_t)(get_tick_ms() - m1_close_retry_until_ms) < 0) {
            return;
        }
        motor_set_pwm(MOTOR_M1, (uint8_t)db_get_param(DF_M1_START_DUTY));
        m1_close_step = M1_CLOSE_STEP_RUN;
        return;
    }

    if (m1_close_step == M1_CLOSE_STEP_LOCK_WAIT) {
        if ((int32_t)(get_tick_ms() - m1_close_lock_wait_until_ms) < 0) {
            return;
        }

        /* Keep M1 reverse hold active while lock motor engages to prevent rebound. */
        door_lock();
        /* Check lock sensor (active-low): LL asserted, UL deasserted. */
        if (M3_LL_READ() == RESET && M3_UL_READ() != RESET) {
            m1_close_step = M1_CLOSE_STEP_HOLD_WAIT;
            return;
        }

        m1_close_lock_retry++;
        if (m1_close_lock_retry >= MAX_LOCK_RETRIES) {
            db_set_live(LD_LOCK_RETRY_COUNT, m1_close_lock_retry);
            db_set_live(LD_ERROR_CODE, ERR_RUNTIME_LOCK);
            door_alarm_play(ERR_RUNTIME_LOCK);
            alarm_played = 1u;
            m1_state  = DOOR_ERROR;
            sys_state = SYS_STATE_ERROR;
            db_set_live(LD_M1_STATE,  (uint32_t)m1_state);
            db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
            m1_close_step = M1_CLOSE_STEP_IDLE;
            return;
        }

        m1_close_retry_until_ms = get_tick_ms() + retry_delay_ms;
        m1_close_step = M1_CLOSE_STEP_LOCK_RETRY_WAIT;
        return;
    }

    if (m1_close_step == M1_CLOSE_STEP_LOCK_RETRY_WAIT) {
        if ((int32_t)(get_tick_ms() - m1_close_retry_until_ms) < 0) {
            return;
        }
        /* Increase reverse hold pressure on each retry */
        m1_close_current_rev_duty += (uint8_t)db_get_param(DF_M1_CLOSE_REV_DUTY_DELTA);
        m1_close_step = M1_CLOSE_STEP_LOCK_PUSH;
    }

    if (m1_close_step == M1_CLOSE_STEP_LOCK_PUSH) {
        /* Keep M1 in reverse hold first, then engage the lock during the hold window. */
        motor_enable(MOTOR_M1);
        motor_set_direction(MOTOR_M1, MOTOR_DIR_REV);
        motor_set_pwm(MOTOR_M1, m1_close_current_rev_duty);

        if ((int32_t)(get_tick_ms() - m1_close_lock_request_at_ms) < 0) {
            return;
        }

        m1_close_lock_wait_until_ms = get_tick_ms();
        m1_close_step = M1_CLOSE_STEP_LOCK_WAIT;
        return;
    }

    /* M1_CLOSE_STEP_RUN */
    pos = m1_pos();
    raw_pos = m1_raw_pos();

    {
        uint8_t home_hit = (M1_HOME_READ() == RESET) ? 1u : 0u;
        uint8_t pos_hit = (pos <= (float)zero_error) ? 1u : 0u;

        if (home_hit || pos_hit) {
            if (m1_close_home_confirm_cnt < 2u) {
                m1_close_home_confirm_cnt++;
            }
        } else {
            m1_close_home_confirm_cnt = 0u;
        }

        if (m1_close_home_confirm_cnt >= 2u) {
            motor_set_direction(MOTOR_M1, MOTOR_DIR_REV);
            motor_enable(MOTOR_M1);
            motor_set_pwm(MOTOR_M1, m1_close_current_rev_duty);
            m1_close_hold_until_ms = get_tick_ms() + close_hold_ms;
            if (has_lock) {
                m1_close_lock_request_at_ms = get_tick_ms() + close_lock_delay_ms;
                m1_close_step = M1_CLOSE_STEP_LOCK_PUSH;
            } else {
                m1_close_step = M1_CLOSE_STEP_HOLD_WAIT;
            }
            return;
        }
    }

    {
        float pid_out = pid_compute(&pid_m1, 0.0f, pos, (float)check_interval / 1000.0f);
        uint8_t duty = pid_duty_pct(pid_out);
        motor_set_pwm(MOTOR_M1, duty);
    }

    door_check_timeout();
    if (sys_state == SYS_STATE_ERROR) {
        motor_disable(MOTOR_M1);
        RELAY1_SET(0);
        m1_close_step = M1_CLOSE_STEP_IDLE;
        return;
    }

    m1_close_check_cnt++;
    if (m1_close_check_cnt >= m1_close_checks_per_window) {
        if (check_obstruction(pos, m1_close_prev_pos)) {
            m1_close_block_count++;
            db_set_live(LD_BLOCK_COUNT, m1_close_block_count);
            if (m1_close_block_count >= MAX_BLOCK_RETRIES) {
                motor_set_pwm(MOTOR_M1, 0u);
                motor_disable(MOTOR_M1);
                RELAY1_SET(0);
                door_beep(BEEP_SHORT_MS);
                m1_state  = DOOR_BLOCKED;
                blocked_from_state = SYS_STATE_CLOSING;
                sys_state = SYS_STATE_BLOCKED;
                db_set_live(LD_M1_STATE,  (uint32_t)m1_state);
                db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);
                db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
                m1_close_step = M1_CLOSE_STEP_IDLE;
                return;
            }
            motor_set_pwm(MOTOR_M1, 0u);
            m1_close_retry_until_ms = get_tick_ms() + retry_delay_ms;
            m1_close_step = M1_CLOSE_STEP_RETRY_WAIT;
        } else {
            m1_close_block_count = 0u;
        }
        m1_close_prev_pos = pos;
        m1_close_check_cnt = 0u;
    }
}

/* ============================================================================
 * Secondary door (M2) open / close sequences
 * ========================================================================== */

/**
  * @brief  Secondary door (M2) open sequence.
  *         Wait until (M1_POS ��� M2_POS) > OPEN_DIFF_ANGLE, then PID forward.
  */
void door_sub_open(void)
{
    uint32_t diff_angle    = db_get_param(DF_OPEN_DIFF_ANGLE);
    uint32_t open_angle    = db_get_param(DF_M2_OPEN_ANGLE);
    uint32_t retry_delay_ms= db_get_param(DF_BLOCK_RETRY_DELAY_SEC) * 1000u;
    uint32_t check_interval= (uint32_t)db_get_param(DF_TIME_WINDOW);
    float    p1, p2, pos;
    uint32_t block_count   = 0u;
    uint32_t detect_time   = db_get_param(DF_BLOCK_DETECT_TIME);
    uint32_t checks_per_window;
    uint32_t check_cnt;
    float    prev_pos;

    checks_per_window = (detect_time > check_interval) ? (detect_time / check_interval) : 1u;

    m2_state = DOOR_OPENING;
    db_set_live(LD_M2_STATE, (uint32_t)m2_state);

    /* Wait for M1 to be ahead of M2 by OPEN_DIFF_ANGLE */
    while (1) {
        p1 = m1_pos();
        p2 = m2_pos();
        if ((p1 - p2) >= (float)diff_angle) break;
        door_check_timeout();
        if (sys_state == SYS_STATE_ERROR || sys_state == SYS_STATE_BLOCKED) return;
        vTaskDelay(pdMS_TO_TICKS(check_interval));
        FEED_WDT();
    }

    pid_init(&pid_m2, 1.0f, 0.05f, 0.1f);
    motor_set_direction(MOTOR_M2, MOTOR_DIR_FWD);
    motor_enable(MOTOR_M2);
    RELAY2_SET(1);

    block_count = 0u;
    prev_pos    = m2_pos();
    check_cnt   = 0u;

    while (1) {
        pos = m2_pos();

        if (pos >= (float)open_angle) break;

        float pid_out = pid_compute(&pid_m2, (float)open_angle, pos, (float)check_interval / 1000.0f);
        uint8_t duty = pid_duty_pct(pid_out);
        motor_set_pwm(MOTOR_M2, duty);

        vTaskDelay(pdMS_TO_TICKS(check_interval));
        FEED_WDT();
        door_check_timeout();
        if (sys_state == SYS_STATE_ERROR || sys_state == SYS_STATE_BLOCKED) {
            motor_disable(MOTOR_M2);
            RELAY2_SET(0);
            return;
        }

        check_cnt++;
        if (check_cnt >= checks_per_window) {
            if (check_obstruction(pos, prev_pos)) {
                block_count++;
                db_set_live(LD_BLOCK_COUNT, block_count);
                if (block_count >= MAX_BLOCK_RETRIES) {
                    motor_set_pwm(MOTOR_M2, 0u);
                    motor_disable(MOTOR_M2);
                    RELAY2_SET(0);
                    door_beep(BEEP_SHORT_MS);   /* single alert: entering blocked-retry */
                    m2_state  = DOOR_BLOCKED;
                    blocked_from_state = SYS_STATE_OPENING;
                    sys_state = SYS_STATE_BLOCKED;
                    db_set_live(LD_M2_STATE,  (uint32_t)m2_state);
                    db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);
                    db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
                    return;
                }
                motor_set_pwm(MOTOR_M2, 0u);
                delay_rtos_ms_feed_wdt(retry_delay_ms);
                motor_set_pwm(MOTOR_M2, (uint8_t)db_get_param(DF_M2_START_DUTY));
            } else {
                block_count = 0u;  /* moving normally �� reset consecutive counter */
            }
            prev_pos  = pos;
            check_cnt = 0u;
        }
    }

    motor_set_pwm(MOTOR_M2, 0u);
    motor_disable(MOTOR_M2);
    RELAY2_SET(0);
    refresh_stopped_position(MOTOR_M2);

    m2_state = DOOR_OPEN;
    db_set_live(LD_M2_STATE, (uint32_t)m2_state);
}

/**
  * @brief  Secondary door (M2) close sequence.
    *         Dual-door staged behavior:
    *           1) Before main door closes: pre-close to
    *              (DF_M2_OPEN_ANGLE - DF_OPEN_DIFF_ANGLE).
    *           2) After main door starts/finishes closing: close to home.
  */
void door_sub_close(void)
{
    uint32_t diff_angle     = db_get_param(DF_OPEN_DIFF_ANGLE);
    uint32_t m2_open_angle  = db_get_param(DF_M2_OPEN_ANGLE);
    uint32_t zero_error     = db_get_param(DF_M2_ZERO_ERROR);
    uint32_t retry_delay_ms = db_get_param(DF_BLOCK_RETRY_DELAY_SEC) * 1000u;
    uint32_t check_interval = (uint32_t)db_get_param(DF_TIME_WINDOW);
    uint32_t detect_time    = db_get_param(DF_BLOCK_DETECT_TIME);
    float    pos;

    if (m2_close_step == M2_CLOSE_STEP_IDLE) {
        m2_close_completed = 0u;
        m2_close_pre_reached = 0u;
        m2_close_block_count = 0u;
        m2_close_check_cnt = 0u;
        m2_close_checks_per_window = (detect_time > check_interval) ? (detect_time / check_interval) : 1u;
        m2_close_prev_pos = m2_pos();

        m2_close_mode_preclose = (m1_state != DOOR_CLOSING && m1_state != DOOR_CLOSED) ? 1u : 0u;
        m2_close_pre_target = (m2_open_angle > diff_angle) ? (m2_open_angle - diff_angle) : 0u;

        m2_state = DOOR_CLOSING;
        db_set_live(LD_M2_STATE, (uint32_t)m2_state);

        pid_init(&pid_m2, 1.0f, 0.05f, 0.1f);
        motor_set_direction(MOTOR_M2, MOTOR_DIR_REV);
        motor_enable(MOTOR_M2);
        RELAY2_SET(1);

        m2_close_step = M2_CLOSE_STEP_RUN;
        return;
    }

    if (m2_close_step == M2_CLOSE_STEP_RETRY_WAIT) {
        if ((int32_t)(get_tick_ms() - m2_close_retry_until_ms) < 0) {
            return;
        }
        motor_set_pwm(MOTOR_M2, (uint8_t)db_get_param(DF_M2_START_DUTY));
        m2_close_step = M2_CLOSE_STEP_RUN;
        return;
    }

    pos = m2_pos();

    if (m2_close_mode_preclose) {
        if (pos <= (float)m2_close_pre_target || M2_HOME_READ() == RESET) {
            /* Pre-close threshold reached: allow M1 to start, but keep M2 close state running. */
            m2_close_mode_preclose = 0u;
            m2_close_pre_reached = 1u;
            /* Reset obstruction baseline at phase switch to avoid false blocked/retry due to boundary pause. */
            m2_close_prev_pos = pos;
            m2_close_check_cnt = 0u;
        }
    } else {
        if (M2_HOME_READ() == RESET || pos <= (float)zero_error) {
            motor_set_pwm(MOTOR_M2, 0u);
            motor_disable(MOTOR_M2);
            RELAY2_SET(0);
            adc_reset_pot_filter(MOTOR_M2);
            (void)m2_pos();

            m2_close_hold_active = 0u;
            m2_state = DOOR_CLOSED;
            db_set_live(LD_M2_STATE, (uint32_t)m2_state);
            m2_close_completed = 1u;
            m2_close_step = M2_CLOSE_STEP_IDLE;
            return;
        }
    }

    {
        float pid_out = pid_compute(&pid_m2, 0.0f, pos, (float)check_interval / 1000.0f);
        uint8_t duty = pid_duty_pct(pid_out);
        motor_set_pwm(MOTOR_M2, duty);
    }

    door_check_timeout();
    if (sys_state == SYS_STATE_ERROR) {
        motor_disable(MOTOR_M2);
        RELAY2_SET(0);
        m2_close_step = M2_CLOSE_STEP_IDLE;
        return;
    }

    m2_close_check_cnt++;
    if (m2_close_check_cnt >= m2_close_checks_per_window) {
        if (check_obstruction(pos, m2_close_prev_pos)) {
            m2_close_block_count++;
            db_set_live(LD_BLOCK_COUNT, m2_close_block_count);
            if (m2_close_block_count >= MAX_BLOCK_RETRIES) {
                motor_set_pwm(MOTOR_M2, 0u);
                motor_disable(MOTOR_M2);
                RELAY2_SET(0);
                door_beep(BEEP_SHORT_MS);
                m2_state  = DOOR_BLOCKED;
                blocked_from_state = SYS_STATE_CLOSING;
                sys_state = SYS_STATE_BLOCKED;
                db_set_live(LD_M2_STATE,  (uint32_t)m2_state);
                db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);
                db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
                m2_close_step = M2_CLOSE_STEP_IDLE;
                return;
            }
            motor_set_pwm(MOTOR_M2, 0u);
            m2_close_retry_until_ms = get_tick_ms() + retry_delay_ms;
            m2_close_step = M2_CLOSE_STEP_RETRY_WAIT;
        } else {
            m2_close_block_count = 0u;
        }
        m2_close_prev_pos = pos;
        m2_close_check_cnt = 0u;
    }
}

/* ============================================================================
 * Obstruction check (public, for external call)
 * ========================================================================== */

/**
  * @brief  Wrapper for obstacle detection �� checks M1 and M2 movement.
  */
void door_block_detect(void)
{
    /* Obstruction detection is integrated into the open/close sequences.
       This function is provided for external polling if needed. */
}

static void door_sub_open_worker(void *arg)
{
    (void)arg;
    door_sub_open();
    vTaskDelete(NULL);
}

/* ============================================================================
 * Main state machine dispatcher
 * ========================================================================== */

/**
  * @brief  Main state machine �� called every TIME_WINDOW ms.
  */
void door_ctrl_run(void)
{
    uint8_t  dip        = board_get_dip();
    uint8_t  dual_door  = (dip & 0x04u) ? 1u : 0u;
    uint32_t cfg_auto_cycles;
    uint32_t cfg_auto_open_hold_sec;
    float    pos;

    /* Top-level guard: even if a branch misses a feed point, reload once here. */
    FEED_WDT();

    db_set_live(LD_SYS_STATE, (uint32_t)sys_state);

    cfg_auto_cycles = db_get_param(DF_AUTO_TEST_CYCLES);
    cfg_auto_open_hold_sec = db_get_param(DF_AUTO_TEST_OPEN_HOLD_SEC);
    if (cfg_auto_cycles == 0u) {
        auto_test_active = 0u;
        auto_test_target_cycles = 0u;
        auto_test_done_cycles = 0u;
        auto_test_open_hold_until_ms = 0u;
    } else if (!auto_test_active || auto_test_target_cycles != cfg_auto_cycles) {
        auto_test_active = 1u;
        auto_test_target_cycles = cfg_auto_cycles;
        auto_test_done_cycles = 0u;
        auto_test_open_hold_until_ms = 0u;
    }
    db_set_live(LD_AUTO_TEST_TARGET, auto_test_target_cycles);
    db_set_live(LD_AUTO_TEST_DONE, auto_test_done_cycles);

    /* Keep live position values fresh for host polling in every state. */
    pos = m1_pos();
    (void)m2_pos();

    /* POT window fault guard: midpoint-zero strategy expects normal readings away from 0/360 edges. */
    {
        uint8_t m1_fault = pot_raw_is_fault(m1_raw_pos());
        uint8_t m2_fault = dual_door ? pot_raw_is_fault(m2_raw_pos()) : 0u;

        if (m1_fault || m2_fault) {
            uint32_t err = m2_fault ? ERR_POT_FAULT_M2 : ERR_POT_FAULT_M1;
            db_set_live(LD_ERROR_CODE, err);
            m1_state  = DOOR_ERROR;
            m2_state  = DOOR_ERROR;
            sys_state = SYS_STATE_ERROR;
            db_set_live(LD_M1_STATE,  (uint32_t)m1_state);
            db_set_live(LD_M2_STATE,  (uint32_t)m2_state);
            db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
            return;
        }
    }

    /* TEST button short-press: replay alarm when an error is active */
    {
        flag_status test_now = TEST_READ();
        if (test_now == RESET && test_btn_prev == SET) {
            /* Falling edge detected */
            uint32_t err = db_get_live(LD_ERROR_CODE);
            if (err != 0u) {
                door_alarm_play(err);
            }
        }
        test_btn_prev = test_now;
    }
{
        uint32_t remote_cmd = db_get_live(LD_REMOTE_CMD);
        if (remote_cmd != REMOTE_CMD_NONE) {
            uint8_t has_lock = (dip & 0x02u) ? 1u : 0u;

            switch (remote_cmd) {
            case REMOTE_CMD_OPEN:
                if (sys_state == SYS_STATE_WAIT || sys_state == SYS_STATE_CLOSE_DONE) {
                    db_set_live(LD_BLOCK_COUNT, 0u);
                    blocked_retry_count = 0u;
                    db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
                    db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)SYS_STATE_WAIT);
                    operation_start_tick = get_tick_ms();
                    sys_state = SYS_STATE_OPENING;
                }
                break;

            case REMOTE_CMD_CLOSE:
                if (sys_state == SYS_STATE_OPEN_DONE ||
                    (sys_state == SYS_STATE_WAIT && (m1_state == DOOR_OPEN || m2_state == DOOR_OPEN))) {
                    db_set_live(LD_BLOCK_COUNT, 0u);
                    blocked_retry_count = 0u;
                    db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
                    db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)SYS_STATE_WAIT);
                    operation_start_tick = get_tick_ms();
                    closing_flow_reset();
                    sys_state = SYS_STATE_CLOSING;
                }
                break;

            case REMOTE_CMD_LOCK:
                if (has_lock && (sys_state == SYS_STATE_WAIT || sys_state == SYS_STATE_OPEN_DONE || sys_state == SYS_STATE_CLOSE_DONE)) {
                    door_lock();
                }
                break;

            case REMOTE_CMD_UNLOCK:
                if (has_lock && (sys_state == SYS_STATE_WAIT || sys_state == SYS_STATE_OPEN_DONE || sys_state == SYS_STATE_CLOSE_DONE)) {
                    door_unlock();
                }
                break;

            case REMOTE_CMD_CLEAR_ERROR:
                if (sys_state == SYS_STATE_ERROR) {
                    clear_error_state();
                }
                break;

            default:
                break;
            }

            db_set_live(LD_REMOTE_CMD, REMOTE_CMD_NONE);
        }
    }

    
    switch (sys_state) {

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_INIT:
        db_set_live(LD_CLOSE_STAGE, CLOSE_STAGE_IDLE);
        close_done_phase = 0u;
        close_done_pot_bad = 0u;
        blocked_phase = 0u;
        wait_auto_open_armed = (pos < (float)db_get_param(DF_OPEN_TRIGGER_ANGLE)) ? 1u : 0u;
        sys_state = SYS_STATE_WAIT;
        break;

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_WAIT:
        db_set_live(LD_CLOSE_STAGE, CLOSE_STAGE_IDLE);
        close_done_phase = 0u;
        close_done_pot_bad = 0u;
        blocked_phase = 0u;

        if (auto_test_active) {
            wait_auto_open_armed = 0u;
            db_set_live(LD_BLOCK_COUNT, 0u);
            blocked_retry_count = 0u;
            db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
            db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)SYS_STATE_WAIT);
            operation_start_tick = get_tick_ms();
            sys_state = SYS_STATE_OPENING;
            break;
        }

        if ((int32_t)(get_tick_ms() - wait_auto_open_guard_until_ms) >= 0) {
            float trigger = (float)db_get_param(DF_OPEN_TRIGGER_ANGLE);
            float arm_threshold = trigger - 2.0f;
            if (arm_threshold < 0.0f) {
                arm_threshold = 0.0f;
            }

            /* Arm only after position falls below threshold; then trigger on upward crossing. */
            if (pos <= arm_threshold) {
                wait_auto_open_armed = 1u;
            }

            /* Auto-open trigger: upward crossing past trigger angle (edge-triggered). */
            if (wait_auto_open_armed && pos >= trigger) {
                wait_auto_open_armed = 0u;
                db_set_live(LD_BLOCK_COUNT, 0u);
                blocked_retry_count = 0u;
                db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
                db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)SYS_STATE_WAIT);
                operation_start_tick = get_tick_ms();
                sys_state = SYS_STATE_OPENING;
                break;
            }
        }

        /* TG_OPEN active (active-low) */
        if (TG_OPEN_READ() == RESET) {
            wait_auto_open_armed = 0u;
            db_set_live(LD_BLOCK_COUNT, 0u);
            blocked_retry_count = 0u;
            db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
            db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)SYS_STATE_WAIT);
            operation_start_tick = get_tick_ms();
            sys_state = SYS_STATE_OPENING;
        }
        break;

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_OPENING:
        db_set_live(LD_CLOSE_STAGE, CLOSE_STAGE_IDLE);
        db_set_live(LD_OPEN_COUNT, db_get_live(LD_OPEN_COUNT) + 1u);
        LED_SET(1);

        if (dual_door) {
            /* Run M1 and M2 open concurrently; M2 starts after angle-diff condition. */
            TaskHandle_t sub_open_task = NULL;
            if (xTaskCreate(door_sub_open_worker,
                            "d2open",
                            320,
                            NULL,
                            tskIDLE_PRIORITY + 1,
                            &sub_open_task) == pdPASS) {
                door_main_open();

                while (sys_state != SYS_STATE_ERROR &&
                       sys_state != SYS_STATE_BLOCKED &&
                       (m2_state == DOOR_IDLE || m2_state == DOOR_OPENING)) {
                    delay_rtos_ms_feed_wdt((uint32_t)db_get_param(DF_TIME_WINDOW));
                }
            } else {
                /* Fallback to sequential flow if task creation fails. */
                door_main_open();
                if (sys_state != SYS_STATE_ERROR && sys_state != SYS_STATE_BLOCKED) {
                    door_sub_open();
                }
            }
        } else {
            door_main_open();
        }

        if (sys_state != SYS_STATE_ERROR && sys_state != SYS_STATE_BLOCKED) {
            sys_state = SYS_STATE_OPEN_DONE;
        }
        break;

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_OPEN_DONE:
        db_set_live(LD_CLOSE_STAGE, CLOSE_STAGE_IDLE);
        SET_OPEN_DONE(1);
        LED_SET(1);

        if (auto_test_active) {
            if (auto_test_open_hold_until_ms == 0u) {
                auto_test_open_hold_until_ms = get_tick_ms() + (cfg_auto_open_hold_sec * 1000u);
            }
            if ((int32_t)(get_tick_ms() - auto_test_open_hold_until_ms) < 0) {
                break;
            }
            auto_test_open_hold_until_ms = 0u;
            SET_OPEN_DONE(0);
            db_set_live(LD_BLOCK_COUNT, 0u);
            blocked_retry_count = 0u;
            db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
            db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)SYS_STATE_WAIT);
            operation_start_tick = get_tick_ms();
            closing_flow_reset();
            sys_state = SYS_STATE_CLOSING;
            break;
        }

        /* Hold OPEN_DONE high until close trigger */
        if (TG_CLOSE_READ() == RESET) {
            auto_test_open_hold_until_ms = 0u;
            SET_OPEN_DONE(0);
            db_set_live(LD_BLOCK_COUNT, 0u);
            blocked_retry_count = 0u;
            db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
            db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)SYS_STATE_WAIT);
            operation_start_tick = get_tick_ms();
            closing_flow_reset();
            sys_state = SYS_STATE_CLOSING;
        }
        break;

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_CLOSING:
        SET_OPEN_DONE(0);
        if (!closing_started) {
            db_set_live(LD_CLOSE_COUNT, db_get_live(LD_CLOSE_COUNT) + 1u);
            closing_started = 1u;
        }

        if (dual_door) {
            /* Keep M2 close controller running continuously; M1 starts when M2 pre-close threshold is reached. */
            door_sub_close();
            if (sys_state == SYS_STATE_ERROR || sys_state == SYS_STATE_BLOCKED) {
                break;
            }

            if (closing_flow_phase == 0u) {
                db_set_live(LD_CLOSE_STAGE, CLOSE_STAGE_M2_PRE);
                if (m2_close_pre_reached) {
                    closing_flow_phase = 1u;
                }
            }

            if (closing_flow_phase >= 1u) {
                db_set_live(LD_CLOSE_STAGE, (closing_flow_phase == 1u) ? CLOSE_STAGE_M1_MAIN : CLOSE_STAGE_M2_FINAL);
                if (!m1_close_completed) {
                    door_main_close();
                    if (sys_state == SYS_STATE_ERROR || sys_state == SYS_STATE_BLOCKED) {
                        break;
                    }
                    if (m1_close_completed) {
                        closing_flow_phase = 2u;
                    }
                }
            }

            if (m1_close_completed && m2_close_completed) {
                closing_flow_reset();
                sys_state = SYS_STATE_CLOSE_DONE;
            }
        } else {
            db_set_live(LD_CLOSE_STAGE, CLOSE_STAGE_M1_MAIN);
            door_main_close();
            if (sys_state == SYS_STATE_ERROR || sys_state == SYS_STATE_BLOCKED) {
                break;
            }
            if (m1_close_completed) {
                closing_flow_reset();
                sys_state = SYS_STATE_CLOSE_DONE;
            }
        }
        break;

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_CLOSE_DONE: {
        db_set_live(LD_CLOSE_STAGE, CLOSE_STAGE_IDLE);

        if (close_done_phase == 0u) {
            /* Phase 0: evaluate home window once and start CLOSE_DONE pulse. */
            float   pot_min = (float)db_get_param(DF_M1_ZERO_MIN);
            float   pot_max = (float)db_get_param(DF_M1_ZERO_MAX);
            float   m1_raw  = m1_raw_pos();
            uint8_t dip_val = board_get_dip();

            close_done_pot_bad = 0u;
            if (m1_raw < pot_min || m1_raw > pot_max) {
                close_done_pot_bad = 1u;
            }
            if ((dip_val & 0x04u) && !close_done_pot_bad) {
                float m2_min = (float)db_get_param(DF_M2_ZERO_MIN);
                float m2_max = (float)db_get_param(DF_M2_ZERO_MAX);
                float m2_raw = m2_raw_pos();
                if (m2_raw < m2_min || m2_raw > m2_max) {
                    close_done_pot_bad = 1u;
                }
            }

            SET_CLOSE_DONE(1);
            LED_SET(0);
            close_done_tick_ms = get_tick_ms();
            close_done_phase = 1u;
            break;
        }

        if (close_done_phase == 1u) {
            /* Hold CLOSE_DONE pulse for 500ms without blocking the state machine loop. */
            if ((get_tick_ms() - close_done_tick_ms) < 500u) {
                break;
            }
            SET_CLOSE_DONE(0);
            if (close_done_pot_bad) {
                door_beep(BEEP_SHORT_MS);
                close_done_tick_ms = get_tick_ms();
                close_done_phase = 2u;
                break;
            }
            close_done_phase = 0u;
            close_done_pot_bad = 0u;

            if (auto_test_active) {
                auto_test_done_cycles++;
                if (auto_test_target_cycles > 0u && auto_test_done_cycles >= auto_test_target_cycles) {
                    auto_test_done_cycles = auto_test_target_cycles;
                    auto_test_active = 0u;
                    db_set_param(DF_AUTO_TEST_CYCLES, 0u);
                }
                db_set_live(LD_AUTO_TEST_DONE, auto_test_done_cycles);
            }

            sys_state = SYS_STATE_WAIT;
            break;
        }

        if (close_done_phase == 2u) {
            if ((get_tick_ms() - close_done_tick_ms) < BEEP_GAP_MS) {
                break;
            }
            door_beep(BEEP_SHORT_MS);
            close_done_phase = 0u;
            close_done_pot_bad = 0u;
            sys_state = SYS_STATE_WAIT;
            break;
        }

        close_done_phase = 0u;
        close_done_pot_bad = 0u;
        sys_state = SYS_STATE_WAIT;
        break;
    }

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_BLOCKED: {
        db_set_live(LD_CLOSE_STAGE, CLOSE_STAGE_IDLE);
        /* Automatic retry after BLOCK_RETRY_DELAY_SEC.
           Use non-blocking timer phases so each run stays within TIME_WINDOW. */
        uint32_t retry_delay_ms = db_get_param(DF_BLOCK_RETRY_DELAY_SEC) * 1000u;

        if (blocked_phase == 0u) {
            motor_disable(MOTOR_M1);
            motor_disable(MOTOR_M2);
            door_beep(200u);
            blocked_tick_ms = get_tick_ms();
            blocked_phase = 1u;
            break;
        }

        if (blocked_phase == 1u) {
            if ((get_tick_ms() - blocked_tick_ms) < retry_delay_ms) {
                break;
            }

            blocked_retry_count++;
            db_set_live(LD_BLOCK_RETRY_COUNT, blocked_retry_count);
            db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);

            if (blocked_retry_count > MAX_BLOCK_RETRIES) {
                db_set_live(LD_ERROR_CODE, 1u);
                door_alarm_play(1u);
                alarm_played = 1u;
                m1_state  = DOOR_ERROR;
                m2_state  = DOOR_ERROR;
                sys_state = SYS_STATE_ERROR;
                db_set_live(LD_M1_STATE,  (uint32_t)m1_state);
                db_set_live(LD_M2_STATE,  (uint32_t)m2_state);
                blocked_phase = 0u;
                break;
            }

            blocked_phase = 0u;
            operation_start_tick = get_tick_ms();
            if (blocked_from_state == SYS_STATE_CLOSING) {
                closing_flow_reset();
                sys_state = SYS_STATE_CLOSING;
            } else {
                sys_state = SYS_STATE_OPENING;
            }
            break;
        }

        blocked_phase = 0u;
        break;
    }

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_ERROR:
        db_set_live(LD_CLOSE_STAGE, CLOSE_STAGE_IDLE);
        closing_flow_reset();
        motor_disable(MOTOR_M1);
        motor_disable(MOTOR_M2);
        LED_SET(1);
        /* Play alarm once on entry; subsequent calls just hold LED on */
        if (!alarm_played) {
            door_alarm_play(db_get_live(LD_ERROR_CODE));
            alarm_played = 1u;
        }

        if (TEST_READ() == RESET) {
            if (test_press_start_ms == 0u) {
                test_press_start_ms = get_tick_ms();
            } else if ((get_tick_ms() - test_press_start_ms) >= TEST_LONG_PRESS_CLEAR_MS) {
                clear_error_state();
                break;
            }
        } else {
            test_press_start_ms = 0u;
        }

        /* Stay in error state — recovery via TG_OPEN or TG_CLOSE */
        if (TG_OPEN_READ() == RESET || TG_CLOSE_READ() == RESET) {
            clear_error_state();
        }
        break;

    default:
        db_set_live(LD_CLOSE_STAGE, CLOSE_STAGE_IDLE);
        sys_state = SYS_STATE_WAIT;
        break;
    }

    /* Feed again on function exit as a coarse safety net. */
    FEED_WDT();
}
