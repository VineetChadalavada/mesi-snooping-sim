/* ==========================================================================
 * sim.cpp  --  the MESI snooping protocol itself.
 *
 * One "step" is a set of 1..4 processor requests that all arrive in the same
 * cycle.  A step runs like this:
 *
 *   1. every cache does its own tag lookup, in parallel            (T_CACHE)
 *   2. the ones that hit are finished, nothing goes on the bus
 *   3. the ones that miss (or need an upgrade) assert BREQ
 *   4. the arbiter grants the bus to one of them                     (T_ARB)
 *   5. that master runs its transaction to completion               (tenure)
 *   6. back to 4 until nobody is asking for the bus any more
 *
 * Step 6 is where the interesting bug lives, and we handle it: while a
 * master sits waiting for the bus, somebody else's transaction can change
 * its state.  A cache that wanted a BusUpgr (it had the block in S) may find
 * itself in I by the time it is granted, and must issue a BusRdX instead.
 * That re-plan is flagged in the dashboard.
 * ========================================================================== */
#include "sim.h"
#include "mesi.h"
#include "ui.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  setup / teardown                                                   */
/* ------------------------------------------------------------------ */

void sim_init(Sim *s)
{
    for (int i = 0; i < NUM_CPUS; i++)
        cache_init(&s->cache[i], i);
    mem_init(&s->mem);
    s->golden = (unsigned int *)malloc(sizeof(unsigned int) * MEM_WORDS);
    s->logfile = fopen("run_log.txt", "w");
    sim_reset(s);
}

void sim_free(Sim *s)
{
    mem_free(&s->mem);
    free(s->golden);
    if (s->logfile) {
        fclose(s->logfile);
        s->logfile = 0;
    }
}

void sim_reset(Sim *s)
{
    for (int i = 0; i < NUM_CPUS; i++)
        cache_reset(&s->cache[i]);
    mem_reset(&s->mem);
    bus_reset(&s->bus);
    cov_reset();

    for (int i = 0; i < MEM_WORDS; i++)
        s->golden[i] = s->mem.word[i];

    s->cycle        = 0;
    s->checks       = 0;
    s->check_fails  = 0;
    s->xfer_valid   = 0;
    s->step_no      = 0;
    s->result_count = 0;
    s->log.clear();
    s->tracked.clear();
    s->headline = "idle";
    for (int i = 0; i < NUM_CPUS; i++)
        s->result[i].used = 0;
}

/* ------------------------------------------------------------------ */
/*  logging                                                            */
/* ------------------------------------------------------------------ */

void sim_log(Sim *s, const char *fmt, ...)
{
    char body[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    char line[600];
    snprintf(line, sizeof(line), "[%05ld] %s", s->cycle, body);

    s->log.push_back(std::string(line));
    if ((int)s->log.size() > LOG_KEEP)
        s->log.erase(s->log.begin());

    if (s->logfile) {
        fprintf(s->logfile, "%s\n", line);
        fflush(s->logfile);
    }
}

void sim_track(Sim *s, unsigned addr)
{
    unsigned blk = addr_block(addr);
    for (size_t i = 0; i < s->tracked.size(); i++)
        if (s->tracked[i] == blk)
            return;
    s->tracked.push_back(blk);
    if ((int)s->tracked.size() > TRACK_MAX)
        s->tracked.erase(s->tracked.begin());
}

unsigned sim_victim_addr(unsigned tag, unsigned set)
{
    return (tag << (INDEX_BITS + OFFSET_BITS)) | (set << OFFSET_BITS);
}

/* ------------------------------------------------------------------ */
/*  data checking                                                      */
/* ------------------------------------------------------------------ */

static void check_read(Sim *s, int cpu, unsigned addr, unsigned got, ReqResult *res)
{
    unsigned want = s->golden[addr >> 2];
    s->checks++;
    res->value_seen = got;
    if (got != want) {
        s->check_fails++;
        res->value_bad = 1;
        sim_log(s, "!! COHERENCE VIOLATION  P%d read %08X from 0x%06X, expected %08X",
                cpu, got, addr, want);
    }
}

static void do_write(Sim *s, Cache *c, CacheLine *l, unsigned addr, unsigned value)
{
    l->data[addr_word(addr)] = value;
    s->golden[addr >> 2]     = value;
    (void)c;
}

/* ------------------------------------------------------------------ */
/*  diagram 2 : remotely initiated accesses (the snoop)                */
/* ------------------------------------------------------------------ */

static void snoop_all(Sim *s, int requester, int cmd, unsigned addr)
{
    MemBus  *b   = &s->bus;
    unsigned set = addr_set(addr);
    unsigned tag = addr_tag(addr);

    b->shared_line = 0;
    b->supplier    = -1;
    s->xfer_valid  = 0;

    for (int i = 0; i < NUM_CPUS; i++) {
        if (i == requester)
            continue;

        Cache *c   = &s->cache[i];
        int    way = cache_find(c, addr);

        if (way < 0) {
            /* If the frame is here but Invalid, that is a real
             * "I --snoop--> I" arc of the remote diagram: we see the
             * transaction, we do nothing. */
            for (int w = 0; w < NUM_WAYS; w++) {
                if (c->line[set][w].tag == tag && c->line[set][w].state == ST_I) {
                    cov_hit(TR_I_SNOOP_I);
                    strcpy(b->snoop_note[i], "I (no action)");
                }
            }
            continue;
        }

        CacheLine *l  = &c->line[set][way];
        int        st = l->state;

        /* SHARED is a wired-OR line: anybody holding a valid copy pulls it.
         * The requester only consults it on a BusRd, to decide E vs S. */
        b->shared_line = 1;

        if (cmd == BUS_RD) {
            if (st == ST_M) {
                cov_hit(TR_M_BUSRD_S);
                for (int k = 0; k < WORDS_PER_BLOCK; k++)
                    s->xfer[k] = l->data[k];
                s->xfer_valid = 1;
                /* Flush = hand the block to the requester AND bring memory
                 * up to date, so the block can be clean-shared afterwards */
                mem_write_block(&s->mem, addr, l->data);
                b->supplier         = i;
                b->caused_writeback = 1;
                c->flushes++;
                l->state = ST_S;
                strcpy(b->snoop_note[i], "M->S  Flush");
            } else if (st == ST_E) {
                cov_hit(TR_E_BUSRD_S);
                l->state = ST_S;
                strcpy(b->snoop_note[i], "E->S  SHARED");
            } else {
                cov_hit(TR_S_BUSRD_S);
                strcpy(b->snoop_note[i], "S->S  SHARED");
            }
        } else if (cmd == BUS_RDX) {
            if (st == ST_M) {
                cov_hit(TR_M_BUSRDX_I);
                for (int k = 0; k < WORDS_PER_BLOCK; k++)
                    s->xfer[k] = l->data[k];
                s->xfer_valid = 1;
                mem_write_block(&s->mem, addr, l->data);
                b->supplier         = i;
                b->caused_writeback = 1;
                c->flushes++;
                strcpy(b->snoop_note[i], "M->I  Flush");
            } else if (st == ST_E) {
                cov_hit(TR_E_BUSRDX_I);
                strcpy(b->snoop_note[i], "E->I  inval");
            } else {
                cov_hit(TR_S_BUSRDX_I);
                strcpy(b->snoop_note[i], "S->I  inval");
            }
            l->state = ST_I;
            c->invalidations++;
        } else if (cmd == BUS_UPGR) {
            /* Only S copies can exist when somebody issues BusUpgr: the
             * upgrader was in S, so nobody can have been in E or M. */
            cov_hit(TR_S_BUSUPGR_I);
            l->state = ST_I;
            c->invalidations++;
            strcpy(b->snoop_note[i], "S->I  inval");
        }
    }
}

/* ------------------------------------------------------------------ */
/*  diagram 1 : locally initiated accesses                             */
/* ------------------------------------------------------------------ */

/* Hit path: no bus transaction at all. */
static void do_hit(Sim *s, int cpu, const Request *r, ReqResult *res)
{
    Cache     *c   = &s->cache[cpu];
    unsigned   set = addr_set(r->addr);
    int        way = cache_find(c, r->addr);
    CacheLine *l   = &c->line[set][way];
    int        st  = l->state;

    res->from_state = st;
    res->bus_cmd    = BUS_NONE;
    res->data_source= SRC_NONE;

    if (r->op == OP_READ) {
        if      (st == ST_S) cov_hit(TR_S_PRRD_S);
        else if (st == ST_E) cov_hit(TR_E_PRRD_E);
        else                 cov_hit(TR_M_PRRD_M);
        check_read(s, cpu, r->addr, l->data[addr_word(r->addr)], res);
        c->read_hits++;
        strcpy(res->outcome, "READ HIT");
        sim_log(s, "P%d  RD  0x%06X  HIT in %s", cpu, r->addr, state_name(st));
    } else {
        if (st == ST_E) {
            cov_hit(TR_E_PRWR_M);
            l->state = ST_M;
            sim_log(s, "P%d  WR  0x%06X  HIT in E, silent upgrade E->M", cpu, r->addr);
        } else {
            cov_hit(TR_M_PRWR_M);
            sim_log(s, "P%d  WR  0x%06X  HIT in M", cpu, r->addr);
        }
        do_write(s, c, l, r->addr, r->value);
        c->write_hits++;
        strcpy(res->outcome, "WRITE HIT");
    }

    res->to_state = l->state;
    cache_touch(c, set, way);
}

/* Bus path.  Returns the number of cycles the master held the bus. */
static int do_bus_txn(Sim *s, int cpu, const Request *r, ReqResult *res, int planned_cmd)
{
    Cache    *c   = &s->cache[cpu];
    MemBus   *b   = &s->bus;
    unsigned  set = addr_set(r->addr);
    unsigned  tag = addr_tag(r->addr);
    int       tenure = 0;

    bus_clear_txn(b);
    b->requester = cpu;
    b->addr      = addr_block(r->addr);

    /* --- re-plan, in case a snoop hit us while we waited for the bus --- */
    int way = cache_find(c, r->addr);
    int st  = (way >= 0) ? c->line[set][way].state : ST_I;
    int cmd;

    if (st == ST_S && r->op == OP_WRITE)
        cmd = BUS_UPGR;
    else if (st == ST_I)
        cmd = (r->op == OP_WRITE) ? BUS_RDX : BUS_RD;
    else {
        /* E or M by the time we got the bus.  A snoop can only ever push us
         * down, so this should be unreachable; handle it as a hit anyway
         * rather than issuing a pointless transaction. */
        do_hit(s, cpu, r, res);
        return 0;
    }

    res->from_state = st;
    if (planned_cmd != BUS_NONE && planned_cmd != cmd) {
        res->replanned = 1;
        sim_log(s, "P%d  re-plans after losing arbitration: %s -> %s (state is now %s)",
                cpu, bus_cmd_name(planned_cmd), bus_cmd_name(cmd), state_name(st));
    }

    /* --- make room for the block (write-allocate) --------------------- */
    int vway = way;
    if (cmd != BUS_UPGR) {
        vway = cache_victim(c, r->addr);
        CacheLine *vl = &c->line[set][vway];
        res->evicted_state = -1;
        if (vl->state != ST_I) {
            unsigned vaddr = sim_victim_addr(vl->tag, set);
            res->evicted_state = vl->state;
            res->evicted_addr  = vaddr;
            if (vl->state == ST_M) {
                cov_hit(TR_M_REPL_I);
                mem_write_block(&s->mem, vaddr, vl->data);
                c->writebacks++;
                b->caused_writeback = 1;
                b->txn_count[BUS_WB]++;
                tenure += T_BUS + T_MEM;     /* write-back rides this tenure */
                sim_log(s, "P%d  evicts 0x%06X (M) -> BusWB, memory updated", cpu, vaddr);
            } else if (vl->state == ST_E) {
                cov_hit(TR_E_REPL_I);
                sim_log(s, "P%d  evicts 0x%06X (E) silently, block was clean", cpu, vaddr);
            } else {
                cov_hit(TR_S_REPL_I);
                sim_log(s, "P%d  evicts 0x%06X (S) silently, block was clean", cpu, vaddr);
            }
            vl->state = ST_I;
        }
    }

    /* --- address phase ------------------------------------------------ */
    b->cmd = cmd;
    b->txn_count[cmd]++;
    tenure += T_BUS;
    sim_log(s, "BUS %s  0x%06X  (set %03u tag %02X) driven by P%d",
            bus_cmd_name(cmd), addr_block(r->addr), set, tag, cpu);

    /* --- snoop phase: every other cache checks its tags in parallel --- */
    snoop_all(s, cpu, cmd, r->addr);
    tenure += T_CACHE;
    if (b->supplier >= 0)
        sim_log(s, "SNOOP SHARED=%d, P%d will flush the block", b->shared_line, b->supplier);
    else
        sim_log(s, "SNOOP SHARED=%d, no cache can supply the block", b->shared_line);

    /* --- data phase ---------------------------------------------------- */
    CacheLine *l = &c->line[set][vway];

    if (cmd == BUS_UPGR) {
        b->data_source = SRC_NONE;          /* no data moves, just kills    */
        c->upgrades++;
    } else if (s->xfer_valid) {
        b->data_source = SRC_CACHE;
        for (int k = 0; k < WORDS_PER_BLOCK; k++)
            l->data[k] = s->xfer[k];
        /* cache-to-cache: one cache access to read it out + one bus cycle.
         * Memory snarfs the same block off the bus, which costs memory 3
         * cycles of occupancy but is off our critical path. */
        tenure += T_CACHE + T_BUS;
        sim_log(s, "DATA cache-to-cache transfer P%d -> P%d", b->supplier, cpu);
    } else {
        b->data_source = SRC_MEMORY;
        mem_read_block(&s->mem, r->addr, l->data);
        tenure += T_MEM + T_BUS;
        sim_log(s, "DATA main memory supplies the block (%d cycles)", T_MEM);
    }

    /* --- fill and set the new state ------------------------------------ */
    if (cmd != BUS_UPGR)
        l->tag = tag;

    int newstate;
    if (cmd == BUS_RD) {
        newstate = b->shared_line ? ST_S : ST_E;
        cov_hit(b->shared_line ? TR_I_PRRD_S : TR_I_PRRD_E);
        c->read_misses++;
        strcpy(res->outcome, "READ MISS");
    } else if (cmd == BUS_RDX) {
        newstate = ST_M;
        cov_hit(TR_I_PRWR_M);
        c->write_misses++;
        strcpy(res->outcome, "WRITE MISS");
    } else {
        newstate = ST_M;
        cov_hit(TR_S_PRWR_M);
        c->write_hits++;                    /* the data was already here    */
        strcpy(res->outcome, "UPGRADE");
    }
    l->state = newstate;
    tenure  += T_CACHE;                     /* fill + deliver to the CPU    */

    if (r->op == OP_WRITE)
        do_write(s, c, l, r->addr, r->value);
    else
        check_read(s, cpu, r->addr, l->data[addr_word(r->addr)], res);

    cache_touch(c, set, vway);

    res->bus_cmd     = cmd;
    res->data_source = b->data_source;
    res->supplier    = b->supplier;
    res->to_state    = newstate;

    sim_log(s, "P%d  %s 0x%06X  %s -> %s   (tenure %d cyc)",
            cpu, r->op == OP_WRITE ? "WR" : "RD", r->addr,
            state_name(st), state_name(newstate), tenure);

    b->busy_cycles += tenure;
    b->tenure       = tenure;
    return tenure;
}

/* ------------------------------------------------------------------ */
/*  one simulation step                                                */
/* ------------------------------------------------------------------ */

void sim_step(Sim *s, const Request *reqs, int n)
{
    Request  pend[NUM_CPUS];
    int      planned[NUM_CPUS];
    int      slot_of[NUM_CPUS];
    long     start_cycle = s->cycle;

    s->step_no++;
    s->result_count = n;
    bus_clear_txn(&s->bus);

    for (int i = 0; i < NUM_CPUS; i++) {
        planned[i]    = BUS_NONE;
        slot_of[i]    = -1;
    }
    for (int i = 0; i < NUM_CPUS; i++) {
        s->result[i].used          = 0;
        s->result[i].replanned     = 0;
        s->result[i].evicted_state = -1;
        s->result[i].value_bad     = 0;
        s->result[i].supplier      = -1;
        s->result[i].bus_cmd       = BUS_NONE;
        s->result[i].data_source   = SRC_NONE;
        strcpy(s->result[i].outcome, "-");
    }

    /* ---- phase 1: every cache looks up its own tags, in parallel ---- */
    s->cycle += T_CACHE;

    for (int i = 0; i < n; i++) {
        const Request *r = &reqs[i];
        ReqResult *res   = &s->result[i];
        res->used = 1;
        res->req  = *r;
        sim_track(s, r->addr);

        Cache *c = &s->cache[r->cpu];
        if (r->op == OP_READ) c->reads++; else c->writes++;

        int way = cache_find(c, r->addr);
        int st  = (way >= 0) ? c->line[addr_set(r->addr)][way].state : ST_I;

        int needs_bus = 0;
        if (way < 0) {
            needs_bus = 1;
            planned[r->cpu] = (r->op == OP_WRITE) ? BUS_RDX : BUS_RD;
            if (r->op == OP_READ) sim_log(s, "P%d  RD  0x%06X  MISS (I)", r->cpu, r->addr);
            else                  sim_log(s, "P%d  WR  0x%06X  MISS (I)", r->cpu, r->addr);
        } else if (st == ST_S && r->op == OP_WRITE) {
            needs_bus = 1;
            planned[r->cpu] = BUS_UPGR;
            sim_log(s, "P%d  WR  0x%06X  hit in S, needs exclusive access", r->cpu, r->addr);
        }

        if (needs_bus) {
            pend[r->cpu]    = *r;
            slot_of[r->cpu] = i;
            arb_request(&s->bus.arb, r->cpu);
            sim_log(s, "ARB P%d asserts BREQ (%s)", r->cpu, bus_cmd_name(planned[r->cpu]));
        } else {
            do_hit(s, r->cpu, r, res);
            res->latency = T_CACHE;
        }
    }

    /* ---- phase 2: the arbiter serialises everyone who wants the bus ---- */
    while (arb_pending(&s->bus.arb)) {
        s->cycle += T_ARB;

        int nreq = 0;
        for (int i = 0; i < NUM_CPUS; i++)
            if (s->bus.arb.breq[i]) nreq++;

        int winner = arb_arbitrate(&s->bus.arb);
        if (winner < 0)
            break;

        if (nreq > 1)
            sim_log(s, "ARB %d masters requesting, round robin grants P%d "
                       "(next priority P%d)", nreq, winner, s->bus.arb.rr_ptr);
        else
            sim_log(s, "ARB grants the bus to P%d", winner);

        int slot   = slot_of[winner];
        int tenure = do_bus_txn(s, winner, &pend[winner], &s->result[slot],
                                planned[winner]);
        s->cycle += tenure;

        /* everybody still waiting paid for that whole tenure */
        for (int i = 0; i < NUM_CPUS; i++)
            if (s->bus.arb.breq[i])
                s->cache[i].arb_wait_cycles += T_ARB + tenure;

        s->result[slot].latency = (int)(s->cycle - start_cycle);
        arb_release(&s->bus.arb);
    }

    for (int i = 0; i < n; i++)
        s->cache[reqs[i].cpu].stall_cycles += s->result[i].latency;
}
