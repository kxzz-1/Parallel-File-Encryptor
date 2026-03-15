#ifndef OMP_SCHEDULER_H
#define OMP_SCHEDULER_H

#include <stdint.h>
#include "strategy.h"
#include "mpi_handler.h"

// encrypts a chunk using OpenMP threads
// each thread handles a slice of the chunk
void omp_encrypt_chunk(Chunk *chunk, uint8_t *output,
                       const uint8_t *key, const uint8_t *nonce,
                       Strategy *s);

#endif