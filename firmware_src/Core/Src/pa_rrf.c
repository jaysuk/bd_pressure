/*
 * pa_rrf.c
 *
 * Pressure Advance calibration state machine for RepRapFirmware.
 *
 * Overview
 * --------
 * When triggered (pa_rrf_start()), this module takes full control of the
 * UART/USB link to drive RRF through an extrusion sequence, reads the
 * ADS1220 strain gauge via the existing pa.lib functions, collects PA
 * result samples, picks the best value and applies it with M572.
 *
 * The state machine is non-blocking at the top level: pa_rrf_run() is
 * called on every main-loop iteration.  Inside each state that must wait
 * for an RRF "ok", it calls rrf_wait_ok() which spins — but continues
 * to sample the ADC so pa.lib never starves.
 *
 * Trigger command format (sent by RRF macro over USB)
 * ---------------------------------------------------
 *   l:H<high_mm_min>:L<low_mm_min>:T<travel_mm_min>:S<pa_step>:N<steps>:E<extruder>;\n
 *
 *   All fields are optional; omitted fields use the defaults defined in
 *   pa_rrf_params_default().  Example:
 *     l:H10800:L3000:T24000:S0.002:N50:E0;\n
 *
 *   NOTE: there is no TEMP/nozzle temperature parameter. Heating must be
 *   performed by the RRF macro (M104 + M116) BEFORE sending the trigger.
 *   Once M118 hands the USB channel to the sensor, RRF cannot send heater
 *   commands until calibration completes.
 *
 * RRF macro
 * ---------
 *   See macros/pa_calibrate.g — copy to /sys/pa_calibrate.g on the Duet SD.
 *   ; bd_pressure drives everything once triggered.
 *   ; RRF will receive M572 Dx Sy from the sensor when done.
 */

#include "pa_rrf.h"
#include "rrf_comm.h"
/* Do NOT include ADS1220.h here — its generic macro names (RESET, START, etc.)
   clash with the STM32 CMSIS device header enumerators.  Forward-declare only
   what we actually use from the ADS1220 driver. */
#include "pa.h"
#include "main.h"
#include "iousart.h"

/* Forward declarations from ads1220.c / ADS1220.h */
extern unsigned char PolarFlag;
int GetAD(unsigned char channel, unsigned char continue_mode);

#include <string.h>
#include <stdlib.h>   /* atoi, atof */

/* -----------------------------------------------------------------------
 * External symbols from main.c / pa.lib
 * --------------------------------------------------------------------- */
extern int           raw_dat[];
extern int           r_index;
extern unsigned char pa_result[];
extern unsigned char pa_list;
extern unsigned char PolarFlag;

/* R_CMD is the Receive_D struct in main.c — we need status_clk */
extern unsigned char status_clk_old; /* triggers ADS1220 reinit in main loop */

/* USART2_printf declared in main.c */
extern void USART2_printf(char *fmt, ...);

/* tim14_n millisecond counter */
extern unsigned int tim14_n;

/* PA OSR/ENDSTOP constants from main.c */
#ifndef PA_OSR
#define PA_OSR      7
#endif
#ifndef ENDSTOP_OSR
#define ENDSTOP_OSR 2
#endif

/* Access R_CMD.status_clk without pulling in the full struct definition
   by declaring a minimal extern pointer — actually simpler to just include
   the struct via main.h.  main.h doesn't expose R_CMD externally, so we
   re-declare only what we need. */
typedef struct {
    unsigned char version[16];
    unsigned char measue_data[32];
    unsigned char status_clk;
    unsigned char out_data_mode;
    unsigned char THRHOLD_Z;
    unsigned char range;
    unsigned char set_normal;
    unsigned char invert_data;
} _Receive_D_shadow;
extern _Receive_D_shadow R_CMD;

/* -----------------------------------------------------------------------
 * Lightweight float/integer formatters — avoids pulling _printf_fp.o /
 * fpconst.o from the ARM C library (which would overflow the 32 KB limit).
 * --------------------------------------------------------------------- */

/* Write unsigned decimal into buf; return pointer past last char. */
static char *_u32_s(char *p, uint32_t v)
{
    if (v == 0) { *p++ = '0'; return p; }
    char *start = p;
    while (v) { *p++ = (char)('0' + v % 10); v /= 10; }
    /* reverse in-place */
    char *end = p - 1, *s = start;
    while (s < end) { char t = *s; *s++ = *end; *end-- = t; }
    return p;
}

/* Write float with `prec` decimal places into buf; return pointer past last char. */
static char *_f_s(char *p, float v, uint8_t prec)
{
    if (v < 0.0f) { *p++ = '-'; v = -v; }
    uint32_t ipart = (uint32_t)v;
    float    frac  = v - (float)ipart;
    /* scale fractional part */
    uint32_t scale = 1; uint8_t i = prec;
    while (i--) scale *= 10;
    uint32_t fi = (uint32_t)(frac * (float)scale + 0.5f);
    if (fi >= scale) { ipart++; fi = 0; }
    p = _u32_s(p, ipart);
    if (prec) {
        *p++ = '.';
        /* zero-pad fractional digits */
        uint32_t ts = scale / 10;
        while (ts > fi && ts > 1) { *p++ = '0'; ts /= 10; }
        if (fi > 0) p = _u32_s(p, fi);
        else        *p++ = '0';
    }
    return p;
}

/* -----------------------------------------------------------------------
 * Extrusion ratio (mm filament per mm XY travel) — derived from the
 * Klipper macro values: 0.92643 mm-E over 20 mm-X = 0.046322 mm/mm.
 * Applied to the actual X distances from the params so the E amounts
 * remain correct even if x_start/x_mid_l/x_mid_r/x_end are customised.
 * --------------------------------------------------------------------- */
#define E_PER_MM       0.046322f  /* filament mm per XY mm                 */

/* Prime move: fixed distance (78→158 = 80 mm default) × ratio */
/* Recalculated at runtime from params — see PA_RRF_PRIME state  */

/* Dwell after the prime move (ms) */
#define PRIME_DWELL_MS 4000

/* Maximum PA data points we collect */
#define PA_DATA_MAX    64

/* -----------------------------------------------------------------------
 * Per-sample record
 * --------------------------------------------------------------------- */
typedef struct {
    float   pa_value;
    uint8_t result;     /* pa_result[] value from pa.lib (lower = better) */
} pa_sample_t;

/* -----------------------------------------------------------------------
 * Module state
 * --------------------------------------------------------------------- */
static pa_rrf_state_t  s_state      = PA_RRF_IDLE;
static pa_rrf_params_t s_params;
static pa_rrf_result_t s_result;

static pa_sample_t     s_data[PA_DATA_MAX];
static uint8_t         s_data_count = 0;

static uint8_t         s_step       = 0;   /* current iteration index       */
static uint8_t         s_pa_list_snapshot = 0; /* pa_list at step entry      */

/* USB disconnect watchdog — time of last successful ok receipt */
#define WATCHDOG_MS  30000u
static uint32_t s_last_ok_time = 0;

/* -----------------------------------------------------------------------
 * Defaults
 * --------------------------------------------------------------------- */
void pa_rrf_params_default(pa_rrf_params_t *p)
{
    p->pa_start     = 0.00f;
    p->pa_step      = 0.002f;
    p->pa_steps     = 50;
    p->speed_travel = 24000;
    p->speed_high   = 10800;
    p->speed_low    = 3000;
    p->x_start      = 78.0f;
    p->x_mid_l      = 98.0f;
    p->x_mid_r      = 138.0f;
    p->x_end        = 158.0f;
    p->y_base       = 38.75f;
    p->y_step       = 3.5f;
    p->extruder     = 0;
}

/* -----------------------------------------------------------------------
 * Parameter parser
 *
 * Scans for key:value pairs separated by ':'.
 * Recognised keys (case-sensitive):
 *   H  = speed_high   (integer mm/min)
 *   L  = speed_low    (integer mm/min)
 *   T  = speed_travel (integer mm/min)
 *   S  = pa_step      (float)
 *   N  = pa_steps     (integer)
 *   E  = extruder     (integer)
 *   P  = pa_start     (float, starting PA value — default 0.0)
 * --------------------------------------------------------------------- */
void pa_rrf_parse_params(pa_rrf_params_t *p, const char *str)
{
    if (!str || !*str)
        return;

    const char *s = str;
    while (*s)
    {
        /* Find next ':' */
        const char *colon = strchr(s, ':');
        if (!colon)
            break;

        const char *key   = colon + 1;      /* key starts after ':' */
        const char *val;

        /* Identify key by first character(s) */
        if (*key == 'H') {
            val = key + 1;
            p->speed_high = (uint32_t)atoi(val);
        } else if (*key == 'L') {
            val = key + 1;
            p->speed_low = (uint32_t)atoi(val);
        } else if (*key == 'T') {
            val = key + 1;
            p->speed_travel = (uint32_t)atoi(val);
        } else if (*key == 'S') {
            val = key + 1;
            p->pa_step = (float)atof(val);
        } else if (*key == 'N') {
            val = key + 1;
            p->pa_steps = (uint8_t)atoi(val);
        } else if (*key == 'E') {
            val = key + 1;
            p->extruder = (uint8_t)atoi(val);
        } else if (*key == 'P') {
            val = key + 1;
            p->pa_start = (float)atof(val);
        } else {
            /* Unknown key — log it so the user can spot typos in the trigger */
            USART2_printf("PA_RRF: unknown param key '%c', ignored\n", *key);
        }

        s = colon + 1;  /* advance past this ':' and keep scanning */
    }

    /* --- Range clamps --------------------------------------------------- */
    /* Speeds: must be positive; cap at 60000 mm/min (1000 mm/s) */
    if (p->speed_high   < 100)  p->speed_high   = 100;
    if (p->speed_low    < 100)  p->speed_low    = 100;
    if (p->speed_travel < 100)  p->speed_travel = 100;
    if (p->speed_high   > 60000) p->speed_high  = 60000;
    if (p->speed_low    > 60000) p->speed_low   = 60000;
    if (p->speed_travel > 60000) p->speed_travel = 60000;

    /* PA step: must be positive; cap at 0.1 (very large step already) */
    if (p->pa_step <= 0.0f)  p->pa_step = 0.001f;
    if (p->pa_step >  0.1f)  p->pa_step = 0.1f;

    /* PA start: must be non-negative; cap at 2.0 */
    if (p->pa_start < 0.0f)  p->pa_start = 0.0f;
    if (p->pa_start > 2.0f)  p->pa_start = 2.0f;

    /* Steps: 1..64 */
    if (p->pa_steps < 1)   p->pa_steps = 1;
    if (p->pa_steps > 64)  p->pa_steps = 64;

    /* Extruder index: 0..7 */
    if (p->extruder > 7)   p->extruder = 0;
}

/* -----------------------------------------------------------------------
 * Query / abort
 * --------------------------------------------------------------------- */
pa_rrf_state_t pa_rrf_get_state(void)   { return s_state; }
pa_rrf_result_t pa_rrf_get_result(void) { return s_result; }

void pa_rrf_abort(void)
{
    if (s_state == PA_RRF_IDLE || s_state == PA_RRF_DONE || s_state == PA_RRF_ABORTED)
        return;

    /* Switch bd_pressure back to endstop mode */
    R_CMD.status_clk = ENDSTOP_OSR;

    /* Cancel any in-progress move cleanly (M0 = pause/cancel).
     * M112 (emergency stop) is NOT used here — it halts RRF and requires
     * the user to send M999 to recover.  M0 stops motion without locking
     * the firmware, which is the right behaviour for a user-requested abort. */
    rrf_send_raw("M0");
    USART2_printf("PA_RRF: aborted\n");
    s_state = PA_RRF_ABORTED;
}

/* -----------------------------------------------------------------------
 * Start
 * --------------------------------------------------------------------- */
void pa_rrf_start(const pa_rrf_params_t *p)
{
    if (s_state != PA_RRF_IDLE && s_state != PA_RRF_DONE && s_state != PA_RRF_ABORTED)
    {
        USART2_printf("PA_RRF: already running\n");
        return;
    }

    s_params           = *p;
    s_data_count       = 0;
    s_step             = 0;
    s_pa_list_snapshot = 0;   /* reset so stale value from a previous run is not used */
    s_result.valid     = false;

    memset(s_data, 0, sizeof(s_data));

    /* Switch ADC to PA (low-rate, high-resolution) mode */
    R_CMD.status_clk = PA_OSR;
    /* The mode change is detected in the main loop which calls
       ADS1220_Init(4, 0x34) — we don't call it here to avoid
       re-entrancy with the main loop's ADS1220 usage. */

    /* Reset pa.lib state */
    r_index  = 0;
    pa_list  = 0;
    memset(raw_dat, 0, RAW_DATE_LEN * sizeof(int));
    memset(pa_result, 0, 128);

    rrf_comm_init();
    s_last_ok_time = tim14_n;   /* initialise watchdog baseline */

    s_state = PA_RRF_HANDSHAKE;
    USART2_printf("PA_RRF: starting\n");
}

/* -----------------------------------------------------------------------
 * ADC sample helper
 *
 * Reads one ADC sample and appends to raw_dat[] exactly as the main loop
 * does.  Call this inside any blocking wait so pa.lib stays fed.
 * --------------------------------------------------------------------- */
static void _sample_adc(void)
{
    int tempA = GetAD(4, 1);
    if (PolarFlag == R_CMD.invert_data)
        tempA = -tempA;
    tempA = tempA / 1000 + 6000;

    if (r_index >= RAW_DATE_LEN)
        r_index = 0;
    raw_dat[r_index++] = tempA;
}

/* -----------------------------------------------------------------------
 * Single shared rx-drain index.
 *
 * rxData[] is a 64-byte circular buffer filled by the TIM3 ISR via
 * iousart.c.  re_index is the write pointer.  We track our own read
 * pointer here so every call to _drain_rx() picks up where we left off,
 * regardless of which function (_cmd / _raw_cmd / _dwell_ms) calls it.
 * --------------------------------------------------------------------- */
extern uint8_t rxData[];
extern int     re_index;
#define RXDATA_SIZE 64
static int s_rx_read = 0;   /* our read pointer into rxData[] */

static void _drain_rx(void)
{
    while (s_rx_read != re_index)
    {
        rrf_feed_byte(rxData[s_rx_read]);
        s_rx_read = (s_rx_read + 1) % RXDATA_SIZE;
    }
}

/* -----------------------------------------------------------------------
 * Core blocking wait — samples ADC and drains RX until "ok" or timeout.
 * Used by both _cmd() and _raw_cmd().
 * --------------------------------------------------------------------- */
static bool _wait_ok_with_adc(const char *label, uint32_t timeout_ms)
{
    uint32_t start = tim14_n;
    while (1)
    {
        _sample_adc();
        _drain_rx();

        if (rrf_ok_pending()) {
            rrf_ok_consume();
            s_last_ok_time = tim14_n;   /* reset watchdog */
            return true;
        }

        if ((tim14_n - start) >= timeout_ms) {
            (void)label;
            USART2_printf("PA_RRF: timeout\n");
            return false;
        }

        /* USB disconnect watchdog: if no ok has arrived for WATCHDOG_MS ms
         * since the last successful ok (or since start) abort the run.
         * s_state is set by the caller (all callers do { s_state = ABORTED; return; }
         * on false), so we only need to restore endstop mode here. */
        if ((tim14_n - s_last_ok_time) >= WATCHDOG_MS) {
            USART2_printf("PA_RRF: watchdog — no ok for %u ms, aborting\n",
                          (unsigned)WATCHDOG_MS);
            R_CMD.status_clk = ENDSTOP_OSR;
            return false;
        }
    }
}

/* Send numbered GCode line, wait for "ok". */
static bool _cmd(const char *gcode)
{
    rrf_send(gcode);
    return _wait_ok_with_adc(gcode, RRF_OK_TIMEOUT_MS);
}

/* Send numbered GCode line with an extended timeout — for homing moves
 * (G28) which can take longer than RRF_OK_TIMEOUT_MS on large beds. */
#define RRF_HOME_TIMEOUT_MS  60000u   /* 60 s — enough for any sane homing speed */
static bool _cmd_long(const char *gcode)
{
    rrf_send(gcode);
    return _wait_ok_with_adc(gcode, RRF_HOME_TIMEOUT_MS);
}

/* Send unnumbered GCode line (M555 P2, M110 N0), wait for "ok". */
static bool _raw_cmd(const char *gcode)
{
    rrf_send_raw(gcode);
    return _wait_ok_with_adc(gcode, RRF_OK_TIMEOUT_MS);
}

/* Simple millisecond dwell that keeps sampling the ADC and draining RX */
static void _dwell_ms(uint32_t ms)
{
    uint32_t start = tim14_n;
    while ((tim14_n - start) < ms) {
        _sample_adc();
        _drain_rx();
    }
}

/* -----------------------------------------------------------------------
 * Best-PA selection
 *
 * Mirrors the Klipper logic: from the collected samples, find the one
 * with the lowest result value (pa.lib result encodes residual error —
 * lower is better).  Discard the first quarter of samples as warm-up
 * (minimum 1, maximum 5) and ignore any with result == 0 (no data).
 *
 * Using a proportional warm-up skip means short runs (N=8) still get
 * useful results instead of always requiring >5 samples before evaluating.
 * --------------------------------------------------------------------- */
static bool _pick_best_pa(float *best_pa_out)
{
    if (s_data_count == 0)
        return false;

    /* Warm-up: skip first quarter of samples, at least 1, at most 5 */
    uint8_t skip = s_data_count / 4;
    if (skip < 1) skip = 1;
    if (skip > 5) skip = 5;

    if (s_data_count <= skip)
        return false;

    uint8_t  best_idx   = skip;
    uint8_t  best_score = 255;

    for (uint8_t i = skip; i < s_data_count; i++)
    {
        if (s_data[i].result == 0)
            continue;
        if (s_data[i].result < best_score) {
            best_score = s_data[i].result;
            best_idx   = i;
        }
    }

    if (best_score == 255)
        return false;   /* all zeros — no valid data */

    *best_pa_out = s_data[best_idx].pa_value;
    return true;
}

/* -----------------------------------------------------------------------
 * Main state machine — called every main loop iteration
 * --------------------------------------------------------------------- */
void pa_rrf_run(void)
{
    char buf[RRF_TX_LINE_MAX];

    switch (s_state)
    {
    /* ------------------------------------------------------------------ */
    case PA_RRF_IDLE:
    case PA_RRF_DONE:
    case PA_RRF_ABORTED:
        return;

    /* ------------------------------------------------------------------ */
    case PA_RRF_HANDSHAKE:
        /*
         * Step 1: Send M555 P2 WITHOUT a line number so that RRF enters
         *         Marlin-emulation mode and starts sending "ok" responses
         *         on THIS channel.  Must be sent raw (unnumbered) because
         *         RRF hasn't been told to expect numbered lines yet.
         *
         * Step 2: Reset RRF's line counter with M110 N0 (also unnumbered).
         *
         * Step 3: Reset our own line counter to match.
         */
        USART2_printf("PA_RRF: handshake\n");

        if (!_raw_cmd("M555 P2"))    { s_state = PA_RRF_ABORTED; return; }
        if (!_raw_cmd("M110 N0"))    { s_state = PA_RRF_ABORTED; return; }
        rrf_reset_line_counter();

        /* Nozzle must already be at temperature before this point.
           Heating is the macro's responsibility (M109/M116 before M118). */
        s_state = PA_RRF_INIT_MOVES;
        break;

    /* ------------------------------------------------------------------ */
    case PA_RRF_INIT_MOVES:
        USART2_printf("PA_RRF: init moves\n");

        /* Relative extrusion, absolute XY */
        if (!_cmd("G90"))            { s_state = PA_RRF_ABORTED; return; }
        if (!_cmd("M83"))            { s_state = PA_RRF_ABORTED; return; }

        /* Set initial PA to a known value */
        { char *p = buf; memcpy(p,"M572 D",6); p+=6; p=_u32_s(p,(uint32_t)s_params.extruder); memcpy(p," S",2); p+=2; p=_f_s(p,s_params.pa_start,4); *p='\0'; }
        if (!_cmd(buf))              { s_state = PA_RRF_ABORTED; return; }

        /* Prime: push 4 mm at low speed to seat filament */
        if (!_cmd("G1 E4 F300"))     { s_state = PA_RRF_ABORTED; return; }

        s_state = PA_RRF_PRIME;
        break;

    /* ------------------------------------------------------------------ */
    case PA_RRF_PRIME:
    {
        USART2_printf("PA_RRF: prime move\n");

        /* Calculate extrusion amounts from actual X distances.
         * E_PER_MM is the filament-mm per XY-mm ratio derived from the
         * Klipper macro reference values (0.046322 mm/mm).  Using the
         * actual params means E stays correct if X coordinates are customised. */
        float e_prime   = (s_params.x_end - s_params.x_start) * E_PER_MM;

        /* Fast move to start position while extruding prime bead */
        { char *p = buf; memcpy(p,"G1 X",4); p+=4; p=_f_s(p,s_params.x_start,2); memcpy(p," Y",2); p+=2; p=_f_s(p,s_params.y_base,2); memcpy(p," F",2); p+=2; p=_u32_s(p,(uint32_t)s_params.speed_high); memcpy(p," E",2); p+=2; p=_f_s(p,e_prime,5); *p='\0'; }
        if (!_cmd(buf))              { s_state = PA_RRF_ABORTED; return; }

        /* Drain move buffer, then dwell to let sensor stabilise */
        if (!_cmd("M400"))           { s_state = PA_RRF_ABORTED; return; }
        _dwell_ms(PRIME_DWELL_MS);

        s_step  = 0;
        s_state = PA_RRF_STEP;
        break;
    }

    /* ------------------------------------------------------------------ */
    case PA_RRF_STEP:
    {
        if (s_step >= s_params.pa_steps) {
            s_state = PA_RRF_FINISH;
            break;
        }

        /* Calculate extrusion amounts from actual X distances */
        float e_low  = (s_params.x_mid_l - s_params.x_start) * E_PER_MM;
        float e_high = (s_params.x_mid_r - s_params.x_mid_l) * E_PER_MM;
        float e_low2 = (s_params.x_end   - s_params.x_mid_r) * E_PER_MM;

        float pa_val = s_params.pa_start + s_step * s_params.pa_step;
        float y_pos  = s_params.y_base   + s_step * s_params.y_step;

        /* Set PA for this step */
        { char *p = buf; memcpy(p,"M572 D",6); p+=6; p=_u32_s(p,(uint32_t)s_params.extruder); memcpy(p," S",2); p+=2; p=_f_s(p,pa_val,4); *p='\0'; }
        if (!_cmd(buf))              { s_state = PA_RRF_ABORTED; return; }

        /* Travel to line start */
        { char *p = buf; memcpy(p,"G1 X",4); p+=4; p=_f_s(p,s_params.x_start,2); memcpy(p," Y",2); p+=2; p=_f_s(p,y_pos,2); memcpy(p," F",2); p+=2; p=_u32_s(p,(uint32_t)s_params.speed_travel); *p='\0'; }
        if (!_cmd(buf))              { s_state = PA_RRF_ABORTED; return; }

        /* Low-speed segment: x_start → x_mid_l */
        { char *p = buf; memcpy(p,"G1 X",4); p+=4; p=_f_s(p,s_params.x_mid_l,2); memcpy(p," Y",2); p+=2; p=_f_s(p,y_pos,2); memcpy(p," F",2); p+=2; p=_u32_s(p,(uint32_t)s_params.speed_low); memcpy(p," E",2); p+=2; p=_f_s(p,e_low,5); *p='\0'; }
        if (!_cmd(buf))              { s_state = PA_RRF_ABORTED; return; }

        /* High-speed segment: x_mid_l → x_mid_r */
        { char *p = buf; memcpy(p,"G1 X",4); p+=4; p=_f_s(p,s_params.x_mid_r,2); memcpy(p," Y",2); p+=2; p=_f_s(p,y_pos,2); memcpy(p," F",2); p+=2; p=_u32_s(p,(uint32_t)s_params.speed_high); memcpy(p," E",2); p+=2; p=_f_s(p,e_high,5); *p='\0'; }
        if (!_cmd(buf))              { s_state = PA_RRF_ABORTED; return; }

        /* Low-speed segment: x_mid_r → x_end */
        { char *p = buf; memcpy(p,"G1 X",4); p+=4; p=_f_s(p,s_params.x_end,2); memcpy(p," Y",2); p+=2; p=_f_s(p,y_pos,2); memcpy(p," F",2); p+=2; p=_u32_s(p,(uint32_t)s_params.speed_low); memcpy(p," E",2); p+=2; p=_f_s(p,e_low2,5); *p='\0'; }
        if (!_cmd(buf))              { s_state = PA_RRF_ABORTED; return; }

        /* Drain move buffer — after this the sensor data is valid */
        if (!_cmd("M400"))           { s_state = PA_RRF_ABORTED; return; }

        /*
         * Collect pa.lib result.
         * pa_list is incremented by Pressure_advance() (called from
         * main loop) each time a new result is produced.  We snapshot
         * it before the move and check after.
         */
        if (pa_list > s_pa_list_snapshot && s_data_count < PA_DATA_MAX)
        {
            s_data[s_data_count].pa_value = pa_val;
            s_data[s_data_count].result   = pa_result[pa_list - 1];
            s_data_count++;
        }
        s_pa_list_snapshot = pa_list;

        s_step++;

        /* Bail out if we have hit the data cap */
        if (s_data_count >= PA_DATA_MAX)
        {
            USART2_printf("PA_RRF: data cap reached at step %u\n", s_step);
            s_state = PA_RRF_FINISH;
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case PA_RRF_FINISH:
    {
        USART2_printf("PA_RRF: finishing, %u samples\n", s_data_count);

        float best_pa = s_params.pa_start;
        if (_pick_best_pa(&best_pa))
        {
            /* Apply the best PA value */
            { char *p = buf; memcpy(p,"M572 D",6); p+=6; p=_u32_s(p,(uint32_t)s_params.extruder); memcpy(p," S",2); p+=2; p=_f_s(p,best_pa,4); *p='\0'; }
            _cmd(buf);   /* best-effort — don't abort if this fails */

            s_result.best_pa      = best_pa;
            s_result.sample_count = s_data_count;
            s_result.valid        = true;

            USART2_printf("PA_RRF: best PA=%.4f (%u samples)\n",
                          (double)best_pa, s_data_count);
        }
        else
        {
            USART2_printf("PA_RRF: no valid result\n");
            s_result.valid = false;
        }

        /* Home X and Y to clean up toolhead position.
         * Use the extended timeout — homing can take longer than
         * RRF_OK_TIMEOUT_MS on large beds or slow homing speeds. */
        _cmd_long("G28 X Y");   /* best-effort */

        /* Switch bd_pressure back to endstop/probe mode */
        R_CMD.status_clk = ENDSTOP_OSR;

        if (s_result.valid) {
            /*
             * 1. Log to the DWC/Duet console (P2 = HTTP message channel,
             *    visible in DWC and on PanelDue).
             */
            { char *p = buf;
              const char *pre = "M118 P2 S\"bd_pressure: PA calibration done. Best value: M572 D";
              size_t l = strlen(pre); memcpy(p, pre, l); p += l;
              p = _u32_s(p, (uint32_t)s_params.extruder);
              memcpy(p, " S", 2); p += 2; p = _f_s(p, best_pa, 4);
              memcpy(p, "  Add this to your config.g\"", 28); p += 28; *p = '\0'; }
            _cmd(buf);

            /*
             * 2. Pop up a blocking dialog in DWC so the user cannot miss it.
             *    M291 P"<message>" R"<title>" S2 — S2 = OK button, no timeout.
             *    The user must press OK to dismiss it.
             */
            { char *p = buf;
              memcpy(p, "M291 P\"Calibration complete!\\nBest Pressure Advance: ", 52); p += 52;
              p = _f_s(p, best_pa, 4);
              memcpy(p, "\\n\\nAdd to config.g:\\nM572 D", 25); p += 25;
              p = _u32_s(p, (uint32_t)s_params.extruder);
              memcpy(p, " S", 2); p += 2; p = _f_s(p, best_pa, 4);
              memcpy(p, "\" R\"bd_pressure PA Result\" S2", 29); p += 29; *p = '\0'; }
            _cmd(buf);

            /*
             * 3. Write the result to a file on the SD card so it persists
             *    across power cycles and is easy to find.
             *    /sys/pa_result.g will contain a ready-to-use M572 line
             *    the user can copy into config.g.
             *
             *    RRF file-write sequence:
             *      M28 <path>  — open file for writing (creates or overwrites)
             *      <lines>     — written verbatim to the file
             *      M29         — close file
             */
            _cmd("M28 /sys/pa_result.g");
            { char *p = buf; memcpy(p,"M572 D",6); p+=6; p=_u32_s(p,(uint32_t)s_params.extruder); memcpy(p," S",2); p+=2; p=_f_s(p,best_pa,4); memcpy(p," ; bd_pressure PA calibration result",36); p+=36; *p='\0'; }
            _cmd(buf);
            _cmd("M29");
        }
        else
        {
            /* Notify failure visibly too */
            _cmd("M118 P2 S\"bd_pressure: PA calibration failed — no valid result collected\"");
            _cmd("M291 P\"PA calibration did not produce a valid result.\\n"
                 "Check that filament is loaded and the nozzle is at print temperature.\" "
                 "R\"bd_pressure\" S2");
        }

        s_state = PA_RRF_DONE;
        break;
    }

    /* ------------------------------------------------------------------ */
    default:
        s_state = PA_RRF_ABORTED;
        break;
    }
}
