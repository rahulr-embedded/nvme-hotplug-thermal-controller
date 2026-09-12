#include "drive_i2c.h"

#include <stdbool.h>
#include <stddef.h>

#include "app_config.h"
#include "app_i2c.h"
#include "event_logger.h"
#include "slot_manager.h"

#if NVME_SLOT_COUNT != 4U
#error "Update the drive-address table when NVME_SLOT_COUNT changes"
#endif

static const uint8_t drive_addresses_7bit[NVME_SLOT_COUNT] =
{
    SLOT1_DRIVE_ADDRESS,
    SLOT2_DRIVE_ADDRESS,
    SLOT3_DRIVE_ADDRESS,
    SLOT4_DRIVE_ADDRESS
};

static bool drive_i2c_initialized = false;

static uint8_t DriveI2C_Checksum(const uint8_t *packet)
{
    return packet[0] ^ packet[1] ^ packet[2] ^ packet[3] ^ packet[4];
}

static void DriveI2C_RecordError(uint32_t now_ms,
                                 uint8_t slot_id,
                                 uint8_t address_7bit,
                                 const char *error_text)
{
    SlotManager_RecordI2CResult(slot_id,
                                now_ms,
                                false,
                                0U,
                                0U,
                                0U);
    EventLogger_LogDriveError(now_ms,
                              slot_id,
                              address_7bit,
                              error_text);
}

static void DriveI2C_PollSlot(uint8_t slot_id, uint32_t now_ms)
{
    uint8_t command = DRIVE_COMMAND_GET_DATA;
    uint8_t packet[DRIVE_PACKET_SIZE];
    uint8_t address_7bit = drive_addresses_7bit[slot_id - 1U];
    app_i2c_result_t result;

    result = AppI2C_MasterTransmit(
        address_7bit,
        &command,
        1U,
        DRIVE_I2C_TIMEOUT_MS);

    if (result != APP_I2C_OK)
    {
        DriveI2C_RecordError(now_ms,
                             slot_id,
                             address_7bit,
                             AppI2C_ResultName(result));
        return;
    }

    result = AppI2C_MasterReceive(
        address_7bit,
        packet,
        DRIVE_PACKET_SIZE,
        DRIVE_I2C_TIMEOUT_MS);

    if (result != APP_I2C_OK)
    {
        DriveI2C_RecordError(now_ms,
                             slot_id,
                             address_7bit,
                             AppI2C_ResultName(result));
        return;
    }

    if (packet[0] != DRIVE_PACKET_MAGIC)
    {
        DriveI2C_RecordError(now_ms,
                             slot_id,
                             address_7bit,
                             "BAD_MAGIC");
        return;
    }

    if (packet[1] != slot_id)
    {
        DriveI2C_RecordError(now_ms,
                             slot_id,
                             address_7bit,
                             "BAD_SLOT_ID");
        return;
    }

    if (packet[5] != DriveI2C_Checksum(packet))
    {
        DriveI2C_RecordError(now_ms,
                             slot_id,
                             address_7bit,
                             "BAD_CHECKSUM");
        return;
    }

    SlotManager_RecordI2CResult(slot_id,
                                now_ms,
                                true,
                                packet[3],
                                packet[2],
                                packet[4]);

    EventLogger_LogDrivePacket(now_ms,
                               slot_id,
                               address_7bit,
                               packet[3],
                               packet[2],
                               packet[4]);
}

void DriveI2C_Init(I2C_HandleTypeDef *i2c)
{
    drive_i2c_initialized = i2c != NULL;
}

void DriveI2C_Process(uint32_t now_ms)
{
    uint8_t slot_id;

    if (!drive_i2c_initialized)
    {
        return;
    }

    for (slot_id = 1U; slot_id <= NVME_SLOT_COUNT; slot_id++)
    {
        const nvme_slot_t *slot = SlotManager_GetSlot(slot_id);

        if (slot->stable_present &&
            ((uint32_t)(now_ms - slot->last_i2c_poll_ms) >=
             DRIVE_POLL_PERIOD_MS))
        {
            DriveI2C_PollSlot(slot_id, now_ms);
        }
    }
}
