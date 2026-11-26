#ifndef MEMORIA_H
#define MEMORIA_H

#include "memoria/processo.h"

void memoria_iniciar(void);

int memoria_interpretar_tamanho(const char *texto, long *bytes);
int memoria_tamanho_do_arquivo(const char *caminho, long *bytes);
int memoria_criar_processo(const char *nome, TipoProcesso tipo, long tamanho_bytes, int exibir, Processo **criado);

typedef struct {
    int molduras;            
    int posicoes_swap;       
    long kb_reservados;      
} ResumoLiberacao;

void memoria_liberar_processo(Processo *processo, ResumoLiberacao *resumo);
int memoria_exigir_dono_ou_kernel(const Processo *processo, const char *acao);
void memoria_registrar_operacao(void);

int cmd_terminate_process(int argc, char **argv);
int cmd_memory_status(int argc, char **argv);
int cmd_memory_compact(int argc, char **argv);
int cmd_translate_address(int argc, char **argv);
int cmd_execute_step(int argc, char **argv);
int cmd_memory_algoritmo(int argc, char **argv);
int cmd_memory_relatorio(int argc, char **argv);

#endif