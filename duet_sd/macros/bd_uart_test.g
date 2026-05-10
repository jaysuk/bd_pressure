; bd_uart_test.g
; Diagnostic macro — tests ver; and mode; UART reads.
; Run from DWC macros panel to confirm sensor communication.
;
; NOTE: M261.2 always returns raw bytes — the responses below will appear
; as byte arrays e.g. {98,100,...}. This is expected RRF behaviour.
; The sensor version is read from global.bd_version instead (set in bd_globals.g).

M118 P0 S"bd_uart_test: switching to device mode..."
M575 P{global.bd_port} S7 B{global.bd_baud}
G4 P300

; --- ver; — shows raw byte response for diagnostic purposes only ---
M118 P0 S"bd_uart_test: sending ver; (raw bytes expected)..."
M260.2 P{global.bd_port} S"ver;"
G4 P500
M261.2 P{global.bd_port} B32 V"bd_ver"
M118 P0 S{"bd_uart_test: ver; raw response = " ^ var.bd_ver}
M118 P0 S{"bd_uart_test: version from global = " ^ global.bd_version}

; --- mode; before c; (should be endstop) ---
M118 P0 S"bd_uart_test: sending mode; (raw bytes expected)..."
M260.2 P{global.bd_port} S"mode;"
G4 P500
M261.2 P{global.bd_port} B16 V"bd_mode"
M118 P0 S{"bd_uart_test: mode; raw response = " ^ var.bd_mode}

; --- c; then mode; ---
M118 P0 S"bd_uart_test: sending c; to arm PA mode..."
M260.2 P{global.bd_port} S"c;"
G4 P500
M260.2 P{global.bd_port} S"mode;"
G4 P500
M261.2 P{global.bd_port} B16 V"bd_mode2"
M118 P0 S{"bd_uart_test: mode; after c; raw response = " ^ var.bd_mode2}

; --- e; restore endstop mode ---
M260.2 P{global.bd_port} S"e;"
G4 P200

; --- restore raw mode ---
M575 P{global.bd_port} S2 B{global.bd_baud}
M118 P0 S"bd_uart_test: done. Port restored to raw mode."
