#ifndef FILE_IO_H
#define FILE_IO_H

#include <stdint.h>
#include <stddef.h>

// magic bytes — every encrypted file starts with these
// lets us detect if a file was encrypted by our program
#define MAGIC        "PENC"
#define MAGIC_SIZE   4
#define NONCE_SIZE   16
#define HMAC_SIZE    32
#define HEADER_SIZE  (MAGIC_SIZE + NONCE_SIZE + sizeof(uint64_t) + HMAC_SIZE)

// the first 28 bytes of every encrypted file on disk
typedef struct __attribute__((packed)) {
    uint8_t  magic[MAGIC_SIZE];
    uint8_t  nonce[NONCE_SIZE];
    uint64_t original_size;
    uint8_t  hmac[HMAC_SIZE];
} FileHeader;

// holds a file's raw bytes in memory
typedef struct {
    uint8_t  *data;
    uint64_t  size;
} FileBuffer;

// function declarations
FileBuffer  read_file(const char *path);
int         write_encrypted_file(const char *path, FileHeader *header, uint8_t *data, uint64_t size);
int         write_decrypted_file(const char *path, uint8_t *data, uint64_t size);
FileHeader  read_header(const char *path);
uint8_t    *read_encrypted_data(const char *path, uint64_t *out_size);
void        free_buffer(FileBuffer *buf);
void        generate_nonce(uint8_t *nonce);

#endif