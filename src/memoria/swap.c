#include <stdio.h>
#include <string.h>
#include "armazenamento/disco.h"
#include "armazenamento/escalonador.h"
#include "memoria/swap.h"

static int posicao_ocupada[TOTAL_POSICOES_SWAP];
static long gravacoes;
static long leituras;

void swap_iniciar(void) {
    memset(posicao_ocupada, 0, sizeof posicao_ocupada);
    gravacoes = 0;
    leituras = 0;
}

int swap_reservar_posicao(void) {
    for (int i = 0; i < TOTAL_POSICOES_SWAP; i++) {
        if (!posicao_ocupada[i]) {
            posicao_ocupada[i] = 1;
            return i;
        }
    }
    return -1;
}

void swap_liberar_posicao(int posicao) {
    if (posicao >= 0 && posicao < TOTAL_POSICOES_SWAP) posicao_ocupada[posicao] = 0;
}

int swap_posicoes_em_uso(void) {
    int total = 0;
    for (int i = 0; i < TOTAL_POSICOES_SWAP; i++)
        if (posicao_ocupada[i]) total++;
    return total;
}

static int trilha_da_posicao(int posicao) {
    return PRIMEIRA_TRILHA_SWAP + posicao * (NUM_TRILHAS - PRIMEIRA_TRILHA_SWAP) / TOTAL_POSICOES_SWAP;
}

void swap_descrever_posicao(int posicao, char *saida, size_t capacidade) {
    if (disco_meio() == MEIO_SSD)
        snprintf(saida, capacidade, "posição %d do swap, SSD (sem trilhas)", posicao);
    else
        snprintf(saida, capacidade, "posição %d do swap, Disco 1 trilha %d", posicao, trilha_da_posicao(posicao));
}

static void enfileirar(int posicao, TipoOperacao tipo, const char *prefixo, int identificador, int pagina) {
    if (disco_meio() != MEIO_HDD) return;
    char rotulo[40];
    snprintf(rotulo, sizeof rotulo, "%s PID %d pág %d", prefixo, identificador, pagina);
    escalonador_enfileirar(0, trilha_da_posicao(posicao), tipo, posicao, rotulo);
}

void swap_gravar(int posicao, int identificador, int pagina) {
    gravacoes++;
    enfileirar(posicao, OP_ESCRITA, "Swap-out", identificador, pagina);
}

void swap_ler(int posicao, int identificador, int pagina) {
    leituras++;
    enfileirar(posicao, OP_LEITURA, "Swap-in", identificador, pagina);
}

long swap_total_gravacoes(void) { return gravacoes; }
long swap_total_leituras(void) { return leituras; }
