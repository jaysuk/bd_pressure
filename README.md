# bd_pressure — RepRapFirmware Fork

This is a fork of the [bd_pressure firmware by markniu](https://github.com/markniu/bd_pressure), modified specifically for standalone **RepRapFirmware (Duet)** boards. The original firmware targets Klipper-based printers and is the recommended choice for Klipper users.

> **Note:** Parts of the bd_pressure firmware (specifically the pressure analysis library `pa.lib`) are closed source and are included here as a pre-compiled object. The RRF integration layer, GCode macros, and DWC plugin in this repository are open source under the MIT licence.

---

## What is bd_pressure?

bd_pressure is a strain-gauge sensor module that mounts to your toolhead and serves two purposes:

- **Automatic Pressure Advance calibration** — measures extruder back-pressure directly during controlled extrusion moves, no calibration prints required. The same algorithm used in the original Klipper firmware runs here on the STM32, with results logged and visualised via a DWC plugin.
- **Nozzle Z probe** — acts as a high-precision switch-type endstop, suitable for Z homing, Z tilt correction, and bed mesh levelling.

![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)

---

Video: https://youtu.be/xD0XgPfmwlg

<img src="https://cdn.hackaday.io/images/1561891773995579614.png" width="600">

---

## This fork — what's different from the original

| | Original (Klipper) | This fork (RRF) |
|---|---|---|
| **Target firmware** | Klipper | RepRapFirmware 3.5+ (standalone Duet, no SBC) |
| **Communication** | USB serial (CH340) + Klipper plugin | Hardware UART on WAFER/I2C connector |
| **Calibration control** | Klipper macro (`PA_CALIBRATE`) | RRF GCode macro (`pa_calibrate.g`) |
| **Results** | Klipper console + automatic apply | DWC console + popup + `/sys/pa_result.g` |
| **Visualisation** | — | DWC plugin (charts, analysis, suggested next sweep) |
| **Z probe** | Supported | Supported |

The firmware running on the STM32 is based on markniu's original, with changes to:
- Replace USB CDC with hardware USART1 (PB6/PB7) for direct UART wiring to the Duet
- Add RRF-specific commands (`pdata;`, `score;`, `rdata;`, `ver;`, `mode;`)
- Remove Klipper-specific USB control flow

---

## Hardware

Mounting footprint compatible with E3D and Voron toolhead ecosystems.

<img src="https://static.wixstatic.com/media/0d0edf_1ebb592e9ab04beeacb07abdf56b3e41~mv2.jpg/v1/fill/w_1658,h_1604,al_c,q_85,usm_0.66_1.00_0.01/0d0edf_1ebb592e9ab04beeacb07abdf56b3e41~mv2.jpg" width="600">

**Buy:** [pandapi3d.com](https://www.pandapi3d.com/product-page/bdpressuree)

---

## Installation

### 1. Flash the firmware

See [firmware_src/release_hex/README.md](firmware_src/release_hex/README.md) for the flashing procedure (STM32CubeProgrammer via UART boot).

Download the latest hex: [`firmware_src/release_hex/bd_pressure-rrf-v2.24.hex`](firmware_src/release_hex/bd_pressure-rrf-v2.24.hex)

### 2. Wire up

```
bd_pressure PB6 (TX)  ──► Duet io0 RX
bd_pressure PB7 (RX)  ──► Duet io0 TX
bd_pressure Z out     ──► Duet Z-probe input (zstop)
bd_pressure GND       ──► Duet GND
```

Logic levels are 3.3 V. Default baud rate is 115200.

### 3. Copy SD card files

Copy the contents of [`duet_sd/`](duet_sd/) directly onto your Duet SD card — the folder structure mirrors the Duet SD card layout exactly.

### 4. Add to config.g

```gcode
M98 P"/sys/bd_globals.g"              ; initialise bd_pressure globals
M575 P{global.bd_port} S2 B{global.bd_baud}   ; UART in raw mode
M558 P8 C"zstop" H5 F300:60 T12000   ; Z probe (adjust pin for your board)
G31 P500 X0 Y0 Z-0.1                 ; adjust Z offset after first probe run
```

### 5. Full documentation

**[docs/reprapfirmware.md](docs/reprapfirmware.md)** — complete setup guide including wiring, config.g snippets, all sensor commands, PA calibration walkthrough, and troubleshooting.

---

## DWC Plugin

A companion DWC plugin (**BdPressurePA**) visualises PA calibration logs directly in DWC:

- Loads log from Duet SD card, drag-drop, or file browse
- Three-panel chart: pressure score (res), slopes (lk/rk), signal quality (Hk/Ha)
- Best PA highlighted with red dashed line and green good-zone band
- Automatic analysis: noise assessment, minimum position, slope asymmetry
- Suggested next sweep parameters (zoom-in or range shift) with one-click copy
- Light and dark mode support

**Plugin repository:** *(URL to be added)*

Install via DWC → Settings → Plugins → Upload plugin zip.

---

## PA Calibration overview

`pa_calibrate.g` runs entirely in the air — no bed contact, no calibration prints. The nozzle is raised to a safe Z height and makes slow→fast→slow X moves while extruding, simulating the pressure transients that PA compensates for. The bd_pressure sensor scores each move and logs all 5 pa.lib metrics (`res, lk, rk, Hk, Ha`) per iteration — the same values the original Klipper plugin produces.

Edit the parameters at the top of `pa_calibrate.g` to match your printer (tool number, extruder index, nozzle temperature, PA sweep range and step size), then run:

```gcode
M98 P"/sys/pa_calibrate.g"
```

Results are saved to `/sys/pa_calibrate_log.txt` and `/sys/pa_result.g`. The DWC plugin can load and visualise the log directly from the Duet.

---

## Firmware changelog

See [firmware_src/release_hex/CHANGELOG.md](firmware_src/release_hex/CHANGELOG.md).

---

## Original project & Klipper users

- **Original firmware & Klipper plugin:** https://github.com/markniu/bd_pressure
- **Documentation (Klipper):** https://pandapi3d.cn/en/bdpressure/home
- **Discord:** [discord.gg/z6ahddnGVU](https://discord.gg/z6ahddnGVU)
- **Facebook group:** [facebook.com/groups/380795976169477](https://www.facebook.com/groups/380795976169477)
- **Shop:** [pandapi3d.com](https://www.pandapi3d.com/product-page/bdpressuree)
