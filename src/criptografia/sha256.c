#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "criptografia/sha256.h"

static const uint32_t constantes[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t girar(uint32_t valor, int bits) {
    return (valor >> bits) | (valor << (32 - bits));
}

static void processar_bloco(uint32_t estado[8], const unsigned char bloco[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)bloco[i * 4] << 24) | ((uint32_t)bloco[i * 4 + 1] << 16) |
               ((uint32_t)bloco[i * 4 + 2] << 8) | (uint32_t)bloco[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = girar(w[i - 15], 7) ^ girar(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = girar(w[i - 2], 17) ^ girar(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = estado[0], b = estado[1], c = estado[2], d = estado[3];
    uint32_t e = estado[4], f = estado[5], g = estado[6], h = estado[7];
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = girar(e, 6) ^ girar(e, 11) ^ girar(e, 25);
        uint32_t escolha = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + escolha + constantes[i] + w[i];
        uint32_t s0 = girar(a, 2) ^ girar(a, 13) ^ girar(a, 22);
        uint32_t maioria = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maioria;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    estado[0] += a; estado[1] += b; estado[2] += c; estado[3] += d;
    estado[4] += e; estado[5] += f; estado[6] += g; estado[7] += h;
}

void sha256(const unsigned char *dados, size_t tamanho, unsigned char saida[TAM_HASH]) {
    uint32_t estado[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    size_t completos = tamanho / 64;
    for (size_t i = 0; i < completos; i++) processar_bloco(estado, dados + i * 64);

    unsigned char final[128];
    size_t resto = tamanho % 64;
    memset(final, 0, sizeof final);
    memcpy(final, dados + completos * 64, resto);
    final[resto] = 0x80;
    size_t total_final = (resto < 56) ? 64 : 128;
    uint64_t bits = (uint64_t)tamanho * 8;
    for (int i = 0; i < 8; i++)
        final[total_final - 1 - i] = (unsigned char)(bits >> (8 * i));
    for (size_t i = 0; i < total_final; i += 64) processar_bloco(estado, final + i);

    for (int i = 0; i < 8; i++) {
        saida[i * 4]     = (unsigned char)(estado[i] >> 24);
        saida[i * 4 + 1] = (unsigned char)(estado[i] >> 16);
        saida[i * 4 + 2] = (unsigned char)(estado[i] >> 8);
        saida[i * 4 + 3] = (unsigned char)(estado[i]);
    }
}

void sha256_hex(const unsigned char hash[TAM_HASH], char *saida) {
    for (int i = 0; i < TAM_HASH; i++) sprintf(saida + i * 2, "%02x", hash[i]);
    saida[TAM_HASH * 2] = '\0';
}