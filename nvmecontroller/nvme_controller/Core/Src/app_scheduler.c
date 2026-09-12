#include "app_scheduler.h"

#include <stdbool.h>

#include "app_config.h"

static bool AppScheduler_IsDue(uint32_t now_ms,
                               uint32_t *last_run_ms,
                               uint32_t period_ms)
{
    if ((uint32_t)(now_ms - *last_run_ms) < period_ms)
    {
        return false;
    }

    *last_run_ms += period_ms;
    return true;
}

void AppScheduler_Init(app_scheduler_t *scheduler, uint32_t now_ms)
{
    scheduler->last_slot_process_ms = now_ms;
    scheduler->last_lcd_update_ms = now_ms;
}

uint32_t AppScheduler_Poll(app_scheduler_t *scheduler, uint32_t now_ms)
{
    uint32_t due_tasks = 0U;

    if (AppScheduler_IsDue(now_ms,
                           &scheduler->last_slot_process_ms,
                           SLOT_PROCESS_PERIOD_MS))
    {
        due_tasks |= APP_TASK_SLOT_PROCESS;
    }

    if (AppScheduler_IsDue(now_ms,
                           &scheduler->last_lcd_update_ms,
                           LCD_UPDATE_PERIOD_MS))
    {
        due_tasks |= APP_TASK_LCD_UPDATE;
    }

    return due_tasks;
}
