; pa_calibrate.g — bd_pressure RRF-controlled Pressure Advance calibration
;
; Place this file in /sys/ on the Duet SD card.
; Run via: M98 P"/sys/pa_calibrate.g"
;
; Requirements:
;   - bd_pressure sensor connected via UART (WAFER/I2C connector)
;   - Firmware version: bd_pressure-rrf-v2.18 or later
;   - global.bd_port, global.bd_uart and global.bd_baud set via M98 P"/sys/bd_globals.g"
;
; How it works:
;   Matches the Klipper reference implementation exactly.
;   The nozzle is raised clear of the bed and moves XY while extruding —
;   slow→fast→slow — for each PA value.  The XY velocity creates the
;   pressure transient; the sensor scores it via pa.lib.
;   All 5 pa.lib metrics (res, lk, rk, Hk, Ha) are logged per iteration,
;   matching the Klipper R: output format for direct comparison.

; -----------------------------------------------------------------------
; Parameters — edit these to match your printer and filament
; -----------------------------------------------------------------------
var tool            = 0      ; tool number (T0, T1, etc.)
var extruder        = 0      ; extruder index for M572 (usually same as tool)
var nozzle_temp     = 210    ; °C — nozzle temperature
var pa_start        = 0.0    ; starting PA value
var pa_step         = 0.002  ; PA increment per iteration
var steps           = 50     ; number of iterations

; Speeds — match your typical print speeds
; low_speed:  outer wall / perimeter speed
; high_speed: fast perimeter / infill speed
; travel_speed: travel between lines
var low_speed       = 1020   ; mm/min — Klipper default (51 × 20 mm³/s filament factor)
var high_speed      = 10740  ; mm/min — Klipper default (537 × 20 mm³/s filament factor)
var travel_speed    = 18000  ; mm/min

; Z height — raised clear of bed, nozzle not touching surface
var z_height        = 50     ; mm

; -----------------------------------------------------------------------
; Line geometry — 60mm total, X-only (no Y movement between iterations)
; Slow 15mm → fast 30mm → slow 15mm, centred on bed X
; Extrusion scaled from Klipper reference (0.046322 mm filament per mm XY)
; -----------------------------------------------------------------------
var x_start         = {move.axes[0].min + (move.axes[0].max - move.axes[0].min) / 2 - 30}
var x_mid_l         = {var.x_start + 15}
var x_mid_r         = {var.x_start + 45}
var x_end           = {var.x_start + 60}
var y_pos           = {move.axes[1].min + (move.axes[1].max - move.axes[1].min) / 2}

var e_per_mm        = 0.046322  ; mm filament per mm XY — Klipper reference ratio
var e_slow          = {15 * var.e_per_mm}   ; 15mm × ratio ≈ 0.69mm
var e_fast          = {30 * var.e_per_mm}   ; 30mm × ratio ≈ 1.39mm
var e_prime         = {60 * var.e_per_mm}   ; full line for prime ≈ 2.78mm

; -----------------------------------------------------------------------
; Validation — check line fits on bed X
; -----------------------------------------------------------------------
if var.x_start < move.axes[0].min || var.x_end > move.axes[0].max
    abort "bd_pressure: 60mm line does not fit on bed — reduce line length or centre offset"

; -----------------------------------------------------------------------
; Step 1 — Home if needed
; -----------------------------------------------------------------------
if !move.axes[0].homed || !move.axes[1].homed || !move.axes[2].homed
    G28

; -----------------------------------------------------------------------
; Step 2 — Heat nozzle
; -----------------------------------------------------------------------
T{var.tool}
M118 P0 S"bd_pressure: heating nozzle..."
M568 P{var.tool} S{var.nozzle_temp} A2
M116

; -----------------------------------------------------------------------
; Step 3 — Raise Z, move to start, prime
; -----------------------------------------------------------------------
G90
M83

M118 P0 S{"bd_pressure: PA calibration — " ^ var.steps ^ " steps, X" ^ var.x_start ^ "–X" ^ var.x_end ^ " Y" ^ var.y_pos}

G1 Z{var.z_height} F600
G1 X{var.x_start} Y{var.y_pos} F{var.travel_speed}

; Prime — fast move across full line extruding e_prime, then dwell
G1 F{var.high_speed}
G1 X{var.x_end} Y{var.y_pos} E{var.e_prime}
M400
G4 P4000    ; 4 s dwell — matches Klipper

; -----------------------------------------------------------------------
; Step 4 — Read bd_pressure firmware version then prepare log file
; -----------------------------------------------------------------------
M575 P{global.bd_port} S7 B{global.bd_baud}
G4 P100
M260.2 P{global.bd_port} S"ver;"
G4 P200
M261.2 P{global.bd_port} B32 V"bd_ver"
M575 P{global.bd_port} S2 B{global.bd_baud}
G4 P100

echo >"0:/sys/pa_calibrate_log.txt"  {"# bd_pressure PA calibration"}
echo >>"0:/sys/pa_calibrate_log.txt" {"# date=" ^ {datetime}}
echo >>"0:/sys/pa_calibrate_log.txt" {"# rrf_version=" ^ boards[0].firmwareVersion}
echo >>"0:/sys/pa_calibrate_log.txt" {"# bd_version=" ^ var.bd_ver[0]}
echo >>"0:/sys/pa_calibrate_log.txt" {"# nozzle_temp=" ^ var.nozzle_temp ^ " pa_start=" ^ var.pa_start ^ " pa_step=" ^ var.pa_step ^ " steps=" ^ var.steps}
echo >>"0:/sys/pa_calibrate_log.txt" "iter,pa,res,lk,rk,Hk,Ha"

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

while iterations < var.steps
    set var.pa = var.pa_start + iterations * var.pa_step
    M572 D{var.extruder} S{var.pa}

    M118 P0 S{"bd_pressure: step " ^ (iterations + 1) ^ " of " ^ var.steps ^ " — PA " ^ var.pa}

    ; Travel to line start (same Y every iteration)
    G1 X{var.x_start} Y{var.y_pos} F{var.travel_speed}

    ; Slow→fast→slow X-only move with extrusion
    G1 X{var.x_mid_l} Y{var.y_pos} F{var.low_speed}  E{var.e_slow}
    G1 X{var.x_mid_r} Y{var.y_pos} F{var.high_speed} E{var.e_fast}
    G1 X{var.x_end}   Y{var.y_pos} F{var.low_speed}  E{var.e_slow}

    M400
    G4 P200    ; brief dwell — let sensor finish processing

    ; Read all 5 PA metrics: res, lk, rk, Hk, Ha
    M260.2 P{global.bd_port} S"pdata;"
    G4 P200
    M261.2 P{global.bd_port} B5 V"bd_pa"

    set var.scores[iterations] = {var.bd_pa[0]}
    echo >>"0:/sys/pa_calibrate_log.txt" {iterations ^ "," ^ var.pa ^ "," ^ var.bd_pa[0] ^ "," ^ var.bd_pa[1] ^ "," ^ var.bd_pa[2] ^ "," ^ var.bd_pa[3] ^ "," ^ var.bd_pa[4]}

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

M118 P0 S{"bd_pressure: calibration complete. Best PA = " ^ var.best_pa ^ " (res=" ^ var.best_score ^ ", step " ^ var.best_i ^ ")"}
M291 P{"<b>Calibration complete!</b><br><b>Best Pressure Advance:</b> " ^ var.best_pa ^ "<br><br><b>Add to config.g:</b><br>M572 D" ^ var.extruder ^ " S" ^ var.best_pa ^ "<br><br>Full log: /sys/pa_calibrate_log.txt"} R"bd_pressure PA Result" S2

; -----------------------------------------------------------------------
; Step 9 — Restore sensor and cool down
; -----------------------------------------------------------------------
M260.2 P{global.bd_port} S"e;"
G4 P200
M575 P{global.bd_port} S2 B{global.bd_baud}
M568 P{var.tool} A0
M400
M118 P0 S"bd_pressure: done. Log saved to /sys/pa_calibrate_log.txt"
