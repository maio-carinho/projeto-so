#ifndef EXECUCAO_H
#define EXECUCAO_H

#include "memoria/memoria.h"
#include "memoria/paginacao.h"
#include "memoria/processo.h"

typedef enum { EVENTO_NENHUM, EVENTO_SYSCALL, EVENTO_CONCLUIDO } EventoPasso;

typedef struct {
    int referenciou_pagina;          
    int pagina;
    int escrita;
    ResultadoAcesso acesso;
    EventoPasso evento;              
    const SyscallAgendada *syscall;  
    ResumoLiberacao liberacao;       
} ResultadoPasso;

void execucao_iniciar(void);

long execucao_tempo_atual(void);
long execucao_trocas_de_contexto(void);
long execucao_cpu_ocupada_ms(void);
long execucao_cpu_ociosa_ms(void);

const char *execucao_nome_estado(EstadoProcesso estado);
const char *execucao_nome_prioridade(int prioridade);

long execucao_sortear(long maximo);

void execucao_marcar_pronto(Processo *processo);               
void execucao_despachar(Processo *processo);                   
void execucao_devolver_cpu(Processo *processo, int reenfileirar);   

void execucao_executar_passo(Processo *processo, ResultadoPasso *resultado);
void execucao_passo_ocioso(void);

void execucao_imprimir_eventos(void);

Processo *execucao_escolher_proximo(int usar_prioridades);
int execucao_existe_concorrente(const Processo *processo, int usar_prioridades);
int execucao_existe_pronto_com_prioridade_maior(int prioridade);
int execucao_ha_processos_vivos(void);

void execucao_descrever_syscall(const SyscallAgendada *syscall, char *saida, size_t capacidade);

#endif