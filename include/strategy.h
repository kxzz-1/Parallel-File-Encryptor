#ifndef STRATEGY_H
#define STRATEGY_H

#include <stdint.h>

// file size thresholds
#define MB                    (1024ULL * 1024ULL)
#define GB                    (1024ULL * MB)

#define TINY_THRESHOLD        (1   * MB)     // under 1MB   → serial
#define SMALL_THRESHOLD       (100 * MB)     // under 100MB → MPI + OpenMP
#define LARGE_THRESHOLD       (1   * GB)     // under 1GB   → MPI + OpenMP scaled
                                             // over  1GB   → MPI + OpenCL

// minimum chunk size — prevents too many tiny chunks
#define MIN_CHUNK_SIZE        (512 * 1024)   // 512KB

// modes
#define MODE_SERIAL           0
#define MODE_MPI_OPENMP       1
#define MODE_MPI_OPENCL       2

// this struct is the decision — filled once at startup
// every other module reads from this, nothing is hardcoded
typedef struct {
    int      mode;             // which mode to run
    int      num_chunks;       // how many MPI chunks
    uint64_t chunk_size;       // bytes per chunk
    int      num_threads;      // OpenMP threads per process
    int      gpu_batch_size;   // OpenCL blocks per dispatch
    int      gpu_available;    // was GPU detected?
    int      num_mpi_procs;    // how many MPI processes running
    int      cpu_cores;        // how many CPU cores detected
    uint64_t file_size;        // original file size
} Strategy;

// function declarations
Strategy decide_strategy(uint64_t file_size, int mpi_procs,
                         int cpu_cores, int gpu_available,
                         uint64_t gpu_mem);
void     print_strategy(const Strategy *s);
int      detect_gpu(uint64_t *gpu_mem_out);

#endif