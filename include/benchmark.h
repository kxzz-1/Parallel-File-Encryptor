#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdint.h>
#include "strategy.h"

// holds timing results for one run
typedef struct {
    double   serial_time;      // seconds taken by serial mode
    double   cpu_time;         // seconds taken by MPI + OpenMP
    double   gpu_time;         // seconds taken by MPI + OpenCL
    double   speedup_cpu;      // serial / cpu_time
    double   speedup_gpu;      // serial / gpu_time
    uint64_t file_size;        // file size tested
    int      mpi_procs;        // how many MPI processes
    int      cpu_threads;      // how many OpenMP threads
} BenchmarkResult;

// function declarations
double          get_time();
void            benchmark_print(BenchmarkResult *r);
BenchmarkResult benchmark_run(const char *filepath,
                               const uint8_t *key,
                               Strategy *s);

#endif