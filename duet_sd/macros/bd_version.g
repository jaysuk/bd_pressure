; bd_version.g
; Query the bd_pressure sensor firmware version.
;
; Place this file in /macros/ on the Duet SD card.
; Run via DWC macro button or: M98 P"/macros/bd_version.g"
;
; The sensor will respond with: bd_pressure-rrf-v2
; The response appears in the DWC console (the sensor sends it as M118 P2).
;
; global.bd_port is set in config.g:
;   global.bd_port = 0   ; 0 = USB (default), 1 = io0 (Pi Zero bridge / direct UART)

M118 P2 S"bd_pressure: sending version query (v;) ..."

; Send version query to the sensor
M118 P{global.bd_port} S"v;"

; Wait for response
G4 P500

M118 P2 S"bd_pressure: version query sent. Expected response: bd_pressure-rrf-v2"
