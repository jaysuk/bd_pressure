# bd_pressure — RepRapFirmware Integration

## Overview

bd_pressure supports RepRapFirmware (RRF) on standalone Duet boards (no SBC required).
The module connects via UART and uses two independent interfaces:

| Interface | Purpose |
|---|---|
| UART (WAFER/I2C connector) | PA calibration and sensor commands |
| Z probe endstop pin | Nozzle contact detection for Z homing and bed mesh |

---

## Board compatibility

bd_pressure communicates over a hardware UART at 115200 baud by default.  Connect the sensor's
UART pins (PB6 TX / PB7 RX) to your board's auxiliary UART port.

On most Duet 3 boards (6HC, Mini 5+, etc.) this is the **io0** connector.  Set
`global.bd_port = 1` in your config (see below).

If your board does not have an accessible UART port, a **Pi Zero bridge** can forward
between the sensor's USB (CH340) and the board's UART.
See [docs/pi_zero_bridge.md](pi_zero_bridge.md) for full setup instructions.

---

## First-time setup

### Step 1 — Flash the firmware

Put the STM32 into bootloader mode (hold BOOT0 high, press reset) and flash
`firmware_src/release_hex/bd_pressure-rrf-v2.24.hex` using STM32CubeProgrammer:

```
STM32_Programmer_CLI.exe -c port=<COMx> -w bd_pressure-rrf-v2.24.hex -v --start
```

After flashing, the sensor boots into endstop/probe mode automatically.

### Step 2 — Wire up

```
bd_pressure PB6 (TX)  ──► Duet io0 RX
bd_pressure PB7 (RX)  ──► Duet io0 TX
bd_pressure Z out     ──► Duet Z-probe input (zstop)
bd_pressure GND       ──► Duet GND
```

> Logic levels: PB6/PB7 are 3.3 V.  Check your board's UART voltage before wiring.

### Step 3 — Add to `config.g`

See [duet_sd/sys/config_example.g](../duet_sd/sys/config_example.g) for ready-to-use snippets.
Key lines:

```gcode
; Initialise bd_pressure globals (sets global.bd_port, global.bd_uart, global.bd_baud)
M98 P"/sys/bd_globals.g"

; UART port — raw mode, sensor text appears directly in DWC console
M575 P{global.bd_port} S2 B{global.bd_baud}

; Z probe — Duet 3 (use P5 for Duet 2)
M558 P8 C"zstop" H5 F300:60 T12000
G31 P500 X0 Y0 Z-0.1   ; adjust Z offset after first probe run
```

### Step 4 — Copy macro files to the Duet SD card

See the [Macro file locations](#macro-file-locations) table below.
The files are pre-organised under `duet_sd/` in this repository to mirror the Duet SD
card layout — copy the folder contents directly onto your Duet SD card.

### Step 5 — Verify the sensor

Run `M98 P"/macros/bd_version.g"` from DWC. The sensor should respond with
`bd_pressure-rrf-v2.24` in the DWC Console tab.

Run `M98 P"/macros/bd_status.g"` to confirm mode, threshold, and polarity.

### Step 6 — Test probing

Run a `G30` from DWC. The toolhead should move down and stop when the nozzle
contacts the bed. If it doesn't trigger, lower the threshold with
`M98 P"/macros/bd_set_threshold.g"`.

### Step 7 — Run PA calibration

Edit `pa_calibrate.g` to match your printer speeds and nozzle temperature,
then run `M98 P"/sys/pa_calibrate.g"`.

---

## Macro file locations

The files in this repository under `duet_sd/` mirror the Duet SD card layout exactly.
Copy the `duet_sd/sys/` and `duet_sd/macros/` folders directly to your Duet SD card.

| Repository path | SD card location | Purpose |
|---|---|---|
| `duet_sd/sys/bd_globals.g` | `/sys/bd_globals.g` | Initialise `global.bd_port`, `global.bd_uart`, and `global.bd_baud` — included from `config.g` |
| `duet_sd/sys/pa_calibrate.g` | `/sys/pa_calibrate.g` | Automated PA calibration — main workflow |
| `duet_sd/sys/deployprobe.g` | `/sys/deployprobe.g` | Re-baseline sensor before each probe tap |
| `duet_sd/sys/retractprobe.g` | `/sys/retractprobe.g` | Empty stub (required by RRF) |
| `duet_sd/sys/config_example.g` | reference only — copy snippets into your `config.g` | Config snippets (M558, G31, M575) |
| `duet_sd/macros/bd_version.g` | `/macros/bd_version.g` | Query firmware version |
| `duet_sd/macros/bd_status.g` | `/macros/bd_status.g` | Query mode, threshold, polarity |
| `duet_sd/macros/bd_set_threshold.g` | `/macros/bd_set_threshold.g` | Set probe trigger threshold interactively |
| `duet_sd/macros/bd_endstop_mode.g` | `/macros/bd_endstop_mode.g` | Switch to endstop/probe mode |
| `duet_sd/macros/bd_pa_mode.g` | `/macros/bd_pa_mode.g` | Switch to PA sampling mode (diagnostics) |
| `duet_sd/macros/bd_reboot.g` | `/macros/bd_reboot.g` | Reboot the sensor (equivalent to power cycle) |
| `duet_sd/macros/bd_baud.g` | `/macros/bd_baud.g` | Change baud rate interactively (menu to select 115200/57600/38400/230400) |
| `duet_sd/macros/bd_logging.g` | `/macros/bd_logging.g` | Enable/disable trigger logging and raw ADC output (diagnostic) |

> `/sys/` files are called automatically by RRF (deploy/retract probe, config) or by the
> calibration macro.  `/macros/` files are run manually from DWC or via `M98`.

---

## Sensor command reference

Commands are sent to the sensor as ASCII strings terminated with `;`.
In RRF macros, use `M118 P{global.bd_uart} S"<command>;"` for text output to the console,
or `M260.2 P{global.bd_port} S"<command>;"` when the port is in device mode (S7).

If an unrecognised command is received, the sensor responds with `bd_pressure: unknown command '<x>'` in the console.

### Mode commands

| Command | Description |
|---|---|
| `e;` | Switch to **endstop/probe mode** (default on boot). The sensor monitors the strain gauge and drives the Z probe output pin. |
| `c;` | Switch to **PA sampling mode**. Arms the ADC for continuous PA scoring — resets all ADC buffers and pa.lib state on entry. Used by `pa_calibrate.g`. |

### Query commands

| Command | Response | Description |
|---|---|---|
| `v;` | console message | Firmware version string, formatted as an RRF console message. |
| `ver;` | single raw byte | Firmware version encoded as `major×100 + minor` (e.g. v2.24 → byte value `224`). Read with `M261.2 B1`. Used by `pa_calibrate.g` to log the sensor version. |
| `mode;` | single raw byte | Current operating mode: `0` = PA mode, `1` = endstop mode. Read with `M261.2 B1`. Used by `pa_calibrate.g` to confirm the sensor entered PA mode before logging. |
| `s;` | M291 popup | Full sensor status popup: mode, threshold, invert, version, baud, logging, ADC output state, and current baseline value. |
| `q;` | console message | Threshold value as a human-readable console message. |
| `Q;` | single raw byte | Threshold value as a single raw binary byte. Used by `bd_set_threshold.g` via `M261.2`. |
| `z;` | console message | Current baseline (`normal_z`) value. Useful for confirming the sensor has established a valid baseline after startup. |
| `score;` | single raw byte (0–255) | Latest pa.lib `res` score from the most recent extrusion segment. **Only valid after `c;` has been sent and at least one move has completed.** Lower value = better PA compensation. Returns `0x00` if no result is available yet. |
| `pdata;` | 5 raw bytes | All 5 pa.lib metrics as binary bytes in Klipper field order: `res, lk, rk, Hk, Ha`. Read with `M261.2 B5`. Returns all zeros if no result is available yet. Used by `pa_calibrate.g` — produces the same values as the Klipper `R:` output. |
| `rdata;` | ASCII string | Last result as a full `R:res,lk,rk,Hk,Ha\n` string — the exact format the developer's Klipper plugin reads. Returns `R:0,0,0,0,0\n` if no result is available yet. |

### Baud rate commands

The default baud rate is **115200**, which is suitable for most installations. You may want to change it in the following situations:

- **Reduce to 57600 or 38400** if you are seeing framing errors or garbled output in the DWC console. This can happen with longer cable runs (over ~1 m), poor quality cable, or a noisy enclosure with heavy stepper or heater wiring nearby. A lower baud rate gives each bit more time, making communication more tolerant of signal degradation.
- **Increase to 230400** if you have a very short, high-quality cable run and want the lowest possible latency on `score;` reads during PA calibration. The practical benefit is small — at 115200 a single byte takes ~87 µs, at 230400 it takes ~43 µs — but it may be useful if you are running a large number of calibration steps.

In most cases 115200 will work reliably without any changes.

| Command | Baud rate | Description |
|---|---|---|
| `b0;` | 115200 | Default — set on fresh firmware flash |
| `b1;` | 57600 | |
| `b2;` | 38400 | Lowest supported rate |
| `b3;` | 230400 | Highest supported rate |

The sensor saves the new rate to flash and **reboots automatically**. Run the baud macro from the DWC macros panel:

```gcode
M98 P"/macros/bd_baud.g"
```

A menu will appear to select the desired rate. The macro switches both the sensor and the Duet port, updates `global.bd_baud` for the current session, and shows a popup reminding you to update `bd_globals.g` so the new rate persists across Duet reboots.

### Threshold and baseline commands

| Command | Description |
|---|---|
| `<n>;` | Set trigger threshold to `n` (1–99). Two-digit values are supported (e.g. `12;`). Default is `4`. Saved to flash automatically. |
| `n;` | Auto-find baseline on startup (default). The sensor determines `normal_z` automatically from the first ADC samples. |
| `N;` | Lock the current reading as the baseline (`normal_z`). Use in `deployprobe.g` to re-baseline before each probe tap. |
| `i;` | Normal polarity (default). Probe triggers when pressure increases above baseline. |
| `I;` | Inverted polarity. Probe triggers when pressure decreases below baseline. |

### Data output commands

| Command | Description |
|---|---|
| `d;` | Enable raw ADC data output. The sensor continuously sends raw ADC values over UART. Useful for diagnosing probe sensitivity. **Disable before probing or PA calibration.** |
| `D;` | Disable raw ADC data output (default). |
| `L;` | Enable trigger/open event logging to the DWC console. Useful for diagnosing probe behaviour. Off by default. |
| `l;` | Disable trigger/open event logging (default). |
| `r;` | Reboot the sensor. Equivalent to a power cycle. |

---

## PA calibration

### How it works

`pa_calibrate.g` calibrates PA entirely **in the air** — no printing on the bed is required.
The nozzle is raised to a safe Z height and moves along the X axis while extruding a
slow→fast→slow sequence for each PA value.  The XY velocity creates the pressure transient;
the bd_pressure strain gauge measures the back-pressure in the hotend during each move and
pa.lib scores the result.

This matches the Klipper reference implementation exactly. The 5 pa.lib metrics returned
(`res, lk, rk, Hk, Ha`) are in the same field order as the Klipper `R:` output, so logs
can be shared directly with the developer for comparison.

1. RRF reads the bd_pressure firmware version (`ver;`) for the log header
2. RRF homes all axes if not already homed
3. RRF heats the nozzle to the configured temperature
4. RRF raises to the configured Z height and primes the nozzle across the full line length
5. RRF switches the sensor to device mode (`M575 S7`) and sends `c;` to arm PA sampling
6. RRF queries `mode;` to confirm the sensor is in PA mode and writes it to the log header
7. For each PA value in the sweep:
   - RRF sets the PA value: `M572 D<e> S<pa>`
   - RRF moves X slow→fast→slow along a 60 mm line at fixed Y while extruding
   - RRF waits for moves to complete: `M400`, then dwells 200 ms
   - RRF sends `pdata;` and reads back 5 bytes with `M261.2 B5`: `res, lk, rk, Hk, Ha`
   - All 5 values are appended to `/sys/pa_calibrate_log.txt`
8. RRF finds the lowest `res` value, skipping the first 5 samples as warm-up
9. RRF applies the result with `M572`, saves it to `/sys/pa_result.g`, and shows a DWC popup
10. RRF sends `e;` to return the sensor to endstop/probe mode and restores raw serial mode (`M575 S2`)

### Log file format

Each run writes `/sys/pa_calibrate_log.txt` with a `#` comment header followed by CSV data:

```
# bd_pressure PA calibration
# date=2026-05-11T12:34:56
# rrf_version=3.6.2+1
# bd_version=bd_pressure-rrf-v2.24
# mode=pa
# extruder=0 nozzle_temp=210 pa_start=0.0 pa_step=0.002 steps=50
iter,pa,res,lk,rk,Hk,Ha
0,0.0000,18,6,11,173,206
1,0.0020,21,9,10,217,244
...
```

The `mode=` field is read live from the sensor after arming — if it does not read `pa` the
sensor did not enter PA mode correctly and the calibration should be aborted.

The log can be analysed with:
- **DWC plugin** — install `BdPressurePA-x.x.x.zip` via Settings → Plugins. A PA Calibration
  tab appears under Plugins, showing all 5 metrics as charts with the best PA highlighted.
  Use **Load from Duet** to fetch the log directly from the Duet SD card, or drag-drop a
  local copy. The metadata header is displayed as coloured chips (date, RRF version,
  bd_pressure version, mode, nozzle temp, PA sweep parameters).
- **Python plotter** — run `python tools/pa_log_plot.py /path/to/pa_calibrate_log.txt`.
  Requires `matplotlib` (`pip install matplotlib`).

### Calibration macro parameters

Edit these variables at the top of `pa_calibrate.g`:

```gcode
var tool         = 0       ; tool number (T0, T1, etc.)
var extruder     = 0       ; extruder index for M572
var nozzle_temp  = 210     ; °C nozzle print temperature
var pa_start     = 0.0     ; starting PA value
var pa_step      = 0.002   ; PA increment per iteration
var steps        = 50      ; number of iterations

; Speeds — match your typical print speeds
var low_speed    = 1020    ; mm/min slow segments (outer wall)
var high_speed   = 10740   ; mm/min fast segment (infill / fast perimeter)
var travel_speed = 18000   ; mm/min travel between lines

; Z height — raised clear of bed, nozzle not touching surface
var z_height     = 50      ; mm
```

### PA step / range recommendations

| Application | `pa_step` | `steps` | Range |
|---|---|---|---|
| First calibration, unknown baseline | `0.004` | `50` | 0 – 0.200 |
| Fine-tuning around known value | `0.001` | `40` | 0 – 0.040 |
| Direct drive, short path | `0.002` | `30` | 0 – 0.060 |
| Bowden, long path | `0.005` | `40` | 0 – 0.200 |

### How the result is reported

When calibration completes, the result is delivered three ways:

**1. DWC console message**
```
bd_pressure: calibration complete. Best PA = 0.0420 (res=12, step 21)
```

**2. DWC popup dialog**
A blocking `M291` popup that must be dismissed by pressing **OK**:
```
Calibration complete!
Best Pressure Advance: 0.0420

Add to config.g:
M572 D0 S0.0420

Full log: /sys/pa_calibrate_log.txt
```
The extruder index `D` matches the `var.extruder` value set at the top of `pa_calibrate.g`.

**3. SD card file `/sys/pa_result.g`**
A ready-to-use file written after every successful run containing the literal values:
```gcode
M572 D0 S0.0420
```
Each run overwrites the previous result.  Add `M98 P"/sys/pa_result.g"` to `config.g`
to load it automatically on every boot.

---

## DWC plugin

The **BdPressurePA** plugin visualises PA calibration logs directly inside DWC — no external tools needed.

### Installation

1. Download `BdPressurePA-1.0.0.zip` from the [plugin repository](https://github.com/jaysuk/bd_pressure_dwc_plugin)
2. In DWC go to **Settings → Plugins → External plugins → Upload plugin**
3. Select the zip file and click **Install**
4. A **PA Calibration** tab will appear under the **Plugins** section in the left sidebar

### Loading a log

The plugin can load calibration data three ways:
- **Load from Duet** — fetches `/sys/pa_calibrate_log.txt` directly from the Duet SD card (requires a completed calibration run)
- **Drag and drop** — drag a log file onto the upload zone
- **Browse** — click the zone to open a file picker

### What the plugin shows

| Panel | Content |
|---|---|
| Metadata chips | Date, RRF version, bd_pressure version, mode (teal=pa / purple=endstop), nozzle temperature, extruder index, PA sweep parameters |
| Best PA | Highlighted in green with a one-click **Copy M572** button — uses the correct extruder index from the log |
| Pressure score (res) | Blue line chart with red dashed best-PA line and green shaded good zone (within 20% of best) |
| Slopes (lk / rk) | Left and right pressure slope per iteration |
| Signal quality (Hk / Ha) | Peak heights per iteration — useful for checking sensor signal strength |
| Analysis panel | Automatic assessment of sweep quality: noise level (CV%), minimum position relative to sweep edges, curve flatness, lk/rk asymmetry |
| Suggested next sweep | Ready-to-paste `pa_calibrate.g` variable block — either a zoom-in around the best value or a range shift if the minimum is near an edge |
| Raw data table | All 50 (or N) rows — best iteration highlighted in green |

All analysis runs entirely in the browser on the device loading DWC — nothing is sent to the Duet board.

### Interpreting the results

**res (lower = better)** — the primary score. Look for a clear V-shaped minimum. A flat or noisy curve means either:
- The PA range is too wide — use the suggested zoom-in parameters and run again
- There is variability in the extrusion (temperature, vibration) — try a second run to confirm

**lk / rk (slopes)** — should be small and close together at the optimum PA value. A persistent asymmetry (one consistently higher than the other) can indicate a temperature or flow asymmetry in the hotend.

**Hk / Ha (peak heights)** — should be consistent and high (200+). Low or erratic values suggest the sensor is not receiving a strong pressure signal — check wiring, threshold setting, and extrusion amounts.

---

## Z probe

### Configuration

```gcode
; Z probe — bd_pressure strain gauge endstop
; Duet 3: P8 (filtered digital).  Duet 2: use P5 instead.
M558 P8 C"zstop" H5 F300:60 T12000   ; adjust pin name to match your board wiring
G31 P500 X0 Y0 Z-0.1                 ; adjust Z offset after first probe run
```

### Trigger threshold

The threshold controls how much force is required to trigger the probe. Default is `4`.

```gcode
M118 P{global.bd_uart} S"4;"    ; default — suitable for most setups
M118 P{global.bd_uart} S"7;"    ; less sensitive (requires more force)
M118 P{global.bd_uart} S"2;"    ; more sensitive (triggers with less force)
```

Valid range: `1`–`99`. If the probe false-triggers during travel moves, increase the
threshold. If it fails to trigger on contact, decrease it.

The threshold is **saved to flash automatically** and persists across power cycles.

Use `M98 P"/macros/bd_set_threshold.g"` for an interactive macro that shows the current
value and prompts for a new one.

### Re-baselining before each tap (`deployprobe.g`)

The sensor compares readings against a stored baseline (`normal_z`).  Sending `N;`
before each tap re-locks the baseline to the current reading, compensating for any
thermal drift or toolhead-position effects.

**`sys/deployprobe.g`**
```gcode
M400                                         ; wait for all moves to complete
G4 P500                                      ; dwell for vibration to settle
M118 P{global.bd_uart} S"N;"                 ; re-baseline sensor
G4 P300                                      ; dwell for ADC to capture baseline
```

**`sys/retractprobe.g`**
```gcode
; nothing required — bd_pressure needs no physical retract
```

---

## Building the firmware

The project uses **Keil MDK** (ARM Compiler 6) targeting the STM32C011F6U6.
The project file is at [firmware_src/MDK-ARM/STM32C011F6U6.uvprojx](../firmware_src/MDK-ARM/STM32C011F6U6.uvprojx).

**Requirements:**
- Keil MDK v5.38 or later (MDK-Lite is sufficient — code fits within the 32 KB free limit)
- Keil STM32C0xx DFP pack v2.4.0 (`Keil.STM32C0xx_DFP.2.4.0`)
- ARM Compiler 6.22 (ARMCLANG) — included with MDK
- **MicroLib must be enabled** — go to *Project → Options for Target → Target* and check
  **Use MicroLIB**. Without it the standard C library overhead pushes the binary over the
  32 KB MDK-Lite limit.

> **Note:** The scatter file (`STM32C011F6U6.sct`) limits the code region to 30 KB
> (0x7800) to protect the last 2 KB flash page reserved for user config storage.
> Keil may revert this file on rebuild — verify it stays at `0x00007800` after each build.

**After build:** the post-build script `firmware_src/MDK-ARM/post_build.bat` automatically
copies the compiled hex to `firmware_src/release_hex/` with the version number in the
filename (e.g. `bd_pressure-rrf-v2.20.hex`).

**Flashing:**
See [firmware_src/release_hex/README.md](../firmware_src/release_hex/README.md) for
the STM32CubeProgrammer flashing procedure (boot button + UART).

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Macros run but nothing appears in DWC console | `M575 P1 S2 B{global.bd_baud}` missing from `config.g`, or wrong port number in `global.bd_port` | Add the M575 line and confirm `global.bd_port` matches the physical wiring |
| Console shows raw hex bytes instead of text | Port in device mode (`S7`) was not restored to raw mode (`S2`) after a failed calibration | Send `M575 P{global.bd_port} S2 B{global.bd_baud}` from DWC console, then run `bd_reboot.g` |
| Calibration starts but all values are 0 | `c;` was not sent before the move loop, or moves are too fast for ADC to capture | Confirm `c;` is acknowledged in the console, and ensure enough filament is extruded per cycle |
| `pdata;` always returns all zeros | pa.lib has not produced a result yet — `pa_list` has not incremented | Increase extrusion amounts so the ADC captures a full sample window |
| Log shows `mode=endstop` instead of `mode=pa` | `c;` arm command did not take effect | Check UART is in device mode (S7) before sending `c;` — the macro handles this automatically |
| Wrong PA value applied | Speed parameters don't match actual print speeds | Re-run with `high_speed`/`low_speed` matching your outer wall and travel speeds |
| Probe not triggering | Threshold too high | Run `bd_set_threshold.g` and lower the value |
| Probe false-triggers during travel | Threshold too low | Run `bd_set_threshold.g` and raise the value |
| Sensor unresponsive after calibration | UART left in device mode after a crash | Send `M575 P{global.bd_port} S2 B{global.bd_baud}` to restore raw mode |
| First command after baud change gets no response | Sensor UART still initialising at new rate when first command is sent | Wait 2–3 seconds after `bd_baud.g` completes before sending commands — the sensor needs time to fully boot at the new rate |
| Baud change reverts after Duet reboot | `bd_globals.g` not updated to match new rate | Edit `global bd_baud` in `bd_globals.g` to the new value — `bd_baud.g` shows the exact line in its confirmation popup |
