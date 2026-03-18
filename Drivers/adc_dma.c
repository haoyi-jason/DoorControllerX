#include "adc_dma.h"

#include "at32f4xx.h"
#include "at32f4xx_rcc.h"
#include "at32f4xx_gpio.h"
#include "at32f4xx_adc.h"
#include "at32f4xx_dma.h"

#define ADC_CH_COUNT 4

static volatile uint16_t s_adc_dma_buf[ADC_CH_COUNT];
static adc_snapshot_t g_adc;
static uint16_t s_pot1_prev, s_pot2_prev;

static uint16_t iir_u16(uint16_t y, uint16_t x, uint8_t shift)
{
  int32_t diff = (int32_t)x - (int32_t)y;
  return (uint16_t)((int32_t)y + (diff >> shift));
}

void adc_dma_init(void)
{
  RCC_APB2PeriphClockCmd(RCC_APB2PERIPH_GPIOA |
                         RCC_APB2PERIPH_GPIOB |
                         RCC_APB2PERIPH_ADC1  |
                         RCC_APB2PERIPH_AFIO, ENABLE);

  RCC_AHBPeriphClockCmd(RCC_AHBPERIPH_DMA1, ENABLE);

  GPIO_InitType gi;
  GPIO_StructInit(&gi);
  gi.GPIO_Mode = GPIO_Mode_IN_ANALOG;
  gi.GPIO_MaxSpeed = GPIO_MaxSpeed_2MHz;

  gi.GPIO_Pins = GPIO_Pins_0 | GPIO_Pins_1;
  GPIO_Init(GPIOA, &gi);

  gi.GPIO_Pins = GPIO_Pins_0 | GPIO_Pins_1;
  GPIO_Init(GPIOB, &gi);

  DMA_Flexible_Config(DMA1, Flex_Channel1, DMA_FLEXIBLE_ADC1);

  DMA_InitType di;
  DMA_DefaultInitParaConfig(&di);

  di.DMA_PeripheralBaseAddr  = (uint32_t)&ADC1->RDOR;
  di.DMA_MemoryBaseAddr      = (uint32_t)&s_adc_dma_buf[0];
  di.DMA_Direction           = DMA_DIR_PERIPHERALSRC;
  di.DMA_BufferSize          = ADC_CH_COUNT;
  di.DMA_PeripheralInc       = DMA_PERIPHERALINC_DISABLE;
  di.DMA_MemoryInc           = DMA_MEMORYINC_ENABLE;
  di.DMA_PeripheralDataWidth = DMA_PERIPHERALDATAWIDTH_HALFWORD;
  di.DMA_MemoryDataWidth     = DMA_MEMORYDATAWIDTH_HALFWORD;
  di.DMA_Mode                = DMA_MODE_CIRCULAR;
  di.DMA_Priority            = DMA_PRIORITY_HIGH;
  di.DMA_MTOM                = DMA_MEMTOMEM_DISABLE;

  DMA_Reset(DMA1_Channel1);
  DMA_Init(DMA1_Channel1, &di);
  DMA_ChannelEnable(DMA1_Channel1, ENABLE);

  ADC_InitType ai;
  ADC_StructInit(&ai);

  ai.ADC_Mode = ADC_Mode_Independent;
  ai.ADC_ScanMode = ENABLE;
  ai.ADC_ContinuousMode = ENABLE;
  ai.ADC_ExternalTrig = ADC_ExternalTrig_None;
  ai.ADC_DataAlign = ADC_DataAlign_Right;
  ai.ADC_NumOfChannel = 4;
  ADC_Init(ADC1, &ai);

  /* Verify mapping in datasheet:
     PB0->ch8, PB1->ch9, PA0->ch0, PA1->ch1 */
  ADC_RegularChannelConfig(ADC1, ADC_Channel_8, 1, ADC_SampleTime_28_5);
  ADC_RegularChannelConfig(ADC1, ADC_Channel_9, 2, ADC_SampleTime_28_5);
  ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 3, ADC_SampleTime_28_5);
  ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 4, ADC_SampleTime_28_5);

  ADC_DMACtrl(ADC1, ENABLE);
  ADC_Ctrl(ADC1, ENABLE);

  ADC_ExternalTrigConvCtrl(ADC1, DISABLE);

  ADC_RstCalibration(ADC1);
  while (ADC_GetResetCalibrationStatus(ADC1) == SET) { }

  ADC_StartCalibration(ADC1);
  while (ADC_GetCalibrationStatus(ADC1) == SET) { }

  ADC_SoftwareStartConvCtrl(ADC1, ENABLE);

  g_adc = (adc_snapshot_t){0};
  s_pot1_prev = 0;
  s_pot2_prev = 0;
}

void adc_dma_get_snapshot_1khz(adc_snapshot_t *out)
{
  uint16_t pot1 = s_adc_dma_buf[0];
  uint16_t pot2 = s_adc_dma_buf[1];
  uint16_t i1   = s_adc_dma_buf[2];
  uint16_t i2   = s_adc_dma_buf[3];

  g_adc.pot1_pb0 = iir_u16(g_adc.pot1_pb0, pot1, 2);
  g_adc.pot2_pb1 = iir_u16(g_adc.pot2_pb1, pot2, 2);
  g_adc.i1_pa0   = iir_u16(g_adc.i1_pa0,   i1,   2);
  g_adc.i2_pa1   = iir_u16(g_adc.i2_pa1,   i2,   2);

  g_adc.pot1_vel = (int16_t)((int32_t)g_adc.pot1_pb0 - (int32_t)s_pot1_prev);
  g_adc.pot2_vel = (int16_t)((int32_t)g_adc.pot2_pb1 - (int32_t)s_pot2_prev);

  s_pot1_prev = g_adc.pot1_pb0;
  s_pot2_prev = g_adc.pot2_pb1;

  *out = g_adc;
}
