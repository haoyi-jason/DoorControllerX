#include "app_defs.h"
#include "bin_protocol_lite.h"
#include "eeprom.h"
#include "database.h"

static void delay()
{
	uint16_t reload = 0xfff;
	while(reload--){
		_nop();
	}
}

static uint8_t checksum(uint8_t *data, uint8_t size)
{
    uint8_t ret = 0;
    for(uint8_t i=0;i<size;i++){
        ret += data[i];
    }

    return (~ret & 0xFF);
}

static uint8_t read_char(char *c)
{
    if(appRuntime.comm->r_idx != appRuntime.comm->w_idx){
        *c = appRuntime.comm->rxBuffer[appRuntime.comm->r_idx & UART_BUFFER_MASK];
        appRuntime.comm->r_idx++;
        return 1;
    }
    return 0;
}

uint8_t get_command()
{
    char c;
    uint8_t ret = 0;
    do{
        ret = read_char(&c);
        if(ret == 1){
            if(appRuntime.protocol->head_in){
                appRuntime.protocol->buffer[appRuntime.protocol->size++] = c;
                if(appRuntime.protocol->size > SZ_INDEX && appRuntime.protocol->size == (appRuntime.protocol->buffer[SZ_INDEX] + PKT_SZ)){
                    appRuntime.protocol->head_in = 0;
                    return 1;
                  }
                  if(appRuntime.protocol->size == UART_BUFFER_SIZE){
                    appRuntime.protocol->head_in = 0;
                    appRuntime.protocol->size = 0;
                  }
            }
            else if(c == START_PREFIX){
                appRuntime.protocol->head_in = 1;
                appRuntime.protocol->buffer[0] = c;
                appRuntime.protocol->size = 1;
            }
        }
        delay();
    }while(ret == 1);
    return 0;
}

static void streamWrite(uint8_t *data, uint8_t size)
{
	if(_tidle0 == 1){
	    for(uint8_t i=0;i<size;i++){
	        appRuntime.comm->txBuffer[i] = data[i];
	       
	    }
        appRuntime.comm->tx_size = size;
        appRuntime.comm->tx_idx = 0;
	    // start TX
	    _txr_rxr0 = appRuntime.comm->txBuffer[0];
	    appRuntime.comm->tx_idx++;
	}
}

static void process_data_flash(uint8_t option)
{
    uint8_t *data = &appRuntime.protocol->buffer[3];
//    uint8_t *ptr = appRuntime.protocol->buffer;
    uint8_t buffer[16];
    uint8_t *wptr = buffer;
    uint8_t pkt_sz = appRuntime.protocol->size;
    uint16_t address = (data[3]<<8 | data[2]);
    uint8_t nof_reg = data[1] & 0x7F;
    uint8_t rw = data[1] & 0x80;
    uint8_t i;
    if(rw == 0x00){ // write
      int8_t szWrite = 0;
      uint8_t sz_to_write = pkt_sz - 2;
      wptr = (uint8_t*)&data[4];
      for(i=0;i<nof_reg;i++){
        if(option == 1){
            szWrite = db_write_dataflash(address, wptr);
        }
        sz_to_write -= szWrite;
        address += szWrite;
        wptr += szWrite;
        if(sz_to_write == 0) break;
      }
    }
  
    wptr = buffer;
    *wptr++ = START_PREFIX;
    *wptr++ = FC_DATAFLASH;
    *wptr++ = appRuntime.protocol->id;
    wptr++;  // reserve for packet length
    *wptr++ = data[1]; // rw + nof reg
    *wptr++ = data[2];
    *wptr++ = data[3];
  
//    uint8_t sz_to_read = pkt_sz - 2;
    int8_t sz_read = 0;
    address = (data[3]<<8 | data[2]);
    wptr = &buffer[7];
    for(i=0;i<nof_reg;i++){
        if(option == 1){
            sz_read = db_read_dataflash(address,wptr);
        }
        else if(option == 2){
            sz_read = db_read_livedata(address,wptr);
            buffer[1] = FC_LIVEDATA;
        }
      address += sz_read;
      wptr+= sz_read;
    }
    buffer[3] = (wptr - &buffer[3]-1);
    *wptr++ = checksum(&buffer[1],(wptr-buffer-2));
    *wptr = END_SUFFIX;
    streamWrite(buffer, wptr-buffer+2);
  
}

void protocol_process()
{
    if(get_command() == 1){
        // check crc and parse command
        uint8_t cmd = appRuntime.protocol->buffer[1] & 0x7F;
        if(cmd == FC_DATAFLASH){
            process_data_flash(1);
        }
        else if(cmd == FC_LIVEDATA){
            process_data_flash(2);
        }
    }    
}

void bin_protocol_init()
{
	appRuntime.comm->w_idx = 0;
	appRuntime.comm->r_idx = 0;
	appRuntime.comm->tx_idx = 0;
	appRuntime.comm->tx_size = 0;
	
	//streamWrite("Hello,World!\n",13);
}

