# bd_pressure — RepRapFirmware Integration

## Overview

bd_pressure supports RepRapFirmware (RRF) on standalone Duet boards (no SBC required).
The module connects via USB and uses two independent interfaces:

| Interface | Purpose |
|---|---|
| USB serial (via CH340) | PA calibration — bd_pressure drives RRF over GCode |
| Z probe endstop pin | Nozzle contact detection for Z homing and bed mesh |

---

## First-time setup

### Step 1 — Flash the firmware

Put the STM32 into bootloader mode (hold BOOT0 high, press reset) and flash
`firmware_src/MDK-ARM/STM32C011F6U6/STM32C011F6U6.hex` using STM32CubeProgrammer:

```
STM32_Programmer_CLI.exe -c port=<COMx> -w STM32C011F6U6.hex -v --start
```

After flashing, the sensor boots into endstop/probe mode automatically.

### Step 2 — Wire up

```
bd_pressure USB  ──► Duet USB port
bd_pressure Z    ──► Duet Z-probe input (zprobe.in)
```

### Step 3 — Add to `config.g`

See [duet_sd/sys/config_example.g](../duet_sd/sys/config_example.g) for ready-to-use snippets.
Key lines:

```gcode
; USB pass-through (required for PA calibration macros)
M575 P0 S1 B38400

; Z probe — Duet 3 (use P5 for Duet 2)
M558 P8 C"zstop" H5 F300:60 T12000
G31 P500 X0 Y0 Z-0.1   ; adjust Z offset after first probe run
```

### Step 4 — Copy macro files to the Duet SD card

See the [Macro file locations](#macro-file-locations) table below.
The files are pre-organised under `duet_sd/` in this repository to mirror the Duet SD card layout — copy the folder contents directly onto your Duet SD card.

### Step 5 — Verify the sensor

Run `M98 P"/macros/bd_version.g"` from DWC. The sensor should respond with
`bd_pressure-rrf-v2` on the USB serial console.

Run `M98 P"/macros/bd_status.g"` to confirm mode, threshold, and polarity.

### Step 6 — Test probing

Run a `G30` from DWC. The toolhead should move down and stop when the nozzle
contacts the bed. If it doesn't trigger, lower the threshold with
`M98 P"/macros/bd_set_threshold.g" T3`.

### Step 7 — Run PA calibration

Edit `pa_calibrate.g` to match your printer speeds and nozzle temperature,
then run `M98 P"/sys/pa_calibrate.g"`.

---

## Macro file locations

The files in this repository under `duet_sd/` mirror the Duet SD card layout exactly.
Copy the `duet_sd/sys/` and `duet_sd/macros/` folders directly to your Duet SD card.

| Repository path | SD card location | Purpose |
|---|---|---|
| `duet_sd/sys/pa_calibrate.g` | `/sys/pa_calibrate.g` | Automated PA calibration — main workflow |
| `duet_sd/sys/deployprobe.g` | `/sys/deployprobe.g` | Re-baseline sensor before each probe tap |
| `duet_sd/sys/retractprobe.g` | `/sys/retractprobe.g` | Empty stub (required by RRF) |
| `duet_sd/sys/config_example.g` | reference only — copy snippets into your `config.g` | Config snippets (M558, G31, M575) |
| `duet_sd/macros/bd_version.g` | `/macros/bd_version.g` | Query firmware version |
| `duet_sd/macros/bd_status.g` | `/macros/bd_status.g` | Query mode, threshold, polarity |
| `duet_sd/macros/bd_abort.g` | `/macros/bd_abort.g` | Abort PA calibration mid-run |
| `duet_sd/macros/bd_set_threshold.g` | `/macros/bd_set_threshold.g` | Set probe trigger threshold |
| `duet_sd/macros/bd_endstop_mode.g` | `/macros/bd_endstop_mode.g` | Switch to endstop/probe mode |
| `duet_sd/macros/bd_pa_mode.g` | `/macros/bd_pa_mode.g` | Switch to PA mode (diagnostics) |
| `duet_sd/macros/bd_reboot.g` | `/macros/bd_reboot.g` | Reboot the sensor (equivalent to power cycle) |

> `/sys/` files are called automatically by RRF (deploy/retract probe) or by the
> calibration macro.  `/macros/` files are run manually from DWC or via `M98`.

---

## How bd_pressure knows it's talking to RRF vs Klipper

The firmware detects the host purely from the **trigger command format**.
No configuration change or firmware flag is needed.

| Command received | Host assumed | Action |
|---|---|---|
| `l;` | Klipper | Switch ADC to PA mode, emit raw data stream — Klipper plugin drives the moves |
| `l:H...:L...:...;` | RRF | Parse parameters, launch the RRF state machine — bd_pressure drives RRF |

When connected to a Duet the RRF macro sends `M118 P0 S"l:H10800:..."` which contains
a colon immediately after `l`.  The existing bare `l;` path is untouched so the same
firmware works with both hosts without any modification.

---

## Wiring

```
bd_pressure USB  ──► Duet USB port (same port used for DWC / host PC)
bd_pressure Z    ──► Duet Z-probe input (e.g. Z_PROBE on the mainboard connector)
```

> **Note:** The Duet's USB port can only be connected to one host at a time.
> Disconnect any PC/DWC connection before running PA calibration.

---

## Duet configuration

### 1. Z probe (`config.g`)

The endstop output pin works like a standard switch-type probe.
No plugin is required for probing.

```gcode
; Z probe — bd_pressure strain gauge endstop
; Duet 3: P8 (filtered digital).  Duet 2: use P5 instead.
M558 P8 C"zstop" H5 F300:60 T12000   ; adjust pin name to match your board wiring
G31 P500 X0 Y0 Z-0.1                 ; adjust Z offset after first probe run
```

Add a `deployprobe.g` / `retractprobe.g` pair if you want the sensor re-baselined
before each probe tap.  Ready-to-use files are provided at
[duet_sd/sys/deployprobe.g](../duet_sd/sys/deployprobe.g) and
[duet_sd/sys/retractprobe.g](../duet_sd/sys/retractprobe.g) — copy both to `/sys/`.

**`sys/deployprobe.g`**
```gcode
M400                    ; wait for all moves to complete
G4 P500                 ; dwell to let toolhead vibration settle
M118 P0 S"N;"           ; re-baseline the probe (lock current reading as normal_z)
G4 P300                 ; dwell to let the ADC capture the new baseline
```

**`sys/retractprobe.g`**
```gcode
; nothing required — bd_pressure needs no physical retract
```

---

### 2. PA calibration macro (`sys/pa_calibrate.g`)

The macro is provided ready to use at [duet_sd/sys/pa_calibrate.g](../duet_sd/sys/pa_calibrate.g).
Copy it to `/sys/` on your Duet SD card and edit the parameters at the top.

Key features:
- **Version check** — queries the sensor firmware version at startup and warns if it may not be RRF-capable
- **Multi-tool support** — set `var.tool` and `var.extruder` for any tool/extruder combination
- **Bed heating** — set `var.bed_temp > 0` to heat the bed before calibrating
- **Optional homing** — set `var.home_first = false` if the printer is already homed
- **Abort support** — send `M118 P0 S"a;"` from the DWC console to stop a run mid-calibration

```gcode
; Key parameters — edit to match your printer
var tool          = 0        ; tool number (T0, T1, etc.)
var extruder      = 0        ; extruder index for M572
var nozzle_temp   = 210      ; °C
var bed_temp      = 0        ; °C — set to 0 to skip bed heating
var high_speed    = 10800    ; mm/min fast segment
var low_speed     = 3000     ; mm/min slow segments
var travel_speed  = 24000    ; mm/min travel
var pa_step       = 0.002    ; PA increment per step
var steps         = 50       ; number of steps
var home_first    = true     ; G28 before calibration
```

> **Why `G4` and not a smarter wait?**
> Once bd_pressure owns the USB channel, RRF cannot receive a "done" signal from
> the macro's perspective — the sensor sends `M572` and `G28` as GCode which RRF
> executes, but there's no way for the macro to detect that.  A fixed dwell is the
> simplest reliable solution.  The default is 10 minutes — reduce `G4 S600` if your
> runs are consistently faster.

> **To abort mid-run:** send `M118 P0 S"a;"` from the DWC console.  The sensor will
> stop, issue `M112`, and return to endstop/probe mode.

---

## Parameter reference

All parameters are embedded in the trigger command string as `KEY<value>` pairs
separated by colons.  Every parameter is **optional** — omit any you don't need
and the default value is used.

### Full format

```
l:H<high>:L<low>:T<travel>:S<pa_step>:N<steps>:E<extruder>;
```

### Parameters

| Key | Full name | Unit | Default | Description |
|---|---|---|---|---|
| `H` | High speed | mm/min | `10800` | Speed of the fast extrusion segment in each calibration line. This is the segment where PA effect is most visible. Set this to match your typical outer wall speed. |
| `L` | Low speed | mm/min | `3000` | Speed of the slow extrusion segments at each end of the calibration line. These are the reference segments where pressure should be neutral. |
| `T` | Travel speed | mm/min | `24000` | Speed used to move between calibration lines. Should be your normal travel speed. |
| `S` | PA step | — | `0.002` | How much PA increases with each iteration. With the default 50 steps this gives a test range of 0 – 0.100. Increase to `0.005` for a wider range (0 – 0.250), decrease to `0.001` for finer resolution. |
| `N` | Steps | count | `50` | Number of PA values to test. Total PA range tested = `S × N`. |
| `E` | Extruder index | — | `0` | Which extruder the result is applied to. Becomes the `D` parameter in the `M572` command the sensor sends back. Use `1` for a second extruder, etc. |

> **Nozzle temperature is not a parameter.** Heat the nozzle with `M104`/`M116` in
> the macro **before** `M118`.  Once the trigger is sent, the USB channel belongs to
> bd_pressure and RRF cannot send heater commands until calibration completes.

### Speed recommendations

Speeds should reflect the speeds you actually print at, since PA is speed-dependent.

| Print style | H (high) | L (low) | T (travel) |
|---|---|---|---|
| Slow / draft | 6000 | 1800 | 18000 |
| Standard | 10800 | 3000 | 24000 |
| Fast / input shaping | 18000 | 4800 | 36000 |

To derive speeds from volumetric flow (as the Klipper macro does):
- `L (mm/min) ≈ volumetric_max × 51`
- `H (mm/min) ≈ volumetric_max × 537`

For example, with `MAX_VOL = 20 mm³/s`: L = 1020, H = 10740.

### PA step / range recommendations

| Application | S (step) | N (steps) | Range |
|---|---|---|---|
| First calibration, unknown baseline | `0.004` | `50` | 0 – 0.200 |
| Fine-tuning around known value | `0.001` | `40` | 0 – 0.040 |
| Direct drive, short path | `0.002` | `30` | 0 – 0.060 |
| Bowden, long path | `0.005` | `40` | 0 – 0.200 |

---

## Calibration sequence (what happens)

1. **RRF macro** heats the nozzle (`M104` + `M116`), homes, and positions the toolhead
2. **RRF macro** sends the `l:...:...;` trigger string via `M118 P0`
3. **bd_pressure** receives it, parses the parameters, enters PA calibration mode
4. **bd_pressure** sends `M555 P2` to RRF — enables Marlin-emulation mode so RRF
   replies `ok` to every GCode line (required for the sensor's command handshake)
5. **bd_pressure** sends `M110 N0` to reset the line counter, then begins numbered GCode
6. **bd_pressure** sends the setup commands: `G90`, `M83`, initial `M572 D0 S0`, `G1 E4` prime
7. **bd_pressure** executes 50 calibration lines (or fewer if early-exit triggers):
   - Sets PA: `M572 D0 S<value>`
   - Travels to line start: `G1 X Y F<travel>`
   - Slow segment: `G1 X F<low> E...`
   - Fast segment: `G1 X F<high> E...`
   - Slow segment: `G1 X F<low> E...`
   - Drains queue: `M400` — reads strain gauge result after each drain
8. **bd_pressure** finds the PA value with the lowest residual pressure error
9. **bd_pressure** sends `M572 D<E> S<best_pa>` — RRF applies the result immediately
10. **bd_pressure** sends `G28 X Y` to home the axes
11. **bd_pressure** announces the result three ways (see below), then returns to endstop/probe mode

The entire calibration takes approximately 3–8 minutes depending on speed and step count.

---

## How the result is reported

When calibration completes, bd_pressure delivers the result three ways so it is
impossible to miss and easy to act on:

### 1. DWC console message
`M118 P2` sends a line to the DWC message log and PanelDue screen:
```
bd_pressure: PA calibration done. Best value: M572 D0 S0.0420  Add this to your config.g
```

### 2. DWC popup dialog
`M291` opens a blocking popup in DWC that must be dismissed by pressing **OK**.
It shows the result in a format ready to copy:

```
┌─────────────────────────────────┐
│  bd_pressure PA Result          │
│                                 │
│  Calibration complete!          │
│  Best Pressure Advance: 0.0420  │
│                                 │
│  Add to config.g:               │
│  M572 D0 S0.0420                │
│                                 │
│              [ OK ]             │
└─────────────────────────────────┘
```

The macro's `G4 S480` dwell keeps RRF busy, so the popup will appear and persist
until the user dismisses it.

### 3. SD card file `/sys/pa_result.g`

bd_pressure writes a file to the SD card using `M28`/`M29`.  After calibration,
`/sys/pa_result.g` contains a single ready-to-use line:

```gcode
M572 D0 S0.0420 ; bd_pressure PA calibration result
```

Each calibration run **overwrites** the previous result.  To use it, either:
- Copy the `M572` line directly into `config.g`
- Or add `M98 P"/sys/pa_result.g"` to `config.g` to load it automatically on boot

---

## Building the firmware

The project uses **Keil MDK** (ARM Compiler 6) targeting the STM32C011F6U6.
The project file is at [firmware_src/MDK-ARM/STM32C011F6U6.uvprojx](../firmware_src/MDK-ARM/STM32C011F6U6.uvprojx).

**Requirements:**
- Keil MDK v5.38 or later
- Keil STM32C0xx DFP pack v2.4.0 (`Keil.STM32C0xx_DFP.2.4.0`)
- ARM Compiler 6.22 (ARMCLANG) — included with MDK

**New source files to add to the project** (not yet in the `.uvprojx`):

| File | Group |
|---|---|
| `firmware_src/Core/Src/rrf_comm.c` | Application/User/Core |
| `firmware_src/Core/Src/pa_rrf.c` | Application/User/Core |

In Keil uVision: right-click the `Application/User/Core` group → `Add Existing Files`
and add both `.c` files.  The headers are already in `Core/Inc/` which is on the
include path.

**Flashing:**
See [firmware_src/release_hex/README.md](../firmware_src/release_hex/README.md) for
the STM32CubeProgrammer flashing procedure (boot button + UART).

---

## Z probe sensitivity

The probe trigger threshold and baseline can be tuned by sending commands to the sensor via `M118 P0`.

### Trigger threshold

The threshold controls how much force is required to trigger the probe. The default is `4`.

```gcode
M118 P0 S"4;"    ; default — suitable for most setups
M118 P0 S"7;"    ; less sensitive (requires more force)
M118 P0 S"2;"    ; more sensitive (triggers with less force)
```

Valid range is `0`–`99`. If the probe false-triggers during travel moves, increase the threshold. If it fails to trigger on contact, decrease it.

**The threshold is automatically saved to flash** — it persists across power cycles so you only need to set it once.

### Mode confirmation responses

When switching modes the sensor echoes a confirmation back to the host:

| Command | Response |
|---|---|
| `e;` | `endstop mode` |
| `l;` | `PA mode` |
| `v;` | `bd_pressure-rrf-v2` |
| `s;` | `mode:<endstop\|pa>;thr:<n>;inv:<0\|1>;ver:v2` |

### Status query

`s;` returns a single diagnostic line with all current sensor state:

```gcode
M118 P0 S"s;"
; Response: mode:endstop;thr:4;inv:0;ver:v2
```

Use `M98 P"/macros/bd_status.g"` for a friendly DWC wrapper.

### Reboot

```gcode
M118 P0 S"r;"    ; reboot the sensor (equivalent to power cycle)
```

Useful for recovering the sensor without physical access.

### Baseline (normal_z)

The sensor compares the live reading against a stored baseline. By default it finds this automatically on startup.

```gcode
M118 P0 S"n;"    ; auto-find baseline on startup (default)
M118 P0 S"N;"    ; lock the current reading as the baseline
```

`N;` is useful in `deployprobe.g` to re-baseline the sensor immediately before each probe tap, which compensates for any drift due to temperature or toolhead position.

### Polarity inversion

```gcode
M118 P0 S"i;"    ; normal polarity (default)
M118 P0 S"I;"    ; inverted polarity
```

Use `I;` if the probe triggers in the wrong direction (e.g. triggers when the nozzle lifts instead of when it touches).

### Raw data output

The sensor can stream raw ADC readings over USB, which is useful for diagnosing probe sensitivity without needing a separate serial terminal.

```gcode
M118 P0 S"d;"    ; enable raw data output
M118 P0 S"D;"    ; disable raw data output (default)
```

When enabled, the sensor continuously sends `W:<value>;D:<value>;Y:<value>;R:<value>` lines. Disable it before running PA calibration or probing.

### Aborting PA calibration

If a calibration run needs to be stopped mid-run (e.g. a crash, or wrong parameters):

```gcode
M118 P0 S"a;"    ; abort PA calibration and return to endstop/probe mode
```

A ready-to-use macro is provided at [duet_sd/macros/bd_abort.g](../duet_sd/macros/bd_abort.g) — copy it to `/macros/` and assign it to a DWC button for one-click abort.

The sensor will stop sending GCode to RRF, switch back to endstop mode, and issue `M112` to stop any in-progress move. The sensor also auto-aborts if no `ok` response is received from RRF for 30 seconds (USB disconnect watchdog).

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| No `ok` responses, calibration times out | M555 P2 not taking effect | Make sure nothing else is connected to the Duet USB port |
| Calibration starts but all PA results are 0 | ADC not receiving data during moves | Check that `SAMPLES=30` in `pa.h` is satisfied at 90Hz — each move must take >330ms |
| Wrong PA value applied | Speed parameters don't match print speeds | Re-run with `H`/`L` matching your actual print speeds |
| Probe not triggering | Threshold too high | Send a lower threshold value, e.g. `M118 P0 S"2;"` — see [Z probe sensitivity](#z-probe-sensitivity) |
| Calibration won't start — "already running" | Previous run didn't complete cleanly | Power-cycle the sensor |
