; retractprobe.g
; Called by RRF after each probe tap (G30, G29, etc.)
;
; Place this file in /sys/ on the Duet SD card.
;
; The bd_pressure sensor requires no retract action — it is always
; in contact with the toolhead and does not need to be physically
; deployed or retracted.  This file exists to satisfy RRF's requirement
; for a matching retractprobe.g when deployprobe.g is present.
