# ESP32-C6 firmware — Home-Display build 2.12.13

The ESP32-C6 on the Waveshare ESP32-P4 board is a separate wireless co-processor
with its own flash. Its firmware is **not** part of the ESP32-P4 `firmware.bin`.

This project uses a **project-pinned Home-Display C6 build based on ESP-Hosted
2.12.13**. The stock/original C6 image shipped with a board is not the reference
image for this project.

The required files are kept in:

```text
firmware/c6/esp-hosted-2.12.13/
```

Use this exact version for Home-Display unless the repository is deliberately
updated to another pinned C6 release. Do not replace it with an automatically
detected or arbitrary ESP-Hosted build.

Before flashing, verify the checked-in files against:

```text
firmware/c6/esp-hosted-2.12.13/SHA256SUMS
```

## What must be flashed

The checked-in C6 image consists of four parts:

| Offset | File |
|---:|---|
| `0x0000` | `bootloader/bootloader.bin` |
| `0x8000` | `partition_table/partition-table.bin` |
| `0xD000` | `ota_data_initial.bin` |
| `0x10000` | `network_adapter.bin` |

Flash settings:

- target: `esp32c6`
- flash mode: `dio`
- flash frequency: `80m`
- flash size: `4MB`

The same information is stored in `flash_args` and `flasher_args.json`.

# Installation / first C6 flash

## Required connection

Use the board's **PROG_C6** UART connection and a **3.3-V TTL UART adapter**.

Connect:

| USB/UART adapter | PROG_C6 / C6 |
|---|---|
| TXD | RXD |
| RXD | TXD |
| GND | GND |

Additionally connect:

```text
C6 IO9 -> GND
```

Important:

- TX and RX are crossed.
- Use a common GND.
- **Do not connect the adapter VCC/3.3-V/5-V pin.**
- Power the Waveshare board normally from its own supply.
- Do not flash the ESP32-P4 serial port with the C6 image.

## Enter the ESP32-C6 ROM bootloader

1. Switch the Waveshare board off.
2. Connect `C6 IO9 -> GND`.
3. Connect UART TXD/RXD/GND as shown above.
4. Keep the P4 from taking control of the C6 during startup as required by the
   Waveshare programming procedure (hold the board BOOT button while powering
   the system when using that procedure).
5. Power the board.
6. Release the board BOOT button after startup.
7. Keep `C6 IO9 -> GND` connected while establishing the C6 flashing session.

A successful esptool connection must identify:

```text
ESP32-C6
```

If esptool reports `No serial data received`, check IO9-to-GND, crossed TX/RX,
common GND, the selected COM port and the board power/reset sequence before
changing firmware files.

## Flash the checked-in Home-Display C6 firmware

Open a terminal in:

```text
firmware/c6/esp-hosted-2.12.13
```

Then run, replacing `COM15` with the actual **C6 UART adapter** port:

```powershell
python -m esptool --chip esp32c6 --port COM15 erase_flash
python -m esptool --chip esp32c6 --port COM15 --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB 0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0xd000 ota_data_initial.bin 0x10000 network_adapter.bin
```

If the adapter or connection is unstable at 460800 baud, retry with:

```text
--baud 115200
```

After flashing:

1. Power the board off.
2. Remove the `IO9 -> GND` bridge.
3. Disconnect the external UART adapter.
4. Power the Waveshare board normally.
5. Boot the P4 firmware and check the C6/ESP-Hosted status.

The P4 side must no longer remain at a placeholder such as:

```text
Co-proc [0.0.0]
```

The Home-Display configuration expects the pinned C6 firmware family based on
ESP-Hosted **2.12.13**.

# Build the project-pinned C6 image

The repository also contains the helper used to reproduce the Home-Display C6
build.

Open an ESP-IDF 5.5.5 PowerShell/CMD environment and run from the repository
root:

```powershell
python tools/build_c6_slave.py
```

The generated build is kept below:

```text
build/c6-esp-hosted-slave/
```

The ESP32-C6 firmware must remain separate from the P4 firmware. Never append a
C6 image to the P4 `firmware.bin`.

# Updates from the Home-Display UI

The firmware architecture contains a C6 update service for the pinned project
firmware. That path must target the Home-Display C6 2.12.13 image and validate
the expected image/version data before an update is accepted.

The original board firmware is not a fallback reference for this project.

For first installation, recovery, or a C6 that no longer answers correctly,
use the UART/IO9 procedure above.
