#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define NVME_SLOT_COUNT                 4U

#define SLOT_PROCESS_PERIOD_MS         10U
#define SLOT_DEBOUNCE_MS               50U
#define DRIVE_POLL_PERIOD_MS          1000U
/* Minimum interval between event-driven full LCD frame writes/retries. */
#define LCD_UPDATE_PERIOD_MS           250U

/*
 * Wokwi's STM32C031 I2C1 model can hold SCL low between otherwise valid
 * transfers.  Keep the CubeMX hardware-I2C configuration, but use a software
 * open-drain master on the same PB8/PB9 bus in the simulator build.  The
 * software backend sinks a line as an output and releases it as a pulled-up
 * input so Wokwi resolves the external bus level correctly.
 * Set this to 0U for the unmodified STM32 HAL backend on physical hardware.
 */
#ifndef APP_I2C_USE_SOFTWARE_BACKEND
#define APP_I2C_USE_SOFTWARE_BACKEND      1U
#endif

#ifndef APP_I2C_HALF_PERIOD_US
#define APP_I2C_HALF_PERIOD_US             5U
#endif

#define UART_BAUD_RATE              115200U

/* Application addresses are always stored as unshifted 7-bit values. */
#define LCD_I2C_ADDRESS               0x27U
#define SLOT1_DRIVE_ADDRESS           0x50U
#define SLOT2_DRIVE_ADDRESS           0x51U
#define SLOT3_DRIVE_ADDRESS           0x52U
#define SLOT4_DRIVE_ADDRESS           0x53U

#define DRIVE_COMMAND_GET_DATA        0x01U
#define DRIVE_PACKET_SIZE                6U
#define DRIVE_PACKET_MAGIC            0xA5U
#define DRIVE_STATUS_NORMAL           0x01U

#define DRIVE_I2C_TIMEOUT_MS             25U
#define LCD_I2C_TIMEOUT_MS              100U

#endif /* APP_CONFIG_H */
