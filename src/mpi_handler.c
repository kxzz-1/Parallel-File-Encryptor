#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include "mpi_handler.h"

// master process splits file and sends one chunk to each process
// worker processes receive their chunk
void mpi_scatter_file(FileBuffer *file, Chunk *chunk,
                      Strategy *s, int rank, int nprocs) {

    // arrays only master needs
    int      *sendcounts = NULL;
    int      *displs     = NULL;
    uint64_t *offsets    = NULL;

    if (rank == 0) {
        sendcounts = (int *)malloc(nprocs * sizeof(int));
        displs     = (int *)malloc(nprocs * sizeof(int));
        offsets    = (uint64_t *)malloc(nprocs * sizeof(uint64_t));

        uint64_t remaining = file->size;
        uint64_t pos       = 0;

        /* base chunk size per proc for this buffer, aligned to AES block size (16) */
        uint64_t base_cs = file->size / nprocs;
        base_cs -= (base_cs % 16);

        for (int i = 0; i < nprocs; i++) {
            // last process gets any leftover bytes
            uint64_t cs = (i == nprocs - 1)
                          ? remaining
                          : base_cs;

            sendcounts[i] = (int)cs;
            displs[i]     = (int)pos;
            offsets[i]    = pos;

            pos       += cs;
            remaining -= cs;

            printf("[INFO] Chunk %d → process %d (%d bytes)\n",
                   i, i, sendcounts[i]);
        }
    }

    // broadcast chunk sizes so every process knows its size
    int my_count = 0;
    MPI_Scatter(sendcounts, 1, MPI_INT,
                &my_count,  1, MPI_INT,
                0, MPI_COMM_WORLD);

    // broadcast counter offsets so AES counters don't collide
    uint64_t my_offset = 0;
    MPI_Scatter(offsets, 1, MPI_UINT64_T,
                &my_offset, 1, MPI_UINT64_T,
                0, MPI_COMM_WORLD);

    // allocate memory for this process's chunk
    chunk->size           = (uint64_t)my_count;
    chunk->offset         = my_offset;
    chunk->counter_offset = my_offset / 16; // AES block offset
    chunk->rank           = rank;
    chunk->data           = (uint8_t *)malloc(chunk->size);

    if (!chunk->data) {
        fprintf(stderr, "[ERROR] MPI Process %d: Failed to allocate memory for localized data chunk.\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // scatter the actual file data
    MPI_Scatterv(
        rank == 0 ? file->data : NULL,
        sendcounts, displs, MPI_BYTE,
        chunk->data, my_count, MPI_BYTE,
        0, MPI_COMM_WORLD
    );

    printf("[OK] Process %d received chunk "
           "(%llu bytes, counter offset: %llu)\n",
           rank,
           (unsigned long long)chunk->size,
           (unsigned long long)chunk->counter_offset);

    // master frees its helper arrays
    if (rank == 0) {
        free(sendcounts);
        free(displs);
        free(offsets);
    }
}

// all processes send their encrypted chunk back to master
// master reassembles in correct order
void mpi_gather_results(Chunk *chunk, uint8_t **output,
                        uint64_t *total_size,
                        Strategy *s, int rank, int nprocs) {

    int      *recvcounts = NULL;
    int      *displs     = NULL;

    if (rank == 0) {
        recvcounts = (int *)malloc(nprocs * sizeof(int));
        displs     = (int *)malloc(nprocs * sizeof(int));
    }

    // tell master how big each chunk is
    int my_count = (int)chunk->size;
    MPI_Gather(&my_count,   1, MPI_INT,
               recvcounts,  1, MPI_INT,
               0, MPI_COMM_WORLD);

    if (rank == 0) {
        // calculate displacements for reassembly
        *total_size = 0;
        for (int i = 0; i < nprocs; i++) {
            displs[i]    = (int)*total_size;
            *total_size += recvcounts[i];
        }

        *output = (uint8_t *)malloc(*total_size);
        if (!*output) {
            fprintf(stderr, "[ERROR] MPI Master: Failed to allocate memory to gather parallel results from all processes.\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    // gather all encrypted chunks back to master
    MPI_Gatherv(
        chunk->data,  my_count,   MPI_BYTE,
        rank == 0 ? *output : NULL,
        recvcounts, displs, MPI_BYTE,
        0, MPI_COMM_WORLD
    );

    if (rank == 0) {
        printf("[OK] All chunks gathered — total: %llu bytes\n",
               (unsigned long long)*total_size);
        free(recvcounts);
        free(displs);
    }
}

// frees chunk memory
void free_chunk(Chunk *chunk) {
    if (chunk->data) {
        free(chunk->data);
        chunk->data = NULL;
        chunk->size = 0;
    }
}