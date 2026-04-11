; bd_set_threshold.g
; Set the bd_pressure probe trigger threshold.
;
; Place this file in /macros/ on the Duet SD card.
; Run via: M98 P"/macros/bd_set_threshold.g" T7
;   where T is the threshold value (0–99, default 4)
;
; Higher value = less sensitive (more force to trigger)
; Lower value  = more sensitive (less force to trigger)
;
; The threshold is saved to flash automatically and persists across reboots.

if !exists(param.T)
    M118 P2 S"bd_pressure: error — no threshold specified. Usage: M98 P""/macros/bd_set_threshold.g"" T<value>"
    M99

if param.T < 0 || param.T > 99
    M118 P2 S"bd_pressure: error — threshold must be between 0 and 99"
    M99

M118 P2 S{"bd_pressure: setting threshold to " ^ param.T ^ " (saved to flash)"}
M118 P0 S{param.T ^ ";"}
G4 P300
M118 P2 S"bd_pressure: threshold set. Run bd_status.g to confirm."
