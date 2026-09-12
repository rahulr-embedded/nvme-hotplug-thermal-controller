#include "lcd_display.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_i2c.h"

#define LCD_COLUMNS                 20U
#define LCD_ROWS                     4U
#define LCD_LINE_BUFFER_SIZE        (LCD_COLUMNS + 1U)
#define LCD_FRAME_BUFFER_SIZE       (LCD_ROWS * (LCD_COLUMNS + 1U) * 4U)

#define LCD_PCF_RS                  0x01U
#define LCD_PCF_ENABLE              0x04U
#define LCD_PCF_BACKLIGHT           0x08U

static const uint8_t lcd_row_addresses[LCD_ROWS] =
{
    0x00U,
    0x40U,
    0x14U,
    0x54U
};

static bool lcd_initialized = false;
static bool lcd_refresh_pending = false;
static uint32_t lcd_last_update_ms = 0U;
static uint8_t lcd_last_event_slot = 0U;
static slot_event_t lcd_last_event = SLOT_EVENT_NONE;
static uint8_t lcd_frame[LCD_FRAME_BUFFER_SIZE];
static char lcd_lines[LCD_ROWS][LCD_LINE_BUFFER_SIZE];
static char lcd_formatted[LCD_LINE_BUFFER_SIZE + 8U];

static bool LcdDisplay_Transmit(const uint8_t *data, uint16_t size)
{
    return AppI2C_MasterTransmit(LCD_I2C_ADDRESS,
                                 data,
                                 size,
                                 LCD_I2C_TIMEOUT_MS) == APP_I2C_OK;
}

static bool LcdDisplay_SendNibble(uint8_t nibble)
{
    uint8_t sequence[2];
    uint8_t value = (nibble & 0xF0U) | LCD_PCF_BACKLIGHT;

    sequence[0] = value | LCD_PCF_ENABLE;
    sequence[1] = value;

    return LcdDisplay_Transmit(sequence, sizeof(sequence));
}

static bool LcdDisplay_SendByte(uint8_t value, bool data_mode)
{
    uint8_t sequence[4];
    uint8_t mode = data_mode ? LCD_PCF_RS : 0U;
    uint8_t high = (value & 0xF0U) | LCD_PCF_BACKLIGHT | mode;
    uint8_t low = ((value << 4U) & 0xF0U) | LCD_PCF_BACKLIGHT | mode;

    sequence[0] = high | LCD_PCF_ENABLE;
    sequence[1] = high;
    sequence[2] = low | LCD_PCF_ENABLE;
    sequence[3] = low;

    return LcdDisplay_Transmit(sequence, sizeof(sequence));
}

static void LcdDisplay_AppendByte(uint8_t *buffer,
                                  uint16_t *position,
                                  uint8_t value,
                                  bool data_mode)
{
    uint8_t mode = data_mode ? LCD_PCF_RS : 0U;
    uint8_t high = (value & 0xF0U) | LCD_PCF_BACKLIGHT | mode;
    uint8_t low = ((value << 4U) & 0xF0U) | LCD_PCF_BACKLIGHT | mode;

    buffer[(*position)++] = high | LCD_PCF_ENABLE;
    buffer[(*position)++] = high;
    buffer[(*position)++] = low | LCD_PCF_ENABLE;
    buffer[(*position)++] = low;
}

static void LcdDisplay_PadLine(char *destination, const char *source)
{
    size_t length = strlen(source);

    if (length > LCD_COLUMNS)
    {
        length = LCD_COLUMNS;
    }

    memset(destination, ' ', LCD_COLUMNS);
    memcpy(destination, source, length);
    destination[LCD_COLUMNS] = '\0';
}

static const char *LcdDisplay_SlotText(uint8_t slot_id)
{
    const nvme_slot_t *slot = SlotManager_GetSlot(slot_id);
    return slot->stable_present ? "IN" : "EMPTY";
}

static bool LcdDisplay_WriteFrame(void)
{
    uint16_t position = 0U;
    uint8_t row;
    uint8_t column;

    LcdDisplay_PadLine(lcd_lines[0], "NVME SLOT MONITOR");

    (void)snprintf(lcd_formatted,
                   sizeof(lcd_formatted),
                   "S1:%-5s S2:%-5s",
                   LcdDisplay_SlotText(1U),
                   LcdDisplay_SlotText(2U));
    LcdDisplay_PadLine(lcd_lines[1], lcd_formatted);

    (void)snprintf(lcd_formatted,
                   sizeof(lcd_formatted),
                   "S3:%-5s S4:%-5s",
                   LcdDisplay_SlotText(3U),
                   LcdDisplay_SlotText(4U));
    LcdDisplay_PadLine(lcd_lines[2], lcd_formatted);

    if (lcd_last_event == SLOT_EVENT_NONE)
    {
        LcdDisplay_PadLine(lcd_lines[3], "LAST:NONE");
    }
    else
    {
        (void)snprintf(lcd_formatted,
                       sizeof(lcd_formatted),
                       "LAST:S%u %s",
                       (unsigned int)lcd_last_event_slot,
                       SlotManager_EventName(lcd_last_event));
        LcdDisplay_PadLine(lcd_lines[3], lcd_formatted);
    }

    for (row = 0U; row < LCD_ROWS; row++)
    {
        LcdDisplay_AppendByte(lcd_frame,
                              &position,
                              0x80U | lcd_row_addresses[row],
                              false);

        for (column = 0U; column < LCD_COLUMNS; column++)
        {
            LcdDisplay_AppendByte(lcd_frame,
                                  &position,
                                  (uint8_t)lcd_lines[row][column],
                                  true);
        }
    }

    return LcdDisplay_Transmit(lcd_frame, position);
}

bool LcdDisplay_Init(I2C_HandleTypeDef *i2c, uint32_t now_ms)
{
    if (i2c == NULL)
    {
        return false;
    }

    lcd_initialized = false;
    lcd_refresh_pending = false;
    lcd_last_event_slot = 0U;
    lcd_last_event = SLOT_EVENT_NONE;

    /* HD44780 startup is the only place where short blocking delays are used. */
    HAL_Delay(50U);
    if (!LcdDisplay_SendNibble(0x30U))
    {
        return false;
    }

    HAL_Delay(5U);
    if (!LcdDisplay_SendNibble(0x30U))
    {
        return false;
    }

    HAL_Delay(1U);
    if (!LcdDisplay_SendNibble(0x30U) ||
        !LcdDisplay_SendNibble(0x20U) ||
        !LcdDisplay_SendByte(0x28U, false) ||
        !LcdDisplay_SendByte(0x08U, false) ||
        !LcdDisplay_SendByte(0x01U, false))
    {
        return false;
    }

    HAL_Delay(2U);
    if (!LcdDisplay_SendByte(0x06U, false) ||
        !LcdDisplay_SendByte(0x0CU, false))
    {
        return false;
    }

    lcd_initialized = true;
    lcd_refresh_pending = true;
    lcd_last_update_ms = now_ms - LCD_UPDATE_PERIOD_MS;
    LcdDisplay_Process(HAL_GetTick());

    return true;
}

void LcdDisplay_NotifySlotEvent(const slot_event_record_t *event)
{
    if (event == NULL)
    {
        return;
    }

    lcd_last_event_slot = event->slot_id;
    lcd_last_event = event->event;
    lcd_refresh_pending = true;
}

void LcdDisplay_Process(uint32_t now_ms)
{
    if (!lcd_initialized)
    {
        return;
    }

    /*
     * The displayed content changes only after a confirmed slot event.
     * Avoid rewriting all 80 characters periodically when the frame is
     * unchanged; on the GPIO software-I2C backend that unnecessary traffic
     * can delay the 10 ms debounce and 1000 ms drive-poll tasks.
     */
    if (!lcd_refresh_pending)
    {
        return;
    }

    if ((uint32_t)(now_ms - lcd_last_update_ms) < LCD_UPDATE_PERIOD_MS)
    {
        return;
    }

    lcd_last_update_ms = now_ms;

    if (LcdDisplay_WriteFrame())
    {
        lcd_refresh_pending = false;
    }
}
