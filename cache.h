/* ==========================================================================
 * cache.h  --  the L1 cache that sits next to one processor.
 *
 * Shape is the Opteron data cache of Figure B.5: 64 KB, 2-way set
 * associative, 64-byte blocks, LRU replacement, write-back, write-allocate.
 * 64 KB / (64 B * 2 ways) = 512 sets.
 * ========================================================================== */
#ifndef CACHE_H
#define CACHE_H

#include "config.h"

struct CacheLine {
    int           state;                   /* ST_I / ST_S / ST_E / ST_M      */
    unsigned      tag;
    unsigned long lru;                     /* when the CPU last used this way*/
    unsigned int  data[WORDS_PER_BLOCK];
};

struct Cache {
    int           id;
    CacheLine     line[NUM_SETS][NUM_WAYS];
    unsigned long lru_clock;

    /* statistics */
    long reads, writes;
    long read_hits, write_hits;
    long read_misses, write_misses;
    long upgrades;          /* S -> M, needed a BusUpgr                      */
    long writebacks;        /* dirty victims pushed back to memory           */
    long flushes;           /* times we supplied data to another cache       */
    long invalidations;     /* times a snoop knocked us out of a block       */
    long stall_cycles;      /* total cycles this processor waited            */
    long arb_wait_cycles;   /* of which, cycles spent waiting for the bus     */
};

/* address decode */
unsigned addr_tag   (unsigned addr);
unsigned addr_set   (unsigned addr);
unsigned addr_word  (unsigned addr);        /* word index inside the block   */
unsigned addr_block (unsigned addr);        /* address with the offset zeroed*/

void cache_init (Cache *c, int id);
void cache_reset(Cache *c);

/* returns the way holding this block in a valid state, or -1 */
int  cache_find(Cache *c, unsigned addr);

/* peek at the state without disturbing LRU; ST_I if the block is not here */
int  cache_state(Cache *c, unsigned addr);

/* pick the way to allocate into.  Preference order:
 *    1. the way that already carries this tag but sits in I (frame re-use)
 *    2. any way in state I
 *    3. the least recently used way -- this one has to be evicted           */
int  cache_victim(Cache *c, unsigned addr);

void cache_touch(Cache *c, unsigned set, int way);   /* refresh LRU          */

/* how many lines of this cache are in each of the four states */
void cache_census(Cache *c, long out[4]);

#endif /* CACHE_H */
