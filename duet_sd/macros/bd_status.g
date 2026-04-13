; bd_status.g
; Query the current status of the bd_pressure sensor.
;
; Place this file in /macros/ on the Duet SD card.
; Run via DWC macro button or: M98 P"/macros/bd_status.g"
;
; IMPORTANT: The sensor response goes to the USB serial console (DWC → Console tab),
; NOT back to RRF as a macro variable.  RRF has no way to read data sent by the sensor
; over USB — it is a one-way channel from RRF to the sensor via M118 P0.
; After running this macro, open the DWC Console tab to see the response.
;
; Expected response format (appears in DWC console):
;   mode:<endstop|pa>;thr:<0-99>;inv:<0|1>;ver:v2
;
; Fields:
;   mode  — current operating mode (endstop = probe mode, pa = PA calibration mode)
;   thr   — current trigger threshold (0–99, default 4)
;   inv   — polarity inversion (0 = normal, 1 = inverted)
;   ver   — firmware version
;
; global.bd_port is set in config.g:
;   global.bd_port = 0   ; 0 = USB (default), 1 = io0 (Pi Zero bridge / direct UART)

M118 P2 S"bd_pressure: querying status — check the Console tab for the response..."
M118 P{global.bd_port} S"s;"
G4 P500
M118 P2 S"bd_pressure: expected format: mode:<endstop|pa>;thr:<n>;inv:<0|1>;ver:v2"
