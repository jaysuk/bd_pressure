; bd_version.g
; Query the bd_pressure sensor firmware version.
;
; Place this file in /macros/ on the Duet SD card.
; Run via: M98 P"/macros/bd_version.g"
;
; The sensor will respond with: bd_pressure-rrf-v1
; The response appears on the USB serial port the sensor is connected to.
; On a Duet 3 / Duet 2 with the sensor on the PanelDue port (P1), enable
; M575 serial pass-through to see it in DWC.

M118 P2 S"bd_pressure: sending version query (v;) ..."

; Send version query to the sensor
; P0 = USB host port (change to P1 if sensor is on the PanelDue/aux UART)
M118 P0 S"v;"

; Wait for response
G4 P500

M118 P2 S"bd_pressure: version query sent. Expected response: bd_pressure-rrf-v2"
M118 P2 S"Check the serial terminal connected to the sensor USB port to confirm."
