/*
 * rrf_comm.c
 *
 * Handles all communication FROM bd_pressure TO RepRapFirmware over the
 * shared UART (PA11 TX / PA12 RX via CH340 USB bridge).
 *
 * Protocol:
 *   - RRF is put into Marlin-emulation mode by sending "M555 P2" as the
 *     very first command.  In this mode RRF replies "ok" after every
 *     accepted GCode line, which is what we wait for before advancing
 *     the PA state machine.
 *   - Every subsequent line is formatted as:
 *         N<linenum> <gcode>*<xor_checksum>\n
 *   - The XOR checksum covers everything from 'N' up to and including
 *     the space before '*'.
 *
 * Threading model:
 *   Single-threaded / super-loop.  rrf_feed_byte() is called from the
 *   main loop each time a byte arrives in rxData[].  rrf_wait_ok() spins
 *   in-place; the caller must keep the ADC running inside its own loop
 *   while waiting (see pa_rrf.c).
 */

#include "rrf_comm.h"
#include "iousart.h"
#include "main.h"

#include <string.h>

/* -----------------------------------------------------------------------
 * Timing helper — re-uses tim14_n which is incremented every ~1 ms
 * by the TIM14 ISR already present in iousart.c.
 * --------------------------------------------------------------------- */
extern unsigned int tim14_n;

/* -----------------------------------------------------------------------
 * Internal state
 * --------------------------------------------------------------------- */
static uint32_t  s_line_num  = 0;        /* current outgoing line number  */
static uint8_t   s_ok_count  = 0;        /* pending "ok" responses        */

/* Incoming line assembly buffer */
static char      s_rx_buf[RRF_RX_LINE_MAX];
static uint8_t   s_rx_len    = 0;

/* Outgoing formatted line buffer */
static char      s_tx_buf[RRF_TX_LINE_MAX];

/* -----------------------------------------------------------------------
 * Low-level TX — reuses the existing iouart1_SendByte() bit-bang sender
 * --------------------------------------------------------------------- */
static void _tx_str(const char *s)
{
    while (*s)
        iouart1_SendByte((uint8_t)*s++);
}

/* -----------------------------------------------------------------------
 * XOR checksum over a string (covers chars up to but not including '\0')
 * --------------------------------------------------------------------- */
static uint8_t _checksum(const char *s)
{
    uint8_t cs = 0;
    while (*s)
        cs ^= (uint8_t)*s++;
    return cs;
}

/* -----------------------------------------------------------------------
 * Public: initialise / reset
 * --------------------------------------------------------------------- */
void rrf_comm_init(void)
{
    s_line_num = 0;
    s_ok_count = 0;
    s_rx_len   = 0;
    memset(s_rx_buf, 0, sizeof(s_rx_buf));
}

void rrf_reset_line_counter(void)
{
    s_line_num = 0;
}

/* -----------------------------------------------------------------------
 * Public: send with line-number + checksum
 *
 * Formats:  N<n> <gcode>*<cs>\n
 * The checksum covers the full "N<n> <gcode>" string.
 * --------------------------------------------------------------------- */
/* Write decimal representation of v into buf; return pointer past last char. */
static char *_u32_to_str(char *p, uint32_t v)
{
    if (v == 0) { *p++ = '0'; return p; }
    char tmp[11]; int n = 0;
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    char *s = p + n - 1;
    p += n;
    while (n--) *s-- = tmp[n+1-1+1-1];   /* reverse */
    /* simpler: */
    return p;
}
/* Cleaner reversal helper */
static char *_u32w(char *p, uint32_t v)
{
    if (v == 0) { *p++ = '0'; return p; }
    char *start = p;
    while (v) { *p++ = (char)('0' + v % 10); v /= 10; }
    char *end = p - 1, *s = start;
    while (s < end) { char t = *s; *s++ = *end; *end-- = t; }
    return p;
}

void rrf_send(const char *gcode)
{
    /* Build  "N<n> <gcode>"  into s_tx_buf — no printf, no library bloat. */
    char *p = s_tx_buf;
    *p++ = 'N';
    p = _u32w(p, s_line_num);
    *p++ = ' ';
    size_t glen = strlen(gcode);
    size_t nlen = (size_t)(p - s_tx_buf);
    if (nlen + glen >= sizeof(s_tx_buf))
        glen = sizeof(s_tx_buf) - nlen - 1;
    memcpy(p, gcode, glen);
    p += glen;
    *p = '\0';

    uint8_t cs = _checksum(s_tx_buf);

    /* Append  *<cs>\n  and transmit — build cs string inline */
    char csbuf[6];
    char *q = csbuf;
    *q++ = '*';
    q = _u32w(q, (uint32_t)cs);
    *q++ = '\n';
    *q = '\0';
    _tx_str(s_tx_buf);
    _tx_str(csbuf);

    s_line_num++;
}

/* -----------------------------------------------------------------------
 * Public: send WITHOUT line-number/checksum
 *
 * Used for M110 N0 (reset sequence) and M555 P2 (mode switch) which
 * must be sent before the numbered sequence begins.
 * --------------------------------------------------------------------- */
void rrf_send_raw(const char *gcode)
{
    /* Send without snprintf %s to keep _printf_s.o out of the link */
    _tx_str(gcode);
    _tx_str("\n");
}

/* -----------------------------------------------------------------------
 * Public: feed one incoming byte into the RRF response parser
 *
 * Assembles bytes into lines terminated by '\n'.
 * On a complete line, checks whether it starts with "ok".
 * Returns RRF_OK / RRF_ERROR / RRF_NONE.
 *
 * Call this from main.c whenever a byte has been identified as coming
 * from RRF (i.e. not a bd_pressure command byte).
 * --------------------------------------------------------------------- */
rrf_result_t rrf_feed_byte(uint8_t b)
{
    if (b == '\r')
        return RRF_NONE;   /* ignore carriage-return */

    if (b == '\n')
    {
        s_rx_buf[s_rx_len] = '\0';
        rrf_result_t result = RRF_NONE;

        if (strncmp(s_rx_buf, "ok", 2) == 0)
        {
            s_ok_count++;
            result = RRF_OK;
        }
        else if (strncmp(s_rx_buf, "Error", 5) == 0)
        {
            result = RRF_ERROR;
        }

        /* Reset buffer for next line */
        s_rx_len = 0;
        memset(s_rx_buf, 0, sizeof(s_rx_buf));
        return result;
    }

    /* Accumulate — drop bytes that would overflow */
    if (s_rx_len < (RRF_RX_LINE_MAX - 1))
        s_rx_buf[s_rx_len++] = (char)b;

    return RRF_NONE;
}

/* -----------------------------------------------------------------------
 * Public: non-blocking ok check / consume
 * --------------------------------------------------------------------- */
bool rrf_ok_pending(void)
{
    return (s_ok_count > 0);
}

void rrf_ok_consume(void)
{
    if (s_ok_count > 0)
        s_ok_count--;
}

/* -----------------------------------------------------------------------
 * Public: blocking wait for "ok" with timeout
 *
 * IMPORTANT: this function does NOT drive the ADC.  The caller (pa_rrf.c)
 * must call GetAD() and store into raw_dat[] inside a wrapper loop so
 * that pa.lib continues to receive data during moves.  See pa_rrf.c for
 * the pattern.
 *
 * Uses tim14_n as a millisecond tick source (TIM14 fires every ~1 ms).
 * --------------------------------------------------------------------- */
rrf_result_t rrf_wait_ok(uint32_t timeout_ms)
{
    uint32_t start = tim14_n;

    while (1)
    {
        if (rrf_ok_pending())
        {
            rrf_ok_consume();
            return RRF_OK;
        }

        /* Check timeout — handle 32-bit wrap */
        uint32_t elapsed = tim14_n - start;
        if (elapsed >= timeout_ms)
            return RRF_TIMEOUT;
    }
}
