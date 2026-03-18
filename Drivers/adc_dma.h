#pragma once
#include <stdint.h>

typedef struct
{
  uint16_t pot1_pb0;
  uint16_t pot2_pb1;
  uint16_t i1_pa0;
  uint16_t i2_pa1;
  int16_t  pot1_vel;
  int16_t  pot2_vel;
} adc_snapshot_t;

void adc_dma_init(void);
void adc_dma_get_snapshot_1khz(adc_snapshot_t *out);
