/**
  ******************************************************************************
  * @file     comm_task.c
  * @brief    UART1 binary protocol communication task
  *
  * Frame format:
  *   [STX:0xAA] [LEN:1B] [CMD:1B] [DATA:N bytes] [CRC:1B] [ETX:0x55]
  *   LEN = 1 (CMD) + N (DATA bytes)
  *   CRC = XOR of LEN, CMD, DATA[0..N-1]
  *
  * Commands:
  *   0x01 CMD_READ_PARAM  – DATA=[id(1B)]           → ACK + value(4B)
  *   0x02 CMD_WRITE_PARAM – DATA=[id(1B),val(4B)]   → ACK
  *   0x03 CMD_READ_LIVE   – DATA=[id(1B)]           → ACK + value(4B)
  *   0x04 CMD_WRITE_LIVE  – DATA=[id(1B),val(4B)]   → ACK
  ******************************************************************************
  */

#include "comm_task.h"
#include "database.h"
#include "at32f413_wk_config.h"

/* Private constants ---------------------------------------------------------*/
#define RX_RING_SIZE    128u    /*!< RX ring buffer size (power of 2) */
#define TX_TIMEOUT      10000u  /*!< Transmit flag poll timeout loops */
#define RX_POLL_MS      1u      /*!< Task RX poll period (ms) */

/* Minimum frame: STX + LEN + CMD + CRC + ETX = 5 bytes */
#define FRAME_MIN_SIZE  5u
/* Maximum data bytes per frame */
#define FRAME_MAX_DATA  16u

/* Private variables ---------------------------------------------------------*/
static uint8_t  rx_ring[RX_RING_SIZE];
static uint16_t rx_head = 0u;
static uint16_t rx_tail = 0u;

/* Private helpers -----------------------------------------------------------*/

static inline void ring_push(uint8_t byte)
{
    uint16_t next = (rx_head + 1u) & (RX_RING_SIZE - 1u);
    if (next != rx_tail) {          /* drop byte if full */
        rx_ring[rx_head] = byte;
        rx_head = next;
    }
}

static inline int ring_empty(void)
{
    return rx_head == rx_tail;
}

static inline uint8_t ring_pop(void)
{
    uint8_t b = rx_ring[rx_tail];
    rx_tail = (rx_tail + 1u) & (RX_RING_SIZE - 1u);
    return b;
}

/**
  * @brief  Transmit one byte over USART1 (blocking with timeout).
  */
static void usart_send_byte(uint8_t b)
{
    uint32_t t = TX_TIMEOUT;
    usart_data_transmit(USART1, (uint16_t)b);
    while (!usart_flag_get(USART1, USART_TDC_FLAG) && (--t))
    {
    }
}

/* Public functions ----------------------------------------------------------*/

/**
  * @brief  Initialise the UART1 communication layer (RX ring buffer).
  *         USART1 hardware is already configured by wk_usart1_init().
  */
void comm_task_init(void)
{
    rx_head = 0u;
    rx_tail = 0u;
}

/**
  * @brief  Compute XOR-based CRC over buf[0..len-1].
  * @param  buf  Pointer to byte array (starts at LEN field)
  * @param  len  Number of bytes to process
  * @retval XOR checksum
  */
uint8_t comm_calc_crc(uint8_t *buf, uint8_t len)
{
    uint8_t crc = 0u;
    uint8_t i;
    for (i = 0u; i < len; i++) {
        crc ^= buf[i];
    }
    return crc;
}

/**
  * @brief  Build and transmit a response frame.
  *
  * Frame layout:
  *   [STX] [LEN=1+len] [cmd] [data[0]..data[len-1]] [crc] [ETX]
  *
  * @param  cmd   Response command byte (CMD_ACK or CMD_NAK)
  * @param  data  Pointer to response data (may be NULL when len=0)
  * @param  len   Number of data bytes
  */
void comm_send_response(uint8_t cmd, uint8_t *data, uint8_t len)
{
    uint8_t  frame_len = 1u + len;  /* CMD + DATA */
    uint8_t  crc_buf[FRAME_MAX_DATA + 2u];
    uint8_t  crc;
    uint8_t  i;

    /* Build CRC input: LEN, CMD, DATA */
    crc_buf[0] = frame_len;
    crc_buf[1] = cmd;
    for (i = 0u; i < len; i++) {
        crc_buf[2u + i] = data[i];
    }
    crc = comm_calc_crc(crc_buf, (uint8_t)(2u + len));

    /* Transmit */
    usart_send_byte(PROTO_STX);
    usart_send_byte(frame_len);
    usart_send_byte(cmd);
    for (i = 0u; i < len; i++) {
        usart_send_byte(data[i]);
    }
    usart_send_byte(crc);
    usart_send_byte(PROTO_ETX);
}

/**
  * @brief  Process a fully-received binary protocol frame.
  *         The CRC has already been verified by the caller.
  *
  * @param  payload  Points to LEN byte of the received frame.
  *                  Layout: [LEN][CMD][DATA...][CRC][ETX]
  */
static void process_frame(uint8_t *payload)
{
    uint8_t  frame_len  = payload[0];   /* LEN = 1 + N data bytes */
    uint8_t  cmd        = payload[1];
    uint8_t *frame_data = &payload[2];  /* DATA: frame_len-1 bytes */

    uint8_t  param_id;
    uint32_t value;
    uint8_t  resp[4];

    switch (cmd) {

    case CMD_READ_PARAM:
        if (frame_len < 2u) { comm_send_response(CMD_NAK, NULL, 0u); return; }
        param_id = frame_data[0];
        value = db_get_param(param_id);
        resp[0] = (uint8_t)(value >> 24);
        resp[1] = (uint8_t)(value >> 16);
        resp[2] = (uint8_t)(value >>  8);
        resp[3] = (uint8_t)(value);
        comm_send_response(CMD_ACK, resp, 4u);
        break;

    case CMD_WRITE_PARAM:
        if (frame_len < 6u) { comm_send_response(CMD_NAK, NULL, 0u); return; }
        param_id = frame_data[0];
        value  = ((uint32_t)frame_data[1] << 24)
               | ((uint32_t)frame_data[2] << 16)
               | ((uint32_t)frame_data[3] <<  8)
               |  (uint32_t)frame_data[4];
        db_set_param(param_id, value);
        comm_send_response(CMD_ACK, NULL, 0u);
        break;

    case CMD_READ_LIVE:
        if (frame_len < 2u) { comm_send_response(CMD_NAK, NULL, 0u); return; }
        param_id = frame_data[0];
        value = db_get_live(param_id);
        resp[0] = (uint8_t)(value >> 24);
        resp[1] = (uint8_t)(value >> 16);
        resp[2] = (uint8_t)(value >>  8);
        resp[3] = (uint8_t)(value);
        comm_send_response(CMD_ACK, resp, 4u);
        break;

    case CMD_WRITE_LIVE:
        if (frame_len < 6u) { comm_send_response(CMD_NAK, NULL, 0u); return; }
        param_id = frame_data[0];
        value  = ((uint32_t)frame_data[1] << 24)
               | ((uint32_t)frame_data[2] << 16)
               | ((uint32_t)frame_data[3] <<  8)
               |  (uint32_t)frame_data[4];
        db_set_live(param_id, value);
        comm_send_response(CMD_ACK, NULL, 0u);
        break;

    default:
        comm_send_response(CMD_NAK, NULL, 0u);
        break;
    }
}

/**
  * @brief  FreeRTOS communication task.
  *         Polls USART1 for incoming bytes and parses binary protocol frames.
  *
  * Frame parser state machine:
  *   IDLE → got STX → read LEN → read CMD+DATA+CRC+ETX → process
  */
void comm_task_run(void *pvParameters)
{
    (void)pvParameters;

    /* State machine */
    enum { PS_IDLE, PS_LEN, PS_BODY } parse_state = PS_IDLE;

    uint8_t  frame_buf[FRAME_MAX_DATA + 4u]; /* LEN + CMD + DATA + CRC + ETX */
    uint8_t  body_expected = 0u;
    uint8_t  body_received = 0u;

    comm_task_init();

    while (1) {
        /* Drain USART1 RX FIFO into ring buffer */
        while (usart_flag_get(USART1, USART_RDBF_FLAG)) {
            ring_push((uint8_t)usart_data_receive(USART1));
        }

        /* Parse ring buffer */
        while (!ring_empty()) {
            uint8_t b = ring_pop();

            switch (parse_state) {

            case PS_IDLE:
                if (b == PROTO_STX) {
                    body_received = 0u;
                    parse_state   = PS_LEN;
                }
                break;

            case PS_LEN:
                if (b == 0u || b > (FRAME_MAX_DATA + 1u)) {
                    /* Invalid length — resync */
                    parse_state = PS_IDLE;
                } else {
                    frame_buf[0]  = b;            /* store LEN */
                    body_expected = b + 2u;       /* CMD+DATA (b bytes) + CRC + ETX */
                    body_received = 1u;           /* LEN already stored at index 0 */
                    parse_state   = PS_BODY;
                }
                break;

            case PS_BODY:
                frame_buf[body_received++] = b;
                if (body_received >= (body_expected + 1u)) {
                    /* Full frame received (LEN stored at 0, rest follows) */
                    uint8_t etx_idx = frame_buf[0] + 2u; /* LEN+CMD+DATA + CRC = LEN+2 bytes from idx 0 */
                    if (frame_buf[etx_idx] == PROTO_ETX) {
                        /* CRC is at etx_idx-1 = frame_buf[0]+1 */
                        uint8_t rx_crc   = frame_buf[etx_idx - 1u];
                        uint8_t calc_crc = comm_calc_crc(frame_buf, (uint8_t)(frame_buf[0] + 1u));
                        if (calc_crc == rx_crc) {
                            process_frame(frame_buf);
                        } else {
                            comm_send_response(CMD_NAK, NULL, 0u);
                        }
                    }
                    parse_state = PS_IDLE;
                }
                break;

            default:
                parse_state = PS_IDLE;
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(RX_POLL_MS));
    }
}
