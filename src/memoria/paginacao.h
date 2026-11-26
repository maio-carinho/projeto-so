#ifndef PAGINACAO_H
#define PAGINACAO_H

#include "memoria/processo.h"

#define TOTAL_MOLDURAS      16          
#define MS_POR_REFERENCIA   5

typedef enum { ALGORITMO_FIFO, ALGORITMO_LRU, ALGORITMO_SEGUNDA_CHANCE } AlgoritmoSubstituicao;

typedef struct {
    int acerto;                  
    int moldura;                 
    int veio_do_swap;            
    int substituiu;              
    int vitima_identificador;
    int vitima_pagina;
    long vitima_inativa_ms;      
    int vitima_gravada_no_swap;  
    long instante_ms;            
} ResultadoAcesso;

void paginacao_iniciar(void);

int paginacao_definir_algoritmo(const char *nome);
const char *paginacao_nome_algoritmo(void);

int paginacao_paginas_alocadas(void);
int paginacao_ha_capacidade(int paginas_novas);

void paginacao_imprimir_criacao(const Processo *processo);

void paginacao_acessar(Processo *processo, int pagina, int escrita);

void paginacao_referenciar(Processo *processo, int pagina, int escrita, long instante_ms,
                           int exibir, ResultadoAcesso *resultado);

void paginacao_traduzir(const Processo *processo, int pagina, long deslocamento);

void paginacao_liberar_processo(Processo *processo, int *molduras_liberadas, int *posicoes_swap_liberadas);

int paginacao_molduras_ocupadas(void);
void paginacao_imprimir_molduras(void);
void paginacao_imprimir_tabela(const Processo *processo);
void paginacao_imprimir_estatisticas(void);

long paginacao_fragmentacao_interna_total(int *processos_afetados);
void paginacao_imprimir_fragmentacao_interna(void);

#endif