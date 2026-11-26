#ifndef PROCESSO_H
#define PROCESSO_H

#include "comum/utils.h"

#define TAM_PAGINA               4096
#define MAX_PROCESSOS            16
#define MAX_PAGINAS_POR_PROCESSO 64     
#define PRIMEIRO_IDENTIFICADOR   101    

#define MAX_PRIORIDADES          3      
#define PRIORIDADE_PADRAO        2
#define BURST_PADRAO_MS          10

#define MAX_SYSCALLS_POR_PROCESSO 3
#define MAX_THREADS_POR_PROCESSO  4
#define MAX_MUTEXES_POR_PROCESSO  4

typedef enum { PROCESSO_CRITICO, PROCESSO_NORMAL } TipoProcesso;

typedef enum {
    ESTADO_NOVO,         
    ESTADO_PRONTO,       
    ESTADO_EXECUTANDO,   
    ESTADO_BLOQUEADO,    
    ESTADO_CONCLUIDO     
} EstadoProcesso;

typedef struct {
    int presente;        
    int moldura;         
    int posicao_swap;    
    int modificada;      
} EntradaTabelaPaginas;

typedef struct {
    char nome[TAM_NOME];     
    long instante_ms;        
    long duracao_ms;         
    int aleatoria;           
    int ja_disparada;
} SyscallAgendada;

typedef struct {
    int em_uso;
    char nome[TAM_NOME];
    EstadoProcesso estado;
    int mutex_esperado;      
} Thread;

typedef struct {
    int em_uso;
    char nome[TAM_NOME];
    int dono;                                    
    int fila_espera[MAX_THREADS_POR_PROCESSO];   
    int quantidade_espera;
} Mutex;

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
    int memoria_liberada;   

    EstadoProcesso estado;
    int prioridade;            
    long burst_ms;             
    long executado_ms;         
    long chegada_ms;           
    long conclusao_ms;         
    long espera_ms;            
    long bloqueado_ms;         
    long primeira_execucao_ms; 
    long desbloqueio_em_ms;    
    long ordem_na_fila;        
    SyscallAgendada syscalls[MAX_SYSCALLS_POR_PROCESSO];
    int total_syscalls;

    unsigned int semente_paginas;   
    int ultima_pagina;
    long referencias, acertos, page_faults;

    Thread threads[MAX_THREADS_POR_PROCESSO];
    Mutex mutexes[MAX_MUTEXES_POR_PROCESSO];
} Processo;

void processo_iniciar(void);

int processo_proximo_identificador(void);
int processo_validar_novo(const char *nome);

Processo *processo_criar(const char *nome, TipoProcesso tipo, long tamanho_bytes, int dono_uid);
void processo_remover(Processo *processo);

Processo *processo_por_nome(const char *nome);
Processo *processo_por_identificador(int identificador);
Processo *processo_na_posicao(int posicao);

int processo_listar_ordenados(const Processo **lista, int maximo);
int processo_usa_memoria(const Processo *processo);

#endif