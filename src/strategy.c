#define CL_TARGET_OPENCL_VERSION 300
#include <stdio.h>
#include <omp.h>
#include "strategy.h"

#ifdef __APPLE__
  #include <OpenCL/opencl.h>
#else
  #include <CL/cl.h>
#endif

// detects whether an OpenCL GPU is available
// fills gpu_mem_out with how much memory it has
// returns 1 if found, 0 if not
int detect_gpu(uint64_t *gpu_mem_out) {
    *gpu_mem_out = 0;

    cl_platform_id platform;
    cl_device_id   device;
    cl_uint        num_platforms;

    // check if any OpenCL platforms exist
    if (clGetPlatformIDs(1, &platform, &num_platforms) != CL_SUCCESS)
        return 0;
    if (num_platforms == 0)
        return 0;

    // check if any GPU devices exist on that platform
    if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1,
                       &device, NULL) != CL_SUCCESS)
        return 0;

    // get how much memory the GPU has
    cl_ulong mem = 0;
    clGetDeviceInfo(device, CL_DEVICE_GLOBAL_MEM_SIZE,
                    sizeof(cl_ulong), &mem, NULL);
    *gpu_mem_out = (uint64_t)mem;

    return 1;
}

// the brain of the project
// takes everything detected at runtime and returns a Strategy
Strategy decide_strategy(uint64_t file_size, int mpi_procs,
                         int cpu_cores, int gpu_available,
                         uint64_t gpu_mem) {
    Strategy s;

    s.file_size    = file_size;
    s.num_mpi_procs = mpi_procs;
    s.cpu_cores    = cpu_cores;
    s.gpu_available = gpu_available;
    s.gpu_batch_size = 0;

    // --- decide mode based on file size ---

    if (file_size < TINY_THRESHOLD) {
        // file too small — MPI overhead not worth it
        s.mode        = MODE_SERIAL;
        s.num_chunks  = 1;
        s.chunk_size  = file_size;
        s.num_threads = 1;

    } else if (file_size < SMALL_THRESHOLD) {
        // small file — MPI + OpenMP on CPU
        s.mode        = MODE_MPI_OPENMP;
        s.num_chunks  = mpi_procs;
        s.chunk_size  = file_size / mpi_procs;
        s.num_threads = cpu_cores;

    } else if (file_size < LARGE_THRESHOLD) {
        // medium file — MPI + OpenMP scaled up
        // use multiples of mpi_procs for even load balance
        int chunks    = mpi_procs;
        if (file_size > (uint64_t)mpi_procs * MIN_CHUNK_SIZE * 2) {
             chunks = mpi_procs * 2;
        }

        s.mode        = MODE_MPI_OPENMP;
        s.num_chunks  = chunks;
        s.chunk_size  = file_size / chunks;
        s.num_threads = cpu_cores;

    } else {
        // large file — try GPU
        if (gpu_available) {
            s.mode = MODE_MPI_OPENCL;

            // use at most 25% of GPU memory per batch
            uint64_t safe_mem    = gpu_mem / 4;
            s.gpu_batch_size     = (int)(safe_mem / 16);
            s.num_chunks         = mpi_procs;
            s.chunk_size         = file_size / mpi_procs;
            s.num_threads        = cpu_cores;
        } else {
            // GPU not available — fallback to CPU
            printf("[WARN] GPU not found, falling back to MPI + OpenMP\n");
            s.mode        = MODE_MPI_OPENMP;
            s.num_chunks  = mpi_procs;
            s.chunk_size  = file_size / mpi_procs;
            s.num_threads = cpu_cores;
        }
    }

    // safety check — never allow fewer chunks than MPI processes
    // unless the file is truly tiny (AES block size limits)
    if (s.mode != MODE_SERIAL) {
        if (s.num_chunks < mpi_procs) {
            s.num_chunks = mpi_procs;
        }
        // ensure chunks are at least 16-byte aligned for AES
        s.chunk_size = s.file_size / s.num_chunks;
        if (s.chunk_size % 16 != 0) {
            s.chunk_size += (16 - (s.chunk_size % 16));
        }
    }

    return s;
}

// prints what the program decided — shown in console and GUI
void print_strategy(const Strategy *s) {
    printf("\n");
    printf("==============================================\n");
    printf("  Runtime Strategy\n");
    printf("==============================================\n");
    printf("[INFO] File size     : %llu MB\n",
           (unsigned long long)(s->file_size / MB));
    printf("[INFO] MPI processes : %d\n",   s->num_mpi_procs);
    printf("[INFO] CPU cores     : %d\n",   s->cpu_cores);
    printf("[INFO] GPU available : %s\n",   s->gpu_available ? "yes" : "no");
    printf("[INFO] Chunks        : %d\n",   s->num_chunks);
    printf("[INFO] Chunk size    : %llu MB\n",
           (unsigned long long)(s->chunk_size / MB));
    printf("[INFO] Threads/proc  : %d\n",   s->num_threads);

    printf("[INFO] Mode selected : ");
    if      (s->mode == MODE_SERIAL)     printf("Serial\n");
    else if (s->mode == MODE_MPI_OPENMP) printf("MPI + OpenMP (CPU)\n");
    else if (s->mode == MODE_MPI_OPENCL) printf("MPI + OpenCL (GPU)\n");
    printf("==============================================\n\n");
}