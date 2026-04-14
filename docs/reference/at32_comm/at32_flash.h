#ifndef _AT32_FLASH_
#define _AT32_FLASH_

#include "hal.h"
#include "at32f4xx.h"
//#include "stm32f1xx.h"

/*
#if defined(AT32F413Rx_HD)
#define PAGE_SIZE (uint16_t)0x800 // 2k page size
#define FLASH_START_ADDRESS     ((uint32_t)0x803E000) // page 124*1K*2
#define FLASH_BASE_ADDRESS      ((uint32_t)0x8000000)
#define FLASH_ALL_SIZE          (1024*256) // 256K
#elif defined(AT32F413Cx_MD)
#define PAGE_SIZE (uint16_t)0x400  // 1K page size
#define FLASH_START_ADDRESS     ((uint32_t)0x800F000) // page 60*1K
#define FLASH_BASE_ADDRESS      ((uint32_t)0x8000000)
#define FLASH_ALL_SIZE          (1024*64) // 64K
#endif
*/

// move to new configuration to support bootloader & remote upgrade

//#if defined(AT32F413Rx_HD)
//#define PAGE_SIZE               (uint16_t)0x800         // 2k page size
//#define FLASH_START_ADDRESS     ((uint32_t)0x8007000)   // EEPROM start address
//#define FLASH_BASE_ADDRESS      ((uint32_t)0x8000000)
//#define FLASH_ALL_SIZE          (1024*256)              // 256K
//#define USER_FLASH_BASE_PAGE    16
//
//#elif defined(AT32F413Cx_MD)
//#define PAGE_SIZE (             uint16_t)0x400  // 1K page size
//#define FLASH_START_ADDRESS     ((uint32_t)0x8007000) // page 30*1K
//#define FLASH_BASE_ADDRESS      ((uint32_t)0x8000000)
//#define FLASH_ALL_SIZE          (1024*128) // 128K
//#define USER_FLASH_BASE_PAGE    32
//#endif


void flash_Read(uint32_t ReadAddress,uint16_t *pBuffer,uint16_t NumToRead);
void flash_Write(uint32_t WriteAddress,uint16_t *pBuffer,uint16_t NumToWrite);
void flash_ErasePage(uint32_t page);
#endif