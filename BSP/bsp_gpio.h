#pragma once
#include <stdbool.h>
#include <stdint.h>

void bsp_gpio_init(void);

/* Inputs */
bool bsp_read_pb10(void);
bool bsp_read_pb3(void);
bool bsp_read_pc0(void);
bool bsp_read_pc1(void);

/* Driver enables */
void bsp_drv1_enable(bool on);
void bsp_drv2_enable(bool on);

/* Relays */
void bsp_relay_drv1_to_door1(bool on);
void bsp_relay_drv2_to_door2(bool on);
void bsp_relay_drv2_to_lock(bool on);
