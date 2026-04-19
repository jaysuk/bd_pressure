; pa_calibrate.g — bd_pressure RRF-controlled Pressure Advance calibration
;
; Place this file in /sys/ on the Duet SD card.
; Run via: M98 P"/sys/pa_calibrate.g"
;
; Requirements:
;   - bd_pressure sensor connected via UART (WAFER/I2C connector)
;   - Firmware version: bd_pressure-rrf-v2.17 or later
;   - global.bd_port and global.bd_uart set in config.g (via M98 P"/sys/bd_globals.g")
;   - Nozzle and bed homed before running (or set home_first = true below)
;
; How it works:
;   RRF controls all movement and PA values.  The sensor is armed in
;   sampling mode ('c;'), then after each line RRF requests the pa.lib
;   score via 'score;' (read back with M261.2).  The scores are collected
;   in a vector and the PA value with the lowest score is applied.

; -----------------------------------------------------------------------
; Parameters — edit these to match your printer and filament
; -----------------------------------------------------------------------
var tool          = 0        ; tool number (T0, T1, etc.)
var extruder      = 0        ; extruder index for M572 (usually same as tool)
var nozzle_temp   = 210      ; °C  nozzle print temperature
var bed_temp      = 0        ; °C  bed temperature (set to 0 to skip bed heating)
var high_speed    = 10800    ; mm/min  fast extrusion segment
var low_speed     = 3000     ; mm/min  slow extrusion segments
var travel_speed  = 24000    ; mm/min  travel between lines
var pa_start      = 0.0      ; starting PA value
var pa_step       = 0.002    ; PA increment per iteration
var steps         = 50       ; number of iterations
var home_first    = true     ; true = G28 before calibration

; Calibration pattern geometry — adjust to your bed
; x_start/x_end define the line; x_mid_l/x_mid_r are the slow/fast/slow split points
var x_start       = 78.0
var x_mid_l       = 98.0
var x_mid_r       = 138.0
var x_end         = 158.0
var y_base        = 38.75    ; Y of first line
var y_step        = 3.5      ; Y spacing between lines

; Extrusion ratio: filament mm extruded per mm of XY travel.
; Calculate to match your slicer settings:
;
;   filament_area = π × (filament_diameter / 2)²
;   e_per_mm      = (extrusion_width × layer_height) / filament_area
;
; Example — 0.4 mm nozzle, 0.25 mm layer, 0.48 mm width, 1.75 mm filament:
;   filament_area = π × 0.875² = 2.4053
;   e_per_mm      = (0.48 × 0.25) / 2.4053 = 0.04990
;
; extrusion_width is typically 100–120% of nozzle diameter.
; These values should match your slicer's outer wall settings.
var e_per_mm      = 0.046322

; -----------------------------------------------------------------------
; Step 1 — Select tool and heat
; -----------------------------------------------------------------------
T{var.tool}
M118 P2 S"bd_pressure: heating..."

if var.bed_temp > 0
    M140 S{var.bed_temp}

M568 P{var.tool} S{var.nozzle_temp} A2
M116

; -----------------------------------------------------------------------
; Step 2 — Home and move to start position
; -----------------------------------------------------------------------
if var.home_first
    G28

G90    ; absolute XY
M83    ; relative extrusion

G1 X{var.x_start} Y{var.y_base} F{var.travel_speed}

; -----------------------------------------------------------------------
; Step 3 — Switch sensor to device mode and arm PA sampling
; -----------------------------------------------------------------------
M575 P{global.bd_port} S7 B{global.bd_baud}    ; device mode — enables M261.2 reads
G4 P100                              ; brief settle
M260.2 P{global.bd_port} S"c;"      ; arm sensor in PA sampling mode
G4 P500                              ; let sensor switch ADC mode

; Prime: push 4 mm of filament at low speed to seat
G1 E4 F300

; Fast prime move across the bed to stabilise pressure
var e_prime = (var.x_end - var.x_start) * var.e_per_mm
G1 X{var.x_end} Y{var.y_base} F{var.high_speed} E{var.e_prime}
M400
G4 P4000    ; 4 s dwell to let sensor stabilise

; -----------------------------------------------------------------------
; Step 4 — Calibration loop
; -----------------------------------------------------------------------
M118 P2 S"bd_pressure: starting PA calibration sweep..."

; Pre-calculate extrusion amounts (constant across all lines)
var e_low  = (var.x_mid_l - var.x_start) * var.e_per_mm
var e_high = (var.x_mid_r - var.x_mid_l) * var.e_per_mm
var e_low2 = (var.x_end   - var.x_mid_r) * var.e_per_mm

var scores = vector(var.steps, 0)
var pa     = var.pa_start    ; current PA value — updated each iteration
var y      = var.y_base      ; current Y position — updated each iteration

while iterations < var.steps
    set var.pa = var.pa_start + iterations * var.pa_step
    set var.y  = var.y_base   + iterations * var.y_step

    M572 D{var.extruder} S{var.pa}

    ; Travel to line start
    G1 X{var.x_start} Y{var.y} F{var.travel_speed}

    ; Low-speed segment: x_start → x_mid_l
    G1 X{var.x_mid_l} Y{var.y} F{var.low_speed} E{var.e_low}

    ; High-speed segment: x_mid_l → x_mid_r
    G1 X{var.x_mid_r} Y{var.y} F{var.high_speed} E{var.e_high}

    ; Low-speed segment: x_mid_r → x_end
    G1 X{var.x_end}   Y{var.y} F{var.low_speed} E{var.e_low2}

    ; Wait for all moves to complete before requesting score
    M400

    ; Request score from sensor — firmware sends one raw binary byte (0-255)
    M260.2 P{global.bd_port} S"score;"
    M261.2 P{global.bd_port} B1 V"bd_score"   ; creates/updates var.bd_score
    set var.scores[iterations] = {var.bd_score[0]}

; -----------------------------------------------------------------------
; Step 5 — Find best PA value
;
; Skip the first quarter of samples as warm-up.
; Minimum 1 sample skipped, maximum 5.
; -----------------------------------------------------------------------
var skip = var.steps / 4
if var.skip < 1
    set var.skip = 1
if var.skip > 5
    set var.skip = 5

var best_score = 255
var best_i     = var.skip    ; index of best score found so far
var idx        = var.skip    ; scratch index — updated each iteration

while iterations + var.skip < var.steps
    set var.idx = iterations + var.skip
    if var.scores[var.idx] < var.best_score && var.scores[var.idx] > 0
        set var.best_score = var.scores[var.idx]
        set var.best_i     = var.idx

var best_pa = var.pa_start + var.best_i * var.pa_step

; -----------------------------------------------------------------------
; Step 6 — Apply result and report
; -----------------------------------------------------------------------
M572 D{var.extruder} S{var.best_pa}

; Save result to SD card
M28 /sys/pa_result.g
M572 D{var.extruder} S{var.best_pa} ; bd_pressure PA calibration result
M29

M118 P2 S{"bd_pressure: calibration complete. Best PA = " ^ var.best_pa ^ " (score " ^ var.best_score ^ ", step " ^ var.best_i ^ ")"}
M291 P{"<b>Calibration complete!</b><br><b>Best Pressure Advance:</b> " ^ var.best_pa ^ "<br><br><b>Add to config.g:</b><br>M572 D" ^ var.extruder ^ " S" ^ var.best_pa} R"bd_pressure PA Result" S2

; -----------------------------------------------------------------------
; Step 7 — Restore sensor to endstop mode and clean up
; -----------------------------------------------------------------------
M260.2 P{global.bd_port} S"e;"      ; return sensor to endstop/probe mode
G4 P200
M575 P{global.bd_port} S2 B{global.bd_baud}   ; restore raw mode
M400
G28 X Y
M118 P2 S"bd_pressure: done. Check /sys/pa_result.g for saved result."
