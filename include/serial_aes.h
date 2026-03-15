#ifndef SERIAL_AES_H
#define SERIAL_AES_H

#include <stdint.h>

// AES constants
#define AES_BLOCK_SIZE    16    // AES always works on 16 byte blocks
#define AES_KEY_SIZE      32    // 256 bit key = 32 bytes
#define AES_ROUNDS        14    // AES-256 uses 14 rounds

// main encryption/decryption function
// in CTR mode encrypt and decrypt are identical
void aes_ctr_encrypt(
    const uint8_t *input,      // data to encrypt/decrypt
    uint8_t       *output,     // result goes here
    uint64_t       length,     // how many bytes
    const uint8_t *key,        // 32 byte key
    const uint8_t *nonce,      // 16 byte nonce from file header
    uint64_t       counter_offset  // which block we start from
                               // important for MPI — each process
                               // starts at a different offset
);

// helper — encrypts a single 16 byte block
void aes_encrypt_block(
    const uint8_t *input,
    uint8_t       *output,
    const uint32_t *round_keys
);

// key expansion — turns 32 byte key into round keys
void aes_key_expansion(
    const uint8_t  *key,
    uint32_t       *round_keys
);

#endif