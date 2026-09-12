#ifndef APP_SCHEDULER_H
#define APP_SCHEDULER_H

#include <stdint.h>

#define APP_TASK_SLOT_PROCESS          (1UL << 0)
#define APP_TASK_LCD_UPDATE            (1UL << 1)

typedef struct
{
    uint32_t last_slot_process_ms;
    uint32_t last_lcd_update_ms;
} app_scheduler_t;

void AppScheduler_Init(app_scheduler_t *scheduler, uint32_t now_ms);
uint32_t AppScheduler_Poll(app_scheduler_t *scheduler, uint32_t now_ms);

#endif /* APP_SCHEDULER_H */
