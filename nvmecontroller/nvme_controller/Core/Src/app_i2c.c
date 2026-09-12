#include "app_i2c.h"

#include <stdbool.h>
#include <stddef.h>

#include "app_config.h"

#define APP_I2C_GPIO_PORT             GPIOB
#define APP_I2C_SCL_PIN               GPIO_PIN_8
#define APP_I2C_SDA_PIN               GPIO_PIN_9
#define APP_I2C_BUS_PINS              (APP_I2C_SCL_PIN | APP_I2C_SDA_PIN)
#define APP_I2C_SCL_MODE_SHIFT        (8U * 2U)
#define APP_I2C_SDA_MODE_SHIFT        (9U * 2U)
#define APP_I2C_GPIO_MODE_MASK        0x3U
#define APP_I2C_GPIO_OUTPUT_MODE      0x1U

static I2C_HandleTypeDef *app_i2c_handle = NULL;
static app_i2c_result_t app_i2c_last_result = APP_I2C_INVALID_ARGUMENT;

static app_i2c_result_t AppI2C_SetResult(app_i2c_result_t result)
{
    app_i2c_last_result = result;
    return result;
}

#if APP_I2C_USE_SOFTWARE_BACKEND

static void AppI2C_DelayUs(uint32_t delay_us)
{
    uint32_t reload = SysTick->LOAD + 1U;
    uint32_t ticks_per_us = SystemCoreClock / 1000000U;
    uint32_t target_ticks = ticks_per_us * delay_us;
    uint32_t previous = SysTick->VAL;
    uint32_t elapsed = 0U;

    while (elapsed < target_ticks)
    {
        uint32_t current = SysTick->VAL;

        if (previous >= current)
        {
            elapsed += previous - current;
        }
        else
        {
            elapsed += previous + reload - current;
        }

        previous = current;
    }
}

static void AppI2C_DelayHalfPeriod(void)
{
    AppI2C_DelayUs(APP_I2C_HALF_PERIOD_US);
}

/*
 * Wokwi's STM32C031 GPIO model does not resolve an open-drain GPIO output
 * with ODR=1 back to a HIGH input level on PB8/PB9.  I2C release is therefore
 * represented by input mode with a pull-up, while LOW is represented by an
 * open-drain output whose ODR bit is already zero.  This is electrically
 * equivalent to releasing or sinking an open-drain bus and also remains safe
 * on a physical STM32.
 */
static void AppI2C_PinLow(uint32_t pin, uint32_t mode_shift)
{
    uint32_t moder;

    APP_I2C_GPIO_PORT->BSRR = pin << 16U;
    moder = APP_I2C_GPIO_PORT->MODER;
    moder &= ~(APP_I2C_GPIO_MODE_MASK << mode_shift);
    moder |= APP_I2C_GPIO_OUTPUT_MODE << mode_shift;
    APP_I2C_GPIO_PORT->MODER = moder;
}

static void AppI2C_PinRelease(uint32_t mode_shift)
{
    APP_I2C_GPIO_PORT->MODER &=
        ~(APP_I2C_GPIO_MODE_MASK << mode_shift);
}

static void AppI2C_SclLow(void)
{
    AppI2C_PinLow(APP_I2C_SCL_PIN, APP_I2C_SCL_MODE_SHIFT);
}

static void AppI2C_SclRelease(void)
{
    AppI2C_PinRelease(APP_I2C_SCL_MODE_SHIFT);
}

static void AppI2C_SdaLow(void)
{
    AppI2C_PinLow(APP_I2C_SDA_PIN, APP_I2C_SDA_MODE_SHIFT);
}

static void AppI2C_SdaRelease(void)
{
    AppI2C_PinRelease(APP_I2C_SDA_MODE_SHIFT);
}

static bool AppI2C_SclIsHigh(void)
{
    return (APP_I2C_GPIO_PORT->IDR & APP_I2C_SCL_PIN) != 0U;
}

static bool AppI2C_SdaIsHigh(void)
{
    return (APP_I2C_GPIO_PORT->IDR & APP_I2C_SDA_PIN) != 0U;
}

static bool AppI2C_HasTimedOut(uint32_t start_ms, uint32_t timeout_ms)
{
    return (timeout_ms != HAL_MAX_DELAY) &&
           ((uint32_t)(HAL_GetTick() - start_ms) >= timeout_ms);
}

static app_i2c_result_t AppI2C_RaiseScl(uint32_t start_ms,
                                        uint32_t timeout_ms)
{
    AppI2C_SclRelease();

    while (!AppI2C_SclIsHigh())
    {
        if (AppI2C_HasTimedOut(start_ms, timeout_ms))
        {
            return APP_I2C_TIMEOUT;
        }
    }

    return APP_I2C_OK;
}

static app_i2c_result_t AppI2C_Start(uint32_t start_ms,
                                     uint32_t timeout_ms)
{
    app_i2c_result_t result;

    AppI2C_SdaRelease();
    result = AppI2C_RaiseScl(start_ms, timeout_ms);
    if (result != APP_I2C_OK)
    {
        return result;
    }

    if (!AppI2C_SdaIsHigh())
    {
        return APP_I2C_BUS_BUSY;
    }

    AppI2C_DelayHalfPeriod();
    AppI2C_SdaLow();
    AppI2C_DelayHalfPeriod();
    AppI2C_SclLow();

    return APP_I2C_OK;
}

static app_i2c_result_t AppI2C_Stop(uint32_t start_ms,
                                    uint32_t timeout_ms)
{
    app_i2c_result_t result;

    AppI2C_SdaLow();
    AppI2C_DelayHalfPeriod();

    result = AppI2C_RaiseScl(start_ms, timeout_ms);
    if (result != APP_I2C_OK)
    {
        AppI2C_SdaRelease();
        return result;
    }

    AppI2C_DelayHalfPeriod();
    AppI2C_SdaRelease();
    AppI2C_DelayHalfPeriod();

    return APP_I2C_OK;
}

static app_i2c_result_t AppI2C_WriteByte(uint8_t value,
                                         uint32_t start_ms,
                                         uint32_t timeout_ms)
{
    uint8_t mask;
    app_i2c_result_t result;
    bool acknowledged;

    for (mask = 0x80U; mask != 0U; mask >>= 1U)
    {
        if ((value & mask) != 0U)
        {
            AppI2C_SdaRelease();
        }
        else
        {
            AppI2C_SdaLow();
        }

        AppI2C_DelayHalfPeriod();
        result = AppI2C_RaiseScl(start_ms, timeout_ms);
        if (result != APP_I2C_OK)
        {
            return result;
        }

        AppI2C_DelayHalfPeriod();
        AppI2C_SclLow();
    }

    AppI2C_SdaRelease();
    AppI2C_DelayHalfPeriod();
    result = AppI2C_RaiseScl(start_ms, timeout_ms);
    if (result != APP_I2C_OK)
    {
        return result;
    }

    AppI2C_DelayHalfPeriod();
    acknowledged = !AppI2C_SdaIsHigh();
    AppI2C_SclLow();

    return acknowledged ? APP_I2C_OK : APP_I2C_NACK;
}

static app_i2c_result_t AppI2C_ReadByte(uint8_t *value,
                                        bool acknowledge,
                                        uint32_t start_ms,
                                        uint32_t timeout_ms)
{
    uint8_t mask;
    uint8_t received = 0U;
    app_i2c_result_t result;

    AppI2C_SdaRelease();

    for (mask = 0x80U; mask != 0U; mask >>= 1U)
    {
        AppI2C_DelayHalfPeriod();
        result = AppI2C_RaiseScl(start_ms, timeout_ms);
        if (result != APP_I2C_OK)
        {
            return result;
        }

        AppI2C_DelayHalfPeriod();
        if (AppI2C_SdaIsHigh())
        {
            received |= mask;
        }
        AppI2C_SclLow();
    }

    if (acknowledge)
    {
        AppI2C_SdaLow();
    }
    else
    {
        AppI2C_SdaRelease();
    }

    AppI2C_DelayHalfPeriod();
    result = AppI2C_RaiseScl(start_ms, timeout_ms);
    if (result != APP_I2C_OK)
    {
        AppI2C_SdaRelease();
        return result;
    }

    AppI2C_DelayHalfPeriod();
    AppI2C_SclLow();
    AppI2C_SdaRelease();
    *value = received;

    return APP_I2C_OK;
}

static app_i2c_result_t AppI2C_RecoverBus(void)
{
    uint8_t pulse;
    uint32_t start_ms = HAL_GetTick();
    app_i2c_result_t result;

    AppI2C_SdaRelease();
    result = AppI2C_RaiseScl(start_ms, 2U);
    if (result != APP_I2C_OK)
    {
        return result;
    }

    if (!AppI2C_SdaIsHigh())
    {
        for (pulse = 0U; pulse < 9U; pulse++)
        {
            AppI2C_SclLow();
            AppI2C_DelayHalfPeriod();
            result = AppI2C_RaiseScl(start_ms, 2U);
            if (result != APP_I2C_OK)
            {
                return result;
            }
            AppI2C_DelayHalfPeriod();
        }

        result = AppI2C_Stop(start_ms, 2U);
        if (result != APP_I2C_OK)
        {
            return result;
        }
    }

    return AppI2C_SdaIsHigh() ? APP_I2C_OK : APP_I2C_BUS_BUSY;
}

#else

static app_i2c_result_t AppI2C_FromHalStatus(HAL_StatusTypeDef status)
{
    uint32_t error_code;

    if (status == HAL_OK)
    {
        return APP_I2C_OK;
    }

    if (status == HAL_BUSY)
    {
        return APP_I2C_BUS_BUSY;
    }

    error_code = HAL_I2C_GetError(app_i2c_handle);
    if ((status == HAL_TIMEOUT) ||
        ((error_code & HAL_I2C_ERROR_TIMEOUT) != 0U))
    {
        return APP_I2C_TIMEOUT;
    }

    if ((error_code & HAL_I2C_ERROR_AF) != 0U)
    {
        return APP_I2C_NACK;
    }

    return APP_I2C_BUS_BUSY;
}

#endif

app_i2c_result_t AppI2C_Init(I2C_HandleTypeDef *i2c)
{
    if ((i2c == NULL) || (i2c->Instance == NULL))
    {
        return AppI2C_SetResult(APP_I2C_INVALID_ARGUMENT);
    }

    app_i2c_handle = i2c;

#if APP_I2C_USE_SOFTWARE_BACKEND
    GPIO_InitTypeDef gpio = {0};

    __HAL_I2C_DISABLE(app_i2c_handle);
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Prepare a LOW output latch, then release both lines as pulled-up inputs. */
    HAL_GPIO_WritePin(APP_I2C_GPIO_PORT, APP_I2C_BUS_PINS, GPIO_PIN_RESET);
    gpio.Pin = APP_I2C_BUS_PINS;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(APP_I2C_GPIO_PORT, &gpio);

    /* Keep open-drain selected whenever a line temporarily enters output mode. */
    APP_I2C_GPIO_PORT->OTYPER |= APP_I2C_BUS_PINS;

    return AppI2C_SetResult(AppI2C_RecoverBus());
#else
    return AppI2C_SetResult(APP_I2C_OK);
#endif
}

app_i2c_result_t AppI2C_MasterTransmit(uint8_t address_7bit,
                                      const uint8_t *data,
                                      uint16_t size,
                                      uint32_t timeout_ms)
{
    if ((app_i2c_handle == NULL) ||
        (address_7bit > 0x7FU) ||
        ((data == NULL) && (size != 0U)))
    {
        return AppI2C_SetResult(APP_I2C_INVALID_ARGUMENT);
    }

#if APP_I2C_USE_SOFTWARE_BACKEND
    uint32_t start_ms = HAL_GetTick();
    app_i2c_result_t result;
    uint16_t index;

    result = AppI2C_Start(start_ms, timeout_ms);
    if (result == APP_I2C_OK)
    {
        result = AppI2C_WriteByte((uint8_t)(address_7bit << 1U),
                                  start_ms,
                                  timeout_ms);
    }

    for (index = 0U; (index < size) && (result == APP_I2C_OK); index++)
    {
        result = AppI2C_WriteByte(data[index], start_ms, timeout_ms);
    }

    if ((result != APP_I2C_TIMEOUT) || AppI2C_SclIsHigh())
    {
        app_i2c_result_t stop_result = AppI2C_Stop(start_ms, timeout_ms);
        if ((result == APP_I2C_OK) && (stop_result != APP_I2C_OK))
        {
            result = stop_result;
        }
    }

    return AppI2C_SetResult(result);
#else
    return AppI2C_SetResult(
        AppI2C_FromHalStatus(
            HAL_I2C_Master_Transmit(app_i2c_handle,
                                    (uint16_t)address_7bit << 1U,
                                    (uint8_t *)data,
                                    size,
                                    timeout_ms)));
#endif
}

app_i2c_result_t AppI2C_MasterReceive(uint8_t address_7bit,
                                     uint8_t *data,
                                     uint16_t size,
                                     uint32_t timeout_ms)
{
    if ((app_i2c_handle == NULL) ||
        (address_7bit > 0x7FU) ||
        ((data == NULL) && (size != 0U)))
    {
        return AppI2C_SetResult(APP_I2C_INVALID_ARGUMENT);
    }

#if APP_I2C_USE_SOFTWARE_BACKEND
    uint32_t start_ms = HAL_GetTick();
    app_i2c_result_t result;
    uint16_t index;

    result = AppI2C_Start(start_ms, timeout_ms);
    if (result == APP_I2C_OK)
    {
        result = AppI2C_WriteByte((uint8_t)((address_7bit << 1U) | 0x01U),
                                  start_ms,
                                  timeout_ms);
    }

    for (index = 0U; (index < size) && (result == APP_I2C_OK); index++)
    {
        result = AppI2C_ReadByte(&data[index],
                                 index + 1U < size,
                                 start_ms,
                                 timeout_ms);
    }

    if ((result != APP_I2C_TIMEOUT) || AppI2C_SclIsHigh())
    {
        app_i2c_result_t stop_result = AppI2C_Stop(start_ms, timeout_ms);
        if ((result == APP_I2C_OK) && (stop_result != APP_I2C_OK))
        {
            result = stop_result;
        }
    }

    return AppI2C_SetResult(result);
#else
    return AppI2C_SetResult(
        AppI2C_FromHalStatus(
            HAL_I2C_Master_Receive(app_i2c_handle,
                                   (uint16_t)address_7bit << 1U,
                                   data,
                                   size,
                                   timeout_ms)));
#endif
}

app_i2c_result_t AppI2C_GetLastResult(void)
{
    return app_i2c_last_result;
}

const char *AppI2C_ResultName(app_i2c_result_t result)
{
    switch (result)
    {
        case APP_I2C_OK:
            return "OK";
        case APP_I2C_NACK:
            return "NACK";
        case APP_I2C_BUS_BUSY:
            return "BUS_BUSY";
        case APP_I2C_TIMEOUT:
            return "TIMEOUT";
        case APP_I2C_INVALID_ARGUMENT:
        default:
            return "INVALID_ARGUMENT";
    }
}

const char *AppI2C_BackendName(void)
{
#if APP_I2C_USE_SOFTWARE_BACKEND
    return "SOFTWARE-PB8/PB9-V2";
#else
    return "HAL-I2C1";
#endif
}
