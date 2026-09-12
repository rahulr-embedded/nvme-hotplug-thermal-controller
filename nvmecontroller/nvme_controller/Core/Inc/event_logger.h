#ifndef EVENT_LOGGER_H
#define EVENT_LOGGER_H

#include <stdint.h>

#include "stm32c0xx_hal.h"
#include "slot_manager.h"

void EventLogger_Init(UART_HandleTypeDef *uart);
void EventLogger_PrintStartup(void);
void EventLogger_LogStatus(uint32_t now_ms);
void EventLogger_LogSlotEvent(const slot_event_record_t *event);
void EventLogger_LogDrivePacket(uint32_t now_ms,
                                uint8_t slot_id,
                                uint8_t address_7bit,
                                uint8_t sequence,
                                uint8_t status,
                                uint8_t data);
void EventLogger_LogDriveError(uint32_t now_ms,
                               uint8_t slot_id,
                               uint8_t address_7bit,
                               const char *error_text);
void EventLogger_LogSystemError(uint32_t now_ms,
                                const char *error_text);
void EventLogger_LogI2CDiagnostic(uint32_t now_ms,
                                  const I2C_HandleTypeDef *i2c,
                                  const GPIO_TypeDef *gpio_port);

#endif /* EVENT_LOGGER_H */
