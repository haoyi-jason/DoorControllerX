#ifndef _VERSION_
#define _VERSION_

#define HW_VER_MAIN     0x01
#define HW_VER_MAJOR    0x01
#define HW_VER_MINOR    0x00
#define HW_VER_PATCH    0x00

#define SW_VER_MAIN     0x25
#define SW_VER_MAJOR    0x08
#define SW_VER_MINOR    0x05
#define SW_VER_PATCH    0x01

//#pragma location = 0x0803FF00
#pragma location = 0x08000400
__root const uint8_t BuildVersion[8]
={HW_VER_MAIN,HW_VER_MAJOR,HW_VER_MINOR,HW_VER_PATCH,
  SW_VER_MAIN,SW_VER_MAJOR,SW_VER_MINOR,SW_VER_PATCH};
#endif