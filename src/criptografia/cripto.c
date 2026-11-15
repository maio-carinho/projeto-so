#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "criptografia/cripto.h"

#define RODADAS_DERIVACAO 2000

static int semente_iniciada = 0;

void cripto_sal_aleatorio(unsigned char sal[TAM_SAL]) {
    if (!semente_iniciada) {
        srand((unsigned)time(NULL));
        semente_iniciada = 1;
    }
    for (int i = 0; i < TAM_SAL; i++) sal[i] = (unsigned char)(rand() & 0xFF);
}

void cripto_derivar_chave(const char *senha, const unsigned char sal[TAM_SAL],
                          unsigned char chave[TAM_HASH]) {
    unsigned char entrada[TAM_SAL + 256];
    size_t tam_senha = strlen(senha);
    if (tam_senha > 256) tam_senha = 256;
    memcpy(entrada, sal, TAM_SAL);
    memcpy(entrada + TAM_SAL, senha, tam_senha);
    sha256(entrada, TAM_SAL + tam_senha, chave);

    unsigned char rodada[TAM_HASH + TAM_SAL];
    for (int i = 1; i < RODADAS_DERIVACAO; i++) {
        memcpy(rodada, chave, TAM_HASH);
        memcpy(rodada + TAM_HASH, sal, TAM_SAL);
        sha256(rodada, sizeof rodada, chave);
    }
}

void cripto_verificador(const unsigned char chave[TAM_HASH], unsigned char saida[TAM_HASH]) {
    unsigned char entrada[TAM_HASH + 11];
    memcpy(entrada, "verificador", 11);
    memcpy(entrada + 11, chave, TAM_HASH);
    sha256(entrada, sizeof entrada, saida);
}

void cripto_aplicar(const unsigned char chave[TAM_HASH], const unsigned char sal[TAM_SAL],
                    unsigned char *dados, size_t tamanho) {
    unsigned char entrada[TAM_HASH + TAM_SAL + 4];
    unsigned char fluxo[TAM_HASH];
    memcpy(entrada, chave, TAM_HASH);
    memcpy(entrada + TAM_HASH, sal, TAM_SAL);

    for (size_t inicio = 0, contador = 0; inicio < tamanho; inicio += TAM_HASH, contador++) {
        entrada[TAM_HASH + TAM_SAL]     = (unsigned char)(contador >> 24);
        entrada[TAM_HASH + TAM_SAL + 1] = (unsigned char)(contador >> 16);
        entrada[TAM_HASH + TAM_SAL + 2] = (unsigned char)(contador >> 8);
        entrada[TAM_HASH + TAM_SAL + 3] = (unsigned char)(contador);
        sha256(entrada, sizeof entrada, fluxo);
        for (size_t i = 0; i < TAM_HASH && inicio + i < tamanho; i++)
            dados[inicio + i] ^= fluxo[i];
    }
}

int cripto_hashes_iguais(const unsigned char a[TAM_HASH], const unsigned char b[TAM_HASH]) {
    unsigned char diferenca = 0;
    for (int i = 0; i < TAM_HASH; i++) diferenca |= (unsigned char)(a[i] ^ b[i]);
    return diferenca == 0;
}