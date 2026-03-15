#ifndef MPI_HANDLER_H
#define MPI_HANDLER_H

#include <stdint.h>
#include "strategy.h"
#include "file_io.h"

// holds one chunk of the file assigned to one MPI process
typedef struct {
    uint8_t  *data;          // the chunk bytes
    uint64_t  size;          // chunk size in bytes
    uint64_t  offset;        // where this chunk starts in original file
    uint64_t  counter_offset;// AES counter start for this chunk
    int       rank;          // which MPI process owns this
} Chunk;

// function declarations
void mpi_scatter_file(FileBuffer *file, Chunk *chunk,
                      Strategy *s, int rank, int nprocs);
void mpi_gather_results(Chunk *chunk, uint8_t **output,
                        uint64_t *total_size,
                        Strategy *s, int rank, int nprocs);
void free_chunk(Chunk *chunk);

#endif