; config_example.g — bd_pressure configuration snippets for config.g
;
; This file is NOT intended to be used directly as config.g.
; Copy the relevant sections into your existing config.g.
;
; -----------------------------------------------------------------------
; 1. USB serial port — enable Marlin emulation pass-through
; -----------------------------------------------------------------------
; The bd_pressure sensor communicates over the Duet's USB host port (P0).
; M575 enables serial pass-through so the sensor's responses appear in
; the DWC console and so that macros can send commands with M118 P0.
;
M575 P0 S1 B57600   ; USB host port: pass-through at 57600 baud
                    ; Use B38400 if your sensor is running older firmware

; -----------------------------------------------------------------------
; 2. Z probe — define bd_pressure as a switch-type probe
; -----------------------------------------------------------------------
; bd_pressure acts as a standard switch-type endstop on the Z- pin.
; Adjust the pin name and offsets to match your toolhead and bed.
;
M558 P5 C"zstop" H5 F300:60 T12000   ; P5 = switch probe
                                       ; C = input pin (zstop, Z-, etc.)
                                       ; H = dive height (mm)
                                       ; F = probe speed (first:second pass)
                                       ; T = travel speed

G31 P500 X0 Y0 Z-0.1   ; probe trigger value and nozzle offsets
                         ; adjust Z offset after first homing

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
