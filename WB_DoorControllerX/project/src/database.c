/**
  ******************************************************************************
  * @file     database.c
  * @brief    Parameter database — DF_ (EEPROM) and LD_ (live data) tables
  ******************************************************************************
  */

#include "database.h"
#include "at32f413_flash.h"

/* Private constants --------------------------------------------------------*/
#define DB_FLASH_BASE_ADDR      ((uint32_t)0x0803F000u)
#define DB_FLASH_MAGIC          ((uint32_t)0x44463130u) /* "DF10" */
#define DB_FLASH_VERSION        ((uint32_t)0x00000001u)

/* Private types -------------------------------------------------------------*/
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t checksum;
    uint32_t values[DF_NUM_PARAMS];
} df_flash_image_t;

typedef struct {
    uint32_t value;
    uint32_t min;
    uint32_t max;
    uint32_t def;
} df_entry_t;

/* DF_ parameter table (value, min, max, default) ----------------------------*/
static df_entry_t df_table[DF_NUM_PARAMS] = {
    /* DF_BLOCK_RETRY_DELAY_SEC   */ {  2,   1,  10,   2 },
    /* DF_OPEN_TRIGGER_ANGLE      */ { 10,   5,  30,  10 },
    /* DF_OPEN_DIFF_ANGLE         */ { 10,  10,  30,  10 },
    /* DF_LOCK_ACTIVE_TIME        */ { 10,  10,  50,  10 },
    /* DF_BLOCK_DETECT_ANGLE      */ {  2,   1,  20,   2 },
    /* DF_BLOCK_DETECT_TIME       */ { 1000, 100, 2000, 1000 },
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
    /* DF_M1_CLOSE_REV_DUTY_DELTA */ {  5,   1,  20,   5 },
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
    /* DF_AUTO_TEST_OPEN_HOLD_SEC */ {  1,   0,  60,   1 },
    /* DF_M1_STARTUP_RELIEF_MS    */ {500, 100, 2000, 500 },
    /* DF_M1_PID_KP_X1000         */ {733,   0, 65535, 733 },
    /* DF_M1_PID_KI_X1000         */ {202,   0, 65535, 202 },
    /* DF_M1_PID_KD_X1000         */ {  0,   0, 65535,   0 },
    /* DF_M2_PID_KP_X1000         */ {733,   0, 65535, 733 },
    /* DF_M2_PID_KI_X1000         */ {202,   0, 65535, 202 },
    /* DF_M2_PID_KD_X1000         */ {  0,   0, 65535,   0 },
    /* DF_TUNE_TARGET_MOTOR       */ {  1,   1,    2,   1 },
    /* DF_TUNE_SETPOINT_DEG       */ {100,   5,  180, 100 },
    /* DF_TUNE_PWM_DUTY           */ { 25,   1,   90,  25 },
    /* DF_TUNE_TIMEOUT_SEC        */ {  8,   1,   30,   8 },
    /* DF_M1_DECEL_ZONE_DEG       */ { 15,   0,    30,  15 },
    /* DF_M1_DECEL_MAX_DUTY       */ { 25,   5,    80,  25 },
    /* DF_M2_DECEL_ZONE_DEG       */ { 15,   0,    30,  15 },
    /* DF_M2_DECEL_MAX_DUTY       */ { 25,   5,    80,  25 },
    /* DF_BLOCK_NO_CHECK_ANGLE    */ { 20,   0,    45,  20 },
    /* DF_M1_BLOCK_FREE_ANGLE     */ { 80,   0,   120,  80 },
    /* DF_M2_BLOCK_FREE_ANGLE     */ { 80,   0,   120,  80 },
};

/* LD_ live data table -------------------------------------------------------*/
static uint32_t ld_table[LD_NUM_PARAMS];

/* Private helpers -----------------------------------------------------------*/
static void db_load_defaults(void)
{
  uint8_t i;

  for (i = 0; i < DF_NUM_PARAMS; i++) {
    df_table[i].value = df_table[i].def;
  }
}

static uint32_t db_calc_checksum(const df_flash_image_t *image)
{
  uint32_t checksum = image->version ^ image->count;
  uint32_t i;

  for (i = 0u; i < DF_NUM_PARAMS; i++) {
    checksum ^= image->values[i];
  }

  return checksum;
}

static uint8_t db_flash_image_valid(const df_flash_image_t *image)
{
  if (image->magic != DB_FLASH_MAGIC) return 0u;
  if (image->version != DB_FLASH_VERSION) return 0u;
  if (image->count != DF_NUM_PARAMS) return 0u;
  if (image->checksum != db_calc_checksum(image)) return 0u;
  return 1u;
}

static void db_flash_load(void)
{
  const df_flash_image_t *image = (const df_flash_image_t *)DB_FLASH_BASE_ADDR;
  uint8_t i;

  if (!db_flash_image_valid(image)) {
    return;
  }

  for (i = 0u; i < DF_NUM_PARAMS; i++) {
    uint32_t value = image->values[i];

    if (value < df_table[i].min || value > df_table[i].max) {
      df_table[i].value = df_table[i].def;
    } else {
      df_table[i].value = value;
    }
  }
}

static void db_flash_save(void)
{
  df_flash_image_t image;
  flash_status_type status;
  uint32_t *words = (uint32_t *)&image;
  uint32_t word_count = (uint32_t)(sizeof(df_flash_image_t) / sizeof(uint32_t));
  uint32_t i;

  image.magic = DB_FLASH_MAGIC;
  image.version = DB_FLASH_VERSION;
  image.count = DF_NUM_PARAMS;
  for (i = 0u; i < DF_NUM_PARAMS; i++) {
    image.values[i] = df_table[i].value;
  }
  image.checksum = db_calc_checksum(&image);

  flash_unlock();
  flash_flag_clear(FLASH_ODF_FLAG | FLASH_PRGMERR_FLAG | FLASH_EPPERR_FLAG);

  status = flash_sector_erase(DB_FLASH_BASE_ADDR);
  if (status == FLASH_OPERATE_DONE) {
    for (i = 0u; i < word_count; i++) {
      status = flash_word_program(DB_FLASH_BASE_ADDR + (i * 4u), words[i]);
      if (status != FLASH_OPERATE_DONE) {
        break;
      }
    }
  }

  flash_lock();
}

/**
  * @brief  Initialize database: load DF_ defaults and clear LD_ values.
  *         Loads saved DF_ values from reserved MCU flash sector when valid.
  */
void db_init(void)
{
    uint8_t i;

  db_load_defaults();
  db_flash_load();

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
  uint32_t clamped = value;

    if (id >= DF_NUM_PARAMS) return;
  if (clamped < df_table[id].min) clamped = df_table[id].min;
  if (clamped > df_table[id].max) clamped = df_table[id].max;
  if (df_table[id].value == clamped) return;

  df_table[id].value = clamped;
  db_flash_save();
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
