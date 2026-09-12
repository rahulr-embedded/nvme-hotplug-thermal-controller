#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32c0xx_hal.h"
#include "slot_manager.h"

bool LcdDisplay_Init(I2C_HandleTypeDef *i2c, uint32_t now_ms);
void LcdDisplay_NotifySlotEvent(const slot_event_record_t *event);
void LcdDisplay_Process(uint32_t now_ms);

#endif /* LCD_DISPLAY_H */
