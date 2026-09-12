#ifndef DRIVE_I2C_H
#define DRIVE_I2C_H

#include <stdint.h>

#include "stm32c0xx_hal.h"

void DriveI2C_Init(I2C_HandleTypeDef *i2c);
void DriveI2C_Process(uint32_t now_ms);

#endif /* DRIVE_I2C_H */
