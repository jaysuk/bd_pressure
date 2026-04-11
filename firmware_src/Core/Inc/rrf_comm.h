#ifndef __RRF_COMM_H__
#define __RRF_COMM_H__

#include <stdint.h>
#include <stdbool.h>

/* Maximum length of a single line we send to RRF.
   RRF USB input buffer is 512-640 bytes.  Most commands are short, but
   M291 popup messages with embedded newlines can reach ~200 chars. */
#define RRF_TX_LINE_MAX   256

/* Maximum length of a response line we buffer from RRF. */
#define RRF_RX_LINE_MAX   128

/* How long to wait for an "ok" before declaring a timeout (milliseconds).
   Moves can take several seconds; use a generous limit. */
#define RRF_OK_TIMEOUT_MS  15000

/* -----------------------------------------------------------------------
 * Result codes
 * --------------------------------------------------------------------- */
typedef enum {
    RRF_NONE  = 0,   /* no complete line received yet   */
    RRF_OK    = 1,   /* received "ok"                   */
    RRF_ERROR = 2,   /* received "Error:" line           */
    RRF_TIMEOUT = 3  /* rrf_wait_ok() timed out         */
} rrf_result_t;

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

/* Call once at startup to reset line counter and rx buffer. */
void rrf_comm_init(void);

/* Feed one byte received from the UART into the RRF response parser.
   Returns RRF_OK or RRF_ERROR when a complete line is recognised,
   RRF_NONE otherwise. Call this from the byte-receive path in main.c
   ONLY when the byte is destined for the RRF parser (not a bd_pressure
   command). */
rrf_result_t rrf_feed_byte(uint8_t b);

/* Send a GCode line to RRF with line-number and XOR checksum.
   The caller passes bare GCode, e.g. "G1 X100 F3000".
   This function prepends N<n> and appends *<cs>\n automatically. */
void rrf_send(const char *gcode);

/* Send a line without line-number/checksum (used only for M110 N0
   which resets the sequence and must itself be unnumbered). */
void rrf_send_raw(const char *gcode);

/* Block until an "ok" arrives from RRF or the timeout elapses.
   While waiting the ADC main loop must still run — the caller is
   responsible for continuing to call GetAD() / store raw_dat in its
   own loop; this function only handles the UART-response side.
   Returns RRF_OK, RRF_ERROR, or RRF_TIMEOUT. */
rrf_result_t rrf_wait_ok(uint32_t timeout_ms);

/* Returns true if at least one "ok" is pending (non-blocking check). */
bool rrf_ok_pending(void);

/* Consume one pending "ok" (call after rrf_ok_pending() returns true). */
void rrf_ok_consume(void);

/* Reset the line counter (call before starting a new calibration run). */
void rrf_reset_line_counter(void);

#endif /* __RRF_COMM_H__ */
