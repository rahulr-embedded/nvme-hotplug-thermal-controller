#include "event_logger.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_i2c.h"

static UART_HandleTypeDef *logger_uart = NULL;
/* Logging runs only in main context, so one shared buffer avoids large stack use. */
static char logger_message[384];

static void EventLogger_Transmit(const char *text)
{
    size_t length;

    if ((logger_uart == NULL) || (text == NULL))
    {
        return;
    }

    length = strlen(text);
    if ((length == 0U) || (length > UINT16_MAX))
    {
        return;
    }

    (void)HAL_UART_Transmit(logger_uart,
                            (const uint8_t *)text,
                            (uint16_t)length,
                            HAL_MAX_DELAY);
}

void EventLogger_Init(UART_HandleTypeDef *uart)
{
    logger_uart = uart;
}

void EventLogger_PrintStartup(void)
{
    int length;

    length = snprintf(
        logger_message,
        sizeof(logger_message),
        "\r\n"
        "================================================\r\n"
        "ARM MULTI-SLOT NVME PRESENCE MONITOR\r\n"
        "Slots: %u\r\n"
        "Presence: Active Low\r\n"
        "Debounce: %u ms\r\n"
        "I2C Backend: %s\r\n"
        "I2C LCD Address: 0x%02X\r\n"
        "Drive Addresses: 0x%02X 0x%02X 0x%02X 0x%02X\r\n"
        "System Ready\r\n"
        "================================================\r\n",
        (unsigned int)NVME_SLOT_COUNT,
        (unsigned int)SLOT_DEBOUNCE_MS,
        AppI2C_BackendName(),
        (unsigned int)LCD_I2C_ADDRESS,
        (unsigned int)SLOT1_DRIVE_ADDRESS,
        (unsigned int)SLOT2_DRIVE_ADDRESS,
        (unsigned int)SLOT3_DRIVE_ADDRESS,
        (unsigned int)SLOT4_DRIVE_ADDRESS);

    if ((length > 0) && (length < (int)sizeof(logger_message)))
    {
        EventLogger_Transmit(logger_message);
    }
}

void EventLogger_LogStatus(uint32_t now_ms)
{
    const nvme_slot_t *slot1 = SlotManager_GetSlot(1U);
    const nvme_slot_t *slot2 = SlotManager_GetSlot(2U);
    const nvme_slot_t *slot3 = SlotManager_GetSlot(3U);
    const nvme_slot_t *slot4 = SlotManager_GetSlot(4U);
    int length;

    length = snprintf(
        logger_message,
        sizeof(logger_message),
        "STATUS,%08lu,S1=%s,S2=%s,S3=%s,S4=%s\r\n",
        (unsigned long)now_ms,
        slot1->stable_present ? "IN" : "EMPTY",
        slot2->stable_present ? "IN" : "EMPTY",
        slot3->stable_present ? "IN" : "EMPTY",
        slot4->stable_present ? "IN" : "EMPTY");

    if ((length > 0) && (length < (int)sizeof(logger_message)))
    {
        EventLogger_Transmit(logger_message);
    }
}

void EventLogger_LogSlotEvent(const slot_event_record_t *event)
{
    int length;

    if (event == NULL)
    {
        return;
    }

    length = snprintf(logger_message,
                      sizeof(logger_message),
                      "EVT,%08lu,SLOT%u,%s\r\n",
                      (unsigned long)event->timestamp_ms,
                      (unsigned int)event->slot_id,
                      SlotManager_EventName(event->event));

    if ((length > 0) && (length < (int)sizeof(logger_message)))
    {
        EventLogger_Transmit(logger_message);
    }
}

void EventLogger_LogDrivePacket(uint32_t now_ms,
                                uint8_t slot_id,
                                uint8_t address_7bit,
                                uint8_t sequence,
                                uint8_t status,
                                uint8_t data)
{
    int length;

    length = snprintf(
        logger_message,
        sizeof(logger_message),
        "I2C,%08lu,SLOT%u,ADDR=0x%02X,SEQ=%02X,STATUS=%02X,"
        "DATA=%02X,CHECKSUM=OK\r\n",
        (unsigned long)now_ms,
        (unsigned int)slot_id,
        (unsigned int)address_7bit,
        (unsigned int)sequence,
        (unsigned int)status,
        (unsigned int)data);

    if ((length > 0) && (length < (int)sizeof(logger_message)))
    {
        EventLogger_Transmit(logger_message);
    }
}

void EventLogger_LogDriveError(uint32_t now_ms,
                               uint8_t slot_id,
                               uint8_t address_7bit,
                               const char *error_text)
{
    int length;

    length = snprintf(logger_message,
                      sizeof(logger_message),
                      "I2C,%08lu,SLOT%u,ADDR=0x%02X,ERROR=%s\r\n",
                      (unsigned long)now_ms,
                      (unsigned int)slot_id,
                      (unsigned int)address_7bit,
                      error_text);

    if ((length > 0) && (length < (int)sizeof(logger_message)))
    {
        EventLogger_Transmit(logger_message);
    }
}

void EventLogger_LogSystemError(uint32_t now_ms,
                                const char *error_text)
{
    int length;

    length = snprintf(logger_message,
                      sizeof(logger_message),
                      "SYS,%08lu,ERROR=%s\r\n",
                      (unsigned long)now_ms,
                      error_text);

    if ((length > 0) && (length < (int)sizeof(logger_message)))
    {
        EventLogger_Transmit(logger_message);
    }
}

void EventLogger_LogI2CDiagnostic(uint32_t now_ms,
                                  const I2C_HandleTypeDef *i2c,
                                  const GPIO_TypeDef *gpio_port)
{
    int length;

    if ((i2c == NULL) ||
        (i2c->Instance == NULL) ||
        (gpio_port == NULL))
    {
        return;
    }

    length = snprintf(
        logger_message,
        sizeof(logger_message),
        "I2C_DIAG,%08lu,BACKEND=%s,LAST=%s,ERR=0x%08lX,ISR=0x%08lX,CR1=0x%08lX,"
        "CR2=0x%08lX,TIMING=0x%08lX,TXDR=0x%02lX,XC=%u,XS=%u,"
        "STATE=0x%02X,MODER=0x%08lX,PUPDR=0x%08lX,OTYPER=0x%04lX,"
        "ODR=0x%04lX,IDR=0x%04lX,AFRH=0x%08lX,CLK=%lu\r\n",
        (unsigned long)now_ms,
        AppI2C_BackendName(),
        AppI2C_ResultName(AppI2C_GetLastResult()),
        (unsigned long)HAL_I2C_GetError(i2c),
        (unsigned long)i2c->Instance->ISR,
        (unsigned long)i2c->Instance->CR1,
        (unsigned long)i2c->Instance->CR2,
        (unsigned long)i2c->Instance->TIMINGR,
        (unsigned long)(i2c->Instance->TXDR & I2C_TXDR_TXDATA),
        (unsigned int)i2c->XferCount,
        (unsigned int)i2c->XferSize,
        (unsigned int)i2c->State,
        (unsigned long)gpio_port->MODER,
        (unsigned long)gpio_port->PUPDR,
        (unsigned long)gpio_port->OTYPER,
        (unsigned long)gpio_port->ODR,
        (unsigned long)gpio_port->IDR,
        (unsigned long)gpio_port->AFR[1],
        (unsigned long)SystemCoreClock);

    if ((length > 0) && (length < (int)sizeof(logger_message)))
    {
        EventLogger_Transmit(logger_message);
    }
}
