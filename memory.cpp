#include "memory.h"
#include <stdlib.h>

/* Fill memory with a recognisable pattern so that, while watching the
 * dashboard, you can tell at a glance whether a word came out of DRAM or was
 * put there by a processor.  0xDA7A.... reads as DATA. */
static unsigned int initial_value(unsigned int word_index)
{
    return 0xDA7A0000u | (word_index & 0xFFFFu);
}

void mem_init(MainMemory *m)
{
    m->word = (unsigned int *)malloc(sizeof(unsigned int) * MEM_WORDS);
    mem_reset(m);
}

void mem_free(MainMemory *m)
{
    free(m->word);
    m->word = 0;
}

void mem_reset(MainMemory *m)
{
    for (int i = 0; i < MEM_WORDS; i++)
        m->word[i] = initial_value(i);
    m->reads = 0;
    m->writes = 0;
    m->busy_cycles = 0;
}

void mem_read_block(MainMemory *m, unsigned addr, unsigned int *dst)
{
    unsigned base = (addr & ~(unsigned)(BLOCK_BYTES - 1)) >> 2;
    for (int i = 0; i < WORDS_PER_BLOCK; i++)
        dst[i] = m->word[base + i];
    m->reads++;
    m->busy_cycles += T_MEM;
}

void mem_write_block(MainMemory *m, unsigned addr, const unsigned int *src)
{
    unsigned base = (addr & ~(unsigned)(BLOCK_BYTES - 1)) >> 2;
    for (int i = 0; i < WORDS_PER_BLOCK; i++)
        m->word[base + i] = src[i];
    m->writes++;
    m->busy_cycles += T_MEM;
}

unsigned int mem_peek_word(MainMemory *m, unsigned addr)
{
    return m->word[addr >> 2];
}
