/**
  ******************************************************************************
  * @file     door_ctrl.h
  * @brief    Main door control state machine for DoorControllerX
  ******************************************************************************
  */

#ifndef DOOR_CTRL_H
#define DOOR_CTRL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

/* System-level states -------------------------------------------------------*/
typedef enum {
    SYS_STATE_INIT = 0,
    SYS_STATE_WAIT,
    SYS_STATE_OPENING,
    SYS_STATE_OPEN_DONE,
    SYS_STATE_CLOSING,
    SYS_STATE_CLOSE_DONE,
    SYS_STATE_BLOCKED,
    SYS_STATE_ERROR
} sys_state_t;

/* Per-door substates --------------------------------------------------------*/
typedef enum {
    DOOR_IDLE = 0,
    DOOR_OPENING,
    DOOR_OPEN,
    DOOR_CLOSING,
    DOOR_CLOSED,
    DOOR_BLOCKED,
    DOOR_ERROR
} door_state_t;

/* Exported variables --------------------------------------------------------*/
extern volatile uint32_t g_dbg_reset_reason;     /* Reset reason: 0=normal, 1=IWDG, 2=WWDG, 3=software, 4=PIN, 5=POR, 99=HardFault */
extern volatile uint32_t g_dbg_hardfault_stage;  /* Startup stage where HardFault occurred (if reset_reason=99) */

/* Exported functions --------------------------------------------------------*/
void door_ctrl_init(void);
void door_ctrl_task(void *pvParameters);
void door_ctrl_run(void);

void door_main_open(void);
void door_main_close(void);
void door_sub_open(void);
void door_sub_close(void);

void door_lock(void);
void door_unlock(void);
int  door_check_lock_error(void);

void door_block_detect(void);
void door_beep(uint16_t duration_ms);
void door_alarm_play(uint32_t error_code);
void door_check_timeout(void);

#ifdef __cplusplus
}
#endif

#endif /* DOOR_CTRL_H */
