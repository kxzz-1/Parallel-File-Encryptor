#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <mpi.h>
#include <omp.h>
#include <libgen.h>
#include "file_io.h"
#include "strategy.h"
#include "serial_aes.h"
#include "mpi_handler.h"
#include "omp_scheduler.h"
#include "opencl_aes.h"
#include "benchmark.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#define KEY_SIZE 32
#define STREAM_CHUNK_SIZE (64 * 1024 * 1024) // 64MB pulses

// prints usage instructions
void print_usage(const char *prog) {
    printf("\n");
    printf("  Parallel File Encrypter/Decrypter\n");
    printf("  MPI + OpenMP + OpenCL | AES-CTR\n");
    printf("\n");
    printf("  Usage:\n");
    printf("  mpirun -np 4 %s -e <file> -k <keyfile>\n", prog);
    printf("  mpirun -np 4 %s -d <file.enc> -k <keyfile>\n", prog);
    printf("  mpirun -np 4 %s -e <file> -k <keyfile> --bench\n", prog);
    printf("\n");
    printf("  Flags:\n");
    printf("  -e        encrypt\n");
    printf("  -d        decrypt\n");
    printf("  -k        key file (32 bytes)\n");
    printf("  --bench   benchmark serial vs parallel\n");
    printf("  --serial  force serial mode\n");
    printf("  --cpu     force MPI + OpenMP mode\n");
    printf("  --gpu     force MPI + OpenCL mode\n");
    printf("  --json    machine-readable status output\n");
    printf("\n");
}

// loads 32 byte key from a file
int load_key(const char *path, uint8_t *key) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        // if no key file, use a default test key
        fprintf(stderr, "[WARN] Key file not found."
                        " Using default test key.\n");
        memcpy(key, "01234567890123456789012345678901", KEY_SIZE);
        return 0;
    }
    size_t read_bytes = fread(key, 1, KEY_SIZE, f);
    if (read_bytes != KEY_SIZE) {
        fprintf(stderr, "Error: Could not read %d bytes from %s\\n", KEY_SIZE, path);
        fclose(f);
        exit(1);
    }
    fclose(f);
    printf("[INFO] Key loaded from: %s\n", path);
    return 0;
}

// builds output filename
// encrypt: file.pdf      → file.pdf.enc (same folder)
// decrypt: file.pdf.enc  → file.pdf     (same folder)
void build_output_name(const char *input, char *output,
                       int encrypting) {
    char path_tmp1[512], path_tmp2[512];
    strcpy(path_tmp1, input);
    strcpy(path_tmp2, input);

    char *dir  = dirname(path_tmp1);
    char *base = basename(path_tmp2);

    char temp_name[512];
    if (encrypting) {
        sprintf(temp_name, "%s.enc", base);
    } else {
        strcpy(temp_name, base);
        // if starts with/contains .enc, remove it for decryption
        char *ext = strstr(temp_name, ".enc");
        if (ext) *ext = '\0';
        else     strcat(temp_name, ".dec");
    }

    sprintf(output, "%s/%s", dir, temp_name);
}

int main(int argc, char *argv[]) {

    // ── Init MPI ────────────────────────────
    MPI_Init(&argc, &argv);

    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    // ── Parse arguments ─────────────────────
    char    *input_file  = NULL;
    char    *key_file    = NULL;
    char    *output_file_arg = NULL;
    int      encrypting  = 1;    // 1=encrypt, 0=decrypt
    int      bench_mode  = 0;
    int      force_mode  = -1;   // -1=auto, 0=serial, 1=cpu, 2=gpu
    int      json_mode   = 0;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-e"))       { encrypting = 1; input_file = argv[++i]; }
        else if (!strcmp(argv[i], "-d"))       { encrypting = 0; input_file = argv[++i]; }
        else if (!strcmp(argv[i], "-o"))       { output_file_arg = argv[++i]; }
        else if (!strcmp(argv[i], "-k"))       { key_file = argv[++i]; }
        else if (!strcmp(argv[i], "--bench"))  { bench_mode  = 1; }
        else if (!strcmp(argv[i], "--serial")) { force_mode  = 0; }
        else if (!strcmp(argv[i], "--cpu"))    { force_mode  = 1; }
        else if (!strcmp(argv[i], "--gpu"))    { force_mode  = 2; }
        else if (!strcmp(argv[i], "--json"))   { json_mode   = 1; }
    }

    if (!input_file && rank == 0) {
        print_usage(argv[0]);
        MPI_Finalize();
        return 1;
    }

    // ── Load key ────────────────────────────
    uint8_t key[KEY_SIZE];
    load_key(key_file, key);

    // ── Pre-calculate sizes ──────────────────
    uint64_t total_size = 0;
    FILE *f_in = NULL;
    FILE *f_out = NULL;
    FileHeader header = {0};

    if (rank == 0) {
        f_in = fopen(input_file, "rb");
        if (!f_in) {
            fprintf(stderr, "[ERROR] Could not open input file: %s (errno: %d)\n", input_file, errno);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        if (encrypting) {
            fseek(f_in, 0, SEEK_END);
            total_size = (uint64_t)ftell(f_in);
            fseek(f_in, 0, SEEK_SET);

            memcpy(header.magic, MAGIC, MAGIC_SIZE);
            generate_nonce(header.nonce);
            header.original_size = total_size;
        } else {
            header = read_header(input_file);
            if (header.original_size == 0) {
                fprintf(stderr, "[ERROR] Header validation failed for %s. Aborting.\n", input_file);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            total_size = header.original_size;
            fseek(f_in, HEADER_SIZE, SEEK_SET);
        }
        printf("[DEBUG] Rank 0: total_size = %llu, mode = %s\n", 
               (unsigned long long)total_size, encrypting ? "ENCRYPT" : "DECRYPT");
    }

    MPI_Bcast(&total_size, 1, MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);
    
    // Broadcast nonce for all modes
    uint8_t nonce[16];
    if (rank == 0) memcpy(nonce, header.nonce, 16);
    MPI_Bcast(nonce, 16, MPI_BYTE, 0, MPI_COMM_WORLD);

    // ── Decide strategy ─────────────────────
    // Detect hardware
    uint64_t gpu_mem = 0;
    int gpu_available = detect_gpu(&gpu_mem);
    int cpu_cores = omp_get_max_threads();
    Strategy s = decide_strategy(total_size, nprocs, cpu_cores, gpu_available, gpu_mem);
    
    if (force_mode == 0) s.mode = MODE_SERIAL;
    if (force_mode == 1) s.mode = MODE_MPI_OPENMP;
    if (force_mode == 2) s.mode = MODE_MPI_OPENCL;
    if (rank == 0) print_strategy(&s);

    /* ── Emit strategy JSON for GUI ── */
    if (rank == 0 && json_mode) {
        const char *mode_str = "MPI+OpenMP";
        if      (s.mode == MODE_SERIAL)     mode_str = "Serial";
        else if (s.mode == MODE_MPI_OPENCL) mode_str = "MPI+OpenCL";
        else                                mode_str = "MPI+OpenMP";
        printf("{\"status\": \"strategy\", \"mode\": \"%s\", "
               "\"mpi_procs\": %d, \"omp_threads\": %d, "
               "\"gpu\": %d, \"gpu_vram\": %llu, \"file_size\": %llu}\n",
               mode_str, nprocs, s.num_threads,
               gpu_available, (unsigned long long)gpu_mem,
               (unsigned long long)total_size);
        fflush(stdout);
    }

    // ── Benchmark Mode ──────────────────────
    if (bench_mode) {
        if (rank == 0) printf("[BENCH] Starting benchmark comparison...\n");
        BenchmarkResult r = benchmark_run(input_file, key, &s);
        if (rank == 0) benchmark_print(&r);
        MPI_Finalize();
        return 0;
    }

    // ── Prep Output ─────────────────────────
    char output_file[512];
    if (output_file_arg) {
        snprintf(output_file, sizeof(output_file), "%s", output_file_arg);
    } else {
        build_output_name(input_file, output_file, encrypting);
    }
    if (rank == 0) {
        f_out = fopen(output_file, "wb");
        if (encrypting) {
            // Write placeholder header, will seek back and update HMAC later
            fwrite(&header, 1, HEADER_SIZE, f_out);
        }
    }

    // ── Streaming Loop ──────────────────────
    EVP_MAC *mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
    EVP_MAC_CTX *mctx = EVP_MAC_CTX_new(mac);
    OSSL_PARAM params[2];
    params[0] = OSSL_PARAM_construct_utf8_string("digest", "SHA256", 0);
    params[1] = OSSL_PARAM_construct_end();
    EVP_MAC_init(mctx, key, KEY_SIZE, params);

    uint64_t processed = 0;
    uint8_t *stream_buffer = (rank == 0) ? malloc(STREAM_CHUNK_SIZE) : NULL;

    if (rank == 0 && json_mode) printf("{\"status\": \"starting\", \"total\": %llu}\n", (unsigned long long)total_size);

    /* record start time for elapsed */
    double run_start = MPI_Wtime();

    /* signal all ranks as active */
    if (json_mode) {
        /* flush stdout so GUI gets notified immediately */
        if (rank == 0) fflush(stdout);
        MPI_Barrier(MPI_COMM_WORLD);
        /* each rank signals itself as active */
        /* only rank 0 writes to stdout — send thread state for each rank */
        if (rank == 0) {
            for (int r = 0; r < nprocs; r++) {
                printf("{\"status\": \"thread\", \"rank\": %d, \"state\": \"active\"}\n", r);
            }
            fflush(stdout);
        }
    }

    OpenCLContext ctx;
    if (s.mode == MODE_MPI_OPENCL) {
        if (opencl_init(&ctx) != 0) s.mode = MODE_MPI_OPENMP;
    }

    while (processed < total_size) {
        uint64_t chunk_size = (total_size - processed > STREAM_CHUNK_SIZE) 
                               ? STREAM_CHUNK_SIZE : (total_size - processed);
        
        FileBuffer fb = {NULL, chunk_size};
        if (rank == 0) {
            if (fread(stream_buffer, 1, chunk_size, f_in) != chunk_size) {
                fprintf(stderr, "[ERROR] Read error at %llu\n", (unsigned long long)processed);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            fb.data = stream_buffer;
            if (!encrypting) EVP_MAC_update(mctx, stream_buffer, chunk_size);
        }

        // Parallel processing of this pulse
        uint8_t *processed_chunk = NULL;
        if (s.mode == MODE_SERIAL) {
            if (rank == 0) {
                processed_chunk = malloc(chunk_size);
                aes_ctr_encrypt(fb.data, processed_chunk, chunk_size, key, nonce, processed/16);
            }
        } else {
            Chunk chunk;
            mpi_scatter_file(&fb, &chunk, &s, rank, nprocs);
            chunk.counter_offset = (processed + chunk.offset) / 16;
            
            uint8_t *chunk_out = malloc(chunk.size);
            if (s.mode == MODE_MPI_OPENCL && ctx.ready)
                opencl_encrypt_chunk(&ctx, &chunk, chunk_out, key, nonce, &s);
            else
                omp_encrypt_chunk(&chunk, chunk_out, key, nonce, &s);
            
            memcpy(chunk.data, chunk_out, chunk.size);
            free(chunk_out);
            
            uint64_t gathered_size;
            mpi_gather_results(&chunk, &processed_chunk, &gathered_size, &s, rank, nprocs);
            free_chunk(&chunk);
        }

        if (rank == 0 && processed_chunk) {
            if (encrypting) EVP_MAC_update(mctx, processed_chunk, chunk_size);
            fwrite(processed_chunk, 1, chunk_size, f_out);
            if (json_mode) {
                printf("{\"status\": \"progress\", \"processed\": %llu}\n",
                       (unsigned long long)(processed + chunk_size));
                fflush(stdout);
            } else {
                printf("[INFO] Processed %llu/%llu bytes\n",
                       (unsigned long long)(processed + chunk_size),
                       (unsigned long long)total_size);
            }
            free(processed_chunk);
        }
        processed += chunk_size;
    }

    // ── Finalize ────────────────────────────
    if (rank == 0) {
        double elapsed = MPI_Wtime() - run_start;

        if (encrypting) {
            // Auth header metadata too
            EVP_MAC_update(mctx, header.magic, MAGIC_SIZE);
            EVP_MAC_update(mctx, header.nonce, NONCE_SIZE);
            EVP_MAC_update(mctx, (uint8_t*)&header.original_size, sizeof(uint64_t));
            size_t hmac_len_out;
            EVP_MAC_final(mctx, header.hmac, &hmac_len_out, 32);
            
            fseek(f_out, 0, SEEK_SET);
            fwrite(&header, 1, HEADER_SIZE, f_out);
            if (json_mode) {
                printf("{\"status\": \"complete\", \"output\": \"%s\"}\n", output_file);
                printf("{\"status\": \"timing\", \"mode\": \"cpu\", \"time\": %.4f}\n", elapsed);
                /* signal all ranks done */
                for (int r = 0; r < nprocs; r++)
                    printf("{\"status\": \"thread\", \"rank\": %d, \"state\": \"done\"}\n", r);
                fflush(stdout);
            } else {
                printf("[OK] Encryption complete. Elapsed: %.3fs\n", elapsed);
            }
        } else {
            uint8_t computed_hmac[32];
            EVP_MAC_update(mctx, header.magic, MAGIC_SIZE);
            EVP_MAC_update(mctx, header.nonce, NONCE_SIZE);
            EVP_MAC_update(mctx, (uint8_t*)&header.original_size, sizeof(uint64_t));
            size_t hmac_len_out;
            EVP_MAC_final(mctx, computed_hmac, &hmac_len_out, 32);
            
            if (memcmp(computed_hmac, header.hmac, 32) != 0) {
                fprintf(stderr, "[ERROR] Integrity check FAILED! The file has been tampered with or the key is wrong.\n");
                if (json_mode) printf("{\"status\": \"error\", \"message\": \"Integrity check failed\"}\n");
                fclose(f_out); f_out = NULL;
                remove(output_file);
            } else {
                if (json_mode) {
                    printf("{\"status\": \"complete\", \"output\": \"%s\"}\n", output_file);
                    printf("{\"status\": \"timing\", \"mode\": \"cpu\", \"time\": %.4f}\n", elapsed);
                    for (int r = 0; r < nprocs; r++)
                        printf("{\"status\": \"thread\", \"rank\": %d, \"state\": \"done\"}\n", r);
                    fflush(stdout);
                } else {
                    printf("[OK] Decryption complete. Integrity verified. Elapsed: %.3fs\n", elapsed);
                }
            }
        }
        
        if (f_in) { fclose(f_in); f_in = NULL; }
        if (f_out) { fclose(f_out); f_out = NULL; }
        free(stream_buffer);
    }

    if (s.mode == MODE_MPI_OPENCL) opencl_cleanup(&ctx);
    EVP_MAC_CTX_free(mctx);
    EVP_MAC_free(mac);
    MPI_Finalize();
    return 0;
}