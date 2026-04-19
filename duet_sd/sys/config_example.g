; config_example.g — bd_pressure configuration snippets for config.g
;
; This file is NOT intended to be used directly as config.g.
; Copy the relevant sections into your existing config.g.
;
; -----------------------------------------------------------------------
; 1. Serial port selection — set once, used by all bd_pressure macros
; -----------------------------------------------------------------------
; bd_globals.g sets three globals used by all bd_pressure macros:
;   global.bd_port — M575/M261.2 port number (1 = first aux UART)
;   global.bd_uart — M118 port number for sending to the sensor (2 = first aux UART)
;   global.bd_baud — baud rate (default 115200)
; These are separate because M575 and M118 use different port numbering.
; If you move the sensor to a different port or change baud rate, update bd_globals.g.
;
M98 P"/sys/bd_globals.g"     ; initialise bd_pressure globals

; -----------------------------------------------------------------------
; 2. UART port — device mode for M260.2/M261.2
; -----------------------------------------------------------------------
; S2 = raw mode, sensor responses appear directly in the DWC console.
; The baud rate is read from global.bd_baud (set in bd_globals.g).
;
M575 P{global.bd_port} S2 B{global.bd_baud}

; -----------------------------------------------------------------------
; 3. Z probe — define bd_pressure as a switch-type probe
; -----------------------------------------------------------------------
; bd_pressure acts as a standard digital switch endstop on the Z- pin.
; Use the line that matches your board generation.
; Adjust the pin name (C parameter) and offsets to match your wiring.
;
; Duet 3 (6HC, Mini 5+, etc.) — filtered digital input:
M558 P8 C"zstop" H5 F300:60 T12000   ; P8 = filtered digital (recommended for Duet 3)
                                       ; C = input pin name — check your board pinout
                                       ; H = dive height (mm)
                                       ; F = probe speeds mm/min (first:second pass)
                                       ; T = travel speed mm/min

; Duet 2 (WiFi, Ethernet) — use P5 instead:
; M558 P5 C"zprobe.in" H5 F300:60 T12000

G31 P500 X0 Y0 Z-0.1   ; probe trigger value and nozzle offsets
                         ; *** adjust Z offset after first probe run ***

; -----------------------------------------------------------------------
; 4. deploy/retract macros
; -----------------------------------------------------------------------
; RRF calls deployprobe.g before each probe tap and retractprobe.g after.
; Both files must exist in /sys/ — retractprobe.g can be empty.
; They are included in the duet_sd/sys/ folder of this repository.

; -----------------------------------------------------------------------
; 5. Pressure Advance — apply calibration result
; -----------------------------------------------------------------------
; After running pa_calibrate.g the result is saved to /sys/pa_result.g.
; Include it here so it is applied automatically on every boot.
;
; Uncomment the line below once you have a calibration result:
; M98 P"/sys/pa_result.g"
