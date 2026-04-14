#ifndef _BOOTCONFIG_
#define _BOOTCONFIG_

#define USER_FLASH_BASE 0x08008000  // 32K, page 16 or 32

#if defined(AT32F413Rx_HD)
#define PAGE_SIZE               (uint16_t)0x800         // 2k page size
//#define FLASH_START_ADDRESS     ((uint32_t)0x8007000)   // EEPROM start address
#define FLASH_START_ADDRESS     ((uint32_t)(FLASH_BASE_ADDRESS+FLASH_ALL_SIZE - PAGE_SIZE*8))   // EEPROM start address
#define FLASH_BASE_ADDRESS      ((uint32_t)0x8000000)
#define FLASH_ALL_SIZE          (1024*256)              // 256K
#define USER_FLASH_BASE_PAGE    16

#elif defined(AT32F413Cx_MD)
#define PAGE_SIZE (             uint16_t)0x400  // 1K page size
//#define FLASH_START_ADDRESS     ((uint32_t)0x8007000) // page 30*1K
#define FLASH_START_ADDRESS     ((uint32_t)0x800F000) // page 60*1K
#define FLASH_BASE_ADDRESS      ((uint32_t)0x8000000)
#define FLASH_ALL_SIZE          (1024*64) // 64K
#define USER_FLASH_BASE_PAGE    32
#endif

#define PAGE_NUMBER(x) (x/PAGE_SIZE)
#define BL_CONFIG_ADDRESS       0x0800F800      // bootloader option
#define BL_CONFIG_PAGE          PAGE_NUMBER(BL_CONFIG_ADDRESS)
#define BOOT_KEY                0x53290921

// this structure of data should locat @0x8007c00
typedef struct{
  uint32_t bootOption;  // for bootloader to check if enter loader mode
  uint32_t fw_version;  // 20210827, yyyymmdd
  uint32_t product_id;  // 00061000, project number + board number
  uint32_t serialNum[2];// number on board, 21060001 yymmxxxx, xxxx: serial number
}boot_info;

#endif