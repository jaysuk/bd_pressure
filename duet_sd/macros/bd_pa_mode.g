; bd_pa_mode.g
; Switch bd_pressure to Pressure Advance (PA) calibration mode (Klipper raw mode).
;
; Place this file in /macros/ on the Duet SD card.
; Run via DWC macro button or: M98 P"/macros/bd_pa_mode.g"
;
; NOTE: For RRF automated PA calibration use pa_calibrate.g instead —
; that macro handles the full calibration sequence automatically.
; This macro only switches the ADC mode; it does NOT start a calibration run.
; Use it if you need to manually put the sensor into PA mode for diagnostics.
;
; global.bd_port is set in config.g:
;   global.bd_port = 0   ; 0 = USB (default), 1 = io0 (Pi Zero bridge / direct UART)

M118 P2 S"bd_pressure: switching to PA mode (raw ADC output)..."
M118 P{global.bd_port} S"l;"
G4 P300
M118 P2 S"bd_pressure: now in PA mode. Send e; or run bd_endstop_mode.g to return to probe mode."
