#include "bus.h"
#include <string.h>

const char *bus_cmd_name(int cmd)
{
    switch (cmd) {
        case BUS_RD:   return "BusRd";
        case BUS_RDX:  return "BusRdX";
        case BUS_UPGR: return "BusUpgr";
        case BUS_WB:   return "BusWB";
        default:       return "-";
    }
}

/* ------------------------------------------------------------------ */
/*  the arbiter                                                        */
/* ------------------------------------------------------------------ */

void arb_reset(Arbiter *a)
{
    for (int i = 0; i < NUM_CPUS; i++) {
        a->breq[i]   = 0;
        a->grants[i] = 0;
        a->denied[i] = 0;
    }
    a->rr_ptr       = 0;
    a->owner        = -1;
    a->arbitrations = 0;
    a->contended    = 0;
}

void arb_request(Arbiter *a, int cpu)
{
    a->breq[cpu] = 1;
}

int arb_pending(Arbiter *a)
{
    for (int i = 0; i < NUM_CPUS; i++)
        if (a->breq[i])
            return 1;
    return 0;
}

/* Round-robin scan starting at rr_ptr.  Returns the winning master, or -1
 * if nobody is asking for the bus. */
int arb_arbitrate(Arbiter *a)
{
    int i, cpu, winner = -1;
    int num_requesting = 0;

    for (i = 0; i < NUM_CPUS; i++)
        if (a->breq[i])
            num_requesting++;

    if (num_requesting == 0)
        return -1;

    a->arbitrations++;
    if (num_requesting > 1)
        a->contended++;

    for (i = 0; i < NUM_CPUS; i++) {
        cpu = (a->rr_ptr + i) % NUM_CPUS;
        if (a->breq[cpu]) {
            winner = cpu;
            break;
        }
    }

    /* everybody else who asked this round gets told to wait */
    for (i = 0; i < NUM_CPUS; i++)
        if (a->breq[i] && i != winner)
            a->denied[i]++;

    a->breq[winner] = 0;             /* BREQ drops once BGNT goes out      */
    a->owner        = winner;
    a->grants[winner]++;
    a->rr_ptr       = (winner + 1) % NUM_CPUS;   /* winner -> lowest prio   */
    return winner;
}

void arb_release(Arbiter *a)
{
    a->owner = -1;
}

/* ------------------------------------------------------------------ */
/*  the bus itself                                                     */
/* ------------------------------------------------------------------ */

void bus_reset(MemBus *b)
{
    arb_reset(&b->arb);
    bus_clear_txn(b);
    b->busy_cycles = 0;
    for (int i = 0; i < 5; i++)
        b->txn_count[i] = 0;
}

void bus_clear_txn(MemBus *b)
{
    b->cmd              = BUS_NONE;
    b->addr             = 0;
    b->requester        = -1;
    b->shared_line      = 0;
    b->data_source      = SRC_NONE;
    b->supplier         = -1;
    b->caused_writeback = 0;
    b->tenure           = 0;
    for (int i = 0; i < NUM_CPUS; i++)
        strcpy(b->snoop_note[i], "-");
}
