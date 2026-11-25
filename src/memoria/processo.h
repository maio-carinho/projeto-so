#ifndef PROCESSO_H
#define PROCESSO_H

#include "comum/utils.h"

#define TAM_PAGINA               4096
#define MAX_PROCESSOS            16
#define MAX_PAGINAS_POR_PROCESSO 64     
#define PRIMEIRO_IDENTIFICADOR   100    

typedef enum { PROCESSO_CRITICO, PROCESSO_NORMAL } TipoProcesso;

typedef struct {
    int presente;        
    int moldura;         
    int posicao_swap;    
    int modificada;      
} EntradaTabelaPaginas;

typedef struct {
    int em_uso;
    int identificador;   
    char nome[TAM_NOME];
    TipoProcesso tipo;
    int dono_uid;        
    long tamanho_bytes;

    long inicio_kb;      

    int total_paginas;   
    EntradaTabelaPaginas tabela_paginas[MAX_PAGINAS_POR_PROCESSO];
} Processo;

void processo_iniciar(void);

int processo_proximo_identificador(void);

int processo_validar_novo(const char *nome);

Processo *processo_criar(const char *nome, TipoProcesso tipo, long tamanho_bytes, int dono_uid);
void processo_remover(Processo *processo);

Processo *processo_por_nome(const char *nome);
Processo *processo_por_identificador(int identificador);

Processo *processo_na_posicao(int posicao);

#endif
