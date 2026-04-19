; bd_globals.g — initialise all bd_pressure global variables
;
; Place this file in /sys/ on the Duet SD card.
; Call from config.g with: M98 P"/sys/bd_globals.g"
;
; Only creates each global if it does not already exist, so it is safe
; to call multiple times or early in config.g before other bd macros run.

; bd_port — port number for M575 (configure) and M261.2 (read)
;   1 = first aux UART (io0, WAFER/I2C connector)
;   2 = second aux UART (io1)
if !exists(global.bd_port)
    global bd_port = 1

; bd_uart — port number for M118 (send to sensor)
;   M118 P2 = first aux UART (PanelDue/UART port)
;   M118 P5 = second aux UART
if !exists(global.bd_uart)
    global bd_uart = 2

; bd_baud — UART baud rate shared by all bd_pressure macros
;   Change this value if you have switched the sensor to a different baud rate.
;   Valid values: 115200, 57600, 38400, 230400
if !exists(global.bd_baud)
    global bd_baud = 115200
