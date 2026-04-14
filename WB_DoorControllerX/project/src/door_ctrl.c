/**
  ******************************************************************************
  * @file     door_ctrl.c
  * @brief    Main door control state machine (FreeRTOS task, AT32F413R)
  ******************************************************************************
  *
  * State machine overview:
  *   INIT → WAIT → OPENING → OPEN_DONE → CLOSING → CLOSE_DONE → WAIT
  *                         ↘ BLOCKED ↗ (obstruction retry)
  *                         ↘ ERROR   (after 3 failures or timeout)
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

/* Private constants ---------------------------------------------------------*/
#define MAX_BLOCK_RETRIES   3u
#define MAX_LOCK_RETRIES    3u

/* Private variables ---------------------------------------------------------*/
static sys_state_t  sys_state   = SYS_STATE_INIT;
static door_state_t m1_state    = DOOR_IDLE;
static door_state_t m2_state    = DOOR_IDLE;

static _pid_t pid_m1;
static _pid_t pid_m2;

static uint32_t operation_start_tick = 0u;  /* used for timeout check */
static sys_state_t blocked_from_state = SYS_STATE_WAIT;
static uint32_t blocked_retry_count = 0u;

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
    float pos = adc_to_position(adc_read_m1_pot());
    db_set_live(LD_M1_POS, (uint32_t)(pos * 100.0f));
    return pos;
}

/** Read M2 angle in degrees */
static float m2_pos(void)
{
    float pos = adc_to_position(adc_read_m2_pot());
    db_set_live(LD_M2_POS, (uint32_t)(pos * 100.0f));
    return pos;
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

    db_set_live(LD_DIP_VALUE, dip);

    pid_init(&pid_m1, 1.0f, 0.05f, 0.1f);
    pid_init(&pid_m2, 1.0f, 0.05f, 0.1f);

    sys_state = SYS_STATE_WAIT;
    m1_state  = DOOR_IDLE;
    m2_state  = DOOR_IDLE;
    blocked_from_state = SYS_STATE_WAIT;
    blocked_retry_count = 0u;

    db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
    db_set_live(LD_M1_STATE,  (uint32_t)m1_state);
    db_set_live(LD_M2_STATE,  (uint32_t)m2_state);
    db_set_live(LD_BLOCK_COUNT, 0u);
    db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
    db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);
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
        vTaskDelay(pdMS_TO_TICKS(db_get_param(DF_TIME_WINDOW)));
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
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    BUZZER_SET(0);
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

    active_time_ms = db_get_param(DF_LOCK_ACTIVE_TIME) * 100u; /* param unit = 0.1s, ×100 converts to ms */

    D2_SET_PH(0);                              /* unlock direction */
    RELAY3_SET(1);
    motor_enable(MOTOR_M3);
    motor_set_pwm(MOTOR_M3, (uint8_t)db_get_param(DF_M3_START_DUTY));

    vTaskDelay(pdMS_TO_TICKS(active_time_ms));

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

    active_time_ms = db_get_param(DF_LOCK_ACTIVE_TIME) * 100u; /* param unit = 0.1s, ×100 converts to ms */

    D2_SET_PH(1);                              /* lock direction */
    RELAY3_SET(1);
    motor_enable(MOTOR_M3);
    motor_set_pwm(MOTOR_M3, (uint8_t)db_get_param(DF_M3_START_DUTY));

    vTaskDelay(pdMS_TO_TICKS(active_time_ms));

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
        /* Neither sensor triggered — lock/unlock did not complete */
        return 1;
    }
    if (ul == RESET && ll == RESET) {
        /* Both triggered simultaneously — sensor/wiring error */
        return 2;
    }
    return 0;   /* Exactly one sensor triggered — consistent state */
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
        door_beep(200);
        vTaskDelay(pdMS_TO_TICKS(100));
        door_beep(200);
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
  *    2. Unlock M3 → check M3_UL; retry up to 3×
  *    3. On success: PID forward to M1_OPEN_ANGLE
  *    4. On 3 failures: beep + error state
  */
void door_main_open(void)
{
    uint8_t  dip           = board_get_dip();
    uint8_t  has_lock      = (dip & 0x02u) ? 1u : 0u;
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
            vTaskDelay(pdMS_TO_TICKS(200));
            motor_set_pwm(MOTOR_M1, 0u);
            motor_disable(MOTOR_M1);

            /* Activate lock motor to unlock */
            door_unlock();

            /* Check unlock sensor */
            if (M3_UL_READ() != RESET) {
                break;  /* unlock confirmed */
            }

            if (lock_retry < (MAX_LOCK_RETRIES - 1u)) {
                vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
            }
        }

        if (lock_retry >= MAX_LOCK_RETRIES) {
            /* Failed to unlock after 3 attempts */
            db_set_live(LD_LOCK_RETRY_COUNT, lock_retry);
            door_beep(500);
            vTaskDelay(pdMS_TO_TICKS(200));
            door_beep(500);
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
        door_check_timeout();
        if (sys_state == SYS_STATE_ERROR) {
            motor_disable(MOTOR_M1);
            RELAY1_SET(0);
            return;
        }

        /* Obstruction detection: sample every check_interval, check every detect_time */
        check_cnt++;
        if (check_cnt >= checks_per_window) {
            if (check_obstruction(pos, prev_pos)) {
                block_count++;
                db_set_live(LD_BLOCK_COUNT, block_count);
                if (block_count >= MAX_BLOCK_RETRIES) {
                    motor_set_pwm(MOTOR_M1, 0u);
                    motor_disable(MOTOR_M1);
                    RELAY1_SET(0);
                    door_beep(300);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    door_beep(300);
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
                vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
                motor_set_pwm(MOTOR_M1, (uint8_t)db_get_param(DF_M1_START_DUTY));
            }
            prev_pos  = pos;
            check_cnt = 0u;
        }
    }

    motor_set_pwm(MOTOR_M1, 0u);
    motor_disable(MOTOR_M1);
    RELAY1_SET(0);

    m1_state = DOOR_OPEN;
    db_set_live(LD_M1_STATE, (uint32_t)m1_state);
}

/**
  * @brief  Main door (M1) close sequence.
  *
  *  No lock (DIP_1=0): PID reverse until M1_HOME=1 or pos < M1_ZERO_ERROR.
  *  With lock (DIP_1=1):
  *    1. PID reverse to home position
  *    2. Apply M1_CLOSE_FWD_DUTY forward briefly, wait LOCK_ACTIVE_TIME
  *    3. Lock M3 → check M3_LL; retry up to 3×
  */
void door_main_close(void)
{
    uint8_t  dip          = board_get_dip();
    uint8_t  has_lock     = (dip & 0x02u) ? 1u : 0u;
    uint32_t zero_error   = db_get_param(DF_M1_ZERO_ERROR);
    uint8_t  fwd_duty     = (uint8_t)db_get_param(DF_M1_CLOSE_FWD_DUTY);
    uint32_t retry_delay_ms = db_get_param(DF_BLOCK_RETRY_DELAY_SEC) * 1000u;
    uint32_t lock_retry;
    float    pos, prev_pos;
    uint32_t block_count  = 0u;
    uint32_t detect_time  = db_get_param(DF_BLOCK_DETECT_TIME);
    uint32_t check_interval = (uint32_t)db_get_param(DF_TIME_WINDOW);
    uint32_t checks_per_window;
    uint32_t check_cnt;

    checks_per_window = (detect_time > check_interval) ? (detect_time / check_interval) : 1u;

    m1_state = DOOR_CLOSING;
    db_set_live(LD_M1_STATE, (uint32_t)m1_state);

    /* --- Step 1: PID reverse to home --- */
    pid_init(&pid_m1, 1.0f, 0.05f, 0.1f);

    motor_set_direction(MOTOR_M1, MOTOR_DIR_REV);
    motor_enable(MOTOR_M1);
    RELAY1_SET(1);

    block_count = 0u;
    prev_pos    = m1_pos();
    check_cnt   = 0u;

    while (1) {
        pos = m1_pos();

        if (M1_HOME_READ() != RESET || pos < (float)zero_error) {
            break;
        }

        float pid_out = pid_compute(&pid_m1, 0.0f, pos, (float)check_interval / 1000.0f);
        uint8_t duty = pid_duty_pct(pid_out);
        motor_set_pwm(MOTOR_M1, duty);

        vTaskDelay(pdMS_TO_TICKS(check_interval));
        door_check_timeout();
        if (sys_state == SYS_STATE_ERROR) {
            motor_disable(MOTOR_M1);
            RELAY1_SET(0);
            return;
        }

        check_cnt++;
        if (check_cnt >= checks_per_window) {
            if (check_obstruction(pos, prev_pos)) {
                block_count++;
                db_set_live(LD_BLOCK_COUNT, block_count);
                if (block_count >= MAX_BLOCK_RETRIES) {
                    motor_set_pwm(MOTOR_M1, 0u);
                    motor_disable(MOTOR_M1);
                    RELAY1_SET(0);
                    door_beep(300);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    door_beep(300);
                    m1_state  = DOOR_BLOCKED;
                    blocked_from_state = SYS_STATE_CLOSING;
                    sys_state = SYS_STATE_BLOCKED;
                    db_set_live(LD_M1_STATE,  (uint32_t)m1_state);
                    db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);
                    db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
                    return;
                }
                motor_set_pwm(MOTOR_M1, 0u);
                vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
                motor_set_pwm(MOTOR_M1, (uint8_t)db_get_param(DF_M1_START_DUTY));
            }
            prev_pos  = pos;
            check_cnt = 0u;
        }
    }

    motor_set_pwm(MOTOR_M1, 0u);
    motor_disable(MOTOR_M1);
    RELAY1_SET(0);

    /* --- Step 2: lock M3 if electric lock is enabled --- */
    if (has_lock) {
        for (lock_retry = 0u; lock_retry < MAX_LOCK_RETRIES; lock_retry++) {

            /* Brief forward push to ease latch engagement */
            motor_enable(MOTOR_M1);
            motor_set_direction(MOTOR_M1, MOTOR_DIR_FWD);
            motor_set_pwm(MOTOR_M1, fwd_duty);
            vTaskDelay(pdMS_TO_TICKS(db_get_param(DF_LOCK_ACTIVE_TIME) * 100u));
            motor_set_pwm(MOTOR_M1, 0u);
            motor_disable(MOTOR_M1);

            /* Activate lock motor */
            door_lock();

            /* Check lock sensor */
            if (M3_LL_READ() != RESET) {
                break;
            }

            if (lock_retry < (MAX_LOCK_RETRIES - 1u)) {
                vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
            }
        }

        if (lock_retry >= MAX_LOCK_RETRIES) {
            db_set_live(LD_LOCK_RETRY_COUNT, lock_retry);
            door_beep(500);
            vTaskDelay(pdMS_TO_TICKS(200));
            door_beep(500);
            m1_state  = DOOR_ERROR;
            sys_state = SYS_STATE_ERROR;
            db_set_live(LD_M1_STATE,  (uint32_t)m1_state);
            db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
            return;
        }
    }

    m1_state = DOOR_CLOSED;
    db_set_live(LD_M1_STATE, (uint32_t)m1_state);
}

/* ============================================================================
 * Secondary door (M2) open / close sequences
 * ========================================================================== */

/**
  * @brief  Secondary door (M2) open sequence.
  *         Wait until (M1_POS − M2_POS) > OPEN_DIFF_ANGLE, then PID forward.
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
        if (sys_state == SYS_STATE_ERROR) return;
        vTaskDelay(pdMS_TO_TICKS(check_interval));
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
        door_check_timeout();
        if (sys_state == SYS_STATE_ERROR) {
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
                    door_beep(300);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    door_beep(300);
                    m2_state  = DOOR_BLOCKED;
                    blocked_from_state = SYS_STATE_OPENING;
                    sys_state = SYS_STATE_BLOCKED;
                    db_set_live(LD_M2_STATE,  (uint32_t)m2_state);
                    db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);
                    db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
                    return;
                }
                motor_set_pwm(MOTOR_M2, 0u);
                vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
                motor_set_pwm(MOTOR_M2, (uint8_t)db_get_param(DF_M2_START_DUTY));
            }
            prev_pos  = pos;
            check_cnt = 0u;
        }
    }

    motor_set_pwm(MOTOR_M2, 0u);
    motor_disable(MOTOR_M2);
    RELAY2_SET(0);

    m2_state = DOOR_OPEN;
    db_set_live(LD_M2_STATE, (uint32_t)m2_state);
}

/**
  * @brief  Secondary door (M2) close sequence.
  *         Wait until (M2_POS − M1_POS) > OPEN_DIFF_ANGLE (M1 closing first),
  *         then PID reverse until M2_HOME=1 or pos < M2_ZERO_ERROR.
  */
void door_sub_close(void)
{
    uint32_t diff_angle    = db_get_param(DF_OPEN_DIFF_ANGLE);
    uint32_t zero_error    = db_get_param(DF_M2_ZERO_ERROR);
    uint32_t retry_delay_ms= db_get_param(DF_BLOCK_RETRY_DELAY_SEC) * 1000u;
    uint32_t check_interval= (uint32_t)db_get_param(DF_TIME_WINDOW);
    float    p1, p2, pos;
    uint32_t block_count   = 0u;
    uint32_t detect_time   = db_get_param(DF_BLOCK_DETECT_TIME);
    uint32_t checks_per_window;
    uint32_t check_cnt;
    float    prev_pos;

    checks_per_window = (detect_time > check_interval) ? (detect_time / check_interval) : 1u;

    m2_state = DOOR_CLOSING;
    db_set_live(LD_M2_STATE, (uint32_t)m2_state);

    /* Wait until M2 is noticeably ahead of M1 (M1 has already started closing) */
    while (1) {
        p2 = m2_pos();
        p1 = m1_pos();
        if ((p2 - p1) >= (float)diff_angle) break;
        door_check_timeout();
        if (sys_state == SYS_STATE_ERROR) return;
        vTaskDelay(pdMS_TO_TICKS(check_interval));
    }

    pid_init(&pid_m2, 1.0f, 0.05f, 0.1f);
    motor_set_direction(MOTOR_M2, MOTOR_DIR_REV);
    motor_enable(MOTOR_M2);
    RELAY2_SET(1);

    block_count = 0u;
    prev_pos    = m2_pos();
    check_cnt   = 0u;

    while (1) {
        pos = m2_pos();

        if (M2_HOME_READ() != RESET || pos < (float)zero_error) break;

        float pid_out = pid_compute(&pid_m2, 0.0f, pos, (float)check_interval / 1000.0f);
        uint8_t duty = pid_duty_pct(pid_out);
        motor_set_pwm(MOTOR_M2, duty);

        vTaskDelay(pdMS_TO_TICKS(check_interval));
        door_check_timeout();
        if (sys_state == SYS_STATE_ERROR) {
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
                    door_beep(300);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    door_beep(300);
                    m2_state  = DOOR_BLOCKED;
                    blocked_from_state = SYS_STATE_CLOSING;
                    sys_state = SYS_STATE_BLOCKED;
                    db_set_live(LD_M2_STATE,  (uint32_t)m2_state);
                    db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);
                    db_set_live(LD_SYS_STATE, (uint32_t)sys_state);
                    return;
                }
                motor_set_pwm(MOTOR_M2, 0u);
                vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
                motor_set_pwm(MOTOR_M2, (uint8_t)db_get_param(DF_M2_START_DUTY));
            }
            prev_pos  = pos;
            check_cnt = 0u;
        }
    }

    motor_set_pwm(MOTOR_M2, 0u);
    motor_disable(MOTOR_M2);
    RELAY2_SET(0);

    m2_state = DOOR_CLOSED;
    db_set_live(LD_M2_STATE, (uint32_t)m2_state);
}

/* ============================================================================
 * Obstruction check (public, for external call)
 * ========================================================================== */

/**
  * @brief  Wrapper for obstacle detection — checks M1 and M2 movement.
  */
void door_block_detect(void)
{
    /* Obstruction detection is integrated into the open/close sequences.
       This function is provided for external polling if needed. */
}

/* ============================================================================
 * Main state machine dispatcher
 * ========================================================================== */

/**
  * @brief  Main state machine — called every TIME_WINDOW ms.
  */
void door_ctrl_run(void)
{
    uint8_t  dip        = board_get_dip();
    uint8_t  dual_door  = (dip & 0x04u) ? 1u : 0u;
    float    pos;

    db_set_live(LD_SYS_STATE, (uint32_t)sys_state);

    switch (sys_state) {

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_INIT:
        sys_state = SYS_STATE_WAIT;
        break;

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_WAIT:
        pos = m1_pos();

        /* Auto-open trigger: door pushed past trigger angle */
        if (pos >= (float)db_get_param(DF_OPEN_TRIGGER_ANGLE)) {
            db_set_live(LD_BLOCK_COUNT, 0u);
            blocked_retry_count = 0u;
            db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
            db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)SYS_STATE_WAIT);
            operation_start_tick = get_tick_ms();
            sys_state = SYS_STATE_OPENING;
            break;
        }

        /* TG_OPEN active (active-low) */
        if (TG_OPEN_READ() == RESET) {
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
        db_set_live(LD_OPEN_COUNT, db_get_live(LD_OPEN_COUNT) + 1u);
        LED_SET(1);

        if (dual_door) {
            /* Run M1 and M2 open concurrently (M2 waits for M1 lead internally) */
            door_main_open();
            if (sys_state != SYS_STATE_ERROR && sys_state != SYS_STATE_BLOCKED) {
                door_sub_open();
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
        SET_OPEN_DONE(1);
        LED_SET(1);
        /* Hold OPEN_DONE high until close trigger */
        if (TG_CLOSE_READ() == RESET) {
            SET_OPEN_DONE(0);
            db_set_live(LD_BLOCK_COUNT, 0u);
            blocked_retry_count = 0u;
            db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
            db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)SYS_STATE_WAIT);
            operation_start_tick = get_tick_ms();
            sys_state = SYS_STATE_CLOSING;
        }
        break;

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_CLOSING:
        SET_OPEN_DONE(0);
        db_set_live(LD_CLOSE_COUNT, db_get_live(LD_CLOSE_COUNT) + 1u);

        if (dual_door) {
            door_sub_close();
            if (sys_state != SYS_STATE_ERROR && sys_state != SYS_STATE_BLOCKED) {
                door_main_close();
            }
        } else {
            door_main_close();
        }

        if (sys_state != SYS_STATE_ERROR && sys_state != SYS_STATE_BLOCKED) {
            sys_state = SYS_STATE_CLOSE_DONE;
        }
        break;

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_CLOSE_DONE:
        SET_CLOSE_DONE(1);
        LED_SET(0);
        vTaskDelay(pdMS_TO_TICKS(500));
        SET_CLOSE_DONE(0);
        sys_state = SYS_STATE_WAIT;
        break;

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_BLOCKED: {
        /* Automatic retry after BLOCK_RETRY_DELAY_SEC.
           Retry in the same direction as the blocked operation. */
        uint32_t retry_delay_ms = db_get_param(DF_BLOCK_RETRY_DELAY_SEC) * 1000u;
        motor_disable(MOTOR_M1);
        motor_disable(MOTOR_M2);
        door_beep(200);
        vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));

        blocked_retry_count++;
        db_set_live(LD_BLOCK_RETRY_COUNT, blocked_retry_count);
        db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);

        if (blocked_retry_count > MAX_BLOCK_RETRIES) {
            door_beep(500);
            vTaskDelay(pdMS_TO_TICKS(200));
            door_beep(500);
            m1_state  = DOOR_ERROR;
            m2_state  = DOOR_ERROR;
            sys_state = SYS_STATE_ERROR;
            db_set_live(LD_M1_STATE,  (uint32_t)m1_state);
            db_set_live(LD_M2_STATE,  (uint32_t)m2_state);
            db_set_live(LD_ERROR_CODE, 1u);
            break;
        }

        operation_start_tick = get_tick_ms();
        if (blocked_from_state == SYS_STATE_CLOSING) {
            sys_state = SYS_STATE_CLOSING;
        } else {
            sys_state = SYS_STATE_OPENING;
        }
        break;
    }

    /* ---------------------------------------------------------------------- */
    case SYS_STATE_ERROR:
        motor_disable(MOTOR_M1);
        motor_disable(MOTOR_M2);
        LED_SET(1);
        door_beep(200);
        vTaskDelay(pdMS_TO_TICKS(800));
        LED_SET(0);
        vTaskDelay(pdMS_TO_TICKS(200));
        /* Stay in error state — reset required or TG_OPEN/TG_CLOSE to recover */
        if (TG_OPEN_READ() == RESET || TG_CLOSE_READ() == RESET) {
            sys_state = SYS_STATE_WAIT;
            m1_state  = DOOR_IDLE;
            m2_state  = DOOR_IDLE;
            blocked_from_state = SYS_STATE_WAIT;
            blocked_retry_count = 0u;
            db_set_live(LD_ERROR_CODE, 0u);
            db_set_live(LD_BLOCK_RETRY_COUNT, 0u);
            db_set_live(LD_BLOCK_SOURCE_STATE, (uint32_t)blocked_from_state);
        }
        break;

    default:
        sys_state = SYS_STATE_WAIT;
        break;
    }
}
