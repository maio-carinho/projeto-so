#ifndef ESCALONADOR_H
#define ESCALONADOR_H

#include "utils.h"

#define NUM_TRILHAS     200     
#define MAX_DISCOS      3       
#define MAX_REQUISICOES 128     

typedef enum { ALG_FCFS, ALG_SSTF, ALG_SCAN, ALG_CSCAN } Algoritmo;
typedef enum { OP_LEITURA, OP_ESCRITA } TipoOperacao;

typedef struct {
    int trilha;
    TipoOperacao tipo;
    int bloco_fisico;       
    char rotulo[40];        
} Requisicao;

void escalonador_init(void);

void escalonador_reiniciar_discos(void);

int escalonador_ativar(int ativo);

void escalonador_enfileirar(int disco, int trilha, TipoOperacao tipo,
                            int bloco_fisico, const char *rotulo);

int escalonador_quantidade(int disco);
const Requisicao *escalonador_requisicao(int disco, int indice);
void escalonador_limpar_filas(void);

int escalonador_cabeca(int disco);
int escalonador_definir_cabeca(int disco, int trilha);

void escalonador_definir_algoritmo(Algoritmo algoritmo);
Algoritmo escalonador_algoritmo(void);
void escalonador_definir_direcao(int sobe);
int escalonador_direcao_sobe(void);

const char *escalonador_nome(Algoritmo algoritmo);
const char *escalonador_nome_completo(Algoritmo algoritmo);
int escalonador_algoritmo_por_nome(const char *texto, Algoritmo *saida);

long escalonador_simular(int disco, Algoritmo algoritmo);

long escalonador_executar(int disco, Algoritmo algoritmo, int imprimir,
                          int rotular_disco);

long escalonador_total_movimento(int disco);
long escalonador_total_atendidas(int disco);

#endif