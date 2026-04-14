#ifndef _BIN_PROTOCOL_LITE_
#define _BIN_PROTOCOL_LITE_

#define START_PREFIX		0x02
#define END_SUFFIX			0x03

#define SZ_INDEX        3
#define PKT_SZ          6


enum FC_CODE_e{
    FC_DATAFLASH = 0x10,
    FC_LIVEDATA = 0x20,
    FC_COMMAND = 0x30,
    FC_DATA = 0x40
  };



  void protocol_process();
void bin_protocol_init();
#endif