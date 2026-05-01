#ifndef __FLASH_H
#define __FLASH_H
#include <stm32f1xx_hal.h>
void flash_lock();
void flash_unlock();
void flash_erase(uint32_t addr);
void flash_write(uint32_t addr, uint8_t *m_data, uint16_t length);

#endif
