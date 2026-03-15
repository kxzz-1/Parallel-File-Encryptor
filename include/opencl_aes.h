#ifndef OPENCL_AES_H
#define OPENCL_AES_H

#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#include <stdint.h>
#include "strategy.h"
#include "mpi_handler.h"

// holds all OpenCL context — created once, reused
typedef struct {
    cl_platform_id   platform;
    cl_device_id     device;
    cl_context       context;
    cl_command_queue queue;
    cl_program       program;
    cl_kernel        kernel;
    int              ready;    // 1 if setup succeeded
} OpenCLContext;

// function declarations
int  opencl_init(OpenCLContext *ctx);
void opencl_encrypt_chunk(OpenCLContext *ctx, Chunk *chunk,
                          uint8_t *output, const uint8_t *key,
                          const uint8_t *nonce, Strategy *s);
void opencl_cleanup(OpenCLContext *ctx);

#endif