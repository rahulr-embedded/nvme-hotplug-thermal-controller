# ARM Multi-Slot NVMe Presence Monitor

## Current implementation guide

Last verified: **2026-09-03 (IST)**

Project directory:

~~~text
C:/NVMeController/workspace/nvmecontroller/nvme_controller
~~~

## 1. Project overview

This STM32 application monitors four active-low slot-presence inputs, debounces insertion and removal events, displays the current slot state on an I2C LCD2004, and exchanges deterministic test packets with four simulated I2C drives.

The mock drives model assignment-level telemetry only. This project does not implement a PCIe link or the NVMe storage protocol.

| Item | Current implementation |
|---|---|
| Board | NUCLEO-C031C6 |
| MCU | STM32C031C6, ARM Cortex-M0+ |
| System clock | 48 MHz HSI |
| Framework | STM32 HAL, bare-metal main loop |
| Slot count | 4 |
| Presence logic | Active low: LOW = inserted, HIGH = empty |
| Debounce | 50 ms independently per slot |
| Display | LCD2004 with PCF8574 backpack at 0x27 |
| Drive addresses | 0x50, 0x51, 0x52, 0x53 |
| UART | USART2, 115200 baud, 8-N-1 |
| RTOS | None |
| Dynamic allocation | None |

### Current verification status

| Check | Status |
|---|---|
| STM32CubeIDE Debug build | Passed: 0 errors, 0 warnings |
| Current binary size | text 20,480; data 96; bss 3,088 bytes |
| Flash image usage | 20,576 bytes |
| Static RAM usage | 3,184 bytes: data + bss |
| Physical-board HAL backend compilation | Passed |
| diagram.json parsing and unique IDs | Passed |
| Wokwi strict lint | Passed: no issues |
| LCD initialization over the Wokwi bus | Passed |
| Communication with drives 0x50 through 0x53 | Passed |
| Changing sequence/data and valid checksums | Passed |
| Final switch, LCD visual, VCD, and endurance run | Pending |

## 2. Runtime architecture

~~~text
Slide switches
    |
    v
GPIO EXTI callbacks
    |
    | record only a timestamp and pending bit
    v
SlotManager (10 ms task)
    |
    +---- confirmed event ----> UART event log
    |                       \--> LCD refresh request
    |
    +---- stable presence ----> DriveI2C (1000 ms per present slot)
                                  |
                                  v
                               AppI2C
                                  |
                     +------------+-------------+
                     |                          |
                     v                          v
                 LCD at 0x27          mock drives at 0x50-0x53
                                                |
                                                v
                                      packet validation and UART log
~~~

### Initialization order

1. Initialize HAL and the 1 ms SysTick time base.
2. Configure the 48 MHz HSI system clock.
3. Initialize GPIO, CubeMX I2C1, and USART2.
4. Read all four presence pins and establish their startup states.
5. Initialize the selected application I2C backend.
6. Initialize the mock-drive polling module.
7. Initialize the LCD and write its first frame.
8. Print the startup banner and initial slot status.
9. Start the cooperative scheduler.

### Main-loop work

| Work | Scheduling |
|---|---|
| Slot state-machine processing | Every 10 ms |
| Drive polling | Checked continuously; each present slot is due every 1000 ms |
| LCD service | Scheduler checks every 250 ms, but writes only when a frame is pending |
| UART logging | Performed in main context after confirmed events or I2C results |

## 3. Hardware and pin configuration

### Application pins

| MCU pin | Function | Configuration |
|---|---|---|
| PB10 | Slot 1 presence | EXTI rising/falling, internal pull-up |
| PA9 | Slot 2 presence | EXTI rising/falling, internal pull-up |
| PA15 | Slot 3 presence | EXTI rising/falling, internal pull-up |
| PB2 | Slot 4 presence | EXTI rising/falling, internal pull-up |
| PB8 | I2C1 SCL | CubeMX AF6; software-I2C SCL at Wokwi runtime |
| PB9 | I2C1 SDA | CubeMX AF6; software-I2C SDA at Wokwi runtime |
| PA2 | USART2 TX | 115200 baud |
| PA3 | USART2 RX | 115200 baud |
| PA13 | SWDIO | Reserved for debugging |
| PA14 | SWCLK | Reserved for debugging |

### Interrupt routing

| IRQ | Presence inputs |
|---|---|
| EXTI2_3_IRQn | PB2 / Slot 4 |
| EXTI4_15_IRQn | PA9, PB10, PA15 / Slots 2, 1, 3 |

Both IRQs use priority 0. The generated handlers call HAL_GPIO_EXTI_IRQHandler(), which dispatches to the rising- or falling-edge callbacks in slot_manager.c.

### Wokwi power and bus wiring

| Device group | Supply |
|---|---|
| LCD2004 | 5 V |
| Four mock drives | 3.3 V |
| SCL/SDA pull-ups | 4.7 kΩ to 3.3 V |
| Presence switches | MCU input to GND when inserted |

## 4. Central configuration

The active constants are defined in Core/Inc/app_config.h.

| Constant | Value | Purpose |
|---|---:|---|
| NVME_SLOT_COUNT | 4 | Number of independent slots |
| SLOT_PROCESS_PERIOD_MS | 10 ms | Slot-manager task period |
| SLOT_DEBOUNCE_MS | 50 ms | Stable-level confirmation time |
| DRIVE_POLL_PERIOD_MS | 1000 ms | Per-slot packet request interval |
| LCD_UPDATE_PERIOD_MS | 250 ms | Minimum interval between pending LCD frame writes/retries |
| APP_I2C_USE_SOFTWARE_BACKEND | 1 | Selects the Wokwi PB8/PB9 software bus |
| APP_I2C_HALF_PERIOD_US | 5 μs | Nominal software-I2C half-period |
| UART_BAUD_RATE | 115200 | Serial logging rate |
| LCD_I2C_ADDRESS | 0x27 | LCD backpack address |
| DRIVE_I2C_TIMEOUT_MS | 25 ms | Drive transaction timeout |
| LCD_I2C_TIMEOUT_MS | 100 ms | LCD transaction timeout |

All application addresses are stored as unshifted 7-bit values.

## 5. Slot detection and debounce

Each slot uses the same data-driven state machine:

~~~text
EMPTY
  |
  | pin becomes LOW
  v
INSERT_DEBOUNCE
  |
  | remains LOW for 50 ms
  v
PRESENT
  |
  | pin becomes HIGH
  v
REMOVE_DEBOUNCE
  |
  | remains HIGH for 50 ms
  v
EMPTY
~~~

If the pin returns to its previous stable level before 50 ms, the candidate transition is rejected without generating an event.

### Interrupt/main-context split

The EXTI callback performs only three operations:

1. Identify the slot from the GPIO pin.
2. Store HAL_GetTick() for that slot.
3. Set its bit in a volatile pending-edge mask.

The callback does not debounce, format UART text, update the LCD, or start I2C traffic.

The main-loop slot task atomically takes the pending mask and timestamps, reads every current pin level, and processes all four state machines. This allows simultaneous slot changes.

### Startup behavior

SlotManager_Init() reads every physical input during startup:

- A HIGH input starts as EMPTY.
- A LOW input starts as PRESENT.
- A slot already present at startup is immediately eligible for its first I2C poll.
- Startup state is reported in the STATUS record.
- No INSERTED event is generated merely because a drive was already present at reset.

## 6. I2C implementation

### Default Wokwi backend

The current Wokwi build uses:

~~~text
APP_I2C_USE_SOFTWARE_BACKEND = 1
I2C Backend: SOFTWARE-PB8/PB9-V2
~~~

CubeMX still initializes I2C1 on PB8/PB9 at 100 kHz. AppI2C_Init() then disables the peripheral and uses the same physical pins as a software open-drain bus.

| Bus action | PB8/PB9 behavior |
|---|---|
| Assert LOW | Set ODR low and select open-drain output mode |
| Release HIGH | Select input mode with pull-up |
| Read ACK/data | Read GPIO IDR while the line is released |
| Clock stretching | Release SCL and wait for its IDR bit to become HIGH |

The driver implements:

- START and STOP conditions
- 7-bit addressing with read/write bit generation
- byte transmit and receive
- ACK and NACK handling
- timeout handling
- clock-stretch waiting
- nine-clock startup bus recovery

### Physical-board backend

For a physical board, set the following before compiling:

~~~c
#define APP_I2C_USE_SOFTWARE_BACKEND 0U
~~~

That path calls the unmodified STM32 HAL functions:

~~~c
HAL_I2C_Master_Transmit()
HAL_I2C_Master_Receive()
~~~

AppI2C applies the required left shift only at the HAL boundary, so all other modules continue using 7-bit addresses.

## 7. Mock-drive protocol

### Drive instances

| Slot | Address | Initial deterministic seed |
|---:|---:|---:|
| 1 | 0x50 | 0x31 |
| 2 | 0x51 | 0x57 |
| 3 | 0x52 | 0x83 |
| 4 | 0x53 | 0xC5 |

Only slots whose stable state is PRESENT are polled.

- The first request is due immediately after startup presence detection or a confirmed insertion.
- Subsequent requests are due every 1000 ms per slot.
- Polling stops after a confirmed removal.
- A communication failure invalidates the latest packet but never changes physical presence.

### Request

| Byte | Value | Meaning |
|---:|---:|---|
| 0 | 0x01 | GET_DRIVE_DATA |

### Six-byte response

| Index | Field | Rule |
|---:|---|---|
| 0 | Magic | Always 0xA5 |
| 1 | Slot ID | 1 through 4 |
| 2 | Status | Normally 0x01 |
| 3 | Sequence | Starts at 0x01 and increments after a complete response |
| 4 | Data | Deterministic 8-bit LFSR value |
| 5 | Checksum | XOR of bytes 0 through 4 |

Checksum:

~~~c
checksum = packet[0] ^
           packet[1] ^
           packet[2] ^
           packet[3] ^
           packet[4];
~~~

The STM32 validates the magic byte, expected slot ID, and checksum before accepting and logging a packet.

The reusable Wokwi custom chip is instantiated four times. Each instance receives its address, slot ID, and seed from diagram.json attributes.

## 8. LCD behavior

The display is a 20-column by 4-row HD44780-compatible LCD using a PCF8574 I2C backpack at address 0x27.

Current layout:

~~~text
NVME SLOT MONITOR
S1:IN    S2:EMPTY
S3:IN    S4:EMPTY
LAST:S3 INSERTED
~~~

The first frame is written during initialization. After that, the LCD is event-driven:

- A confirmed insertion or removal marks a new frame as pending.
- The 250 ms LCD task services pending frames.
- The 250 ms value also rate-limits retries.
- No frame is transmitted when the displayed content is unchanged.

This avoids continuously rewriting all 80 characters and leaves main-loop time available for debounce and drive polling.

## 9. UART output

USART2 uses 115200 baud, 8 data bits, no parity, one stop bit, and no flow control.

All timestamps are zero-padded decimal milliseconds from HAL_GetTick().

### Startup

~~~text
================================================
ARM MULTI-SLOT NVME PRESENCE MONITOR
Slots: 4
Presence: Active Low
Debounce: 50 ms
I2C Backend: SOFTWARE-PB8/PB9-V2
I2C LCD Address: 0x27
Drive Addresses: 0x50 0x51 0x52 0x53
System Ready
================================================
~~~

### Initial status

~~~text
STATUS,00000400,S1=EMPTY,S2=EMPTY,S3=EMPTY,S4=EMPTY
~~~

### Confirmed events

~~~text
EVT,00001250,SLOT1,INSERTED
EVT,00008430,SLOT1,REMOVED
~~~

### Valid packet

~~~text
I2C,00001310,SLOT1,ADDR=0x50,SEQ=01,STATUS=01,DATA=31,CHECKSUM=OK
~~~

### Drive errors

~~~text
I2C,00002310,SLOT1,ADDR=0x50,ERROR=TIMEOUT
I2C,00003310,SLOT1,ADDR=0x50,ERROR=BAD_CHECKSUM
~~~

Possible bus-layer names are NACK, BUS_BUSY, TIMEOUT, and INVALID_ARGUMENT. Packet validation can also report BAD_MAGIC, BAD_SLOT_ID, or BAD_CHECKSUM.

### Startup I2C errors

If bus or LCD initialization fails, the firmware prints one of these SYS records followed by one detailed I2C_DIAG record:

~~~text
SYS,00000100,ERROR=I2C_BUS_INIT_FAILED
SYS,00000100,ERROR=LCD_INIT_FAILED
~~~

The diagnostic includes the selected backend, last application result, I2C registers, transfer counts, GPIO mode/pull/output/input registers, and SystemCoreClock.

## 10. Wokwi simulation

### Circuit contents

- One NUCLEO-C031C6
- Four labeled slide switches
- One I2C LCD2004
- Two 4.7 kΩ I2C pull-ups
- Four reusable mock NVMe I2C chips
- One logic analyzer
- Serial Monitor connections
- Text labels for each functional region, slot pin, and drive address

### Diagram organization

| Region | Components |
|---|---|
| Left | Presence switches and MCU pin labels |
| Center | STM32 controller |
| Right | Drives 1 through 4 and addresses |
| Lower left | LCD2004 |
| Lower center | I2C pull-ups |
| Lower right | Logic analyzer |

The four drive instances share daisy-chained 3.3 V, ground, SCL, and SDA rails. The switch grounds share one ground chain. These are common electrical nets, not point-to-point communication links.

### Switch operation

- Switch open/HIGH: slot is EMPTY.
- Switch connected to GND/LOW: slot is IN/PRESENT.
- Wokwi switch bounce is enabled so the firmware debounce logic is exercised.

### Logic-analyzer channels

| Channel | Signal |
|---|---|
| D0 | Slot 1 presence / PB10 |
| D1 | Slot 2 presence / PA9 |
| D2 | USART2 TX / PA2 |
| D3 | I2C SCL / PB8 |
| D4 | I2C SDA / PB9 |

wokwi.toml writes the capture to:

~~~text
captures/nvme_controller.vcd
~~~

Fully stop the simulator before inspecting the VCD so Wokwi flushes the capture.

## 11. Source and configuration files

### Application modules

| File | Responsibility |
|---|---|
| Core/Inc/app_config.h | Timings, addresses, protocol constants, backend selection |
| Core/Inc/app_i2c.h | Application I2C API and result types |
| Core/Src/app_i2c.c | Wokwi software-I2C and physical HAL-I2C backends |
| Core/Inc/app_scheduler.h | Cooperative task bits and scheduler state |
| Core/Src/app_scheduler.c | 10 ms slot and 250 ms LCD task timing |
| Core/Inc/slot_manager.h | Slot states, event records, and public API |
| Core/Src/slot_manager.c | GPIO mapping, EXTI callbacks, debounce, slot data |
| Core/Inc/drive_i2c.h | Drive polling API |
| Core/Src/drive_i2c.c | Requests, receives, validates, and logs drive packets |
| Core/Inc/lcd_display.h | LCD public API |
| Core/Src/lcd_display.c | PCF8574/HD44780 initialization and event-driven frames |
| Core/Inc/event_logger.h | UART logger API |
| Core/Src/event_logger.c | Startup, status, event, packet, error, and diagnostic records |

### CubeMX and generated integration

| File | Responsibility |
|---|---|
| nvme_controller.ioc | MCU, clock, GPIO, I2C1, USART2, EXTI, and NVIC configuration |
| Core/Src/main.c | HAL initialization and application orchestration |
| Core/Inc/main.h | Generated presence-pin labels |
| Core/Src/stm32c0xx_it.c | SysTick and EXTI IRQ handlers |
| Core/Src/stm32c0xx_hal_msp.c | I2C/UART clocks, pins, and low-level setup |
| Drivers/ | STM32Cube HAL and CMSIS sources; no custom vendor-I2C patch is required |

### Wokwi files

| File | Responsibility |
|---|---|
| diagram.json | Organized circuit, parts, labels, and electrical connections |
| wokwi.toml | Firmware, ELF, VCD, and custom-chip registration |
| chips/mock-nvme-drive.chip.c | Reusable mock-drive behavior |
| chips/mock-nvme-drive.chip.json | Custom-chip name and pin list |
| chips/mock-nvme-drive.chip.wasm | Compiled custom-chip binary |
| chips/wokwi-api.h | Wokwi custom-chip API declarations |

### Build outputs

~~~text
Debug/nvme_controller.elf
Debug/nvme_controller.hex
Debug/nvme_controller.bin
Debug/nvme_controller.map
Debug/nvme_controller.list
~~~

## 12. Build and validation

### STM32CubeIDE

1. Open STM32CubeIDE.
2. Select workspace C:/NVMeController/workspace.
3. Select project nvme_controller.
4. Choose **Project > Clean**.
5. Choose **Project > Build Project**.
6. Confirm the Console reports 0 errors and 0 warnings.
7. Confirm the ELF, HEX, and BIN files exist under Debug/.

Current Debug size:

~~~text
text    data    bss     dec     hex
20480   96      3088    23664   5c70
~~~

### Recompile the custom drive

Run from the project root:

~~~powershell
./.tools/wokwi-cli/wokwi-cli-win-x64.exe chip compile chips/mock-nvme-drive.chip.c -o chips/mock-nvme-drive.chip.wasm
~~~

### Validate the Wokwi circuit

~~~powershell
./.tools/wokwi-cli/wokwi-cli-win-x64.exe lint --warnings-as-errors
~~~

Expected result:

~~~text
No issues found
~~~

## 13. Run in Wokwi

1. Fully stop any existing Wokwi simulation.
2. Build the latest Debug firmware.
3. Open C:/NVMeController/workspace/nvmecontroller/nvme_controller in VS Code.
4. Open diagram.json.
5. Press F1 and select **Wokwi: Start Simulator**.
6. Verify the banner contains SOFTWARE-PB8/PB9-V2.
7. Set every switch open for an all-empty startup test.
8. Connect a switch to GND to simulate insertion.
9. Watch the Serial Monitor and LCD.
10. Fully stop Wokwi after testing to finalize the VCD.

Wokwi loads these current artifacts:

~~~text
firmware = Debug/nvme_controller.hex
elf      = Debug/nvme_controller.elf
~~~

## 14. Acceptance checklist

| Test | Expected result | Status |
|---:|---|---|
| 1 | Start empty; UART and LCD show all four slots EMPTY | Pending on latest layout |
| 2 | Insert Slot 1; one event after about 50 ms, LCD changes, valid 0x50 packet | Pending on latest layout |
| 3 | Remove Slot 1; one event after about 50 ms and 0x50 polling stops | Pending on latest layout |
| 4 | Toggle Slot 2 for less than 50 ms; no confirmed event | Pending |
| 5 | Insert all slots; all show IN and addresses 0x50-0x53 respond | UART/I2C passed; LCD visual pending |
| 6 | Remove two slots; only the remaining present slots are polled | Pending |
| 7 | Consecutive responses change sequence and deterministic data | Passed |
| 8 | Every accepted packet has the expected slot ID and checksum | Passed |
| 9 | Analyzer captures LCD 0x27 and drives 0x50-0x53 | Pending finalized VCD |
| 10 | Run for at least five minutes without lockup or duplicate events | Pending |
| 11 | Present-slot packet groups remain approximately 1000 ms apart | Pending after event-driven LCD update |

## 15. Troubleshooting

### All slots report IN at startup

All four active-low switches are connected to GND. Move each switch to its open/HIGH position and restart for the empty-start test.

### No insertion or removal event

1. Confirm the switch common pin reaches the documented MCU presence pin.
2. Confirm the inserted position connects the input to GND.
3. Confirm the input is HIGH when open and LOW when inserted.
4. Confirm EXTI2_3_IRQn and EXTI4_15_IRQn remain enabled.
5. Hold the new level for more than 50 ms.

### I2C initialization fails

1. Confirm the banner says SOFTWARE-PB8/PB9-V2.
2. Confirm chips/mock-nvme-drive.chip.wasm exists.
3. Confirm wokwi.toml registers mock-nvme-drive.
4. Confirm the LCD and all drives share PB8/PB9.
5. Confirm both 4.7 kΩ pull-ups connect to 3.3 V.
6. Run strict Wokwi lint.
7. Copy the complete SYS and I2C_DIAG records when reporting the problem.

### LCD does not continuously refresh

This is intentional. The current implementation writes at startup and after confirmed slot events. It does not rewrite an unchanged frame every 250 ms.

### VCD contains only initial levels

Fully stop Wokwi. The analyzer capture may not be complete while the simulation is still running.

### Wokwi runs an older firmware image

1. Stop Wokwi.
2. Rebuild in STM32CubeIDE.
3. Verify the Debug ELF/HEX timestamps changed.
4. Start Wokwi again and check the backend banner.

## 16. Maintenance rules

- Keep application addresses in 7-bit form.
- Keep EXTI callbacks short; never perform UART formatting, LCD updates, or I2C transactions in interrupt context.
- Keep slot behavior data-driven through the four-element array.
- Preserve CubeMX USER CODE regions and separate application modules.
- After CubeMX regeneration, verify the application source files remain in the Debug build.
- Keep APP_I2C_USE_SOFTWARE_BACKEND set to 1 for Wokwi.
- Set APP_I2C_USE_SOFTWARE_BACKEND to 0 for the normal physical-board HAL path.
- Do not patch the STM32 vendor I2C driver for this application.
- Rebuild the custom-chip WASM after changing its C source.
- Run the firmware build and strict Wokwi lint after every functional change.
- Keep this guide synchronized with the current implementation; replace superseded information instead of appending a debugging diary.
