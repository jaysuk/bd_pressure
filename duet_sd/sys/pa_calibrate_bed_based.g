; pa_calibrate.g — bd_pressure RRF-controlled Pressure Advance calibration
;
; Place this file in /sys/ on the Duet SD card.
; Run via: M98 P"/sys/pa_calibrate.g"
;
; Requirements:
;   - bd_pressure sensor connected via UART (WAFER/I2C connector)
;   - Firmware version: bd_pressure-rrf-v2.17 or later
;   - global.bd_port, global.bd_uart and global.bd_baud set via M98 P"/sys/bd_globals.g"
;
; How it works:
;   RRF prints a slow→fast→slow line on the bed for each PA value.
;   The bd_pressure strain gauge scores each line via pa.lib.
;   The geometry is derived from the bed size automatically — only
;   line_length and y_step need adjusting for small beds.

; -----------------------------------------------------------------------
; Parameters — edit these to match your printer and filament
; -----------------------------------------------------------------------
var tool            = 0      ; tool number (T0, T1, etc.)
var extruder        = 0      ; extruder index for M572 (usually same as tool)
var nozzle_temp     = 210    ; °C nozzle print temperature
var bed_temp        = 0      ; °C bed temperature (0 = skip bed heating)
var layer_height    = 0.2    ; mm first layer height
var pa_start        = 0.0    ; starting PA value
var pa_step         = 0.002  ; PA increment per iteration

; Line geometry — centred on the bed automatically
; Reduce line_length for small beds (e.g. 60 for a V0 120mm bed)
; The slow segments are each 25% of line_length, fast segment is 50%
var line_length     = 80     ; mm total line length
var y_step          = 3.5    ; mm spacing between lines
var y_margin        = 20     ; mm clearance from front and back of bed
var x_margin        = 5      ; mm clearance from left and right of bed

; Extrusion — matched to original bd_pressure Klipper implementation
; e_per_mm: filament mm per XY mm. Formula:
;   filament_area = π × (filament_dia/2)²
;   e_per_mm = (extrusion_width × layer_height) / filament_area
; For 0.4mm nozzle, 0.2mm layer, 0.48mm width, 1.75mm filament = 0.04990
var e_per_mm        = 0.04990

; Speeds — use your typical outer wall and travel speeds
var feed_slow       = 1020   ; mm/min slow segments (matches Klipper default)
var feed_fast       = 10740  ; mm/min fast segment  (matches Klipper default)
var feed_travel     = 18000  ; mm/min travel between lines

; -----------------------------------------------------------------------
; Derived geometry — calculated from bed size and line_length
; Uses min/max from OM to handle both corner-origin and centre-origin beds
; -----------------------------------------------------------------------
var x_min       = {move.axes[0].min}
var x_max       = {move.axes[0].max}
var y_min       = {move.axes[1].min}
var y_max       = {move.axes[1].max}
var bed_x       = {var.x_max - var.x_min}
var bed_y       = {var.y_max - var.y_min}
var cx          = {var.x_min + var.bed_x / 2}
var x_start     = {var.cx - var.line_length / 2}
var x_mid_l     = {var.x_start + var.line_length * 0.25}
var x_mid_r     = {var.x_start + var.line_length * 0.75}
var x_end       = {var.x_start + var.line_length}
var y_start     = {var.y_min + var.y_margin}

; -----------------------------------------------------------------------
; Validation — abort with a clear message if geometry doesn't fit
; -----------------------------------------------------------------------
if var.y_step < 1.0
    abort "bd_pressure: y_step is too small (minimum 1.0mm) — lines would overlap"

if var.line_length > var.bed_x - var.x_margin * 2
    abort {"bd_pressure: line_length " ^ var.line_length ^ "mm too long for bed width " ^ var.bed_x ^ "mm — reduce line_length or x_margin"}

if var.x_start < var.x_min
    abort {"bd_pressure: line starts at X" ^ var.x_start ^ " which is outside the bed minimum X" ^ var.x_min ^ " — reduce line_length"}

if var.x_end > var.x_max
    abort {"bd_pressure: line ends at X" ^ var.x_end ^ " which is outside the bed maximum X" ^ var.x_max ^ " — reduce line_length"}

if var.y_start < var.y_min
    abort {"bd_pressure: y_start " ^ var.y_start ^ " is outside the bed minimum Y" ^ var.y_min ^ " — reduce y_margin"}

; Calculate max steps that fit on the bed and cap at 50
var max_steps   = {floor((var.bed_y - var.y_margin * 2) / var.y_step)}
if var.max_steps < 2
    abort {"bd_pressure: only " ^ var.max_steps ^ " lines fit on the bed — increase bed size, reduce y_margin, or reduce y_step"}

var steps       = {var.max_steps < 50 ? var.max_steps : 50}

; Pre-calculate extrusion amounts
var e_slow      = {(var.x_mid_l - var.x_start) * var.e_per_mm}
var e_fast      = {(var.x_mid_r - var.x_mid_l) * var.e_per_mm}
var e_slow2     = {(var.x_end   - var.x_mid_r) * var.e_per_mm}
var e_prime     = {(var.x_end   - var.x_start) * var.e_per_mm}

; -----------------------------------------------------------------------
; Step 1 — Home if any axis is not homed
; -----------------------------------------------------------------------
if !move.axes[0].homed || !move.axes[1].homed || !move.axes[2].homed
    G28

; -----------------------------------------------------------------------
; Step 2 — Heat nozzle and bed
; -----------------------------------------------------------------------
T{var.tool}
M118 P0 S"bd_pressure: heating..."

if var.bed_temp > 0
    M140 S{var.bed_temp}

M568 P{var.tool} S{var.nozzle_temp} A2
M116

; -----------------------------------------------------------------------
; Step 3 — Move to start position and prime
; -----------------------------------------------------------------------
G90
M83

M118 P0 S{"bd_pressure: bed " ^ var.bed_x ^ "x" ^ var.bed_y ^ "mm — " ^ var.steps ^ " steps — line " ^ var.line_length ^ "mm centred at X" ^ var.cx ^ " (X" ^ var.x_start ^ "–X" ^ var.x_end ^ ")"}

G1 X{var.x_start} Y{var.y_start} F{var.feed_travel}
G1 Z{var.layer_height} F300

; Prime: push 4mm then run a full-width line to stabilise pressure
G1 E4 F300
G1 X{var.x_end} Y{var.y_start} F{var.feed_fast} E{var.e_prime}
M400
G4 P4000    ; 4 s dwell to let sensor stabilise

; -----------------------------------------------------------------------
; Step 4 — Prepare debug log file
; -----------------------------------------------------------------------
echo >"0:/sys/pa_calibrate_log.txt" "iter,pa,score"

; -----------------------------------------------------------------------
; Step 5 — Switch sensor to device mode and arm PA sampling
; -----------------------------------------------------------------------
M575 P{global.bd_port} S7 B{global.bd_baud}
G4 P100
M260.2 P{global.bd_port} S"c;"
G4 P500

; -----------------------------------------------------------------------
; Step 6 — Calibration loop
; -----------------------------------------------------------------------
M118 P0 S"bd_pressure: starting PA calibration sweep..."

var scores  = vector(var.steps, 0)
var pa      = var.pa_start
var y       = var.y_start

while iterations < var.steps
    set var.pa = var.pa_start + iterations * var.pa_step
    set var.y  = var.y_start + iterations * var.y_step
    M572 D{var.extruder} S{var.pa}

    M118 P0 S{"bd_pressure: step " ^ (iterations + 1) ^ " of " ^ var.steps ^ " — PA " ^ var.pa}

    ; Travel to line start
    G1 X{var.x_start} Y{var.y} F{var.feed_travel}

    ; Slow→fast→slow line
    G1 X{var.x_mid_l} Y{var.y} F{var.feed_slow} E{var.e_slow}
    G1 X{var.x_mid_r} Y{var.y} F{var.feed_fast} E{var.e_fast}
    G1 X{var.x_end}   Y{var.y} F{var.feed_slow} E{var.e_slow2}

    M400

    ; Request score
    M260.2 P{global.bd_port} S"score;"
    M261.2 P{global.bd_port} B1 V"bd_score"
    set var.scores[iterations] = {var.bd_score[0]}
    echo >>"0:/sys/pa_calibrate_log.txt" {iterations ^ "," ^ var.pa ^ "," ^ var.bd_score[0]}

; -----------------------------------------------------------------------
; Step 7 — Find best PA value (skip first 5 as warm-up)
; -----------------------------------------------------------------------
var skip = 5
if var.skip >= var.steps
    set var.skip = 1

var best_score  = 255
var best_i      = var.skip
var idx         = var.skip

while iterations + var.skip < var.steps
    set var.idx = iterations + var.skip
    if var.scores[var.idx] < var.best_score && var.scores[var.idx] > 0
        set var.best_score = var.scores[var.idx]
        set var.best_i     = var.idx

var best_pa = var.pa_start + var.best_i * var.pa_step

; -----------------------------------------------------------------------
; Step 8 — Apply result and report
; -----------------------------------------------------------------------
M572 D{var.extruder} S{var.best_pa}

M28 /sys/pa_result.g
M572 D{var.extruder} S{var.best_pa} ; bd_pressure PA calibration result
M29

M118 P0 S{"bd_pressure: calibration complete. Best PA = " ^ var.best_pa ^ " (score " ^ var.best_score ^ ", step " ^ var.best_i ^ ")"}
M291 P{"<b>Calibration complete!</b><br><b>Best Pressure Advance:</b> " ^ var.best_pa ^ "<br><br><b>Add to config.g:</b><br>M572 D" ^ var.extruder ^ " S" ^ var.best_pa} R"bd_pressure PA Result" S2

; -----------------------------------------------------------------------
; Step 9 — Restore sensor and cool down
; -----------------------------------------------------------------------
M260.2 P{global.bd_port} S"e;"
G4 P200
M575 P{global.bd_port} S2 B{global.bd_baud}
M568 P{var.tool} A0
M400
M118 P0 S"bd_pressure: done. Check /sys/pa_result.g for saved result."
