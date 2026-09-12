# Mock NVMe I2C drive

`mock-nvme-drive.chip.c` implements the assignment's six-byte mock-drive protocol with Wokwi's native I2C Device API. Four instances use diagram attributes for their 7-bit address, slot ID, and deterministic LFSR seed.

Compile it from the project root with Wokwi CLI 0.20.0 or newer:

```powershell
wokwi-cli chip compile chips/mock-nvme-drive.chip.c -o chips/mock-nvme-drive.chip.wasm
```

The MCU transmits command `0x01`, then reads six bytes. A sequence counter and LFSR advance only after all six response bytes are read.
