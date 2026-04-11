; config_example.g — bd_pressure configuration snippets for config.g
;
; This file is NOT intended to be used directly as config.g.
; Copy the relevant sections into your existing config.g.
;
; -----------------------------------------------------------------------
; 1. USB serial port — enable Marlin emulation pass-through
; -----------------------------------------------------------------------
; The bd_pressure sensor communicates at 38400 baud over the Duet USB host
; port (P0).  M575 S1 enables pass-through so the sensor's responses appear
; in the DWC console and macros can send commands with M118 P0.
;
M575 P0 S1 B38400   ; USB host port: pass-through at 38400 baud (bd_pressure default)

; -----------------------------------------------------------------------
; 2. Z probe — define bd_pressure as a switch-type probe
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
; 3. deploy/retract macros
; -----------------------------------------------------------------------
; RRF calls deployprobe.g before each probe tap and retractprobe.g after.
; Both files must exist in /sys/ — retractprobe.g can be empty.
; They are included in the duet_sd/sys/ folder of this repository.

; -----------------------------------------------------------------------
; 4. Pressure Advance — apply calibration result
; -----------------------------------------------------------------------
; After running pa_calibrate.g the result is saved to /sys/pa_result.g.
; Include it here so it is applied automatically on every boot.
;
; Uncomment the line below once you have a calibration result:
; M98 P"/sys/pa_result.g"

; -----------------------------------------------------------------------
; 5. Optional — restore PA mode on boot for Klipper-style raw data output
; -----------------------------------------------------------------------
; Not needed for RRF automated calibration.  Only uncomment if you are
; using the sensor in raw ADC mode for custom tooling.
;
; M118 P0 S"l;"   ; switch to PA/ADC mode on boot
