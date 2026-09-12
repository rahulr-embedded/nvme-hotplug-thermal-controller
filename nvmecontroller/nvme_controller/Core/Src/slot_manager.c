#include "slot_manager.h"

#include <stddef.h>

#include "app_config.h"
#include "main.h"

#if NVME_SLOT_COUNT != 4U
#error "Update the fixed four-slot GPIO table when NVME_SLOT_COUNT changes"
#endif

typedef struct
{
    GPIO_TypeDef *presence_port;
    uint16_t presence_pin;
} slot_hw_t;

static const slot_hw_t slot_hw[NVME_SLOT_COUNT] =
{
    {SLOT1_PRESENCE_GPIO_Port, SLOT1_PRESENCE_Pin},
    {SLOT2_PRESENCE_GPIO_Port, SLOT2_PRESENCE_Pin},
    {SLOT3_PRESENCE_GPIO_Port, SLOT3_PRESENCE_Pin},
    {SLOT4_PRESENCE_GPIO_Port, SLOT4_PRESENCE_Pin}
};

static nvme_slot_t slots[NVME_SLOT_COUNT];

/* Written by EXTI callbacks and atomically consumed by the main context. */
static volatile uint32_t pending_edge_mask = 0U;
static volatile uint32_t pending_edge_timestamp_ms[NVME_SLOT_COUNT];

static bool SlotManager_ReadPresence(uint8_t slot_index)
{
    return HAL_GPIO_ReadPin(slot_hw[slot_index].presence_port,
                            slot_hw[slot_index].presence_pin) == GPIO_PIN_RESET;
}

static bool SlotManager_DelayElapsed(uint32_t now_ms,
                                     uint32_t start_ms,
                                     uint32_t delay_ms)
{
    return (int32_t)(now_ms - start_ms) >= (int32_t)delay_ms;
}

static void SlotManager_SetState(nvme_slot_t *slot,
                                 slot_state_t state,
                                 bool debounce_pending,
                                 uint32_t edge_timestamp_ms)
{
    slot->state = state;
    slot->debounce_pending = debounce_pending;
    slot->edge_timestamp_ms = edge_timestamp_ms;
}

static uint32_t SlotManager_TakePendingEdges(uint32_t *edge_timestamps_ms)
{
    uint32_t previous_primask;
    uint32_t edge_mask;
    uint8_t slot_index;

    previous_primask = __get_PRIMASK();
    __disable_irq();

    edge_mask = pending_edge_mask;
    pending_edge_mask = 0U;

    for (slot_index = 0U; slot_index < NVME_SLOT_COUNT; slot_index++)
    {
        edge_timestamps_ms[slot_index] =
            pending_edge_timestamp_ms[slot_index];
    }

    if (previous_primask == 0U)
    {
        __enable_irq();
    }

    return edge_mask;
}

static slot_event_t SlotManager_ProcessSlot(nvme_slot_t *slot,
                                            bool present_now,
                                            bool edge_seen,
                                            uint32_t edge_timestamp_ms,
                                            uint32_t now_ms)
{
    switch (slot->state)
    {
        case SLOT_STATE_EMPTY:
            if (present_now)
            {
                SlotManager_SetState(slot,
                                     SLOT_STATE_INSERT_DEBOUNCE,
                                     true,
                                     edge_seen ? edge_timestamp_ms : now_ms);
            }
            break;

        case SLOT_STATE_INSERT_DEBOUNCE:
            if (!present_now)
            {
                SlotManager_SetState(slot,
                                     SLOT_STATE_EMPTY,
                                     false,
                                     now_ms);
            }
            else
            {
                if (edge_seen)
                {
                    slot->edge_timestamp_ms = edge_timestamp_ms;
                }

                if (SlotManager_DelayElapsed(now_ms,
                                             slot->edge_timestamp_ms,
                                             SLOT_DEBOUNCE_MS))
                {
                    slot->stable_present = true;
                    slot->latest_packet_valid = false;
                    slot->last_i2c_poll_ms = now_ms - DRIVE_POLL_PERIOD_MS;

                    SlotManager_SetState(slot,
                                         SLOT_STATE_PRESENT,
                                         false,
                                         now_ms);

                    return SLOT_EVENT_INSERTED;
                }
            }
            break;

        case SLOT_STATE_PRESENT:
            if (!present_now)
            {
                SlotManager_SetState(slot,
                                     SLOT_STATE_REMOVE_DEBOUNCE,
                                     true,
                                     edge_seen ? edge_timestamp_ms : now_ms);
            }
            break;

        case SLOT_STATE_REMOVE_DEBOUNCE:
            if (present_now)
            {
                SlotManager_SetState(slot,
                                     SLOT_STATE_PRESENT,
                                     false,
                                     now_ms);
            }
            else
            {
                if (edge_seen)
                {
                    slot->edge_timestamp_ms = edge_timestamp_ms;
                }

                if (SlotManager_DelayElapsed(now_ms,
                                             slot->edge_timestamp_ms,
                                             SLOT_DEBOUNCE_MS))
                {
                    slot->stable_present = false;
                    slot->latest_packet_valid = false;

                    SlotManager_SetState(slot,
                                         SLOT_STATE_EMPTY,
                                         false,
                                         now_ms);

                    return SLOT_EVENT_REMOVED;
                }
            }
            break;

        default:
            slot->stable_present = false;
            slot->latest_packet_valid = false;
            SlotManager_SetState(slot,
                                 SLOT_STATE_EMPTY,
                                 false,
                                 now_ms);
            break;
    }

    return SLOT_EVENT_NONE;
}

void SlotManager_Init(uint32_t now_ms)
{
    uint8_t slot_index;

    pending_edge_mask = 0U;

    for (slot_index = 0U; slot_index < NVME_SLOT_COUNT; slot_index++)
    {
        nvme_slot_t *slot = &slots[slot_index];
        bool present_now = SlotManager_ReadPresence(slot_index);

        pending_edge_timestamp_ms[slot_index] = now_ms;

        slot->slot_id = slot_index + 1U;
        slot->state = present_now ? SLOT_STATE_PRESENT : SLOT_STATE_EMPTY;
        slot->stable_present = present_now;
        slot->debounce_pending = false;
        slot->edge_timestamp_ms = now_ms;
        slot->last_i2c_poll_ms = present_now
            ? (now_ms - DRIVE_POLL_PERIOD_MS)
            : now_ms;
        slot->latest_sequence = 0U;
        slot->latest_status = 0U;
        slot->latest_data = 0U;
        slot->latest_packet_valid = false;
    }
}

uint8_t SlotManager_Process(uint32_t now_ms,
                            slot_event_record_t *events,
                            uint8_t event_capacity)
{
    uint32_t edge_timestamps_ms[NVME_SLOT_COUNT];
    uint32_t edge_mask;
    uint8_t event_count = 0U;
    uint8_t slot_index;

    edge_mask = SlotManager_TakePendingEdges(edge_timestamps_ms);

    for (slot_index = 0U; slot_index < NVME_SLOT_COUNT; slot_index++)
    {
        nvme_slot_t *slot = &slots[slot_index];
        bool edge_seen = (edge_mask & (1UL << slot_index)) != 0U;
        bool present_now = SlotManager_ReadPresence(slot_index);
        slot_event_t event;

        event = SlotManager_ProcessSlot(slot,
                                        present_now,
                                        edge_seen,
                                        edge_timestamps_ms[slot_index],
                                        now_ms);

        if ((event != SLOT_EVENT_NONE) &&
            (events != NULL) &&
            (event_count < event_capacity))
        {
            events[event_count].timestamp_ms = now_ms;
            events[event_count].slot_id = slot->slot_id;
            events[event_count].event = event;
            event_count++;
        }
    }

    return event_count;
}

const nvme_slot_t *SlotManager_GetSlot(uint8_t slot_id)
{
    if ((slot_id == 0U) || (slot_id > NVME_SLOT_COUNT))
    {
        return NULL;
    }

    return &slots[slot_id - 1U];
}

void SlotManager_RecordI2CResult(uint8_t slot_id,
                                 uint32_t poll_timestamp_ms,
                                 bool packet_valid,
                                 uint8_t sequence,
                                 uint8_t status,
                                 uint8_t data)
{
    nvme_slot_t *slot;

    if ((slot_id == 0U) || (slot_id > NVME_SLOT_COUNT))
    {
        return;
    }

    slot = &slots[slot_id - 1U];
    slot->last_i2c_poll_ms = poll_timestamp_ms;
    slot->latest_packet_valid = packet_valid;

    if (packet_valid)
    {
        slot->latest_sequence = sequence;
        slot->latest_status = status;
        slot->latest_data = data;
    }
}

const char *SlotManager_StateName(slot_state_t state)
{
    switch (state)
    {
        case SLOT_STATE_EMPTY:
            return "EMPTY";
        case SLOT_STATE_INSERT_DEBOUNCE:
            return "INSERT_DEBOUNCE";
        case SLOT_STATE_PRESENT:
            return "PRESENT";
        case SLOT_STATE_REMOVE_DEBOUNCE:
            return "REMOVE_DEBOUNCE";
        default:
            return "UNKNOWN";
    }
}

const char *SlotManager_EventName(slot_event_t event)
{
    switch (event)
    {
        case SLOT_EVENT_NONE:
            return "NONE";
        case SLOT_EVENT_INSERTED:
            return "INSERTED";
        case SLOT_EVENT_REMOVED:
            return "REMOVED";
        default:
            return "UNKNOWN";
    }
}

static void SlotManager_RecordPresenceEdge(uint16_t gpio_pin)
{
    uint8_t slot_index;

    for (slot_index = 0U; slot_index < NVME_SLOT_COUNT; slot_index++)
    {
        if (gpio_pin == slot_hw[slot_index].presence_pin)
        {
            pending_edge_timestamp_ms[slot_index] = HAL_GetTick();
            pending_edge_mask |= (1UL << slot_index);
            break;
        }
    }
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    SlotManager_RecordPresenceEdge(GPIO_Pin);
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    SlotManager_RecordPresenceEdge(GPIO_Pin);
}
