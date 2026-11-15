#ifndef CRIPTO_H
#define CRIPTO_H

#include <stddef.h>
#include "criptografia/sha256.h"

#define TAM_SAL 16

void cripto_sal_aleatorio(unsigned char sal[TAM_SAL]);
void cripto_derivar_chave(const char *senha, const unsigned char sal[TAM_SAL],
                          unsigned char chave[TAM_HASH]);
void cripto_verificador(const unsigned char chave[TAM_HASH], unsigned char saida[TAM_HASH]);
void cripto_aplicar(const unsigned char chave[TAM_HASH], const unsigned char sal[TAM_SAL],
                    unsigned char *dados, size_t tamanho);
int cripto_hashes_iguais(const unsigned char a[TAM_HASH], const unsigned char b[TAM_HASH]);

#endif