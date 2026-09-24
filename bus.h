/* ==========================================================================
 * bus.h  --  the shared MemBus and its Bus Arbiter.
 *
 * ---------------------------------------------------------------------------
 * BUS ARBITER DESIGN  (this is one of the deliverables, so it is written out
 * here in full and repeated in README.md)
 * ---------------------------------------------------------------------------
 *
 * Topology
 *     Centralised, synchronous arbiter.  Every cache controller has its own
 *     private pair of wires to the arbiter:
 *
 *         BREQ[i]  cache i  -> arbiter    "I want the bus"
 *         BGNT[i]  arbiter  -> cache i    "the bus is yours"
 *
 *     A dedicated pair per master (rather than a shared daisy chain) is what
 *     lets the arbiter see all four requests at once and be fair.
 *
 * Policy: round robin / rotating priority
 *     The arbiter keeps a pointer rr_ptr.  When it arbitrates it walks
 *         rr_ptr, rr_ptr+1, ... (mod 4)
 *     and grants the first master whose BREQ is asserted.  Straight after a
 *     grant to master g, rr_ptr becomes (g+1) mod 4, so the winner drops to
 *     lowest priority.
 *
 *     Why round robin: a fixed-priority arbiter would let P0 starve P3 in a
 *     tight loop.  With rotating priority a requester can be passed over at
 *     most NUM_CPUS-1 times, so the worst case wait is bounded at 3 bus
 *     tenures.  It costs almost nothing in hardware (a 2-bit counter plus a
 *     4-bit priority-encoded scan) and needs no per-master state.
 *
 * Timing
 *     T_ARB = 1 cycle    to make the decision and drive BGNT.
 *     The bus is ATOMIC: the winner keeps it for the whole transaction
 *     (address phase -> snoop phase -> data phase), then drops BREQ and the
 *     arbiter is free again.  Holding the bus for the whole transaction is
 *     what makes the snooping protocol correct without extra transient
 *     states: no second transaction for the same block can slip in between
 *     our snoop and our data.
 *
 * Bus lines we model
 *     ADDR[19:0]   block address of the transaction
 *     CMD          BusRd | BusRdX | BusUpgr | BusWB
 *     DATA         one 64-byte block
 *     SHARED       wired-OR, pulled low by ANY snooper that still has a
 *                  valid copy.  This is the line that decides whether a read
 *                  miss ends in E (nobody else has it) or in S.
 * ========================================================================== */
#ifndef BUS_H
#define BUS_H

#include "config.h"

/* bus commands */
enum {
    BUS_NONE = 0,
    BUS_RD,          /* read miss: give me the block, I want to share it   */
    BUS_RDX,         /* write miss: give me the block AND kill every copy  */
    BUS_UPGR,        /* I already have it in S, just kill the other copies */
    BUS_WB           /* write back a dirty victim                          */
};

/* where the data for the last transaction came from */
enum {
    SRC_NONE = 0,
    SRC_MEMORY,
    SRC_CACHE
};

struct Arbiter {
    int  breq[NUM_CPUS];        /* request lines, 1 = asserted             */
    int  rr_ptr;                /* next master to consider                 */
    int  owner;                 /* who holds the bus now, -1 = idle        */
    long grants[NUM_CPUS];      /* how many tenures each master won        */
    long denied[NUM_CPUS];      /* how many times each was passed over     */
    long arbitrations;          /* how many times we had to decide         */
    long contended;             /* arbitrations with more than one BREQ    */
};

struct MemBus {
    Arbiter arb;

    /* snapshot of the most recent transaction, for the dashboard */
    int      cmd;
    unsigned addr;
    int      requester;
    int      shared_line;       /* was SHARED asserted by anybody?         */
    int      data_source;       /* SRC_MEMORY or SRC_CACHE                 */
    int      supplier;          /* which cache flushed, -1 if none         */
    int      caused_writeback;
    int      tenure;            /* cycles the winner held the bus          */
    char     snoop_note[NUM_CPUS][40];   /* what each snooper did          */

    /* statistics */
    long busy_cycles;
    long txn_count[5];          /* indexed by the BUS_* command            */
};

const char *bus_cmd_name(int cmd);

void arb_reset   (Arbiter *a);
void arb_request (Arbiter *a, int cpu);
int  arb_pending (Arbiter *a);          /* any BREQ asserted?              */
int  arb_arbitrate(Arbiter *a);         /* round robin, returns winner     */
void arb_release (Arbiter *a);

void bus_reset(MemBus *b);
void bus_clear_txn(MemBus *b);

#endif /* BUS_H */
