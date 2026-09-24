/* ==========================================================================
 * memory.h  --  the 1 MB shared main memory behind the MemBus.
 *
 * We model the real contents (one 32-bit word per 4 bytes) so the simulator
 * can prove that coherence actually works: every value a processor reads is
 * compared against a golden copy of what the last writer stored.
 * ========================================================================== */
#ifndef MEMORY_H
#define MEMORY_H

#include "config.h"

struct MainMemory {
    unsigned int *word;      /* MEM_WORDS entries                            */
    long reads;              /* block reads served                           */
    long writes;             /* block writes (write-backs / flushes) taken    */
    long busy_cycles;        /* cycles the memory was occupied                */
};

void mem_init(MainMemory *m);
void mem_free(MainMemory *m);
void mem_reset(MainMemory *m);

/* block-granular transfers (that is what the bus actually moves) */
void mem_read_block (MainMemory *m, unsigned addr, unsigned int *dst);
void mem_write_block(MainMemory *m, unsigned addr, const unsigned int *src);

/* single word peek, only used by the UI / shell */
unsigned int mem_peek_word(MainMemory *m, unsigned addr);

#endif /* MEMORY_H */
