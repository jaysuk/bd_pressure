; bd_version.g
; Query the bd_pressure sensor firmware version.
;
; Place this file in /macros/ on the Duet SD card.
; Run via DWC macro button or: M98 P"/macros/bd_version.g"
;
; The sensor will respond with: bd_pressure-rrf-v2
; The response appears in the DWC console (the sensor sends it as M118 P2).

M118 P2 S"bd_pressure: sending version query (v;) ..."

; Send version query to the sensor
; P0 = USB host port (change to P1 if sensor is on the PanelDue/aux UART)
M118 P0 S"v;"

; Wait for response
G4 P500

M118 P2 S"bd_pressure: version query sent. Expected response: bd_pressure-rrf-v2"
