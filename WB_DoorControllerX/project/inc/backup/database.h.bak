/**
  ******************************************************************************
  * @file     database.h
  * @brief    Parameter database (DF_ EEPROM params + LD_ live data)
  ******************************************************************************
  */

#ifndef DATABASE_H
#define DATABASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* DF_ — Stored/EEPROM parameter IDs ----------------------------------------*/
typedef enum {
    DF_BLOCK_RETRY_DELAY_SEC   = 0,  /*!< Retry delay (s) on obstruction, 1-10, default 1 */
    DF_OPEN_TRIGGER_ANGLE,           /*!< Auto-open trigger angle (deg), 5-30, default 10 */
    DF_OPEN_DIFF_ANGLE,              /*!< M1-M2 angle diff before M2 moves, 10-30, default 10 */
    DF_LOCK_ACTIVE_TIME,             /*!< Lock/unlock active time (×0.1 s), 10-50, default 20 */
    DF_BLOCK_DETECT_ANGLE,           /*!< Obstruction detection angle, 10-20, default 10 */
    DF_BLOCK_DETECT_TIME,            /*!< Obstruction detection time window, 10-20, default 10 */
    DF_TIME_WINDOW,                  /*!< State machine period (ms), 1-20, default 5 */
    DF_M1_START_DUTY,                /*!< M1 start PWM duty (%), 1-90, default 20 */
    DF_M1_MAX_DUTY,                  /*!< M1 max PWM duty (%), 1-90, default 80 */
    DF_M2_START_DUTY,                /*!< M2 start PWM duty (%), 1-90, default 20 */
    DF_M2_MAX_DUTY,                  /*!< M2 max PWM duty (%), 1-90, default 80 */
    DF_M3_START_DUTY,                /*!< M3 start PWM duty (%), 1-90, default 30 */
    DF_M3_MAX_DUTY,                  /*!< M3 max PWM duty (%), 1-50, default 50 */
    DF_M1_OPEN_ANGLE,                /*!< M1 open target angle (deg), 50-120, default 100 */
    DF_M2_OPEN_ANGLE,                /*!< M2 open target angle (deg), 50-120, default 100 */
    DF_M1_OPEN_REV_DUTY,             /*!< M1 reverse duty before unlock (%), 5-50, default 20 */
    DF_M1_OPEN_REV_DUTY_DELTA,       /*!< M1 reverse duty increment, 1-20, default 5 */
    DF_M1_CLOSE_FWD_DUTY,            /*!< M1 forward duty after close (%), 5-50, default 20 */
    DF_M1_CLOSE_FWD_DUTY_DELTA,      /*!< M1 forward duty increment, 1-20, default 5 */
    DF_M1_ZERO_ERROR,                /*!< M1 home position error tolerance (deg), 1-20, default 5 */
    DF_M2_ZERO_ERROR,                /*!< M2 home position error tolerance (deg), 1-20, default 5 */
    DF_MAX_OPEN_OPERATION_TIME,      /*!< Max time for open/close operation (s), 5-120, default 30 */
    DF_NUM_PARAMS
} df_param_id_t;

/* LD_ — Live runtime data IDs -----------------------------------------------*/
typedef enum {
    LD_SYS_STATE       = 0,  /*!< Current system state (sys_state_t) */
    LD_M1_STATE,             /*!< M1 door state (door_state_t) */
    LD_M2_STATE,             /*!< M2 door state (door_state_t) */
    LD_M1_POS,               /*!< M1 current position (degrees × 100) */
    LD_M2_POS,               /*!< M2 current position (degrees × 100) */
    LD_M1_SETPOINT,          /*!< M1 PID setpoint (degrees × 100) */
    LD_M2_SETPOINT,          /*!< M2 PID setpoint (degrees × 100) */
    LD_M1_ERROR,             /*!< M1 position error (degrees × 100) */
    LD_M2_ERROR,             /*!< M2 position error (degrees × 100) */
    LD_M1_PWM,               /*!< M1 current PWM duty (%) */
    LD_M2_PWM,               /*!< M2 current PWM duty (%) */
    LD_M1_CURRENT,           /*!< M1 current feedback (ADC raw) */
    LD_M2_CURRENT,           /*!< M2 current feedback (ADC raw) */
    LD_DIP_VALUE,            /*!< DIP switch value at startup */
    LD_BLOCK_COUNT,          /*!< Consecutive obstruction count */
    LD_BLOCK_RETRY_COUNT,    /*!< Automatic retry count after entering BLOCKED */
    LD_BLOCK_SOURCE_STATE,   /*!< Source state that entered BLOCKED (sys_state_t) */
    LD_ERROR_CODE,           /*!< Last error code */
    LD_OPEN_COUNT,           /*!< Total open operations */
    LD_CLOSE_COUNT,          /*!< Total close operations */
    LD_LOCK_RETRY_COUNT,     /*!< Lock/unlock retry count */
    LD_OPERATION_TIME_MS,    /*!< Current operation elapsed time (ms) */
    LD_NUM_PARAMS
} ld_param_id_t;

/* Exported functions --------------------------------------------------------*/
void     db_init(void);
uint32_t db_get_param(uint8_t id);
void     db_set_param(uint8_t id, uint32_t value);
uint32_t db_get_live(uint8_t id);
void     db_set_live(uint8_t id, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* DATABASE_H */
