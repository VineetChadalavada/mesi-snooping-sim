/* ==========================================================================
 * screens.cpp  --  everything the simulator puts on the screen.
 *
 * Kept away from sim.cpp on purpose: sim.cpp is the protocol, this file is
 * only presentation.
 * ========================================================================== */
#include "sim.h"
#include "mesi.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  small formatting helpers                                           */
/* ------------------------------------------------------------------ */

static std::string src_text(int src, int supplier)
{
    if (src == SRC_MEMORY)
        return "main memory";
    if (src == SRC_CACHE)
        return ui_fmt("cache P%d", supplier);
    return "-";
}

static std::string arrow(int from, int to)
{
    return ui_state(from) + " " + C_DIM + "->" + C_RESET + " " + ui_state(to);
}

/* is main memory out of date for this block? it is, if somebody holds it M */
static int block_is_stale(Sim *s, unsigned blk)
{
    for (int i = 0; i < NUM_CPUS; i++)
        if (cache_state(&s->cache[i], blk) == ST_M)
            return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  the live dashboard                                                 */
/* ------------------------------------------------------------------ */

static void draw_requests(Sim *s)
{
    ui_box_top("PROCESSOR REQUESTS  (all issued in the same cycle)");

    if (s->result_count == 0) {
        ui_box_row(std::string(C_DIM) + "  nothing issued yet" + C_RESET);
    }

    for (int i = 0; i < s->result_count; i++) {
        ReqResult *r = &s->result[i];
        if (!r->used)
            continue;

        std::string line;
        line += ui_pad(ui_fmt("%sP%d%s", C_BOLD, r->req.cpu, C_RESET), 4);
        line += ui_pad(r->req.op == OP_WRITE ? "WR" : "RD", 3);
        line += ui_pad(ui_fmt("0x%06X", r->req.addr), 10);
        line += ui_pad(ui_fmt("set %03u tag %02X w%02u",
                              addr_set(r->req.addr), addr_tag(r->req.addr),
                              addr_word(r->req.addr)), 21);

        const char *oc = C_BGRN;
        if (strstr(r->outcome, "MISS"))    oc = C_BYEL;
        if (strstr(r->outcome, "UPGRADE")) oc = C_BMAG;
        line += ui_pad(std::string(oc) + r->outcome + C_RESET, 12);

        line += ui_pad(std::string(C_BCYN) + bus_cmd_name(r->bus_cmd) + C_RESET, 9);
        line += ui_pad(src_text(r->data_source, r->supplier), 14);
        line += ui_pad(arrow(r->from_state, r->to_state), 10);
        line += ui_padleft(ui_fmt("%d cyc", r->latency), 8);
        ui_box_row(line);
    }

    /* extra detail when a single request is in flight */
    if (s->result_count == 1 && s->result[0].used) {
        ReqResult *r = &s->result[0];
        std::string d = "     ";
        if (r->req.op == OP_WRITE)
            d += ui_fmt("stored 0x%08X", r->req.value);
        else
            d += ui_fmt("returned 0x%08X  %s", r->value_seen,
                        r->value_bad ? "<< WRONG VALUE" : "(matches golden copy)");
        if (r->evicted_state >= 0)
            d += ui_fmt("   |   evicted 0x%06X (%s)%s", r->evicted_addr,
                        state_name(r->evicted_state),
                        r->evicted_state == ST_M ? " written back" : " silently");
        if (r->replanned)
            d += std::string("   |   ") + C_BYEL + "re-planned after losing arbitration" + C_RESET;
        ui_box_row(std::string(r->value_bad ? C_BRED : C_DIM) + d + C_RESET);
    }

    ui_box_bottom();
}

static void draw_bus(Sim *s)
{
    MemBus  *b = &s->bus;
    Arbiter *a = &b->arb;

    ui_box_top("MemBus  +  BUS ARBITER  (round robin)");

    std::string l1 = "arbiter   next priority ";
    l1 += std::string(C_BYEL) + ui_fmt("P%d", a->rr_ptr) + C_RESET;
    l1 += "    grants ";
    for (int i = 0; i < NUM_CPUS; i++)
        l1 += ui_fmt("P%d:%ld ", i, a->grants[i]);
    l1 += "   passed over ";
    for (int i = 0; i < NUM_CPUS; i++)
        l1 += ui_fmt("P%d:%ld ", i, a->denied[i]);
    ui_box_row(l1);

    std::string l2 = "last txn  ";
    l2 += ui_pad(std::string(C_BCYN) + C_BOLD + bus_cmd_name(b->cmd) + C_RESET, 9);
    l2 += ui_pad(b->cmd == BUS_NONE ? "-" : ui_fmt("0x%06X", b->addr), 11);
    l2 += ui_pad(b->requester < 0 ? "" : ui_fmt("by P%d", b->requester), 8);
    l2 += ui_pad(ui_fmt("SHARED=%d", b->shared_line), 11);
    l2 += ui_pad("data from " + src_text(b->data_source, b->supplier), 26);
    l2 += ui_fmt("tenure %d cyc", b->tenure);
    ui_box_row(l2);

    std::string l3 = "snoop     ";
    for (int i = 0; i < NUM_CPUS; i++) {
        if (i == b->requester) {
            l3 += ui_pad(ui_fmt("%sP%d requester%s", C_DIM, i, C_RESET), 21);
        } else {
            const char *col = C_DIM;
            if (strstr(b->snoop_note[i], "Flush")) col = C_BRED;
            else if (strstr(b->snoop_note[i], "inval")) col = C_BYEL;
            else if (strstr(b->snoop_note[i], "SHARED")) col = C_BGRN;
            l3 += ui_pad(ui_fmt("P%d %s%s%s", i, col, b->snoop_note[i], C_RESET), 21);
        }
    }
    ui_box_row(l3);

    long total = s->cycle > 0 ? s->cycle : 1;
    std::string l4 = ui_fmt("traffic   bus busy %ld of %ld cyc (%ld%%)   ",
                            b->busy_cycles, s->cycle, (b->busy_cycles * 100) / total);
    l4 += ui_fmt("BusRd %ld   BusRdX %ld   BusUpgr %ld   BusWB %ld",
                 b->txn_count[BUS_RD], b->txn_count[BUS_RDX],
                 b->txn_count[BUS_UPGR], b->txn_count[BUS_WB]);
    ui_box_row(l4);

    ui_box_bottom();
}

static void draw_caches(Sim *s)
{
    std::string titles[NUM_CPUS];
    std::string row[NUM_CPUS];

    for (int i = 0; i < NUM_CPUS; i++)
        titles[i] = ui_fmt("%sP%d L1$%s", C_BOLD, i, C_RESET);
    ui_box_cols_top(titles, NUM_CPUS);

    for (int i = 0; i < NUM_CPUS; i++) {
        long cen[4];
        cache_census(&s->cache[i], cen);
        row[i] = ui_fmt("%sM%ld%s %sE%ld%s %sS%ld%s %sI%ld%s",
                        C_BRED, cen[ST_M], C_RESET, C_BYEL, cen[ST_E], C_RESET,
                        C_BGRN, cen[ST_S], C_RESET, C_DIM,  cen[ST_I], C_RESET);
    }
    ui_box_cols(row, NUM_CPUS);

    for (int i = 0; i < NUM_CPUS; i++) {
        Cache *c = &s->cache[i];
        row[i] = ui_fmt("rd %ld wr %ld hit %ld", c->reads, c->writes,
                        c->read_hits + c->write_hits);
    }
    ui_box_cols(row, NUM_CPUS);

    for (int i = 0; i < NUM_CPUS; i++) {
        Cache *c = &s->cache[i];
        row[i] = ui_fmt("miss %ld wb %ld inv %ld",
                        c->read_misses + c->write_misses, c->writebacks,
                        c->invalidations);
    }
    ui_box_cols(row, NUM_CPUS);

    for (int i = 0; i < NUM_CPUS; i++) {
        Cache *c = &s->cache[i];
        row[i] = ui_fmt("stall %ld  busw %ld", c->stall_cycles, c->arb_wait_cycles);
    }
    ui_box_cols(row, NUM_CPUS);

    ui_box_cols_bottom(NUM_CPUS);
}

static void draw_coherence(Sim *s)
{
    ui_box_top("COHERENCE VIEW  (every block the test has touched)");

    std::string hdr = std::string(C_DIM);
    hdr += ui_pad("block", 12) + ui_pad("set", 5) + ui_pad("tag", 5);
    hdr += "| ";
    for (int i = 0; i < NUM_CPUS; i++)
        hdr += ui_centre(ui_fmt("P%d", i), 5);
    hdr += "| " + ui_pad("memory", 9) + "coherent value at word 0";
    hdr += C_RESET;
    ui_box_row(hdr);

    int start = (int)s->tracked.size() - 6;
    if (start < 0) start = 0;
    for (size_t k = start; k < s->tracked.size(); k++) {
        unsigned blk = s->tracked[k];
        std::string r;
        r += ui_pad(ui_fmt("0x%06X", blk), 12);
        r += ui_pad(ui_fmt("%03u", addr_set(blk)), 5);
        r += ui_pad(ui_fmt("%02X", addr_tag(blk)), 5);
        r += "| ";
        for (int i = 0; i < NUM_CPUS; i++)
            r += ui_state_wide(cache_state(&s->cache[i], blk), 5);
        r += "| ";
        if (block_is_stale(s, blk))
            r += ui_pad(std::string(C_BRED) + "STALE" + C_RESET, 9);
        else
            r += ui_pad(std::string(C_GRN) + "clean" + C_RESET, 9);
        r += ui_fmt("0x%08X", s->golden[blk >> 2]);
        ui_box_row(r);
    }
    if (s->tracked.empty())
        ui_box_row(std::string(C_DIM) + "  (no blocks touched yet)" + C_RESET);

    ui_box_bottom();
}

static void draw_log(Sim *s)
{
    ui_box_top("EVENT LOG  (full transcript is written to run_log.txt)");
    int start = (int)s->log.size() - 6;
    if (start < 0) start = 0;
    for (size_t i = start; i < s->log.size(); i++) {
        const std::string &t = s->log[i];
        const char *col = C_RESET;
        if (t.find("BUS ")   != std::string::npos) col = C_BCYN;
        if (t.find("ARB ")   != std::string::npos) col = C_BYEL;
        if (t.find("SNOOP")  != std::string::npos) col = C_BMAG;
        if (t.find("DATA")   != std::string::npos) col = C_BBLU;
        if (t.find("evicts") != std::string::npos) col = C_YEL;
        if (t.find("!!")     != std::string::npos) col = C_BRED;
        ui_box_row(std::string(col) + t + C_RESET);
    }
    for (int i = (int)s->log.size(); i < 6; i++)
        ui_box_row("");
    ui_box_bottom();
}

void sim_render(Sim *s)
{
    ui_home();

    ui_banner(std::string(C_BOLD) + "MESI SNOOPING CACHE COHERENCE SIMULATOR" + C_RESET
                  + C_DIM + "   Part 1" + C_RESET,
              ui_fmt("cycle %s%05ld%s   step %02ld   %s%s%s",
                     C_BYEL, s->cycle, C_RESET, s->step_no,
                     C_DIM, s->headline.c_str(), C_RESET));

    draw_requests(s);
    draw_bus(s);
    draw_caches(s);
    draw_coherence(s);
    draw_log(s);

    int lc = cov_covered(DIAG_LOCAL),  lt = cov_total(DIAG_LOCAL);
    int rc = cov_covered(DIAG_REMOTE), rt = cov_total(DIAG_REMOTE);
    printf(" %scoverage%s  local %s%d/%d%s   remote %s%d/%d%s      "
           "%sdata checks%s %ld passed, %s%ld failed%s\n",
           C_DIM, C_RESET,
           lc == lt ? C_BGRN : C_BYEL, lc, lt, C_RESET,
           rc == rt ? C_BGRN : C_BYEL, rc, rt, C_RESET,
           C_DIM, C_RESET, s->checks - s->check_fails,
           s->check_fails ? C_BRED : C_BGRN, s->check_fails, C_RESET);

    ui_clear_to_end();
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*  static screens                                                     */
/* ------------------------------------------------------------------ */

void sim_show_config(Sim *s)
{
    (void)s;
    ui_clear_screen();
    ui_banner(std::string(C_BOLD) + "ARCHITECTURAL PARAMETERS AND ASSUMPTIONS" + C_RESET, "Part 1");

    ui_box_top("SYSTEM");
    ui_box_row(ui_fmt("processors                    %d", NUM_CPUS));
    ui_box_row(ui_fmt("shared main memory            %d KB  (%d 32-bit words)",
                      MEM_SIZE_BYTES / 1024, MEM_WORDS));
    ui_box_row(ui_fmt("physical address              %d bits", ADDR_BITS));
    ui_box_row(     "interconnect                  one shared MemBus, atomic transactions");
    ui_box_row(     "coherence protocol            MESI, write-invalidate, snooping");
    ui_box_bottom();

    ui_box_top("L1 CACHE  (one per processor, shape of the Opteron cache, Fig. B.5)");
    ui_box_row(ui_fmt("capacity                      %d KB", L1_SIZE_BYTES / 1024));
    ui_box_row(ui_fmt("block size                    %d bytes  (%d words)",
                      BLOCK_BYTES, WORDS_PER_BLOCK));
    ui_box_row(ui_fmt("associativity                 %d-way set associative", NUM_WAYS));
    ui_box_row(ui_fmt("sets                          %d KB / (%d B x %d ways) = %d sets",
                      L1_SIZE_BYTES / 1024, BLOCK_BYTES, NUM_WAYS, NUM_SETS));
    ui_box_row(     "replacement                   LRU");
    ui_box_row(     "write policy                  write-back, write-allocate");
    ui_box_bottom();

    ui_box_top("ADDRESS BREAKDOWN");
    ui_box_row(ui_fmt("tag %d bits   |   index %d bits (%d sets)   |   offset %d bits (%d B block)",
                      TAG_BITS, INDEX_BITS, NUM_SETS, OFFSET_BITS, BLOCK_BYTES));
    ui_box_row("");
    ui_box_row("   19                 15 14                  6 5                  0");
    ui_box_row("  +---------------------+----------------------+--------------------+");
    ui_box_row("  |    tag    (5 bits)  |    index   (9 bits)  |   offset  (6 bits) |");
    ui_box_row("  +---------------------+----------------------+--------------------+");
    ui_box_row("");
    ui_box_row(ui_fmt("two addresses collide in the same set when they are %d KB apart",
                      SET_STRIDE / 1024));
    ui_box_bottom();

    ui_box_top("TIMING  (as given in the assignment)");
    ui_box_row(ui_fmt("cache access time             %d cycle", T_CACHE));
    ui_box_row(ui_fmt("main memory access time       %d cycles", T_MEM));
    ui_box_row(ui_fmt("MemBus latency                %d cycle, after the arbiter grants", T_BUS));
    ui_box_row(ui_fmt("bus arbitration               %d cycle", T_ARB));
    ui_box_sep();
    ui_box_row(std::string(C_BOLD) + "resulting access latencies" + C_RESET);
    ui_box_row(ui_fmt("  read or write hit                                        %d cycle",
                      T_CACHE));
    ui_box_row(ui_fmt("  write hit in S (BusUpgr)      %d+%d+%d+%d+%d                   = %d cycles",
                      T_CACHE, T_ARB, T_BUS, T_CACHE, T_CACHE,
                      T_CACHE + T_ARB + T_BUS + T_CACHE + T_CACHE));
    ui_box_row(ui_fmt("  miss, memory supplies         %d+%d+%d+%d+%d+%d+%d             = %d cycles",
                      T_CACHE, T_ARB, T_BUS, T_CACHE, T_MEM, T_BUS, T_CACHE,
                      T_CACHE + T_ARB + T_BUS + T_CACHE + T_MEM + T_BUS + T_CACHE));
    ui_box_row(ui_fmt("  miss, another cache flushes   %d+%d+%d+%d+%d+%d+%d             = %d cycles",
                      T_CACHE, T_ARB, T_BUS, T_CACHE, T_CACHE, T_BUS, T_CACHE,
                      T_CACHE + T_ARB + T_BUS + T_CACHE + T_CACHE + T_BUS + T_CACHE));
    ui_box_row(ui_fmt("  dirty victim adds a write-back of %d+%d                        = %d cycles",
                      T_BUS, T_MEM, T_BUS + T_MEM));
    ui_box_bottom();

    ui_box_top("ASSUMPTIONS WE MADE");
    ui_box_row("1. The bus is atomic: the winner of arbitration holds it for the whole");
    ui_box_row("   transaction, so no other request for the same block can interleave.");
    ui_box_row("   This is what keeps the protocol free of transient states.");
    ui_box_row("2. SHARED is a wired-OR line driven by every cache that still holds a");
    ui_box_row("   valid copy.  A read miss ends in E when SHARED stays low, in S when");
    ui_box_row("   it is pulled.  This is the whole reason the E state can exist.");
    ui_box_row("3. Only a cache in M supplies data (Flush).  E and S copies are clean,");
    ui_box_row("   so main memory answers those misses.");
    ui_box_row("4. A Flush both hands the block to the requester and updates memory, so");
    ui_box_row("   memory is clean again afterwards.  Memory snarfs the block in");
    ui_box_row("   parallel, so its 3 cycles are off the requester's critical path.");
    ui_box_row("5. S -> M uses BusUpgr, not BusRdX: we already have valid data, we only");
    ui_box_row("   need the other copies killed, so no data phase is needed.");
    ui_box_row("6. A dirty victim is written back inside the same bus tenure as the");
    ui_box_row("   miss that displaced it (one write-back buffer per cache).");
    ui_box_row("7. Processors are not simulated.  Read and Write requests are injected");
    ui_box_row("   straight into the cache controllers, as the assignment allows.");
    ui_box_row("8. One 32-bit word is read or written per request; the cache still moves");
    ui_box_row("   whole 64-byte blocks on the bus.");
    ui_box_bottom();

    ui_pause("\n   press ENTER to go back to the menu ");
}

void sim_show_diagrams(void)
{
    ui_clear_screen();
    ui_banner(std::string(C_BOLD) + "THE TWO STATE DIAGRAMS THE SIMULATOR IMPLEMENTS" + C_RESET, "MESI");

    ui_box_top("DIAGRAM 1  -  LOCALLY INITIATED ACCESSES  (this processor's own Rd / Wr)");
    ui_box_row("");
    ui_box_row("             +------------------- PrWr / BusRdX -------------------+");
    ui_box_row("             |                                                     |");
    ui_box_row("             |      +--- PrRd / BusRd, SHARED = 0 ---+             v");
    ui_box_row("         +---+---+  |                                v        +-------+");
    ui_box_row("         |   I   +--+                            +-------+    |   M   |");
    ui_box_row("         +---+---+                               |   E   |    +---+---+");
    ui_box_row("             |                                   +---+---+        ^");
    ui_box_row("             | PrRd / BusRd, SHARED = 1              |            |");
    ui_box_row("             v                                       +-- PrWr ----+");
    ui_box_row("         +-------+                                    (no bus txn)");
    ui_box_row("         |   S   +------------ PrWr / BusUpgr --------------------+");
    ui_box_row("         +-------+");
    ui_box_row("");
    ui_box_row(std::string(C_DIM) + "  stay put (plain hit, no bus) :  S+PrRd    E+PrRd    M+PrRd    M+PrWr" + C_RESET);
    ui_box_row(std::string(C_DIM) + "  leave on replacement        :  S -> I    E -> I   silently (block is clean)" + C_RESET);
    ui_box_row(std::string(C_DIM) + "                                 M -> I    after a BusWB of the dirty block" + C_RESET);
    ui_box_bottom();

    ui_box_top("DIAGRAM 2  -  REMOTELY INITIATED ACCESSES  (what a snooped transaction does to us)");
    ui_box_row("");
    ui_box_row(std::string(C_BOLD) + "   we snoop a BusRd                     we snoop a BusRdX or BusUpgr" + C_RESET);
    ui_box_row("   -----------------                     ----------------------------");
    ui_box_row(ui_fmt("   %sM%s  Flush, memory updated  -> %sS%s     %sM%s  Flush                    -> %sI%s",
                      C_BRED, C_RESET, C_BGRN, C_RESET, C_BRED, C_RESET, C_DIM, C_RESET));
    ui_box_row(ui_fmt("   %sE%s  assert SHARED          -> %sS%s     %sE%s  invalidate               -> %sI%s",
                      C_BYEL, C_RESET, C_BGRN, C_RESET, C_BYEL, C_RESET, C_DIM, C_RESET));
    ui_box_row(ui_fmt("   %sS%s  assert SHARED          -> %sS%s     %sS%s  invalidate               -> %sI%s",
                      C_BGRN, C_RESET, C_BGRN, C_RESET, C_BGRN, C_RESET, C_DIM, C_RESET));
    ui_box_row(ui_fmt("   %sI%s  no action              -> %sI%s     %sI%s  no action                -> %sI%s",
                      C_DIM, C_RESET, C_DIM, C_RESET, C_DIM, C_RESET, C_DIM, C_RESET));
    ui_box_row("");
    ui_box_row("   BusUpgr can only ever find copies in S: the cache that issues it was");
    ui_box_row("   itself in S, so by definition nobody held the block in E or M.");
    ui_box_bottom();

    ui_pause("\n   press ENTER to go back to the menu ");
}

void sim_show_coverage(Sim *s)
{
    (void)s;
    ui_clear_screen();
    ui_banner(std::string(C_BOLD) + "STATE TRANSITION COVERAGE" + C_RESET,
              "every arc of both diagrams");

    for (int d = 0; d <= 1; d++) {
        ui_box_top(d == DIAG_LOCAL
                   ? "DIAGRAM 1  -  LOCALLY INITIATED ACCESSES"
                   : "DIAGRAM 2  -  REMOTELY INITIATED ACCESSES");
        std::string h = std::string(C_DIM) + ui_pad("from", 6) + ui_pad("event", 22)
                      + ui_pad("action", 22) + ui_pad("to", 6)
                      + ui_pad("times", 8) + "covered" + C_RESET;
        ui_box_row(h);
        for (int i = 0; i < NUM_TRANSITIONS; i++) {
            if (g_trans[i].diagram != d)
                continue;
            long n = cov_count(i);
            std::string r;
            r += ui_pad(ui_state(g_trans[i].from[0] == 'M' ? ST_M :
                                 g_trans[i].from[0] == 'E' ? ST_E :
                                 g_trans[i].from[0] == 'S' ? ST_S : ST_I), 6);
            r += ui_pad(g_trans[i].event, 22);
            r += ui_pad(g_trans[i].action, 22);
            r += ui_pad(ui_state(g_trans[i].to[0] == 'M' ? ST_M :
                                 g_trans[i].to[0] == 'E' ? ST_E :
                                 g_trans[i].to[0] == 'S' ? ST_S : ST_I), 6);
            r += ui_pad(ui_fmt("%ld", n), 8);
            r += n > 0 ? std::string(C_BGRN) + "yes" + C_RESET
                       : std::string(C_BRED) + "NO" + C_RESET;
            ui_box_row(r);
        }
        ui_box_bottom();
    }

    int lc = cov_covered(DIAG_LOCAL),  lt = cov_total(DIAG_LOCAL);
    int rc = cov_covered(DIAG_REMOTE), rt = cov_total(DIAG_REMOTE);
    printf("\n   local diagram  %s%d of %d arcs%s        remote diagram  %s%d of %d arcs%s\n",
           lc == lt ? C_BGRN : C_BRED, lc, lt, C_RESET,
           rc == rt ? C_BGRN : C_BRED, rc, rt, C_RESET);
    if (lc == lt && rc == rt)
        printf("   %sALL TRANSITIONS OF BOTH STATE DIAGRAMS WERE ACTIVATED.%s\n", C_BGRN, C_RESET);

    ui_pause("\n   press ENTER to go back to the menu ");
}

void sim_show_stats(Sim *s)
{
    ui_clear_screen();
    ui_banner(std::string(C_BOLD) + "SIMULATION STATISTICS" + C_RESET,
              ui_fmt("%ld cycles simulated", s->cycle));

    ui_box_top("PER PROCESSOR");
    ui_box_row(std::string(C_DIM)
               + ui_pad("cpu", 6) + ui_pad("reads", 8) + ui_pad("writes", 8)
               + ui_pad("hits", 8) + ui_pad("misses", 8) + ui_pad("hit%", 8)
               + ui_pad("upgr", 7) + ui_pad("wrback", 8) + ui_pad("inval", 8)
               + ui_pad("flush", 8) + ui_pad("stall", 8) + "buswait" + C_RESET);
    for (int i = 0; i < NUM_CPUS; i++) {
        Cache *c   = &s->cache[i];
        long   acc = c->reads + c->writes;
        long   hit = c->read_hits + c->write_hits;
        long   mis = c->read_misses + c->write_misses;
        std::string r;
        r += ui_pad(ui_fmt("%sP%d%s", C_BOLD, i, C_RESET), 6);
        r += ui_pad(ui_fmt("%ld", c->reads), 8);
        r += ui_pad(ui_fmt("%ld", c->writes), 8);
        r += ui_pad(ui_fmt("%ld", hit), 8);
        r += ui_pad(ui_fmt("%ld", mis), 8);
        r += ui_pad(acc ? ui_fmt("%ld%%", hit * 100 / acc) : "-", 8);
        r += ui_pad(ui_fmt("%ld", c->upgrades), 7);
        r += ui_pad(ui_fmt("%ld", c->writebacks), 8);
        r += ui_pad(ui_fmt("%ld", c->invalidations), 8);
        r += ui_pad(ui_fmt("%ld", c->flushes), 8);
        r += ui_pad(ui_fmt("%ld", c->stall_cycles), 8);
        r += ui_fmt("%ld", c->arb_wait_cycles);
        ui_box_row(r);
    }
    ui_box_bottom();

    MemBus  *b = &s->bus;
    Arbiter *a = &b->arb;
    long total = s->cycle > 0 ? s->cycle : 1;

    ui_box_top("MemBus");
    ui_box_row(ui_fmt("BusRd  %-6ld  BusRdX %-6ld  BusUpgr %-6ld  BusWB %-6ld",
                      b->txn_count[BUS_RD], b->txn_count[BUS_RDX],
                      b->txn_count[BUS_UPGR], b->txn_count[BUS_WB]));
    ui_box_row(ui_fmt("bus busy %ld of %ld cycles  ->  utilisation %ld%%",
                      b->busy_cycles, s->cycle, b->busy_cycles * 100 / total));
    ui_box_bottom();

    ui_box_top("BUS ARBITER");
    ui_box_row(ui_fmt("arbitrations %ld, of which %ld had more than one requester",
                      a->arbitrations, a->contended));
    {
        std::string g = "grants     ";
        std::string d = "passed over";
        for (int i = 0; i < NUM_CPUS; i++) {
            g += ui_fmt("   P%d %-5ld", i, a->grants[i]);
            d += ui_fmt("   P%d %-5ld", i, a->denied[i]);
        }
        ui_box_row(g);
        ui_box_row(d);
    }
    ui_box_row(ui_fmt("next priority P%d   (rotates to winner+1 after every grant)", a->rr_ptr));
    ui_box_row("worst case wait for a requester: 3 bus tenures, so no master can starve");
    ui_box_bottom();

    ui_box_top("MAIN MEMORY");
    ui_box_row(ui_fmt("block reads  %ld     block writes (write-backs and flushes)  %ld",
                      s->mem.reads, s->mem.writes));
    ui_box_row(ui_fmt("memory busy %ld cycles", s->mem.busy_cycles));
    ui_box_bottom();

    ui_box_top("DATA CORRECTNESS CHECK");
    ui_box_row("Every value a processor read was compared with a golden copy of what");
    ui_box_row("the last writer stored.  A single mismatch would mean the protocol");
    ui_box_row("handed out stale data.");
    ui_box_row(ui_fmt("%s%ld reads checked, %ld mismatches%s",
                      s->check_fails ? C_BRED : C_BGRN,
                      s->checks, s->check_fails, C_RESET));
    ui_box_bottom();

    ui_pause("\n   press ENTER to go back to the menu ");
}

void sim_dump_cache(Sim *s, int cpu)
{
    Cache *c = &s->cache[cpu];
    ui_box_top(ui_fmt("P%d L1 CACHE  -  valid lines only", cpu));
    ui_box_row(std::string(C_DIM) + ui_pad("set", 7) + ui_pad("way", 6)
               + ui_pad("tag", 7) + ui_pad("state", 8) + ui_pad("block addr", 14)
               + ui_pad("lru", 8) + "word0" + C_RESET);
    int shown = 0;
    for (int st = 0; st < NUM_SETS && shown < 16; st++) {
        for (int w = 0; w < NUM_WAYS && shown < 16; w++) {
            CacheLine *l = &c->line[st][w];
            if (l->state == ST_I)
                continue;
            std::string r;
            r += ui_pad(ui_fmt("%03d", st), 7);
            r += ui_pad(ui_fmt("%d", w), 6);
            r += ui_pad(ui_fmt("%02X", l->tag), 7);
            r += ui_pad(ui_state(l->state), 8);
            r += ui_pad(ui_fmt("0x%06X", sim_victim_addr(l->tag, st)), 14);
            r += ui_pad(ui_fmt("%lu", l->lru), 8);
            r += ui_fmt("0x%08X", l->data[0]);
            ui_box_row(r);
            shown++;
        }
    }
    if (shown == 0)
        ui_box_row(std::string(C_DIM) + "  (cache is completely invalid)" + C_RESET);
    ui_box_bottom();
}
