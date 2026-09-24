#include "cache.h"
#include "mesi.h"

unsigned addr_tag(unsigned addr)
{
    return addr >> (OFFSET_BITS + INDEX_BITS);
}

unsigned addr_set(unsigned addr)
{
    return (addr >> OFFSET_BITS) & ((1u << INDEX_BITS) - 1u);
}

unsigned addr_word(unsigned addr)
{
    return (addr & (BLOCK_BYTES - 1)) >> 2;
}

unsigned addr_block(unsigned addr)
{
    return addr & ~(unsigned)(BLOCK_BYTES - 1);
}

void cache_init(Cache *c, int id)
{
    c->id = id;
    cache_reset(c);
}

void cache_reset(Cache *c)
{
    for (int s = 0; s < NUM_SETS; s++) {
        for (int w = 0; w < NUM_WAYS; w++) {
            c->line[s][w].state = ST_I;
            c->line[s][w].tag   = 0xFFFFFFFFu;    /* means: no tag here yet  */
            c->line[s][w].lru   = 0;
            for (int i = 0; i < WORDS_PER_BLOCK; i++)
                c->line[s][w].data[i] = 0;
        }
    }
    c->lru_clock      = 0;
    c->reads          = 0;
    c->writes         = 0;
    c->read_hits      = 0;
    c->write_hits     = 0;
    c->read_misses    = 0;
    c->write_misses   = 0;
    c->upgrades       = 0;
    c->writebacks     = 0;
    c->flushes        = 0;
    c->invalidations  = 0;
    c->stall_cycles   = 0;
    c->arb_wait_cycles = 0;
}

int cache_find(Cache *c, unsigned addr)
{
    unsigned set = addr_set(addr);
    unsigned tag = addr_tag(addr);
    for (int w = 0; w < NUM_WAYS; w++) {
        if (c->line[set][w].tag == tag && c->line[set][w].state != ST_I)
            return w;
    }
    return -1;
}

int cache_state(Cache *c, unsigned addr)
{
    int w = cache_find(c, addr);
    if (w < 0)
        return ST_I;
    return c->line[addr_set(addr)][w].state;
}

int cache_victim(Cache *c, unsigned addr)
{
    unsigned set = addr_set(addr);
    unsigned tag = addr_tag(addr);
    int w;

    /* 1. we already own this frame, it is just invalid -- reuse it */
    for (w = 0; w < NUM_WAYS; w++)
        if (c->line[set][w].tag == tag && c->line[set][w].state == ST_I)
            return w;

    /* 2. an empty way.  If more than one is free take the one that has been
     *    idle longest, so a way we never used at all gets picked before one
     *    that is only temporarily invalid. */
    int best = -1;
    for (w = 0; w < NUM_WAYS; w++)
        if (c->line[set][w].state == ST_I)
            if (best < 0 || c->line[set][w].lru < c->line[set][best].lru)
                best = w;
    if (best >= 0)
        return best;

    /* 3. set is full, throw out the least recently used way */
    int victim = 0;
    for (w = 1; w < NUM_WAYS; w++)
        if (c->line[set][w].lru < c->line[set][victim].lru)
            victim = w;
    return victim;
}

void cache_touch(Cache *c, unsigned set, int way)
{
    c->lru_clock++;
    c->line[set][way].lru = c->lru_clock;
}

void cache_census(Cache *c, long out[4])
{
    out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;
    for (int s = 0; s < NUM_SETS; s++)
        for (int w = 0; w < NUM_WAYS; w++)
            out[c->line[s][w].state]++;
}
