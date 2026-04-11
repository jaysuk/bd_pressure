; bd_abort.g
; Abort an in-progress bd_pressure PA calibration run.
;
; Place this file in /macros/ on the Duet SD card.
; Run via DWC macro button or: M98 P"/macros/bd_abort.g"
;
; Safe to run at any time — if no calibration is running the sensor
; ignores the command and remains in endstop/probe mode.

M118 P2 S"bd_pressure: sending abort command..."
M118 P0 S"a;"
G4 P500
M118 P2 S"bd_pressure: abort sent. Sensor should now be in endstop/probe mode."
