# duet_sd — Duet SD card files

This folder mirrors the directory layout of the Duet SD card.
Copy the contents directly onto your Duet SD card:

```
duet_sd/
├── sys/                         →  /sys/   on the Duet SD card
│   ├── bd_globals.g                 Global variables (bd_port, bd_uart, bd_baud) — call from config.g
│   ├── pa_calibrate.g               Automated PA calibration macro — edit parameters before use
│   ├── pa_calibrate_live.g          Plugin-driven PA calibration — called by DWC plugin, do not run manually
│   ├── deployprobe.g                Re-baselines the sensor before each probe tap
│   ├── retractprobe.g               Empty stub required by RRF when deployprobe.g is present
│   └── config_example.g             Config snippets — copy relevant lines into your config.g
│                                    (do NOT copy this file itself to the SD card)
│
└── macros/                      →  /macros/  on the Duet SD card
    ├── bd_version.g                 Query sensor firmware version (console)
    ├── bd_status.g                  Query mode, threshold, polarity, baud (popup)
    ├── bd_set_threshold.g           Set the probe trigger threshold interactively
    ├── bd_endstop_mode.g            Switch sensor to endstop/probe mode
    ├── bd_pa_mode.g                 Switch sensor to PA mode (diagnostics)
    ├── bd_reboot.g                  Reboot the sensor
    ├── bd_baud.g                    Change UART baud rate interactively
    ├── bd_logging.g                 Enable/disable trigger logging and raw ADC output
    └── bd_uart_test.g               Diagnostic: tests ver; and mode; single-byte reads
```

## Quick start

1. Copy **all files** from `sys/` to `/sys/` on the Duet SD card
2. Copy **all files** from `macros/` to `/macros/` on the Duet SD card
3. Open `sys/config_example.g` and copy the relevant lines into your existing `config.g`
4. In `config.g`, add `M98 P"/sys/bd_globals.g"` near the top (before any other bd_ macros)
5. Edit `pa_calibrate.g` — set `var tool`, `var extruder`, `var nozzle_temp`, and your PA sweep range

For full setup instructions see [docs/reprapfirmware.md](../docs/reprapfirmware.md).
