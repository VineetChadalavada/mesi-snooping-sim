/* ==========================================================================
 * tests.h  --  the test vector suite.
 *
 * The vectors are picked so that EVERY arc of both MESI state diagrams gets
 * taken at least once, and so that the bus arbiter has to resolve real
 * contention.  Each step carries a note saying what it is there to prove.
 * ========================================================================== */
#ifndef TESTS_H
#define TESTS_H

#include "sim.h"

/* Four addresses do all the work.
 *
 *   A, B and C all land in set 65, with tags 0, 1 and 2.  A 2-way set can
 *   only hold two of them, so touching the third forces a replacement --
 *   that is how we reach the "Replace" arcs of the local diagram.
 *   Two addresses share a set when they are SET_STRIDE (32 KB) apart.
 *
 *   D lives in a different set entirely and is used for the arbitration
 *   test, so that it cannot disturb the replacement sequence.             */
#define ADDR_A  0x001040u      /* set  65, tag 0 */
#define ADDR_B  0x009040u      /* set  65, tag 1 */
#define ADDR_C  0x011040u      /* set  65, tag 2 */
#define ADDR_D  0x002080u      /* set 130, tag 0 */

struct TestStep {
    const char *note;              /* what this step demonstrates          */
    const char *expect;            /* the arc(s) we expect it to take      */
    int         nreq;
    Request     req[NUM_CPUS];
};

int             tests_count(void);
const TestStep *tests_get(int i);

/* interactive != 0 -> pause after every step and draw the dashboard */
void tests_run(Sim *s, int interactive);

/* print the test vector table (a deliverable in its own right) */
void tests_show_table(void);

#endif /* TESTS_H */
