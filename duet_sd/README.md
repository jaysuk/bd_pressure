# duet_sd — Duet SD card files

This folder mirrors the directory layout of the Duet SD card.
Copy the contents directly onto your Duet SD card:

```
duet_sd/
├── sys/                    →  /sys/   on the Duet SD card
│   ├── pa_calibrate.g          Automated PA calibration macro (edit parameters before use)
│   ├── deployprobe.g           Re-baselines the sensor before each probe tap
│   ├── retractprobe.g          Empty stub required by RRF when deployprobe.g is present
│   └── config_example.g        Config snippets — copy relevant lines into your config.g
│                               (do NOT copy this file itself to the SD card)
│
└── macros/                 →  /macros/  on the Duet SD card
    ├── bd_version.g            Query sensor firmware version
    ├── bd_status.g             Query mode, threshold, and polarity
    ├── bd_abort.g              Abort a PA calibration run mid-way
    ├── bd_reboot.g             Reboot the sensor (equivalent to power cycle)
    ├── bd_set_threshold.g      Set the probe trigger threshold (saved to flash)
    ├── bd_endstop_mode.g       Switch sensor to endstop/probe mode
    └── bd_pa_mode.g            Switch sensor to PA mode (diagnostics only)
```

## Quick start

1. Copy `sys/pa_calibrate.g`, `sys/deployprobe.g`, and `sys/retractprobe.g` to `/sys/` on the SD card.
2. Copy all files from `macros/` to `/macros/` on the SD card.
3. Open `sys/config_example.g` and copy the relevant snippets into your existing `config.g`.
4. **Edit `pa_calibrate.g`** — at minimum set `var.start_x`, `var.start_y`, and `var.start_z`
   to a safe position on your bed before running for the first time.
5. **Optional:** set `var.pa_start` in `pa_calibrate.g` to start the sweep from a known value
   rather than zero — useful for fine-tuning around a previously calibrated PA value.

For full setup instructions see [docs/reprapfirmware.md](../docs/reprapfirmware.md).
