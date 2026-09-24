#include "tests.h"
#include "mesi.h"
#include "ui.h"
#include <stdio.h>

/* Write values are cooked so you can read them off the screen: 0xC0DE<c><n>
 * where <c> is the processor that wrote it and <n> is which write it was. */

static const TestStep g_tests[] = {

/* -- part 1 of the run: walk the local diagram out of Invalid ------------ */
{ "P0 reads A. Nobody else has it, so SHARED stays low and the line lands in E.",
  "local  I --PrRd--> E",
  1, { {0, OP_READ,  ADDR_A, 0} } },

{ "P0 reads A again. Plain cache hit out of E, the bus never sees it.",
  "local  E --PrRd--> E",
  1, { {0, OP_READ,  ADDR_A, 0} } },

{ "P1 reads A. P0 snoops the BusRd, pulls SHARED and drops E to S.",
  "local  I --PrRd--> S     remote  E --BusRd--> S",
  1, { {1, OP_READ,  ADDR_A, 0} } },

{ "P1 reads A again. Hit in S.",
  "local  S --PrRd--> S",
  1, { {1, OP_READ,  ADDR_A, 0} } },

{ "P2 reads A. Two caches already hold it, so both stay in S and answer SHARED.",
  "remote S --BusRd--> S",
  1, { {2, OP_READ,  ADDR_A, 0} } },

{ "P1 writes A. It already has the data in S, so it only needs the other copies\n"
  "  killed: BusUpgr, no data phase. P0 and P2 are invalidated.",
  "local  S --PrWr--> M     remote  S --BusUpgr--> I",
  1, { {1, OP_WRITE, ADDR_A, 0xC0DE1001} } },

{ "P1 writes A again. It owns the block in M, so this is a silent hit.",
  "local  M --PrWr--> M",
  1, { {1, OP_WRITE, ADDR_A, 0xC0DE1002} } },

{ "P1 reads A. Still a silent hit in M.",
  "local  M --PrRd--> M",
  1, { {1, OP_READ,  ADDR_A, 0} } },

{ "P3 reads A. P1 is the dirty owner: it Flushes the block, memory is brought up\n"
  "  to date, and both end up in S. P0 and P2 hold the block in I and do nothing.",
  "remote M --BusRd--> S    remote  I --snoop--> I",
  1, { {3, OP_READ,  ADDR_A, 0} } },

{ "P2 writes A. Its copy was invalidated back in step 6, so this is a write miss:\n"
  "  BusRdX kills the S copies in P1 and P3 and P2 takes the block in M.",
  "local  I --PrWr--> M     remote  S --BusRdX--> I",
  1, { {2, OP_WRITE, ADDR_A, 0xC0DE2001} } },

/* -- part 2: E --PrWr--> M and M --BusRdX--> I -------------------------- */
{ "P0 reads B (same set 65 as A, different tag). Nobody has B, so it lands in E.",
  "local  I --PrRd--> E",
  1, { {0, OP_READ,  ADDR_B, 0} } },

{ "P0 writes B. It is the only copy and it is clean, so E goes to M with no bus\n"
  "  transaction at all. This is the transition the E state exists for.",
  "local  E --PrWr--> M",
  1, { {0, OP_WRITE, ADDR_B, 0xC0DE0001} } },

{ "P3 writes B. P0 is the dirty owner, so it Flushes and is invalidated.",
  "remote M --BusRdX--> I",
  1, { {3, OP_WRITE, ADDR_B, 0xC0DE3001} } },

/* -- part 3: E --BusRdX--> I -------------------------------------------- */
{ "P1 reads D, a block in a different set. Nobody has it, so P1 gets it in E.",
  "local  I --PrRd--> E",
  1, { {1, OP_READ,  ADDR_D, 0} } },

{ "P2 writes D. P1 holds it in E (clean, exclusive) and is simply invalidated -\n"
  "  no Flush is needed because memory is already up to date.",
  "remote E --BusRdX--> I",
  1, { {2, OP_WRITE, ADDR_D, 0xC0DE2002} } },

/* -- part 4: fill P0's set 65 so that replacements start happening ------- */
{ "P0 reads A back. P2 is the dirty owner and Flushes it. P0 now holds A in S.",
  "remote M --BusRd--> S",
  1, { {0, OP_READ,  ADDR_A, 0} } },

{ "P0 reads B back. P3 Flushes it. P0's set 65 is now full: A in S, B in S.",
  "remote M --BusRd--> S",
  1, { {0, OP_READ,  ADDR_B, 0} } },

{ "P0 reads C, the third tag that maps to set 65. The set is full, so the LRU\n"
  "  way (A, clean in S) is dropped silently - no bus traffic for the eviction.",
  "local  S --Replace--> I  then  I --PrRd--> E",
  1, { {0, OP_READ,  ADDR_C, 0} } },

{ "P0 writes B. It still has B in S, so BusUpgr, and P3's S copy is invalidated.",
  "local  S --PrWr--> M",
  1, { {0, OP_WRITE, ADDR_B, 0xC0DE0002} } },

{ "P0 reads A again. Now the LRU way holds C in E - clean and exclusive - so it\n"
  "  too is dropped silently.",
  "local  E --Replace--> I",
  1, { {0, OP_READ,  ADDR_A, 0} } },

{ "P0 reads C again. This time the LRU way holds B in M. A dirty victim cannot\n"
  "  just be dropped: it is written back over the bus before the miss is served.",
  "local  M --Replace--> I  (BusWB)",
  1, { {0, OP_READ,  ADDR_C, 0} } },

/* -- part 5: the bus arbiter under contention --------------------------- */
{ "ALL FOUR processors read D in the same cycle. P2 owns it in M so its access is\n"
  "  a hit and never touches the bus; the other three contend and the round-robin\n"
  "  arbiter serialises them. Watch the grants and the wait cycles.",
  "arbiter: 3 masters contend, round robin picks the order",
  4, { {0, OP_READ, ADDR_D, 0},
       {1, OP_READ, ADDR_D, 0},
       {2, OP_READ, ADDR_D, 0},
       {3, OP_READ, ADDR_D, 0} } },

{ "P0, P1 and P2 all write A in the same cycle. P0 and P2 hold it in S and plan a\n"
  "  BusUpgr; P1 holds nothing and plans a BusRdX. Whoever wins invalidates the\n"
  "  others, so at least one loser has to RE-PLAN its BusUpgr into a BusRdX.",
  "arbiter + protocol: losing arbitration changes the transaction you must issue",
  3, { {0, OP_WRITE, ADDR_A, 0xC0DE0003},
       {1, OP_WRITE, ADDR_A, 0xC0DE1003},
       {2, OP_WRITE, ADDR_A, 0xC0DE2003} } },

{ "P3 reads A one last time. Whoever won the previous round is the dirty owner and\n"
  "  Flushes, so P3 must see that processor's value - proof the protocol works.",
  "remote M --BusRd--> S    (data correctness check)",
  1, { {3, OP_READ,  ADDR_A, 0} } }
};

int tests_count(void)
{
    return (int)(sizeof(g_tests) / sizeof(g_tests[0]));
}

const TestStep *tests_get(int i)
{
    if (i < 0 || i >= tests_count())
        return 0;
    return &g_tests[i];
}

/* print the note, wrapping at the embedded newline we put in the string */
static void print_note(const TestStep *t, int index)
{
    printf(" %sstep %d%s  %s", C_BOLD, index + 1, C_RESET, t->note);
    printf("\n %sexercises%s  %s%s%s\n", C_DIM, C_RESET, C_BCYN, t->expect, C_RESET);
}

void tests_run(Sim *s, int interactive)
{
    int autorun = !interactive;
    int n = tests_count();

    ui_clear_screen();
    ui_hide_cursor();

    for (int i = 0; i < n; i++) {
        const TestStep *t = tests_get(i);

        s->headline = ui_fmt("test %d of %d", i + 1, n);
        sim_log(s, "======== STEP %d ========", i + 1);
        sim_step(s, t->req, t->nreq);

        if (!autorun) {
            sim_render(s);
            print_note(t, i);
            ui_show_cursor();
            int c = ui_pause(ui_fmt("\n %s[ENTER] next   [a] run the rest   [q] stop%s  ",
                                    C_DIM, C_RESET));
            ui_hide_cursor();
            if (c == 'q')
                break;
            if (c == 'a')
                autorun = 1;
        }
    }

    ui_show_cursor();
    s->headline = "run complete";
    ui_clear_screen();
    sim_render(s);

    int lc = cov_covered(DIAG_LOCAL),  lt = cov_total(DIAG_LOCAL);
    int rc = cov_covered(DIAG_REMOTE), rt = cov_total(DIAG_REMOTE);
    printf("\n");
    if (lc == lt && rc == rt)
        printf(" %s ALL %d ARCS OF BOTH STATE DIAGRAMS WERE ACTIVATED %s\n",
               ui_has_color() ? "\033[42;30m" : "", lt + rt,
               ui_has_color() ? "\033[0m" : "");
    else
        printf(" %sNOT all arcs were taken: local %d/%d, remote %d/%d%s\n",
               C_BRED, lc, lt, rc, rt, C_RESET);

    if (s->check_fails == 0)
        printf(" %s %ld data reads all returned the correct value %s\n",
               ui_has_color() ? "\033[42;30m" : "", s->checks,
               ui_has_color() ? "\033[0m" : "");
    else
        printf(" %s%ld of %ld reads returned stale data%s\n",
               C_BRED, s->check_fails, s->checks, C_RESET);

    ui_pause("\n press ENTER to go back to the menu ");
}

void tests_show_table(void)
{
    ui_clear_screen();
    ui_banner(std::string(C_BOLD) + "TEST VECTOR TABLE" + C_RESET,
              ui_fmt("%d steps", tests_count()));

    ui_box_top("ADDRESSES USED");
    ui_box_row(ui_fmt("A = 0x%06X   set %3u  tag %02X", ADDR_A, addr_set(ADDR_A), addr_tag(ADDR_A)));
    ui_box_row(ui_fmt("B = 0x%06X   set %3u  tag %02X", ADDR_B, addr_set(ADDR_B), addr_tag(ADDR_B)));
    ui_box_row(ui_fmt("C = 0x%06X   set %3u  tag %02X", ADDR_C, addr_set(ADDR_C), addr_tag(ADDR_C)));
    ui_box_row(ui_fmt("D = 0x%06X   set %3u  tag %02X", ADDR_D, addr_set(ADDR_D), addr_tag(ADDR_D)));
    ui_box_row("");
    ui_box_row(ui_fmt("A, B and C deliberately share set %u so that a %d-way set cannot hold",
                      addr_set(ADDR_A), NUM_WAYS));
    ui_box_row("all three. That is how the Replace arcs of the local diagram get taken.");
    ui_box_bottom();

    ui_box_top("VECTORS");
    ui_box_row(std::string(C_DIM) + ui_pad("#", 4) + ui_pad("requests", 34)
               + "transitions this step is meant to take" + C_RESET);
    for (int i = 0; i < tests_count(); i++) {
        const TestStep *t = tests_get(i);
        std::string reqs;
        for (int k = 0; k < t->nreq; k++) {
            const Request *r = &t->req[k];
            reqs += ui_fmt("P%d %s 0x%06X  ", r->cpu,
                           r->op == OP_WRITE ? "WR" : "RD", r->addr);
        }
        std::string row = ui_pad(ui_fmt("%d", i + 1), 4)
                        + ui_pad(reqs, 34)
                        + std::string(C_BCYN) + t->expect + C_RESET;
        ui_box_row(row);
    }
    ui_box_bottom();

    ui_pause("\n   press ENTER to go back to the menu ");
}
