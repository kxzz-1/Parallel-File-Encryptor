#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>
#include "omp_scheduler.h"
#include "serial_aes.h"

// splits a chunk across CPU threads
// each thread encrypts its own slice independently
// no thread touches another thread's data — zero race conditions
void omp_encrypt_chunk(Chunk *chunk, uint8_t *output,
                       const uint8_t *key, const uint8_t *nonce,
                       Strategy *s) {

    // tell OpenMP how many threads to use
    // this comes from strategy — detected at runtime
    omp_set_num_threads(s->num_threads);

    int num_threads = s->num_threads;
    uint64_t total  = chunk->size;

    printf("[INFO] Process %d — OpenMP using %d threads "
           "for %llu bytes\n",
           chunk->rank, num_threads,
           (unsigned long long)total);

    // each thread gets an equal slice of the chunk
    // #pragma omp parallel for splits the loop automatically
    #pragma omp parallel for schedule(static)
    for (int t = 0; t < num_threads; t++) {

        // calculate this thread's slice
        uint64_t slice_size = total / num_threads;
        uint64_t start      = (uint64_t)t * slice_size;

        // last thread gets any leftover bytes
        uint64_t end = (t == num_threads - 1)
                       ? total
                       : start + slice_size;

        uint64_t length = end - start;

        // each thread's AES counter starts at a different offset
        // so their keystreams never overlap
        uint64_t thread_counter = chunk->counter_offset
                                + (start / AES_BLOCK_SIZE);

        // encrypt this thread's slice
        aes_ctr_encrypt(
            chunk->data  + start,   // input slice
            output       + start,   // output slice
            length,                 // slice size
            key,                    // same key for all threads
            nonce,                  // same nonce for all threads
            thread_counter          // unique counter per thread
        );

        printf("[INFO] Thread %d → encrypted bytes %llu to %llu\n",
               t,
               (unsigned long long)start,
               (unsigned long long)end);
    }

    printf("[OK] Process %d — all threads done\n", chunk->rank);
}