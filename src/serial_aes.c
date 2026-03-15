#define CL_TARGET_OPENCL_VERSION 300
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "serial_aes.h"

// ─────────────────────────────────────────
// AES lookup tables
// these are standard AES constants
// ─────────────────────────────────────────

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,
    0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,
    0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,
    0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,
    0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,
    0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,
    0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,
    0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,
    0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,
    0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,
    0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,
    0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,
    0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,
    0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,
    0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,
    0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,
    0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t rcon[11] = {
    0x00,0x01,0x02,0x04,0x08,0x10,
    0x20,0x40,0x80,0x1b,0x36
};

// multiply in GF(2^8) — needed for MixColumns step
static uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        uint8_t hi = a & 0x80;
        a <<= 1;
        if (hi) a ^= 0x1b;
        b >>= 1;
    }
    return p;
}

// ─────────────────────────────────────────
// AES key expansion
// turns your 32 byte key into 60 round keys
// ─────────────────────────────────────────
void aes_key_expansion(const uint8_t *key, uint32_t *rk) {
    int i;
    for (i = 0; i < 8; i++) {
        rk[i] = ((uint32_t)key[4*i]   << 24) |
                ((uint32_t)key[4*i+1] << 16) |
                ((uint32_t)key[4*i+2] <<  8) |
                ((uint32_t)key[4*i+3]);
    }
    for (i = 8; i < 60; i++) {
        uint32_t tmp = rk[i-1];
        if (i % 8 == 0) {
            // rotate + substitute + rcon
            tmp = ((uint32_t)sbox[(tmp >> 16) & 0xff] << 24) |
                  ((uint32_t)sbox[(tmp >>  8) & 0xff] << 16) |
                  ((uint32_t)sbox[(tmp      ) & 0xff] <<  8) |
                  ((uint32_t)sbox[(tmp >> 24) & 0xff]);
            tmp ^= ((uint32_t)rcon[i/8] << 24);
        } else if (i % 8 == 4) {
            tmp = ((uint32_t)sbox[(tmp >> 24) & 0xff] << 24) |
                  ((uint32_t)sbox[(tmp >> 16) & 0xff] << 16) |
                  ((uint32_t)sbox[(tmp >>  8) & 0xff] <<  8) |
                  ((uint32_t)sbox[(tmp      ) & 0xff]);
        }
        rk[i] = rk[i-8] ^ tmp;
    }
}

// ─────────────────────────────────────────
// AES single block encryption
// takes 16 bytes in, produces 16 bytes out
// ─────────────────────────────────────────
void aes_encrypt_block(const uint8_t *in, uint8_t *out,
                       const uint32_t *rk) {
    uint8_t s[16], t[16];
    int i;

    // copy input and XOR with first round key
    for (i = 0; i < 16; i++) {
        s[i] = in[i] ^ ((rk[i/4] >> (24 - 8*(i%4))) & 0xff);
    }

    // 13 full rounds
    for (int round = 1; round < AES_ROUNDS; round++) {

        // SubBytes — replace each byte using sbox
        for (i = 0; i < 16; i++) t[i] = sbox[s[i]];

        // ShiftRows — rotate rows left
        uint8_t tmp;
        // row 1: shift left 1
        tmp=t[1]; t[1]=t[5]; t[5]=t[9]; t[9]=t[13]; t[13]=tmp;
        // row 2: shift left 2
        tmp=t[2]; t[2]=t[10]; t[10]=tmp;
        tmp=t[6]; t[6]=t[14]; t[14]=tmp;
        // row 3: shift left 3
        tmp=t[15]; t[15]=t[11]; t[11]=t[7]; t[7]=t[3]; t[3]=tmp;

        // MixColumns — mix each column
        for (i = 0; i < 4; i++) {
            uint8_t a0=t[i*4], a1=t[i*4+1],
                    a2=t[i*4+2], a3=t[i*4+3];
            s[i*4]   = gmul(a0,2)^gmul(a1,3)^a2^a3;
            s[i*4+1] = a0^gmul(a1,2)^gmul(a2,3)^a3;
            s[i*4+2] = a0^a1^gmul(a2,2)^gmul(a3,3);
            s[i*4+3] = gmul(a0,3)^a1^a2^gmul(a3,2);
        }

        // AddRoundKey
        for (i = 0; i < 16; i++) {
            s[i] ^= (rk[round*4 + i/4] >> (24 - 8*(i%4))) & 0xff;
        }
    }

    // final round — no MixColumns
    for (i = 0; i < 16; i++) t[i] = sbox[s[i]];

    // ShiftRows
    uint8_t tmp;
    tmp=t[1]; t[1]=t[5]; t[5]=t[9]; t[9]=t[13]; t[13]=tmp;
    tmp=t[2]; t[2]=t[10]; t[10]=tmp;
    tmp=t[6]; t[6]=t[14]; t[14]=tmp;
    tmp=t[15]; t[15]=t[11]; t[11]=t[7]; t[7]=t[3]; t[3]=tmp;

    // AddRoundKey final
    for (i = 0; i < 16; i++) {
        out[i] = t[i] ^ ((rk[AES_ROUNDS*4 + i/4] >>
                         (24 - 8*(i%4))) & 0xff);
    }
}

// ─────────────────────────────────────────
// AES-CTR encrypt/decrypt
// this is what every other module calls
// encrypt and decrypt are identical in CTR mode
// ─────────────────────────────────────────
void aes_ctr_encrypt(const uint8_t *input, uint8_t *output,
                     uint64_t length, const uint8_t *key,
                     const uint8_t *nonce, uint64_t counter_offset) {
    // expand key once — reused for every block
    uint32_t round_keys[60];
    aes_key_expansion(key, round_keys);

    uint8_t counter_block[AES_BLOCK_SIZE];
    uint8_t keystream[AES_BLOCK_SIZE];

    uint64_t block_num = counter_offset;
    uint64_t pos = 0;

    while (pos < length) {
        // build counter block = nonce + block number
        // first 8 bytes = nonce, last 8 bytes = counter
        memcpy(counter_block, nonce, 8);
        counter_block[8]  = (block_num >> 56) & 0xff;
        counter_block[9]  = (block_num >> 48) & 0xff;
        counter_block[10] = (block_num >> 40) & 0xff;
        counter_block[11] = (block_num >> 32) & 0xff;
        counter_block[12] = (block_num >> 24) & 0xff;
        counter_block[13] = (block_num >> 16) & 0xff;
        counter_block[14] = (block_num >>  8) & 0xff;
        counter_block[15] = (block_num      ) & 0xff;

        // encrypt the counter block to get keystream
        aes_encrypt_block(counter_block, keystream, round_keys);

        // XOR keystream with input to get output
        // handles partial last block too
        uint64_t block_len = (length - pos < AES_BLOCK_SIZE)
                             ? (length - pos)
                             : AES_BLOCK_SIZE;
        for (uint64_t i = 0; i < block_len; i++)
            output[pos + i] = input[pos + i] ^ keystream[i];

        pos += block_len;
        block_num++;
    }
}