#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>

#define TAM_HASH 32

void sha256(const unsigned char *dados, size_t tamanho, unsigned char saida[TAM_HASH]);
void sha256_hex(const unsigned char hash[TAM_HASH], char *saida);

#endif