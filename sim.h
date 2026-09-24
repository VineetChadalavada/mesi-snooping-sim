/* ==========================================================================
 * sim.h  --  the whole machine: 4 caches + the MemBus + 1 MB of memory,
 *            plus the code that walks a processor request through the
 *            MESI protocol.
 *
 * The processors themselves are NOT simulated (the assignment says they do
 * not have to be).  All we do is inject Read and Write requests into the
 * cache controllers.
 * ========================================================================== */
#ifndef SIM_H
#define SIM_H

#include "cache.h"
#include "bus.h"
#include "memory.h"
#include <string>
#include <vector>
#include <stdio.h>

enum { OP_READ = 0, OP_WRITE = 1 };

struct Request {
    int      cpu;
    int      op;
    unsigned addr;
    unsigned value;        /* only meaningful for a write */
};

/* what happened to one request, kept so the dashboard can show it */
struct ReqResult {
    int      used;
    Request  req;
    char     outcome[32];      /* READ HIT / WRITE MISS / UPGRADE ...      */
    int      bus_cmd;
    int      data_source;
    int      supplier;
    int      from_state;
    int      to_state;
    int      latency;
    int      replanned;        /* lost arbitration and had to change cmd   */
    int      evicted_state;    /* state of the victim, -1 if no eviction   */
    unsigned evicted_addr;
    unsigned value_seen;       /* value a read returned                    */
    int      value_bad;        /* set if it did not match the golden copy  */
};

#define LOG_KEEP   400
#define TRACK_MAX  16

struct Sim {
    Cache        cache[NUM_CPUS];
    MainMemory   mem;
    MemBus       bus;
    long         cycle;

    /* reference copy of memory: what every location SHOULD hold.  Used to
     * check that coherence is actually working. */
    unsigned int *golden;
    long          checks;
    long          check_fails;

    /* scratch buffer for a block in flight on the bus */
    unsigned int  xfer[WORDS_PER_BLOCK];
    int           xfer_valid;

    /* presentation state */
    long          step_no;
    ReqResult     result[NUM_CPUS];
    int           result_count;
    std::vector<std::string> log;
    std::vector<unsigned>    tracked;     /* block addresses seen so far   */
    std::string   headline;
    FILE         *logfile;
};

void sim_init (Sim *s);
void sim_free (Sim *s);
void sim_reset(Sim *s);

/* Issue 1..n requests that all arrive in the SAME cycle.  With n > 1 the
 * bus arbiter has to pick an order, which is exactly what we want to show. */
void sim_step(Sim *s, const Request *reqs, int n);

void sim_log(Sim *s, const char *fmt, ...);

/* screens */
void sim_render(Sim *s);                 /* the live dashboard             */
void sim_show_config(Sim *s);
void sim_show_coverage(Sim *s);
void sim_show_stats(Sim *s);
void sim_show_diagrams(void);
void sim_dump_cache(Sim *s, int cpu);

/* helpers shared with the shell / test driver */
unsigned sim_victim_addr(unsigned tag, unsigned set);
void     sim_track(Sim *s, unsigned addr);

#endif /* SIM_H */
