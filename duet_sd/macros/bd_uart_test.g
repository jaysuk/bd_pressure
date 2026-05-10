; bd_uart_test.g
; Diagnostic macro — tests ver; and mode; UART reads.
; Run from DWC macros panel after flashing v2.20 firmware.
; Results appear in the DWC console.

M118 P0 S"bd_uart_test: switching to device mode..."
M575 P{global.bd_port} S7 B{global.bd_baud}
G4 P300

; --- ver; ---
M118 P0 S"bd_uart_test: sending ver;..."
M260.2 P{global.bd_port} S"ver;"
G4 P500
M261.2 P{global.bd_port} B32 V"bd_ver"
M118 P0 S{"bd_uart_test: ver; response = '" ^ var.bd_ver ^ "'"}

; --- mode; (should be endstop on boot) ---
M118 P0 S"bd_uart_test: sending mode;..."
M260.2 P{global.bd_port} S"mode;"
G4 P500
M261.2 P{global.bd_port} B16 V"bd_mode"
M118 P0 S{"bd_uart_test: mode; response = '" ^ var.bd_mode ^ "'"}

; --- c; then mode; (should switch to pa) ---
M118 P0 S"bd_uart_test: sending c; to arm PA mode..."
M260.2 P{global.bd_port} S"c;"
G4 P500
M260.2 P{global.bd_port} S"mode;"
G4 P500
M261.2 P{global.bd_port} B16 V"bd_mode2"
M118 P0 S{"bd_uart_test: mode; after c; = '" ^ var.bd_mode2 ^ "' (expected: pa)"}

; --- e; restore endstop mode ---
M260.2 P{global.bd_port} S"e;"
G4 P200

; --- restore raw mode ---
M575 P{global.bd_port} S2 B{global.bd_baud}
M118 P0 S"bd_uart_test: done. Port restored to raw mode."
