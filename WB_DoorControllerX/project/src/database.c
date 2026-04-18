/**
  ******************************************************************************
  * @file     database.c
  * @brief    Parameter database — DF_ (EEPROM) and LD_ (live data) tables
  ******************************************************************************
  */

#include "database.h"

/* Private types -------------------------------------------------------------*/
typedef struct {
    uint32_t value;
    uint32_t min;
    uint32_t max;
    uint32_t def;
} df_entry_t;

/* DF_ parameter table (value, min, max, default) ----------------------------*/
static df_entry_t df_table[DF_NUM_PARAMS] = {
    /* DF_BLOCK_RETRY_DELAY_SEC   */ {  1,   1,  10,   1 },
    /* DF_OPEN_TRIGGER_ANGLE      */ { 10,   5,  30,  10 },
    /* DF_OPEN_DIFF_ANGLE         */ { 10,  10,  30,  10 },
    /* DF_LOCK_ACTIVE_TIME        */ { 20,  10,  50,  20 },
    /* DF_BLOCK_DETECT_ANGLE      */ {  2,   1,  20,   2 },
    /* DF_BLOCK_DETECT_TIME       */ { 500, 100, 2000, 500 },
    /* DF_TIME_WINDOW             */ {  5,   1,  20,   5 },
    /* DF_M1_START_DUTY           */ { 20,   1,  90,  20 },
    /* DF_M1_MAX_DUTY             */ { 80,   1,  90,  80 },
    /* DF_M2_START_DUTY           */ { 20,   1,  90,  20 },
    /* DF_M2_MAX_DUTY             */ { 80,   1,  90,  80 },
    /* DF_M3_START_DUTY           */ { 30,   1,  90,  30 },
    /* DF_M3_MAX_DUTY             */ { 50,   1,  50,  50 },
    /* DF_M1_OPEN_ANGLE           */ {100,  50, 120, 100 },
    /* DF_M2_OPEN_ANGLE           */ {100,  50, 120, 100 },
    /* DF_M1_OPEN_REV_DUTY        */ { 20,   5,  50,  20 },
    /* DF_M1_OPEN_REV_DUTY_DELTA  */ {  5,   1,  20,   5 },
    /* DF_M1_CLOSE_REV_DUTY       */ { 20,   5,  50,  20 },
    /* DF_M1_CLOSE_FWD_DUTY_DELTA */ {  5,   1,  20,   5 },
    /* DF_M1_ZERO_MIN             */ { 150, 100, 250, 150 },
    /* DF_M1_ZERO_MAX             */ { 210, 100, 350, 210 },
    /* DF_M2_ZERO_MIN             */ { 150, 100, 250, 150 },
    /* DF_M2_ZERO_MAX             */ { 210, 100, 350, 210 },
    /* DF_MAX_OPEN_OPERATION_TIME */ { 30,   5, 120,  30 },
    /* DF_M1_CLOSE_HOLD_TIME      */ {  2,   1,  10,   2 },
    /* DF_M1_ZERO_ERROR           */ {  5,   1,  20,   5 },
    /* DF_HOME_ZERO_SAMPLE_TIME   */ {  1,   1,   5,   1 },
    /* DF_M2_ZERO_ERROR           */ {  5,   1,  20,   5 },
    /* DF_AUTO_TEST_CYCLES        */ {  0,   0, 200,   0 },
};

/* LD_ live data table -------------------------------------------------------*/
static uint32_t ld_table[LD_NUM_PARAMS];

/**
  * @brief  Initialize database: load DF_ defaults and clear LD_ values.
  *         Can be extended to read from flash/EEPROM.
  */
void db_init(void)
{
    uint8_t i;
    for (i = 0; i < DF_NUM_PARAMS; i++) {
        df_table[i].value = df_table[i].def;
    }
    for (i = 0; i < LD_NUM_PARAMS; i++) {
        ld_table[i] = 0u;
    }
}

/**
  * @brief  Read a DF_ (stored) parameter by ID.
  * @param  id  Parameter ID from df_param_id_t
  * @retval Parameter value (clamped to [min, max])
  */
uint32_t db_get_param(uint8_t id)
{
    if (id >= DF_NUM_PARAMS) return 0u;
    return df_table[id].value;
}

/**
  * @brief  Write a DF_ (stored) parameter by ID.
  * @param  id     Parameter ID from df_param_id_t
  * @param  value  New value (clamped to [min, max])
  */
void db_set_param(uint8_t id, uint32_t value)
{
    if (id >= DF_NUM_PARAMS) return;
    if (value < df_table[id].min) value = df_table[id].min;
    if (value > df_table[id].max) value = df_table[id].max;
    df_table[id].value = value;
}

/**
  * @brief  Read a LD_ (live) data value by ID.
  * @param  id  Live data ID from ld_param_id_t
  * @retval Live data value
  */
uint32_t db_get_live(uint8_t id)
{
    if (id >= LD_NUM_PARAMS) return 0u;
    return ld_table[id];
}

/**
  * @brief  Write a LD_ (live) data value by ID.
  * @param  id     Live data ID from ld_param_id_t
  * @param  value  New value
  */
void db_set_live(uint8_t id, uint32_t value)
{
    if (id >= LD_NUM_PARAMS) return;
    ld_table[id] = value;
}
