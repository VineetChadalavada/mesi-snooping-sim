/* ==========================================================================
 * config.h
 *
 * All the architectural parameters for Part 1 of the assignment, in one
 * place so that they are easy to find (and easy to defend in the report).
 *
 *   4 processors, each with its own 64 KB L1 cache.
 *   The L1 has the shape of the Opteron data cache (Fig. B.5 of H&P):
 *   64 KB, 2-way set associative, 64-byte blocks, LRU, write-back,
 *   write-allocate.
 *   All 4 caches hang off one shared MemBus together with a 1 MB memory.
 * ========================================================================== */
#ifndef CONFIG_H
#define CONFIG_H

#define NUM_CPUS         4

/* ---- L1 cache geometry (Opteron, Figure B.5) ---------------------------- */
#define L1_SIZE_BYTES    (64 * 1024)
#define BLOCK_BYTES      64
#define NUM_WAYS         2
#define NUM_SETS         (L1_SIZE_BYTES / (BLOCK_BYTES * NUM_WAYS))  /* 512   */
#define WORDS_PER_BLOCK  (BLOCK_BYTES / 4)                           /* 16    */

/* ---- shared main memory ------------------------------------------------- */
#define MEM_SIZE_BYTES   (1024 * 1024)                               /* 1 MB  */
#define MEM_WORDS        (MEM_SIZE_BYTES / 4)
#define ADDR_BITS        20                                          /* 1 MB  */

/* ---- how a physical address is chopped up ------------------------------- *
 *
 *   19                 15 14                 6 5                 0
 *  +---------------------+---------------------+-------------------+
 *  |     tag  (5 b)      |    index  (9 b)     |   offset  (6 b)   |
 *  +---------------------+---------------------+-------------------+
 */
#define OFFSET_BITS      6                       /* 64 bytes per block       */
#define INDEX_BITS       9                       /* 512 sets                 */
#define TAG_BITS         (ADDR_BITS - INDEX_BITS - OFFSET_BITS)      /* 5     */

/* Two addresses land in the same set when they are 32 KB apart.
 * Handy when you want to force an eviction in the test vectors.          */
#define SET_STRIDE       (BLOCK_BYTES * NUM_SETS)                    /* 32768 */

/* ---- timing, straight out of the assignment sheet ----------------------- */
#define T_CACHE          1      /* cache access time                        */
#define T_MEM            3      /* main memory access time                  */
#define T_BUS            1      /* MemBus latency, after the arbiter grants  */
#define T_ARB            1      /* one cycle to arbitrate                    */

#endif /* CONFIG_H */
