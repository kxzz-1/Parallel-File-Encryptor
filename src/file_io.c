#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/hmac.h>
#include "file_io.h"

// reads any file into memory — works for ALL file types
FileBuffer read_file(const char *path) {
    FileBuffer buf = {NULL, 0};

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[ERROR] Failed to open file for reading: %s. Check if the file exists and you have permissions.\n", path);
        return buf;
    }

    // find file size
    fseek(f, 0, SEEK_END);
    buf.size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // load entire file into memory
    buf.data = (uint8_t *)malloc(buf.size);
    if (!buf.data) {
        fprintf(stderr, "[ERROR] Memory allocation failed. The file might be too large for your system's available RAM.\n");
        fclose(f);
        buf.size = 0;
        return buf;
    }

    size_t read_bytes = fread(buf.data, 1, buf.size, f);
    if (read_bytes != buf.size) {
        fprintf(stderr, "[ERROR] Read operation interrupted or file is corrupted: %s\n", path);
        free(buf.data);
        buf.data = NULL;
        fclose(f);
        buf.size = 0;
        return buf;
    }
    fclose(f);

    printf("[INFO] Read: %s (%llu bytes)\n", path,
           (unsigned long long)buf.size);
    return buf;
}

// generates 16 random bytes for the nonce
// uses /dev/urandom — best random source on Linux
void generate_nonce(uint8_t *nonce) {
    FILE *rng = fopen("/dev/urandom", "rb");
    if (rng) {
        if (fread(nonce, 1, NONCE_SIZE, rng) != NONCE_SIZE) {
            fprintf(stderr, "[WARNING] Failed to read enough random bytes from /dev/urandom\\n");
        }
        fclose(rng);
    } else {
        // fallback if /dev/urandom unavailable
        srand((unsigned int)time(NULL));
        for (int i = 0; i < NONCE_SIZE; i++)
            nonce[i] = (uint8_t)(rand() & 0xFF);
    }
}

// writes encrypted file: [magic][nonce][original_size][data]
int write_encrypted_file(const char *path, FileHeader *header,
                         uint8_t *data, uint64_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[ERROR] Failed to create output file: %s. Check disk space and folder permissions.\n", path);
        return -1;
    }

    fwrite(header, 1, sizeof(FileHeader), f);
    fwrite(data,   1, size,               f);
    fclose(f);

    printf("[OK] Encrypted file written: %s\n", path);
    return 0;
}

// reads just the 28-byte header from an encrypted file
FileHeader read_header(const char *path) {
    FileHeader header = {0};

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[ERROR] Failed to open encrypted file: %s.\n", path);
        return header;
    }

    if (fread(&header, 1, sizeof(FileHeader), f) != sizeof(FileHeader)) {
        fprintf(stderr, "[ERROR] File header is incomplete or corrupted in: %s. This is likely not a valid encrypted file.\n", path);
        fclose(f);
        memset(&header, 0, sizeof(header));
        return header;
    }
    fclose(f);

    // verify this is actually our encrypted file
    if (memcmp(header.magic, MAGIC, MAGIC_SIZE) != 0) {
        fprintf(stderr, "[ERROR] Validation failed: This file was not encrypted by this program (invalid magic bytes).\n");
        memset(&header, 0, sizeof(header));
    }

    return header;
}

// reads everything after the header (the actual encrypted bytes)
uint8_t *read_encrypted_data(const char *path, uint64_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long total  = ftell(f);
    if (total < HEADER_SIZE) {
        fprintf(stderr, "[ERROR] File Size Error: The file '%s' is too small to contain the required encryption header.\n", path);
        fclose(f);
        return NULL;
    }
    *out_size   = (uint64_t)(total - HEADER_SIZE);
    fseek(f, HEADER_SIZE, SEEK_SET);

    uint8_t *data = (uint8_t *)malloc(*out_size);
    if (!data) { fclose(f); return NULL; }

    if (fread(data, 1, *out_size, f) != *out_size) {
        fprintf(stderr, "[ERROR] Failed to read the encrypted data block from: %s.\n", path);
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return data;
}

// writes the final decrypted output file
int write_decrypted_file(const char *path, uint8_t *data, uint64_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[ERROR] Failed to create output file: %s. Check disk space and folder permissions.\n", path);
        return -1;
    }
    fwrite(data, 1, size, f);
    fclose(f);
    printf("[OK] Decrypted file written: %s\n", path);
    return 0;
}

// frees memory allocated by read_file
void free_buffer(FileBuffer *buf) {
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
        buf->size = 0;
    }
}