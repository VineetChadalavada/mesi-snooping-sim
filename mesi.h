/* ==========================================================================
 * mesi.h
 *
 * The MESI states, and the list of every arc in the two state diagrams from
 * the lecture notes ("MESI - locally initiated accesses" and "MESI -
 * remotely initiated accesses").  Every time the simulator takes an arc it
 * calls cov_hit(), so at the end of a run we can print a table proving that
 * all of them were exercised.
 * ========================================================================== */
#ifndef MESI_H
#define MESI_H

/* the four states */
enum {
    ST_I = 0,      /* Invalid                                               */
    ST_S = 1,      /* Shared      - clean, other caches may have it too     */
    ST_E = 2,      /* Exclusive   - clean, this is the only cached copy     */
    ST_M = 3       /* Modified    - dirty, this is the only cached copy     */
};

/* every arc of the two diagrams */
enum {
    /* ---- diagram 1 : locally initiated (processor) accesses ---- */
    TR_I_PRRD_E = 0,
    TR_I_PRRD_S,
    TR_I_PRWR_M,
    TR_S_PRRD_S,
    TR_S_PRWR_M,
    TR_S_REPL_I,
    TR_E_PRRD_E,
    TR_E_PRWR_M,
    TR_E_REPL_I,
    TR_M_PRRD_M,
    TR_M_PRWR_M,
    TR_M_REPL_I,
    /* ---- diagram 2 : remotely initiated (snooped) accesses ---- */
    TR_S_BUSRD_S,
    TR_S_BUSRDX_I,
    TR_S_BUSUPGR_I,
    TR_E_BUSRD_S,
    TR_E_BUSRDX_I,
    TR_M_BUSRD_S,
    TR_M_BUSRDX_I,
    TR_I_SNOOP_I,

    NUM_TRANSITIONS
};

#define DIAG_LOCAL   0
#define DIAG_REMOTE  1

struct TransInfo {
    const char *tag;       /* short name, e.g. "M --BusRd--> S"             */
    int         diagram;   /* DIAG_LOCAL or DIAG_REMOTE                     */
    const char *from;
    const char *event;
    const char *action;
    const char *to;
};

extern const TransInfo g_trans[NUM_TRANSITIONS];

const char *state_name(int st);   /* "M" / "E" / "S" / "I"                  */
const char *state_long(int st);   /* "Modified" ...                         */

/* coverage bookkeeping */
void cov_reset(void);
void cov_hit(int transition);
long cov_count(int transition);
int  cov_covered(int diagram);    /* how many arcs of that diagram were hit */
int  cov_total(int diagram);      /* how many arcs that diagram has         */

#endif /* MESI_H */
