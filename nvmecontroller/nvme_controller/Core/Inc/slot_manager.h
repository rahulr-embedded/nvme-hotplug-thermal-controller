#ifndef SLOT_MANAGER_H
#define SLOT_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    SLOT_STATE_EMPTY = 0,
    SLOT_STATE_INSERT_DEBOUNCE,
    SLOT_STATE_PRESENT,
    SLOT_STATE_REMOVE_DEBOUNCE
} slot_state_t;

typedef enum
{
    SLOT_EVENT_NONE = 0,
    SLOT_EVENT_INSERTED,
    SLOT_EVENT_REMOVED
} slot_event_t;

typedef struct
{
    uint8_t slot_id;
    slot_state_t state;
    bool stable_present;
    bool debounce_pending;
    uint32_t edge_timestamp_ms;
    uint32_t last_i2c_poll_ms;
    uint8_t latest_sequence;
    uint8_t latest_status;
    uint8_t latest_data;
    bool latest_packet_valid;
} nvme_slot_t;

typedef struct
{
    uint32_t timestamp_ms;
    uint8_t slot_id;
    slot_event_t event;
} slot_event_record_t;

void SlotManager_Init(uint32_t now_ms);
uint8_t SlotManager_Process(uint32_t now_ms,
                            slot_event_record_t *events,
                            uint8_t event_capacity);

const nvme_slot_t *SlotManager_GetSlot(uint8_t slot_id);
void SlotManager_RecordI2CResult(uint8_t slot_id,
                                 uint32_t poll_timestamp_ms,
                                 bool packet_valid,
                                 uint8_t sequence,
                                 uint8_t status,
                                 uint8_t data);

const char *SlotManager_StateName(slot_state_t state);
const char *SlotManager_EventName(slot_event_t event);

#endif /* SLOT_MANAGER_H */
