; deployprobe.g
; Called by RRF before each probe tap (G30, G29, etc.)
;
; Place this file in /sys/ on the Duet SD card.
;
; Re-baselines the bd_pressure sensor immediately before probing to
; compensate for any drift due to temperature, toolhead position, or
; time since last baseline.  The dwell times are important:
;   - 500ms before N; lets toolhead vibration from the approach move settle
;   - 300ms after  N; lets the ADC capture and lock the new baseline

M400                    ; wait for all moves to complete
G4 P500                 ; dwell — let toolhead vibration settle
M118 P0 S"N;"           ; re-baseline the probe (lock current reading as normal_z)
G4 P300                 ; dwell — let ADC capture the new baseline
