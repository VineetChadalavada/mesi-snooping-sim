#include "mesi.h"

/* The table below IS the pair of state diagrams, written out as text.
 * Keep the order identical to the enum in mesi.h. */
const TransInfo g_trans[NUM_TRANSITIONS] = {
 /* tag                       diagram       from  event                action                to  */
  { "I --PrRd--> E",          DIAG_LOCAL,   "I",  "PrRd, no sharer",   "issue BusRd",        "E" },
  { "I --PrRd--> S",          DIAG_LOCAL,   "I",  "PrRd, sharer(s)",   "issue BusRd",        "S" },
  { "I --PrWr--> M",          DIAG_LOCAL,   "I",  "PrWr",              "issue BusRdX",       "M" },
  { "S --PrRd--> S",          DIAG_LOCAL,   "S",  "PrRd",              "read hit, no bus",   "S" },
  { "S --PrWr--> M",          DIAG_LOCAL,   "S",  "PrWr",              "issue BusUpgr",      "M" },
  { "S --Replace--> I",       DIAG_LOCAL,   "S",  "Replace",           "silent, block clean","I" },
  { "E --PrRd--> E",          DIAG_LOCAL,   "E",  "PrRd",              "read hit, no bus",   "E" },
  { "E --PrWr--> M",          DIAG_LOCAL,   "E",  "PrWr",              "silent upgrade",     "M" },
  { "E --Replace--> I",       DIAG_LOCAL,   "E",  "Replace",           "silent, block clean","I" },
  { "M --PrRd--> M",          DIAG_LOCAL,   "M",  "PrRd",              "read hit, no bus",   "M" },
  { "M --PrWr--> M",          DIAG_LOCAL,   "M",  "PrWr",              "write hit, no bus",  "M" },
  { "M --Replace--> I",       DIAG_LOCAL,   "M",  "Replace",           "write back block",   "I" },

  { "S --BusRd--> S",         DIAG_REMOTE,  "S",  "snoop BusRd",       "assert SHARED",      "S" },
  { "S --BusRdX--> I",        DIAG_REMOTE,  "S",  "snoop BusRdX",      "invalidate",         "I" },
  { "S --BusUpgr--> I",       DIAG_REMOTE,  "S",  "snoop BusUpgr",     "invalidate",         "I" },
  { "E --BusRd--> S",         DIAG_REMOTE,  "E",  "snoop BusRd",       "assert SHARED",      "S" },
  { "E --BusRdX--> I",        DIAG_REMOTE,  "E",  "snoop BusRdX",      "invalidate",         "I" },
  { "M --BusRd--> S",         DIAG_REMOTE,  "M",  "snoop BusRd",       "Flush + SHARED",     "S" },
  { "M --BusRdX--> I",        DIAG_REMOTE,  "M",  "snoop BusRdX",      "Flush, invalidate",  "I" },
  { "I --snoop--> I",         DIAG_REMOTE,  "I",  "snoop any txn",     "no action",          "I" }
};

static long s_count[NUM_TRANSITIONS];

const char *state_name(int st)
{
    switch (st) {
        case ST_M: return "M";
        case ST_E: return "E";
        case ST_S: return "S";
        default:   return "I";
    }
}

const char *state_long(int st)
{
    switch (st) {
        case ST_M: return "Modified";
        case ST_E: return "Exclusive";
        case ST_S: return "Shared";
        default:   return "Invalid";
    }
}

void cov_reset(void)
{
    for (int i = 0; i < NUM_TRANSITIONS; i++)
        s_count[i] = 0;
}

void cov_hit(int transition)
{
    if (transition >= 0 && transition < NUM_TRANSITIONS)
        s_count[transition]++;
}

long cov_count(int transition)
{
    if (transition < 0 || transition >= NUM_TRANSITIONS)
        return 0;
    return s_count[transition];
}

int cov_covered(int diagram)
{
    int n = 0;
    for (int i = 0; i < NUM_TRANSITIONS; i++)
        if (g_trans[i].diagram == diagram && s_count[i] > 0)
            n++;
    return n;
}

int cov_total(int diagram)
{
    int n = 0;
    for (int i = 0; i < NUM_TRANSITIONS; i++)
        if (g_trans[i].diagram == diagram)
            n++;
    return n;
}
