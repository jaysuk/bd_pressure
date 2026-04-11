# bd_pressure — RepRapFirmware Integration

## Overview

bd_pressure supports RepRapFirmware (RRF) on standalone Duet boards (no SBC required).
The module connects via USB and uses two independent interfaces:

| Interface | Purpose |
|---|---|
| USB serial (via CH340) | PA calibration — bd_pressure drives RRF over GCode |
| Z probe endstop pin | Nozzle contact detection for Z homing and bed mesh |

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
M558 P8 C"zprobe.in" H5 F360 T9000   ; P8 = filtered digital, adjust pin name to your board
G31 P500 X0 Y0 Z-0.03                 ; adjust Z offset after first probe run
```

Add a `deployprobe.g` / `retractprobe.g` pair if you want the sensor re-baselined
before each probe tap:

**`sys/deployprobe.g`**
```gcode
M400                    ; wait for moves
G4 P200                 ; short dwell
M118 P0 S"N;"           ; re-baseline the probe normal_z on the sensor
G4 P200
```

**`sys/retractprobe.g`**
```gcode
; nothing required
```

---

### 2. PA calibration macro (`sys/pa_calibrate.g`)

Copy this macro to your Duet's `sys/` folder.
Adjust the parameters to match your printer (see [Parameter reference](#parameter-reference) below).

**Heating is handled entirely by the macro, before the sensor is triggered.**
This is intentional — once `M118` sends the trigger, the USB channel is handed to
bd_pressure and RRF can no longer send commands on it.  Heating must be complete
before that handover happens.

```gcode
; pa_calibrate.g — bd_pressure automatic Pressure Advance calibration
;
; Usage (standalone, cold start):
;   M98 P"pa_calibrate.g"
;
; Usage (from slicer start GCode, nozzle already heating):
;   M98 P"pa_calibrate.g"
;   ; place this after your M109/M116 in the slicer start sequence
;
; The macro heats the nozzle, homes the printer, positions the toolhead,
; then hands control to bd_pressure via M118.  The sensor drives all the
; extrusion moves and sends M572 Dx Sy back to RRF when done.
; The macro then waits for the sensor to finish before continuing.

; -----------------------------------------------------------------------
; Parameters — edit these to match your printer and filament
; -----------------------------------------------------------------------
var nozzle_temp  = 210      ; °C  nozzle print temperature for this filament
var high_speed   = 10800    ; mm/min  fast extrusion segment (match outer wall speed)
var low_speed    = 3000     ; mm/min  slow extrusion segments
var travel_speed = 24000    ; mm/min  travel between lines
var pa_step      = 0.002    ; PA increment per iteration
var steps        = 50       ; number of iterations  (range = pa_step × steps = 0–0.100)
var extruder     = 0        ; extruder index (D parameter for M572)

; -----------------------------------------------------------------------
; Step 1 — Heat the nozzle
;
; M116 waits for ALL heaters that have been set to reach temperature.
; If the nozzle is already at temperature from a previous M109/M116 in
; your start GCode this completes immediately.
; If starting cold, M104 begins heating and M116 blocks until ready.
; -----------------------------------------------------------------------
M104 T0 S{var.nozzle_temp}     ; start heating (non-blocking)
; Heat the bed here too if needed, e.g.:
; M140 S65
M116                            ; wait for ALL heaters — nozzle + bed if set

; -----------------------------------------------------------------------
; Step 2 — Home and position
;
; The sensor needs the toolhead positioned somewhere safe over the bed
; with enough Y travel for (steps × y_step) mm of lines.
; Default: lines run from Y=38.75 to Y=38.75 + 50×3.5 = Y=213.75
; Adjust the G1 move to suit your bed size.
; -----------------------------------------------------------------------
G28                             ; home all axes
G1 X118 Y20 Z0.3 F{var.travel_speed}   ; move to calibration start position
                                        ; Z=0.3 mm above bed — adjust to first layer height

; -----------------------------------------------------------------------
; Step 3 — Trigger bd_pressure
;
; M118 P0 sends the string on USB channel 0 (the bd_pressure connection).
; The sensor receives it, parses the parameters, and takes over the USB link.
; This line returns immediately in RRF — the sensor is now in control.
; -----------------------------------------------------------------------
M118 P0 S{"l:H" ^ var.high_speed ^ ":L" ^ var.low_speed ^ ":T" ^ var.travel_speed ^ ":S" ^ var.pa_step ^ ":N" ^ var.steps ^ ":E" ^ var.extruder ^ ";"}

; -----------------------------------------------------------------------
; Step 4 — Wait for calibration to finish
;
; bd_pressure will send M572 back to RRF when done, then send G28 X Y.
; We add a generous G4 dwell here so RRF doesn't immediately continue
; with the next line of the slicer start GCode before calibration ends.
;
; Approximate calibration time:
;   steps × (line_time + travel_time) ≈ 50 × 4s ≈ 3–8 minutes
; Adjust the dwell to be safely longer than your expected run time.
; -----------------------------------------------------------------------
G4 S480                         ; wait 8 minutes (480 seconds) — adjust if needed

; -----------------------------------------------------------------------
; Step 5 — Clean up
; -----------------------------------------------------------------------
M400                            ; ensure all moves from the sensor are complete
G28 X Y                         ; home X and Y (sensor also does this, belt-and-braces)
```

> **Why `G4` and not a smarter wait?**
> Once bd_pressure owns the USB channel, RRF cannot receive a "done" signal from
> the macro's perspective — the sensor sends `M572` and `G28` as GCode which RRF
> executes, but there's no way for the macro to detect that.  A fixed dwell is the
> simplest reliable solution.  Size it larger than your longest expected calibration
> run.  If you always use 50 steps at moderate speed, 8 minutes is conservative.

> **What `M118 P0` does:** Sends the trigger string on USB channel 0 — the same
> physical channel bd_pressure is connected to.  The sensor receives it as a plain
> serial command, exactly as if you had typed it into a terminal.

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

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| No `ok` responses, calibration times out | M555 P2 not taking effect | Make sure nothing else is connected to the Duet USB port |
| Calibration starts but all PA results are 0 | ADC not receiving data during moves | Check that `SAMPLES=30` in `pa.h` is satisfied at 90Hz — each move must take >330ms |
| Wrong PA value applied | Speed parameters don't match print speeds | Re-run with `H`/`L` matching your actual print speeds |
| Probe not triggering | Threshold too high | Send a lower threshold value, e.g. `M118 P0 S"2;"` — see [Z probe sensitivity](#z-probe-sensitivity) |
| Calibration won't start — "already running" | Previous run didn't complete cleanly | Power-cycle the sensor |
