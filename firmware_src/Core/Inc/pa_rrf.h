#ifndef __PA_RRF_H__
#define __PA_RRF_H__

#include <stdint.h>
#include <stdbool.h>

/* -----------------------------------------------------------------------
 * Parameters for a PA calibration run.
 * Populated by parse_pa_params() from the "l:" trigger command, or
 * filled with defaults by pa_rrf_params_default().
 *
 * Speed units: mm/min (RRF F-word convention).
 * PA step:     added each iteration, so total range = pa_step * pa_steps.
 * --------------------------------------------------------------------- */
typedef struct {
    float    pa_start;          /* starting PA value            (default 0.00) */
    float    pa_step;           /* PA increment per iteration   (default 0.002)*/
    uint8_t  pa_steps;          /* number of iterations         (default 50)   */
    uint32_t speed_travel;      /* travel speed  mm/min         (default 24000)*/
    uint32_t speed_high;        /* fast extrude  mm/min         (default 10800)*/
    uint32_t speed_low;         /* slow extrude  mm/min         (default 3000) */
    /* NOTE: nozzle heating is NOT handled by the firmware.
       Heat the nozzle with M109/M116 in the RRF macro BEFORE sending the
       trigger command.  By the time the sensor takes over the USB channel
       the nozzle must already be at temperature. */
    float    x_start;           /* left travel  start           (default  78)  */
    float    x_mid_l;           /* low→high transition          (default  98)  */
    float    x_mid_r;           /* high→low transition          (default 138)  */
    float    x_end;             /* right end of line            (default 158)  */
    float    y_base;            /* Y of first line              (default 38.75)*/
    float    y_step;            /* Y increment per iteration    (default  3.5) */
    uint8_t  extruder;          /* extruder index for M572      (default 0)    */
} pa_rrf_params_t;

/* -----------------------------------------------------------------------
 * Calibration result (available after state machine reaches DONE)
 * --------------------------------------------------------------------- */
typedef struct {
    float    best_pa;           /* the PA value to apply        */
    uint8_t  sample_count;      /* how many data points were collected */
    bool     valid;             /* true if result is usable     */
} pa_rrf_result_t;

/* -----------------------------------------------------------------------
 * State machine states (exposed so main.c can log/report if desired)
 * --------------------------------------------------------------------- */
typedef enum {
    PA_RRF_IDLE = 0,
    PA_RRF_HANDSHAKE,      /* M555 P2, M110 N0                        */
    PA_RRF_INIT_MOVES,     /* M83, G90, M572, prime extrude           */
    PA_RRF_PRIME,          /* first fast move + dwell                 */
    PA_RRF_STEP,           /* the 50-iteration measurement loop       */
    PA_RRF_FINISH,         /* apply result, home, restore mode        */
    PA_RRF_DONE,           /* completed — result is valid             */
    PA_RRF_ABORTED         /* error / timeout — safe state            */
} pa_rrf_state_t;

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

/* Fill params with sensible defaults. */
void pa_rrf_params_default(pa_rrf_params_t *p);

/* Parse parameter string from the "l:" trigger command.
 * Format: l:H<high>:L<low>:T<travel>:S<pa_step>:N<steps>:TEMP<temp>:E<extruder>;\n
 * Any field that is absent keeps the default value already in *p.
 * Call pa_rrf_params_default(p) first, then this function. */
void pa_rrf_parse_params(pa_rrf_params_t *p, const char *param_str);

/* Start a calibration run with the given parameters.
 * Has no effect if a run is already in progress. */
void pa_rrf_start(const pa_rrf_params_t *p);

/* Abort an in-progress run and return to endstop mode. */
void pa_rrf_abort(void);

/* Run one iteration of the state machine.
 * Call this from the main loop on every pass INSTEAD of (or as well as)
 * the normal Pressure_advance() call — pa_rrf internally calls GetAD()
 * and feeds raw_dat[] while it is active so pa.lib keeps receiving data.
 * When the machine is IDLE this function returns immediately. */
void pa_rrf_run(void);

/* Query current state. */
pa_rrf_state_t pa_rrf_get_state(void);

/* Retrieve result (only valid when state == PA_RRF_DONE). */
pa_rrf_result_t pa_rrf_get_result(void);

#endif /* __PA_RRF_H__ */
