#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include "benchmark.h"
#include "file_io.h"
#include "serial_aes.h"
#include "omp_scheduler.h"
#include "opencl_aes.h"
#include "mpi_handler.h"

// returns current time in seconds
// MPI_Wtime is the most accurate timer for MPI programs
double get_time() {
    return MPI_Wtime();
}

// runs all three modes and measures time for each
BenchmarkResult benchmark_run(const char *filepath,
                               const uint8_t *key,
                               Strategy *s) {
    BenchmarkResult r = {0};
    r.file_size   = s->file_size;
    r.mpi_procs   = s->num_mpi_procs;
    r.cpu_threads = s->num_threads;

    FileBuffer file = read_file(filepath);
    if (!file.data) return r;

    uint8_t *output = (uint8_t *)malloc(file.size);
    uint8_t  nonce[16];
    generate_nonce(nonce);

    // ── Serial run ──────────────────────────
    printf("\n[BENCH] Running serial...\n");
    double t1 = get_time();

    aes_ctr_encrypt(file.data, output, file.size,
                    key, nonce, 0);

    double t2 = get_time();
    r.serial_time = t2 - t1;
    printf("[BENCH] Serial done: %.4f seconds\n", r.serial_time);

    // ── MPI + OpenMP run ────────────────────
    printf("\n[BENCH] Running MPI + OpenMP...\n");
    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    MPI_Barrier(MPI_COMM_WORLD);  // sync all processes
    t1 = get_time();

    Chunk chunk;
    mpi_scatter_file(&file, &chunk, s, rank, nprocs);

    uint8_t *chunk_out = (uint8_t *)malloc(chunk.size);
    omp_encrypt_chunk(&chunk, chunk_out, key, nonce, s);

    uint8_t  *gathered = NULL;
    uint64_t  total    = 0;
    mpi_gather_results(&chunk, &gathered, &total, s, rank, nprocs);

    MPI_Barrier(MPI_COMM_WORLD);
    t2 = get_time();

    if (rank == 0) {
        r.cpu_time    = t2 - t1;
        r.speedup_cpu = r.serial_time / r.cpu_time;
        printf("[BENCH] MPI + OpenMP done: %.4f seconds\n",
               r.cpu_time);
    }

    free(chunk_out);
    free_chunk(&chunk);
    if (gathered) free(gathered);

    // ── MPI + OpenCL run ────────────────────
    if (s->gpu_available) {
        printf("\n[BENCH] Running MPI + OpenCL...\n");

        OpenCLContext ctx;
        opencl_init(&ctx);

        MPI_Barrier(MPI_COMM_WORLD);
        t1 = get_time();

        Chunk chunk2;
        mpi_scatter_file(&file, &chunk2, s, rank, nprocs);

        uint8_t *gpu_out = (uint8_t *)malloc(chunk2.size);

        if (ctx.ready)
            opencl_encrypt_chunk(&ctx, &chunk2, gpu_out,
                                 key, nonce, s);

        uint8_t  *gathered2 = NULL;
        uint64_t  total2    = 0;
        mpi_gather_results(&chunk2, &gathered2, &total2,
                           s, rank, nprocs);

        MPI_Barrier(MPI_COMM_WORLD);
        t2 = get_time();

        if (rank == 0) {
            r.gpu_time    = t2 - t1;
            r.speedup_gpu = r.serial_time / r.gpu_time;
            printf("[BENCH] MPI + OpenCL done: %.4f seconds\n",
                   r.gpu_time);
        }

        free(gpu_out);
        free_chunk(&chunk2);
        if (gathered2) free(gathered2);
        opencl_cleanup(&ctx);
    }

    free(output);
    free_buffer(&file);
    return r;
}

// prints a clean benchmark results table
void benchmark_print(BenchmarkResult *r) {
    if (r->serial_time == 0) return;

    printf("\n");
    printf("╔══════════════════════════════════════════╗\n");
    printf("║           Benchmark Results               ║\n");
    printf("╠══════════════════════════════════════════╣\n");
    printf("║ File size    : %6llu MB                  ║\n",
           (unsigned long long)(r->file_size / (1024*1024)));
    printf("║ MPI procs    : %6d                      ║\n",
           r->mpi_procs);
    printf("║ OMP threads  : %6d                      ║\n",
           r->cpu_threads);
    printf("╠══════════════════════════════════════════╣\n");
    printf("║ Serial       : %8.4f seconds            ║\n",
           r->serial_time);
    printf("║ MPI + OpenMP : %8.4f seconds            ║\n",
           r->cpu_time);
    if (r->gpu_time > 0)
    printf("║ MPI + OpenCL : %8.4f seconds            ║\n",
           r->gpu_time);
    printf("╠══════════════════════════════════════════╣\n");
    printf("║ CPU speedup  :       %6.2fx              ║\n",
           r->speedup_cpu);
    if (r->speedup_gpu > 0)
    printf("║ GPU speedup  :       %6.2fx              ║\n",
           r->speedup_gpu);
    printf("╚══════════════════════════════════════════╝\n\n");
}