# Flashing the bd_pressure firmware

## Requirements

- [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html) (free download from ST)
- USB cable connected to the bd_pressure sensor

## Procedure

1. Hold the **BOOT** button on the sensor
2. Power on the sensor via USB
3. Release the BOOT button
4. Open STM32CubeProgrammer, select the UART port, and click **Connect**
5. Open the hex file and click **Download**
6. Press reset (or power cycle) to boot into the new firmware

Video reference (same process as bdwidth): https://youtu.be/c74Q1chOo8M

---

## Available firmware

| File | Version | Description |
|---|---|---|
| `bd_pressure-rrf-v2.24.hex` | v2.24 | Current release — RRF UART firmware |

For the full list of changes see [CHANGELOG.md](CHANGELOG.md).

---

## Verifying the flash

After flashing, the sensor should appear in the DWC console once wired and `config.g` is set up.
Run `M98 P"/macros/bd_version.g"` — the console should show `bd_pressure-rrf-v2.24`.

Alternatively, open a serial terminal at **115200 baud** and send `v;` — the sensor will respond with:

```
bd_pressure-rrf-v2.24
```
