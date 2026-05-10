# bd_pressure Firmware Changelog

## v2.24 — bd_pressure-rrf-v2.24 (2026-05-10)

**Bug fix: `mode;` command intercepted by `e;` handler**
- `mode;` ends with the character `e`, so `cmd = rxData[j-1]` was `'e'` — causing the single-char `e;` handler to fire first, switching the sensor back to endstop mode and returning ASCII 77 (`M`) instead of the mode byte
- Fixed by moving `ver;` and `mode;` multi-char checks to **before** the `cmd=='e'` single-char handler in the dispatch chain

**Bug fix: version minor displayed as full byte value**
- `var.bd_ver_minor = var.bd_ver_raw[0] % 100` used `%` which RRF GCode does not support as modulo
- Fixed to `var.bd_ver_raw[0] - var.bd_ver_major * 100` in both `pa_calibrate.g` and `bd_uart_test.g`

---

## v2.23 — bd_pressure-rrf-v2.23 (2026-05-10)

**Bug fix: all multi-char commands used absolute rxData indexing**
- `score;`, `rdata;`, `pdata;`, `ver;`, `mode;` all used `rxData[0..N]` absolute indexing, which is only correct if the command starts at buffer position 0
- Fixed to use `rxData[j-N..j-1]` (relative to the `;` position) for all multi-char command matches
- This was the root cause of all multi-char commands failing silently

---

## v2.22 — bd_pressure-rrf-v2.22 (2026-05-10)

**Bug fix: `FIRMWARE_VERSION_BYTE` define had trailing `u` suffix**
- Removed `u` suffix from the numeric literal; post_build.bat was also fixed (trailing space in findstr pattern) so it no longer matches `FIRMWARE_VERSION_BYTE`

---

## v2.21 — bd_pressure-rrf-v2.21 (2026-05-10)

**New sensor commands (single-byte binary responses)**
- `ver;` — replaced string response with single-byte encoded version: `major×100 + minor` (e.g. 221 for v2.21). Read with `M261.2 B1`
- `mode;` — replaced string response with single-byte: `0` = PA mode, `1` = endstop mode. Read with `M261.2 B1`

---

## v2.20 — bd_pressure-rrf-v2.20 (2026-05-10)

**New sensor commands**
- `mode;` — returns current operating mode as `pa\n` or `endstop\n` over UART, readable with `M261.2 B16`. Used by `pa_calibrate.g` to confirm the sensor entered PA mode and record it in the log header.

---

## v2.19 — bd_pressure-rrf-v2.19 (2026-05-10)

**New sensor commands**
- `ver;` — returns firmware version string (`bd_pressure-rrf-v2.19\n`) directly over UART, readable with `M261.2 B32`. Distinct from `v;` which routes to the RRF console. Used by `pa_calibrate.g` to log the sensor version.

**PA calibration log improvements** (`pa_calibrate.g`)
- Log file now includes a `#` comment header before the CSV data: `date`, `rrf_version`, `bd_version` (from `ver;`), `mode` (from `mode;`), `nozzle_temp`, `pa_start`, `pa_step`, `steps`
- Older log files without headers are still supported by the plotter and DWC plugin

**DWC plugin** (`BdPressurePA`)
- Metadata header displayed as coloured chips above the charts: date, RRF version, bd_pressure version, mode (teal = pa, purple = endstop), nozzle temperature, PA sweep parameters

**Python plotter** (`tools/pa_log_plot.py`)
- Parses `#` header lines and displays them as a subtitle on the figure and as printed metadata in the terminal

---

## v2.18 — bd_pressure-rrf-v2.18 (2026-05-10)

**PA calibration rewrite — matches Klipper reference implementation**
- Calibration now uses XY moves at raised Z (nozzle clear of bed) rather than E-only stationary extrusion — matches how Klipper's `PA_E` macro works
- Fixed Y position at bed centre; X-only movement: 15 mm slow → 30 mm fast → 15 mm slow per iteration
- `e_per_mm = 0.046322` — Klipper reference extrusion ratio (mm filament per mm XY)
- All 5 pa.lib metrics now logged per iteration: `res, lk, rk, Hk, Ha` — same field order as Klipper `R:` output

**New sensor commands**
- `pdata;` — returns 5 raw bytes (`res, lk, rk, Hk, Ha`) readable with `M261.2 B5`
- `rdata;` — returns full `R:res,lk,rk,Hk,Ha\n` ASCII string for direct developer comparison

**`pa.lib` integration**
- `pa_vals[]` populated from `k_left`, `k_right`, `H_left`, `H_right` globals after `get_low_value()` returns
- `R:` string formatted using `_u32_to_dec()` (no `snprintf` / stdio dependency)
- `c;` command now clears `pa_vals` and `pa_rdata_ready` on entry

**GCode macros**
- All `M118 P2` changed to `M118 P0` (P2 = PanelDue, not visible in DWC console)
- Homing check: `if !move.axes[0].homed || !move.axes[1].homed || !move.axes[2].homed → G28`

**DWC plugin** (`BdPressurePA`)
- Initial release of the DWC PA Calibration plugin
- Load log by drag-drop, file browse, or **Load from Duet** (fetches `/sys/pa_calibrate_log.txt` directly)
- Three-panel chart: res, lk/rk slopes, Hk/Ha signal quality
- Best PA highlighted with red dashed line; one-click **Copy M572** button
- Raw data table in collapsible panel

---

## v2 — bd_pressure-rrf-v2.hex (2026-04-12)

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

**Input validation**
- `pa_rrf_parse_params()` now clamps all parameters to safe ranges after parsing:
  speeds clamped to 100–60000 mm/min; `pa_step` to 0.001–0.1; `pa_steps` to 1–64;
  extruder index to 0–7 — malformed trigger commands can no longer cause a hang
- Unknown parameter keys in the trigger string are now logged to the console rather than silently ignored
- New `P` parameter sets the PA sweep start value (default 0.0), e.g. `:P0.03:S0.001:N40;` sweeps 0.030–0.070

**Hardware UART on I2C connector pins**
- Replaced software bit-bang UART (TIM1/TIM3 on PA11/PA12) with hardware USART1
- USART1 TX/RX mapped to PA11[PA9] / PA12[PA10] (AF1) — the two pins on the I2C connector
- I2C slave interface removed (no longer needed for RRF-only builds)
- Enables direct 3.3V UART wiring from the connector to a Raspberry Pi or printer board
  without a USB-to-serial adapter or Pi Zero OTG bridge

**Build**
- Compiler: ARM Compiler 6.24 (ARMCLANG), Keil MDK Lite 5.43
- Code size: 26,784 bytes (target: STM32C011F6U6, 32KB flash)
- 0 errors, 0 warnings

---

## v1 (original)

- Klipper PA calibration support
- Z probe / nozzle endstop mode
- Raw ADC data output (`d;` / `D;`)
- Threshold tuning (`0;`–`99;`)
- Polarity inversion (`i;` / `I;`)
- Auto and manual baseline (`n;` / `N;`)
