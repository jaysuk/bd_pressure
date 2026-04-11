# Firmware Download into the sensor

1. Press and Hold on the boot button on the sensor
2. Power on the sensor with usb cable
3. Release the boot button
4. Open the STM32CubeProgrammer.exe
5. Choose the UART port and click connect button in the STM32CubeProgrammer
6. Open the firmware hex file and click Download
7. Finish

This process is the same as bdwidth sensor, here is the video of bdwidth: https://youtu.be/c74Q1chOo8M

---

## Available firmware versions

| File | Description |
|---|---|
| `bd_pressure-rrf-v2.hex` | RRF + Klipper firmware — PA calibration, status query, flash-persistent threshold |

For full changelog see [CHANGELOG.md](CHANGELOG.md).

## Verifying the flash

After flashing, open a serial terminal at **38400 baud** and send `v;` — the sensor should respond:

```
bd_pressure-rrf-v2
```
