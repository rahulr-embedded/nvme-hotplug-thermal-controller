#ifndef APP_I2C_H
#define APP_I2C_H

#include <stdint.h>

#include "stm32c0xx_hal.h"

typedef enum
{
    APP_I2C_OK = 0,
    APP_I2C_NACK,
    APP_I2C_BUS_BUSY,
    APP_I2C_TIMEOUT,
    APP_I2C_INVALID_ARGUMENT
} app_i2c_result_t;

app_i2c_result_t AppI2C_Init(I2C_HandleTypeDef *i2c);
app_i2c_result_t AppI2C_MasterTransmit(uint8_t address_7bit,
                                      const uint8_t *data,
                                      uint16_t size,
                                      uint32_t timeout_ms);
app_i2c_result_t AppI2C_MasterReceive(uint8_t address_7bit,
                                     uint8_t *data,
                                     uint16_t size,
                                     uint32_t timeout_ms);
app_i2c_result_t AppI2C_GetLastResult(void);
const char *AppI2C_ResultName(app_i2c_result_t result);
const char *AppI2C_BackendName(void);

#endif /* APP_I2C_H */
