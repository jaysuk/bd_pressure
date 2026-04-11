# bd_pressure Firmware Changelog

## v2 — bd_pressure-rrf-v2.hex

**RepRapFirmware integration (standalone Duet, no SBC)**
- Automated PA calibration: sensor drives RRF over USB using Marlin emulation (M555 P2)
- Trigger via `M118 P0 S"l:H<high>:L<low>:T<travel>:S<pa_step>:N<steps>:E<extruder>;"` from RRF macro
- Result reported three ways: DWC console (M118 P2), DWC popup (M291), SD card file `/sys/pa_result.g`
- USB disconnect watchdog: auto-aborts calibration if no `ok` received for 30 seconds

**New sensor commands**
- `v;` — returns firmware version string (`bd_pressure-rrf-v2`)
- `a;` — aborts in-progress PA calibration, returns to endstop/probe mode
- `r;` — reboots the sensor
- `s;` — status query: returns `mode:<endstop|pa>;thr:<n>;inv:<0|1>;ver:v2`

**Probe improvements**
- `THRHOLD_Z` threshold persists across power cycles (saved to flash, page 15)
- Flash wear guard: skips erase/write if value unchanged
- `e;` and `l;` mode switches now echo a confirmation response to the host

**Build**
- Compiler: ARM Compiler 6.24 (ARMCLANG), Keil MDK Lite 5.43
- Code size: 30,716 bytes (target: STM32C011F6U6, 32KB flash)
- 0 errors, 0 warnings

---

## v1 (original)

- Klipper PA calibration support
- Z probe / nozzle endstop mode
- Raw ADC data output (`d;` / `D;`)
- Threshold tuning (`0;`–`99;`)
- Polarity inversion (`i;` / `I;`)
- Auto and manual baseline (`n;` / `N;`)
