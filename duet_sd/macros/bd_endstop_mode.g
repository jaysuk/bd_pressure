; bd_endstop_mode.g
; Switch bd_pressure to endstop / Z-probe mode.
;
; Place this file in /macros/ on the Duet SD card.
; Run via DWC macro button or: M98 P"/macros/bd_endstop_mode.g"
;
; This is the normal operating mode for Z homing and bed mesh probing.
; The sensor is automatically in this mode on power-up.
;
; global.bd_port is set in config.g:
;   global.bd_port = 0   ; 0 = USB (default), 1 = io0 (Pi Zero bridge / direct UART)

M118 P2 S"bd_pressure: switching to endstop/probe mode..."
M118 P{global.bd_port} S"e;"
G4 P300
M118 P2 S"bd_pressure: now in endstop/probe mode."
