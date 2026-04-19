; deployprobe.g
; Called by RRF automatically before each probe tap (G30, G29, mesh bed levelling, etc.)
;
; Place this file in /sys/ on the Duet SD card.
; A matching retractprobe.g (empty stub) must also be present in /sys/.
;
; WHY THIS FILE EXISTS:
; bd_pressure is a strain gauge sensor — it measures *relative* force, not absolute.
; It compares the live reading against a stored baseline (normal_z).  Over time, the
; baseline can drift due to:
;   - Thermal expansion of the hotend or frame between probing points
;   - Toolhead position changes loading the frame differently
;   - Time elapsed since the sensor was last baselined
;
; Re-baselineing immediately before each tap ensures the sensor always starts from a
; known-good zero reference, giving consistent and repeatable probe trigger heights
; regardless of thermal state or bed mesh point order.
;
; The dwell times are important:
;   - 500ms before N; — lets toolhead vibration from the approach move settle
;     so the baseline is captured while the frame is static
;   - 300ms after  N; — lets the ADC capture and lock the new baseline before
;     RRF begins the probe descent

M400                    ; wait for all moves to complete
G4 P500                 ; dwell — let toolhead vibration settle
M118 P{global.bd_uart} S"N;"  ; re-baseline the probe (lock current reading as normal_z)
G4 P300                 ; dwell — let ADC capture the new baseline
