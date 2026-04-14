#include "hal.h"
#include "at32_flash.h"
#include "bootConfig.h"

//uint16_t pageBuffer(PAGE_SIZE/2);

static uint16_t FLASH_ReadHalfWord(uint32_t ReadAddress)
{
  return *((uint16_t*)ReadAddress);
}

static void flashWrite_NoCheck(uint32_t WriteAddress, uint16_t *pBuffer, uint16_t NumToWrite)
{
  uint16_t i;
  for(i=0;i<NumToWrite;i++){
    FLASH_ProgramHalfWord(WriteAddress, pBuffer[i]);
    WriteAddress += 2;
  }
}

void flash_Read(uint32_t ReadAddress, uint16_t *pBuffer, uint16_t NumToRead)
{
  uint16_t i;
  for(i=0;i<NumToRead;i++){
    pBuffer[i] = FLASH_ReadHalfWord(ReadAddress);
    ReadAddress += 2;
  }
}

void flash_Write(uint32_t WriteAddress, uint16_t *pBuffer, uint16_t NumToWrite)
{
  uint32_t pagePos;
  uint16_t pageOffset;
  uint16_t pageRemainSize;
  uint16_t i;
  uint32_t offaddr;
  uint16_t pageBuffer[PAGE_SIZE/2];
  
  if(WriteAddress < FLASH_BASE_ADDRESS || WriteAddress >= (FLASH_BASE_ADDRESS + FLASH_ALL_SIZE)){
    return;
  }

  FLASH_Unlock();
  offaddr = WriteAddress - FLASH_BASE_ADDRESS;
  pagePos = offaddr / PAGE_SIZE;
  pageOffset = (offaddr % PAGE_SIZE)/2;
  pageRemainSize = PAGE_SIZE/2 - pageOffset;
  
  if(NumToWrite <= pageRemainSize){
    pageRemainSize = NumToWrite;
  }
  
  while(1){
    flash_Read(pagePos*PAGE_SIZE+FLASH_BASE_ADDRESS,pageBuffer,PAGE_SIZE/2);
    for(i=0;i<pageRemainSize;i++){
      if(pageBuffer[pageOffset+i] != 0xffff) break;
    }
    
    if(i < pageRemainSize){
      FLASH_ErasePage(pagePos*PAGE_SIZE+FLASH_BASE_ADDRESS);
      for(i=0;i<pageRemainSize;i++){
        pageBuffer[i + pageOffset] = pBuffer[i];
      }
      flashWrite_NoCheck(pagePos*PAGE_SIZE+FLASH_BASE_ADDRESS,pageBuffer,PAGE_SIZE/2);  
    }
    else{
      flashWrite_NoCheck(WriteAddress,pBuffer,pageRemainSize);
    }
    
    if(NumToWrite == pageRemainSize){
      break;
    }
    else{
      pagePos++;
      pageOffset = 0;
      pBuffer += pageRemainSize;
      WriteAddress += pageRemainSize*2;
      NumToWrite -= pageRemainSize;
      if(NumToWrite > (PAGE_SIZE/2)){
        pageRemainSize = PAGE_SIZE/2;
      }
      else{
        pageRemainSize = NumToWrite;
      }
    }
  }
  FLASH_Lock();
}
  
void flash_ErasePage(uint32_t page)
{
  uint32_t pagePos = page;
  uint16_t pageOffset;
  uint16_t pageRemainSize;
  uint16_t i;
  uint32_t offaddr;
  uint16_t pageBuffer[PAGE_SIZE/2];
  
  FLASH_Unlock();
  FLASH_ErasePage(pagePos*PAGE_SIZE+FLASH_BASE_ADDRESS);
  FLASH_Lock();
}


  