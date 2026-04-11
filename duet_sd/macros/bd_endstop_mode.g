; bd_endstop_mode.g
; Switch bd_pressure to endstop / Z-probe mode.
;
; Place this file in /macros/ on the Duet SD card.
; Run via DWC macro button or: M98 P"/macros/bd_endstop_mode.g"
;
; This is the normal operating mode for Z homing and bed mesh probing.
; The sensor is automatically in this mode on power-up.

M118 P2 S"bd_pressure: switching to endstop/probe mode..."
M118 P0 S"e;"
G4 P300
M118 P2 S"bd_pressure: now in endstop/probe mode."
