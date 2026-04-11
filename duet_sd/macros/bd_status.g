; bd_status.g
; Query the current status of the bd_pressure sensor.
;
; Place this file in /macros/ on the Duet SD card.
; Run via DWC macro button or: M98 P"/macros/bd_status.g"
;
; The sensor responds with a single line in the DWC console:
;   mode:<endstop|pa>;thr:<0-99>;inv:<0|1>;ver:v2
;
; Fields:
;   mode  — current operating mode (endstop = probe mode, pa = PA calibration mode)
;   thr   — current trigger threshold (0–99, default 4)
;   inv   — polarity inversion (0 = normal, 1 = inverted)
;   ver   — firmware version

M118 P2 S"bd_pressure: querying status..."
M118 P0 S"s;"
G4 P500
M118 P2 S"bd_pressure: status response sent. Expected format: mode:<endstop|pa>;thr:<n>;inv:<0|1>;ver:v2"
