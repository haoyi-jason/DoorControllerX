#ifndef _NVM_CONFIG_
#define _NVM_CONFIG_

#define SZ_NVM_BOARD    64
#define SZ_NVM_RF       64 
#define SZ_NVM_PPMU     512

#define OFFSET_NVM_BOARD        0
#define OFFSET_NVM_RF           OFFSET_NVM_BOARD + SZ_NVM_BOARD
#define OFFSET_NVM_PPMU         OFFSET_NVM_RF + SZ_NVM_RF

#endif