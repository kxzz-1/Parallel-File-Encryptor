#define CL_TARGET_OPENCL_VERSION 300
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opencl_aes.h"
#include "serial_aes.h"

// loads the kernel source from file
static char *load_kernel_source(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[ERROR] Cannot open kernel: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *src = (char *)malloc(size + 1);
    if (!src) {
        fclose(f);
        return NULL;
    }
    size_t read_bytes = fread(src, 1, size, f);
    if (read_bytes != (size_t)size) {
        fprintf(stderr, "[ERROR] Could not read entire kernel: %s\\n", path);
        free(src);
        fclose(f);
        return NULL;
    }
    src[size] = '\0';
    fclose(f);
    return src;
}

// sets up OpenCL — platform, device, context, queue, kernel
int opencl_init(OpenCLContext *ctx) {
    ctx->ready = 0;
    cl_int err;

    err = clGetPlatformIDs(1, &ctx->platform, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[ERROR] No OpenCL platform found\n");
        return -1;
    }

    err = clGetDeviceIDs(ctx->platform, CL_DEVICE_TYPE_GPU,
                         1, &ctx->device, NULL);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[ERROR] No OpenCL GPU found\n");
        return -1;
    }

    // print GPU name
    char name[128];
    clGetDeviceInfo(ctx->device, CL_DEVICE_NAME,
                    sizeof(name), name, NULL);
    printf("[INFO] OpenCL device: %s\n", name);

    ctx->context = clCreateContext(NULL, 1, &ctx->device,
                                   NULL, NULL, &err);
    if (err != CL_SUCCESS) return -1;

    ctx->queue = clCreateCommandQueueWithProperties(
                     ctx->context, ctx->device, 0, &err);
    if (err != CL_SUCCESS) return -1;

    // load and build kernel
    char *src = load_kernel_source("kernels/aes_kernel.cl");
    if (!src) return -1;

    ctx->program = clCreateProgramWithSource(
                       ctx->context, 1,
                       (const char **)&src, NULL, &err);
    free(src);
    if (err != CL_SUCCESS) return -1;

    err = clBuildProgram(ctx->program, 1, &ctx->device,
                         NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        // print build errors if any
        size_t log_size;
        clGetProgramBuildInfo(ctx->program, ctx->device,
                              CL_PROGRAM_BUILD_LOG,
                              0, NULL, &log_size);
        char *log = (char *)malloc(log_size);
        clGetProgramBuildInfo(ctx->program, ctx->device,
                              CL_PROGRAM_BUILD_LOG,
                              log_size, log, NULL);
        fprintf(stderr, "[ERROR] Kernel build failed:\n%s\n", log);
        free(log);
        return -1;
    }

    ctx->kernel = clCreateKernel(ctx->program,
                                 "aes_ctr_kernel", &err);
    if (err != CL_SUCCESS) return -1;

    ctx->ready = 1;
    printf("[OK] OpenCL initialized\n");
    return 0;
}

// encrypts a chunk using the GPU
void opencl_encrypt_chunk(OpenCLContext *ctx, Chunk *chunk,
                          uint8_t *output, const uint8_t *key,
                          const uint8_t *nonce, Strategy *s) {
    cl_int err;

    // expand key on CPU — send round keys to GPU
    uint32_t round_keys[60];
    aes_key_expansion(key, round_keys);

    uint64_t total_blocks = (chunk->size + 15) / 16;
    uint64_t padded_size  = total_blocks * 16;

    // pad input to multiple of 16
    uint8_t *padded = (uint8_t *)calloc(padded_size, 1);
    memcpy(padded, chunk->data, chunk->size);

    // create GPU buffers
    cl_mem buf_in  = clCreateBuffer(ctx->context,
                         CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                         padded_size, padded, &err);
    cl_mem buf_out = clCreateBuffer(ctx->context,
                         CL_MEM_WRITE_ONLY,
                         padded_size, NULL, &err);
    cl_mem buf_rk  = clCreateBuffer(ctx->context,
                         CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                         60 * sizeof(uint32_t), round_keys, &err);
    cl_mem buf_non = clCreateBuffer(ctx->context,
                         CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                         8, (void *)nonce, &err);

    // set kernel arguments
    clSetKernelArg(ctx->kernel, 0, sizeof(cl_mem), &buf_in);
    clSetKernelArg(ctx->kernel, 1, sizeof(cl_mem), &buf_out);
    clSetKernelArg(ctx->kernel, 2, sizeof(cl_mem), &buf_rk);
    clSetKernelArg(ctx->kernel, 3, sizeof(cl_mem), &buf_non);
    clSetKernelArg(ctx->kernel, 4, sizeof(cl_ulong),
                   &chunk->counter_offset);
    clSetKernelArg(ctx->kernel, 5, sizeof(cl_ulong),
                   &total_blocks);

    // launch kernel — one thread per block
    size_t global_size = (size_t)total_blocks;
    clEnqueueNDRangeKernel(ctx->queue, ctx->kernel,
                           1, NULL, &global_size,
                           NULL, 0, NULL, NULL);
    clFinish(ctx->queue);

    // read results back
    uint8_t *gpu_out = (uint8_t *)malloc(padded_size);
    clEnqueueReadBuffer(ctx->queue, buf_out, CL_TRUE,
                        0, padded_size, gpu_out,
                        0, NULL, NULL);

    // copy only original size (trim padding)
    memcpy(output, gpu_out, chunk->size);

    printf("[OK] Process %d — GPU encrypted %llu bytes\n",
           chunk->rank, (unsigned long long)chunk->size);

    // cleanup
    free(padded);
    free(gpu_out);
    clReleaseMemObject(buf_in);
    clReleaseMemObject(buf_out);
    clReleaseMemObject(buf_rk);
    clReleaseMemObject(buf_non);
}

// releases all OpenCL resources
void opencl_cleanup(OpenCLContext *ctx) {
    if (!ctx->ready) return;
    clReleaseKernel(ctx->kernel);
    clReleaseProgram(ctx->program);
    clReleaseCommandQueue(ctx->queue);
    clReleaseContext(ctx->context);
    ctx->ready = 0;
    printf("[OK] OpenCL cleaned up\n");
}