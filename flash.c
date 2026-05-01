#include "flash.h"
void flash_erase(uint32_t addr){
	FLASH_EraseInitTypeDef pErase;
	uint32_t pError;
	pErase.NbPages=1;
	pErase.PageAddress=addr;
	pErase.TypeErase= FLASH_TYPEERASE_PAGES;
	HAL_FLASHEx_Erase(&pErase,&pError);
}

void flash_write(uint32_t addr, uint8_t *m_data, uint16_t length){
	for(int i=0;i<length;i+=2){
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,addr+i,*(m_data+i)|(*(m_data+i+1)<<8));
	}
	
}

void flash_lock(){
	HAL_FLASH_Lock();
}

void flash_unlock(){
	HAL_FLASH_Unlock();
}