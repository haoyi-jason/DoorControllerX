/**
  ******************************************************************************
  * @file     comm_task.h
  * @brief    UART1 binary protocol communication task
  ******************************************************************************
  */

#ifndef COMM_TASK_H
#define COMM_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

/* Binary Protocol frame bytes -----------------------------------------------*/
#define PROTO_STX   0xAAu
#define PROTO_ETX   0x55u

/* Command codes -------------------------------------------------------------*/
#define CMD_READ_PARAM   0x01u   /*!< Read DF_ parameter by ID */
#define CMD_WRITE_PARAM  0x02u   /*!< Write DF_ parameter by ID + value */
#define CMD_READ_LIVE    0x03u   /*!< Read LD_ live data by ID */
#define CMD_WRITE_LIVE   0x04u   /*!< Write LD_ live data by ID + value */
#define CMD_ACK          0xF0u   /*!< Acknowledgment */
#define CMD_NAK          0xF1u   /*!< Negative acknowledgment */

/* Exported functions --------------------------------------------------------*/
void    comm_task_init(void);
void    comm_task_run(void *pvParameters);
void    comm_task_rx_isr_handler(void);
void    comm_send_response(uint8_t cmd, uint8_t *data, uint8_t len);
uint8_t comm_calc_crc(uint8_t *buf, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* COMM_TASK_H */
