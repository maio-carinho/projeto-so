#ifndef SWAP_H
#define SWAP_H

#include <stddef.h>

#define TOTAL_POSICOES_SWAP  128        
#define PRIMEIRA_TRILHA_SWAP 100

void swap_iniciar(void);

int swap_reservar_posicao(void);        
void swap_liberar_posicao(int posicao);
int swap_posicoes_em_uso(void);

void swap_gravar(int posicao, int identificador, int pagina);
void swap_ler(int posicao, int identificador, int pagina);

void swap_descrever_posicao(int posicao, char *saida, size_t capacidade);
long swap_total_gravacoes(void);
long swap_total_leituras(void);

#endif
