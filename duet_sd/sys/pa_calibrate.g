; pa_calibrate.g — bd_pressure automatic Pressure Advance calibration for RRF
;
; Place this file in /sys/ on the Duet SD card.
; Run via: M98 P"/sys/pa_calibrate.g"
;
; Requirements:
;   - bd_pressure sensor connected to Duet USB (P0)
;   - Firmware version: bd_pressure-rrf-v2 or later
;   - Nozzle and bed homed before running (or set home_first = true below)

; -----------------------------------------------------------------------
; Parameters — edit these to match your printer and filament
; -----------------------------------------------------------------------
; global.bd_port must be set in config.g (0 = USB, 1 = io0 / Pi Zero bridge)
var tool          = 0        ; tool number (T0, T1, etc.)
var extruder      = 0        ; extruder index for M572 (usually same as tool)
var nozzle_temp   = 210      ; °C  nozzle print temperature
var bed_temp      = 0        ; °C  bed temperature (set to 0 to skip bed heating)
var high_speed    = 10800    ; mm/min  fast extrusion segment (match outer wall speed)
var low_speed     = 3000     ; mm/min  slow extrusion segments
var travel_speed  = 24000    ; mm/min  travel between lines
var pa_start      = 0.0      ; starting PA value (0.0 = sweep from zero)
var pa_step       = 0.002    ; PA increment per iteration
var steps         = 50       ; number of iterations (range = pa_start + pa_step × steps)
var home_first    = true     ; true = G28 before calibration, false = assume already homed

; *** MUST SET BEFORE FIRST USE ***
; Safe XY position near the centre of your bed, clear of any clips or brackets.
; Z should be just above the bed (0.3 mm is typical — ensure the nozzle will not drag).
; The sensor drives all moves from this starting position onwards.
var start_x       = 118.0    ; *** ADJUST to your bed centre X ***
var start_y       = 20.0     ; *** ADJUST to a clear Y position on your bed ***
var start_z       = 0.3      ; mm above bed — increase if you have a textured plate

; -----------------------------------------------------------------------
; Step 0 — Version check
;
; Query the sensor firmware version and warn if it does not respond with
; the expected RRF-capable version string.  This catches the case where
; old firmware is still installed.
;
; The sensor responds on USB serial (not back to RRF), so we cannot
; programmatically check the response from within a macro.  Instead we
; send the query, pause briefly, then warn the user to check the console.
; If the sensor is not RRF-capable the calibration trigger (l:...) will
; be ignored and the macro will time out on the G4 dwell.
; -----------------------------------------------------------------------
M118 P2 S"bd_pressure: checking firmware version..."
M118 P{global.bd_port} S"v;"
G4 P500
M118 P2 S"bd_pressure: if version is not 'bd_pressure-rrf-v2', abort now (M108) and flash the correct firmware."

; -----------------------------------------------------------------------
; Step 1 — Select tool
; -----------------------------------------------------------------------
T{var.tool}

; -----------------------------------------------------------------------
; Step 2 — Heat
; -----------------------------------------------------------------------
M118 P2 S"bd_pressure: heating to temperature..."

if var.bed_temp > 0
    M140 S{var.bed_temp}        ; start bed heating (non-blocking)

M104 T{var.tool} S{var.nozzle_temp}   ; start nozzle heating (non-blocking)
M116                                   ; wait for ALL heaters to reach temperature

; -----------------------------------------------------------------------
; Step 3 — Home and position
; -----------------------------------------------------------------------
if var.home_first
    G28

G1 X{var.start_x} Y{var.start_y} Z{var.start_z} F{var.travel_speed}

; -----------------------------------------------------------------------
; Step 4 — Trigger bd_pressure
;
; M118 P{global.bd_port} sends the trigger string to the sensor over USB.
; The sensor parses the parameters and takes over the USB link.
; RRF returns immediately from this line — the sensor is now in control.
;
; If the sensor does not respond within the G4 dwell below, it is likely:
;   a) not connected or powered,
;   b) running old firmware that doesn't support the RRF trigger format, or
;   c) M575 P0 S1 B38400 is missing from config.g.
; In any of these cases the dwell will expire and the macro will continue
; to Step 6 (clean-up) without a result. Check the DWC console for messages
; from the sensor — if nothing appears, check wiring and firmware version.
; -----------------------------------------------------------------------
M118 P2 S"bd_pressure: starting PA calibration — waiting for sensor response..."
M118 P{global.bd_port} S{"l:H" ^ var.high_speed ^ ":L" ^ var.low_speed ^ ":T" ^ var.travel_speed ^ ":S" ^ var.pa_step ^ ":N" ^ var.steps ^ ":E" ^ var.extruder ^ ":P" ^ var.pa_start ^ ";"}

; -----------------------------------------------------------------------
; Step 5 — Wait for calibration to complete
;
; bd_pressure sends M572 and G28 X Y to RRF when done, then reports the
; result via M118 P2 (DWC console) and M291 (DWC popup).
;
; The dwell is calculated as steps × 10 s — a conservative upper bound
; (~5 s/line typical, 10 s/line gives headroom for slow moves or heat soak).
; With the default 50 steps this is 500 s (~8 min).
;
; To abort mid-run: send  M118 P{global.bd_port} S"a;"  from the DWC console.
; -----------------------------------------------------------------------
G4 S{var.steps * 10}            ; wait steps × 10 s (adjust multiplier if needed)
M118 P2 S"bd_pressure: dwell complete. If no result popup appeared, check sensor connection and firmware version."

; -----------------------------------------------------------------------
; Step 6 — Clean up
; -----------------------------------------------------------------------
M400                            ; ensure all moves are complete
G28 X Y                         ; home X and Y (sensor also does this — belt and braces)
M118 P2 S"bd_pressure: calibration complete. Check DWC popup or /sys/pa_result.g for result."
