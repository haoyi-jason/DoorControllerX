#include <string.h>
#include "app_defs.h"
#include "database.h"
#include "eeprom.h"

#define PARAM_U8_OFFSET     0x0
#define PARAM_U16_OFFSET    0x80
#define PARAM_U32_OFFSET    0x100
#define PARAM_I8_OFFSET     0x180
#define PARAM_I16_OFFSET    0x200
#define PARAM_I32_OFFSET    0x280
#define PARAM_F32_OFFSET	0x300


#define MAX_U8_VARS		40
#define MAX_U16_VARS	40
#define MAX_U32_VARS	8
#define MAX_I8_VARS		24
#define MAX_I16_VARS	12
#define MAX_I32_VARS	8
#define MAX_F32_VARS	8

#define MAX_U8_LDS		8
#define MAX_U16_LDS		20
#define MAX_U32_LDS		1
#define MAX_I8_LDS		8
#define MAX_I16_LDS		1
#define MAX_I32_LDS		1
#define MAX_F32_LDS		1


struct database_s{
	uint8_t u8_vars[PARAM_INDEX(MAX_U8_VARS)];
	uint16_t u16_vars[PARAM_INDEX(MAX_U16_VARS)];
	uint32_t u32_vars[PARAM_INDEX(MAX_U32_VARS)];
	int8_t i8_vars[PARAM_INDEX(MAX_I8_VARS)];
	int16_t i16_vars[PARAM_INDEX(MAX_I16_VARS)];
	int32_t i32_vars[PARAM_INDEX(MAX_I32_VARS)];
	uint32_t f32_vars[PARAM_INDEX(MAX_F32_VARS)];
};
struct livedata_s{
	uint8_t u8_ld[PARAM_INDEX(MAX_U8_LDS)];
	uint16_t u16_ld[PARAM_INDEX(MAX_U16_LDS)];
	uint32_t u32_ld[PARAM_INDEX(MAX_U32_LDS)];
	int8_t i8_ld[PARAM_INDEX(MAX_I8_LDS)];
	int16_t i16_ld[PARAM_INDEX(MAX_I16_LDS)];
	int32_t i32_ld[PARAM_INDEX(MAX_I32_LDS)];
	uint32_t f32_ld[PARAM_INDEX(MAX_F32_LDS)];
};

//struct database_s database;
struct livedata_s livedata;

void database_init()
{
	volatile uint8_t i;
	
	for(i=0;i<MAX_U8_LDS;i++){
		livedata.u8_ld[i] = 0;
	}
	
	for(i=0;i<MAX_U16_LDS;i++){
		livedata.u16_ld[i] = 0;
	}
	
	for(i=0;i<MAX_U32_LDS;i++){
		livedata.u32_ld[i] = 0;
	}

	for(i=0;i<MAX_I8_LDS;i++){
		livedata.i8_ld[i] = 0;
	}
	
	for(i=0;i<MAX_I16_LDS;i++){
		livedata.i16_ld[i] = 0;
	}

	for(i=0;i<MAX_I32_LDS;i++){
		livedata.i32_ld[i] = 0;
	}

	for(i=0;i<MAX_F32_LDS;i++){
		livedata.f32_ld[i] = 0;
	}
	


}

byte db_read_dataflash(u16 regAddr, u8 *data)
{
//	uint8_t buffer[4];
	uint8_t ret = 0;
    volatile u8 type = PARAM_TYPE(regAddr);
    volatile u8 index = PARAM_INDEX(regAddr);
    uint8_t i;
    volatile word address;
    switch(type){
    	case 0:{// u8
   			if((regAddr & LIVE_DATA_MASK) == 0){
   		 		if(index < MAX_U8_VARS){
    				address = PARAM_U8_OFFSET + index;
	    			*data = ReadEEPROM(address);
	    			ret = 1;
    			}
    		}
    		else{
    			if(index < MAX_U8_LDS){
	    			*data = livedata.u8_ld[index];
	    			ret = 1;
    			}
    		}
    	}
    	break;
    	case 1:{ // u16
			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_U16_VARS){
	    			address = PARAM_U16_OFFSET + index * 2;
	    			for(i=0;i<2;i++,address++){
	    				*data++ = ReadEEPROM(address);					
	    			}
		    		ret = 2;
		    	}
			}
			else{
	    		if(index < MAX_U16_LDS){
		    		memcpy(data,&livedata.u16_ld[index],2);
		    		ret = 2;
	    		}
			}
    		}
    	break;
    	case 2:{ // u32;
			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_U32_VARS){
	    			address = PARAM_U32_OFFSET + index * 4;
	    			for(i=0;i<4;i++,address++){
	    				*data++ = ReadEEPROM(address);	
	    			}
		    		ret = 4;	
	    		}
			}
			else{
	    		if(index < MAX_U32_LDS){
		    		memcpy(data,&livedata.u32_ld[index],4);
		    		ret = 4;	
	    		}
			}
    	}
    	break;	
    	case 3:{// I8
   			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_I8_VARS){
	    			address = PARAM_I8_OFFSET + index;
	    			*data = ReadEEPROM(address);
	    			ret = 1;	
	    		}
   			}
   			else{
	    		if(index < MAX_I8_LDS){
	    			*data = livedata.i8_ld[index];
	    			ret = 1;	
	    		}
   			}
    	}
    	break;
    	case 4:{ // i16
   			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_I16_VARS){
	    			address = PARAM_I16_OFFSET + index*2;
	    			for(i=0;i<2;i++,address++){
	    				*data++ = ReadEEPROM(address);	
	    			}
		    		ret = 2;
	    		}
   			}
   			else{
	    		if(index < MAX_I16_LDS){
		    		memcpy(data,&livedata.i16_ld[index],2);
		    		ret = 2;
	    		}
   			}
    		
    	}
    	break;
    	case 5:{ // i32;
   			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_I32_VARS){
	    			address = PARAM_I32_OFFSET + index*4;
	    			for(i=0;i<4;i++,address++){
	    				*data++ = ReadEEPROM(address);	
	    			}
	    			ret = 4;
	    		}
   			}
   			else{
	    		if(index < MAX_I32_LDS){
	    			memcpy(data,&livedata.i32_ld[index],4);
	    			ret = 4;
	    		}
   			}
    	}
    	break;	
    	case 6:{ // f32;
   			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_F32_VARS){
	    			address = PARAM_F32_OFFSET + index * 4;
	    			for(i=0;i<4;i++,address++){
	    				*data++ = ReadEEPROM(address);	
	    			}
	    			ret = 4;
	    		}
   			}
   			else{
	    		if(index < MAX_F32_LDS){
	    			memcpy(&livedata.f32_ld[index],data,4);
	    			ret = 4;
	    		}
   			}
    	}
    	break;	
    }
    return ret;
}

byte db_write_dataflash(u16 regAddr, u8 *data)
{
	uint8_t ret = 0;
    volatile u8 type = PARAM_TYPE(regAddr);
    volatile u8 index = PARAM_INDEX(regAddr);
    uint8_t i;
    volatile word address;
    switch(type){
    	case 0:{// u8
   			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_U8_VARS){
	    			address = PARAM_U8_OFFSET + index;
	    			WriteEEPROM(address,*data);
	    			ret = 1;
	    		}
   			}
   			else{
	    		if(index < MAX_U8_LDS){
	    			livedata.u8_ld[index] = *data;	
	    			ret = 1;
	    		}   			
    		}
    	}
    	break;
    	case 1:{ // u16
   			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_U16_VARS){
	    			address = PARAM_U16_OFFSET + index * 2;
	    			for(i=0;i<2;i++,address++){
	    				WriteEEPROM(address, *data++);	
	    			}
		    		ret = 2;
	    		}
   			}
   			else{
	    		if(index < MAX_U16_LDS){
	    			memcpy(&livedata.u16_ld[index],data,2);
	    			ret = 2;
	    		}   			
   			}
    	}
    	break;
    	case 2:{ // u32;
   			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_U32_VARS){
	    			address = PARAM_U32_OFFSET + index * 4;
	    			for(i=0;i<4;i++,address++){
	    				WriteEEPROM(address, *data++);	
	    			}
		    		ret = 4;	
	    		}
   			}
   			else{
	    		if(index < MAX_U32_LDS){
	    			memcpy(&livedata.u32_ld[index],data,4);
	    			ret = 4;
	    		}   			
   			}
    	}
    	break;	
    	case 3:{// I8
   			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_I8_VARS){
	    			address = PARAM_I8_OFFSET + index;
	    			WriteEEPROM(address, *data);
	    			ret = 1;	
	    		}
   			}
   			else{
	    		if(index < MAX_I8_LDS){
	    			livedata.i8_ld[index] = *data;	
	    			ret = 1;
	    		}   			
   			}
    	}
    	break;
    	case 4:{ // i16
   			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_I16_VARS){
	    			address = PARAM_I16_OFFSET + index * 2;
	    			for(i=0;i<2;i++,address++){
	    				WriteEEPROM(address, *data++);	
	    			}
		    		ret = 2;
	    		}
   			}
   			else{
	    		if(index < MAX_I16_LDS){
	    			memcpy(&livedata.i16_ld[index],data,2);
	    			ret = 2;
	    		}   			
   			}
    	}
    	break;
    	case 5:{ // u32;
   			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_I32_VARS){
	    			address = PARAM_I32_OFFSET + index * 4;
	    			for(i=0;i<4;i++,address++){
	    				WriteEEPROM(address, *data++);	
	    			}
	    			ret = 4;
	    		}
   			}
   			else{
	    		if(index < MAX_I32_LDS){
	    			memcpy(&livedata.u32_ld[index],data,4);
	    			ret = 4;
	    		}   			
   			}
    	}
    	break;	
    	case 6:{ // f32;
   			if((regAddr & LIVE_DATA_MASK) == 0){
	    		if(index < MAX_F32_VARS){
	    			address = PARAM_F32_OFFSET + index * 4;
	    			for(i=0;i<4;i++,address++){
	    				WriteEEPROM(address,*data++);	
	    			}
	    			ret = 4;
	    		}
   			}
   			else{
	    		if(index < MAX_F32_LDS){
	    			memcpy(&livedata.f32_ld[index],data,4);
	    			ret = 4;
	    		}   			
   			}
    	}
    	break;	
    }
    return ret;

}

//byte db_read_livedata(u16 regAddr, u8 *data)
//{
//	uint8_t ret = 0;
//    volatile u8 type = PARAM_TYPE(regAddr);
//    u8 index = PARAM_INDEX(regAddr);
//    switch(type){
//    	case 0:{// u8
//    		//#ifdef NOF_U8_LD
//    		if(index < MAX_U8_LDS){
//    			*data = livedata.u8_ld[index];	
//    			ret = 1;
//    		}
//    		//#endif
//    	}
//    	break;
//    	case 1:{ // u16
//    		//#ifdef NOF_U16_LD
//    		if(index < MAX_U16_LDS){
//	    		memcpy(data,&livedata.u16_ld[index],2);
//	    		ret = 2;
//    		}
//    		//#endif
//    	}
//    	break;
//    	case 2:{ // u32;
//    		//#ifdef NOF_U32_LD
//    		if(index < MAX_U32_LDS){
//	    		memcpy(data,&livedata.u32_ld[index],4);
//	    		ret = 4;	
//    		}
//    		//#endif
//    	}
//    	break;	
//    	case 3:{// I8
//    		//#ifdef NOF_I8_LD
//    		if(index < MAX_I8_LDS){
//    			*data = livedata.i8_ld[index];
//    			ret = 1;	
//    		}
//    		//#endif
//    	}
//    	break;
//    	case 4:{ // i16
//    		//#ifdef NOF_I16_LD
//    		if(index < MAX_I16_LDS){
//	    		memcpy(data,&livedata.i16_ld[index],2);
//	    		ret = 2;
//    		}
//    		//#endif
//    	}
//    	break;
//    	case 5:{ // i32;
//    		//#ifdef NOF_I32_LD
//    		if(index < MAX_I32_LDS){
//    			memcpy(data,&livedata.i32_ld[index],4);
//    			ret = 4;
//    		}
//    		//#endif
//    	}
//    	break;	
//    	case 6:{ // f32;
//    		//#ifdef NOF_F32_LD
//    		if(index < MAX_F32_LDS){
//    			memcpy(data,&livedata.f32_ld[index],4);
//    			ret = 4;
//    		}
//    		//#endif
//    	}
//    	break;	
//    }
//    return ret;
//}

//byte db_write_livedata(u16 regAddr, u8 *data)
//{
//	uint8_t ret = 0;
//    u8 type = PARAM_TYPE(regAddr);
//    u8 index = PARAM_INDEX(regAddr);
//    switch(type){
//    	case 0:{// u8
//    		//#ifdef NOF_U8_LD
//    		if(index < MAX_U8_LDS){
//    			livedata.u8_ld[index] = *data;	
//    			ret = 1;
//    		}
//    		//#endif
//    	}
//    	break;
//    	case 1:{ // u16
//    		//#ifdef NOF_U16_LD
//    		if(index < MAX_U16_LDS){
//	    		memcpy(&livedata.u16_ld[index],data,2);
//	    		ret = 2;
//    		}
//    		//#endif
//    	}
//    	break;
//    	case 2:{ // u32;
//    		//#ifdef NOF_U32_LD
//    		if(index < MAX_U32_LDS){
//	    		memcpy(&livedata.u32_ld[index],data,4);
//	    		ret = 4;	
//    		}
//    		//#endif
//    	}
//    	break;	
//    	case 3:{// I8
//    		//#ifdef NOF_I8_LD
//    		if(index < MAX_I8_LDS){
//    			livedata.i8_ld[index] = *data;
//    			ret = 1;	
//    		}
//    		//#endif
//    	}
//    	break;
//    	case 4:{ // i16
//    		//#ifdef NOF_I16_LD
//    		if(index < MAX_I16_LDS){
//	    		memcpy(&livedata.i16_ld[index],data,2);
//	    		ret = 2;
//    		}
//    		//#endif
//    	}
//    	break;
//    	case 5:{ // u32;
//    		//#ifdef NOF_I32_LD
//    		if(index < MAX_I32_LDS){
//    			memcpy(&livedata.i32_ld[index],data,4);
//    			ret = 4;
//    		}
//    		//#endif
//    	}
//    	break;	
//    	case 6:{ // f32;
//    		//#ifdef NOF_F32_LD
//    		if(index < MAX_F32_LDS){
//    			memcpy(&livedata.f32_ld[index],data,4);
//    			ret = 4;
//    		}
//    		//#endif
//    	}
//    	break;	
//    }
//    return ret;
//}