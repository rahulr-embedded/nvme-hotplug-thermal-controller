#include "wokwi-api.h"

#include <stdbool.h>
#include <stdint.h>

#define MOCK_DRIVE_MAX_INSTANCES       4U
#define MOCK_DRIVE_GET_DATA_COMMAND  0x01U
#define MOCK_DRIVE_PACKET_SIZE         6U
#define MOCK_DRIVE_MAGIC             0xA5U
#define MOCK_DRIVE_STATUS_NORMAL     0x01U

typedef struct
{
    uint8_t slot_id;
    uint8_t sequence;
    uint8_t lfsr;
    uint8_t command;
    uint8_t byte_index;
    uint8_t packet[MOCK_DRIVE_PACKET_SIZE];
    bool command_valid;
    bool read_transaction;
    i2c_dev_t i2c_device;
} mock_drive_t;

static mock_drive_t mock_drives[MOCK_DRIVE_MAX_INSTANCES];
static uint8_t mock_drive_instance_count = 0U;

static uint8_t MockDrive_NextLfsr(uint8_t value)
{
    uint8_t feedback;

    feedback = (uint8_t)(((value >> 7U) ^
                          (value >> 5U) ^
                          (value >> 4U) ^
                          (value >> 3U)) & 0x01U);
    value = (uint8_t)((value << 1U) | feedback);

    return (value == 0U) ? 0xA7U : value;
}

static void MockDrive_PreparePacket(mock_drive_t *drive)
{
    drive->packet[0] = MOCK_DRIVE_MAGIC;
    drive->packet[1] = drive->slot_id;
    drive->packet[2] = MOCK_DRIVE_STATUS_NORMAL;
    drive->packet[3] = drive->sequence;
    drive->packet[4] = drive->lfsr;
    drive->packet[5] = drive->packet[0] ^
                       drive->packet[1] ^
                       drive->packet[2] ^
                       drive->packet[3] ^
                       drive->packet[4];
}

static bool MockDrive_OnConnect(void *user_data,
                                uint32_t address,
                                bool read)
{
    mock_drive_t *drive = (mock_drive_t *)user_data;
    (void)address;

    drive->read_transaction = read;
    drive->byte_index = 0U;

    if (read)
    {
        if (!drive->command_valid ||
            (drive->command != MOCK_DRIVE_GET_DATA_COMMAND))
        {
            return false;
        }

        MockDrive_PreparePacket(drive);
    }
    else
    {
        drive->command = 0U;
        drive->command_valid = false;
    }

    return true;
}

static uint8_t MockDrive_OnRead(void *user_data)
{
    mock_drive_t *drive = (mock_drive_t *)user_data;

    if (drive->byte_index >= MOCK_DRIVE_PACKET_SIZE)
    {
        return 0xFFU;
    }

    return drive->packet[drive->byte_index++];
}

static bool MockDrive_OnWrite(void *user_data, uint8_t data)
{
    mock_drive_t *drive = (mock_drive_t *)user_data;

    if (drive->byte_index != 0U)
    {
        return false;
    }

    drive->byte_index = 1U;
    drive->command = data;
    drive->command_valid = data == MOCK_DRIVE_GET_DATA_COMMAND;

    return drive->command_valid;
}

static void MockDrive_OnDisconnect(void *user_data)
{
    mock_drive_t *drive = (mock_drive_t *)user_data;

    if (drive->read_transaction &&
        drive->command_valid &&
        (drive->byte_index >= MOCK_DRIVE_PACKET_SIZE))
    {
        drive->sequence++;
        drive->lfsr = MockDrive_NextLfsr(drive->lfsr);
        drive->command_valid = false;
    }
}

void chip_init(void)
{
    mock_drive_t *drive;
    uint32_t address_attribute;
    uint32_t slot_attribute;
    uint32_t seed_attribute;
    i2c_config_t i2c_config;

    if (mock_drive_instance_count >= MOCK_DRIVE_MAX_INSTANCES)
    {
        return;
    }

    drive = &mock_drives[mock_drive_instance_count++];

    address_attribute = attr_init("address", 0x50U);
    slot_attribute = attr_init("slotId", 1U);
    seed_attribute = attr_init("seed", 0x31U);

    drive->slot_id = (uint8_t)attr_read(slot_attribute);
    drive->sequence = 1U;
    drive->lfsr = (uint8_t)attr_read(seed_attribute);
    drive->command = 0U;
    drive->byte_index = 0U;
    drive->command_valid = false;
    drive->read_transaction = false;

    i2c_config.user_data = drive;
    i2c_config.address = attr_read(address_attribute);
    i2c_config.scl = pin_init("SCL", INPUT_PULLUP);
    i2c_config.sda = pin_init("SDA", INPUT_PULLUP);
    i2c_config.connect = MockDrive_OnConnect;
    i2c_config.read = MockDrive_OnRead;
    i2c_config.write = MockDrive_OnWrite;
    i2c_config.disconnect = MockDrive_OnDisconnect;

    for (uint8_t index = 0U; index < 8U; index++)
    {
        i2c_config.reserved[index] = 0U;
    }

    drive->i2c_device = i2c_init(&i2c_config);
}
