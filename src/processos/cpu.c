#include <stdio.h>
#include <string.h>
#include "comum/utils.h"
#include "seguranca/seguranca.h"
#include "processos/argumentos.h"
#include "processos/cpu.h"
#include "processos/execucao.h"

#define QUANTUM_PADRAO_MS   2
#define LIMITE_SIMULACAO_MS 100000

typedef enum { ALGORITMO_CPU_PRIORIDADE_RR, ALGORITMO_CPU_RR, ALGORITMO_CPU_FCFS } AlgoritmoCpu;

static const char *nome_do_algoritmo(AlgoritmoCpu algoritmo) {
    switch (algoritmo) {
        case ALGORITMO_CPU_PRIORIDADE_RR: return "Priority_RR";
        case ALGORITMO_CPU_RR: return "RR";
        default: return "FCFS";
    }
}

static void imprimir_fila_inicial(AlgoritmoCpu algoritmo) {
    printf("Fila de Pronto Inicial:\n");
    int por_prioridade = algoritmo == ALGORITMO_CPU_PRIORIDADE_RR;
    int niveis = por_prioridade ? MAX_PRIORIDADES : 1;

    for (int nivel = 1; nivel <= niveis; nivel++) {
        const Processo *impressos[MAX_PROCESSOS];
        int quantidade = 0;
        for (int i = 0; i < MAX_PROCESSOS; i++) {
            const Processo *p = processo_na_posicao(i);
            if (!p || p->estado != ESTADO_PRONTO) continue;
            if (por_prioridade && p->prioridade != nivel) continue;
            int posicao = quantidade++;
            while (posicao > 0 && impressos[posicao - 1]->ordem_na_fila > p->ordem_na_fila) {
                impressos[posicao] = impressos[posicao - 1];
                posicao--;
            }
            impressos[posicao] = p;
        }

        if (por_prioridade) printf("- Prioridade %d (%-6s): [", nivel, execucao_nome_prioridade(nivel));
        else printf("- Fila única (ordem de chegada): [");
        for (int i = 0; i < quantidade; i++) {
            const Processo *p = impressos[i];
            printf("%sPID %d (Burst %ldms", i ? ", " : "", p->identificador, p->burst_ms);
            if (p->executado_ms > 0) printf(", restam %ldms", p->burst_ms - p->executado_ms);
            printf(")");
        }
        printf("]\n");
    }

    for (int i = 0; i < MAX_PROCESSOS; i++) {
        const Processo *p = processo_na_posicao(i);
        if (p && p->estado == ESTADO_NOVO)
            printf("- Chega depois: PID %d (%s) em t=%ldms\n", p->identificador, p->nome, p->chegada_ms);
    }
    for (int i = 0; i < MAX_PROCESSOS; i++) {
        const Processo *p = processo_na_posicao(i);
        if (p && p->estado == ESTADO_BLOQUEADO)
            printf("- Em E/S: PID %d (%s) volta em t=%ldms\n", p->identificador, p->nome, p->desbloqueio_em_ms);
    }
}

static void imprimir_linha_de_pagina(const Processo *processo, const ResultadoPasso *resultado) {
    if (!resultado->referenciou_pagina) return;
    const ResultadoAcesso *a = &resultado->acesso;
    printf("    [t=%ldms] PID %d acessa Pág %02d (%s) -> ", resultado->acesso.instante_ms, processo->identificador,
           resultado->pagina, resultado->escrita ? "escrita" : "leitura");
    if (a->acerto) {
        printf("HIT na Moldura %02d\n", a->moldura);
    } else {
        printf("PAGE FAULT -> Moldura %02d%s", a->moldura, a->veio_do_swap ? " (trazida do Swap)" : "");
        if (a->substituiu)
            printf(" | vítima: PID %d Pág %02d (%s)", a->vitima_identificador, a->vitima_pagina,
                   a->vitima_gravada_no_swap ? "gravada no Swap" : "descartada");
        printf("\n");
    }
}

static void imprimir_segmento(long inicio, long fim, const Processo *processo, const char *motivo) {
    printf("[%2ldms - %2ldms] Executando PID %d (%s) | %s\n", inicio, fim, processo->identificador,
           processo->nome, motivo);
}

static void executar_escalonamento(AlgoritmoCpu algoritmo, long quantum, int trace) {
    int por_prioridade = algoritmo == ALGORITMO_CPU_PRIORIDADE_RR;
    int usa_quantum = algoritmo != ALGORITMO_CPU_FCFS;
    long limite = execucao_tempo_atual() + LIMITE_SIMULACAO_MS;

    printf("--- LINHA DO TEMPO DA CPU ---\n");
    while (execucao_ha_processos_vivos() && execucao_tempo_atual() < limite) {
        Processo *processo = execucao_escolher_proximo(por_prioridade);

        if (!processo) {
            long inicio = execucao_tempo_atual();
            while (!execucao_escolher_proximo(por_prioridade) && execucao_ha_processos_vivos() &&
                   execucao_tempo_atual() < limite)
                execucao_passo_ocioso();
            printf("[%2ldms - %2ldms] CPU ociosa (aguardando E/S ou chegada de processo)\n",
                   inicio, execucao_tempo_atual());
            execucao_imprimir_eventos();
            continue;
        }

        long inicio = execucao_tempo_atual();
        long fatia = 0;
        char motivo[200];
        int segmento_terminou = 0;
        execucao_despachar(processo);

        while (!segmento_terminou) {
            ResultadoPasso resultado;
            execucao_executar_passo(processo, &resultado);
            fatia++;
            if (trace) imprimir_linha_de_pagina(processo, &resultado);

            if (resultado.evento == EVENTO_CONCLUIDO) {
                int fila_vazia = !execucao_existe_concorrente(processo, por_prioridade);
                int tamanho = snprintf(motivo, sizeof motivo, "PID %d CONCLUÍDO!", processo->identificador);
                if (resultado.liberacao.molduras > 0)
                    tamanho += snprintf(motivo + tamanho, sizeof motivo - (size_t)tamanho,
                                        " (%d moldura(s) liberada(s))", resultado.liberacao.molduras);
                if (fila_vazia && por_prioridade)
                    snprintf(motivo + tamanho, sizeof motivo - (size_t)tamanho, " (Fila P%d VAZIA)", processo->prioridade);
                segmento_terminou = 1;
            } else if (resultado.evento == EVENTO_SYSCALL) {
                char descricao[100];
                execucao_descrever_syscall(resultado.syscall, descricao, sizeof descricao);
                snprintf(motivo, sizeof motivo, "Dispara %s -> Estado BLOQUEADO (até t=%ldms)", descricao,
                         processo->desbloqueio_em_ms);
                segmento_terminou = 1;
            } else if (por_prioridade && execucao_existe_pronto_com_prioridade_maior(processo->prioridade)) {
                execucao_devolver_cpu(processo, 1);
                snprintf(motivo, sizeof motivo, "Preemptado: processo de prioridade maior ficou PRONTO -> Volta p/ Fila P%d",
                         processo->prioridade);
                segmento_terminou = 1;
            } else if (usa_quantum && fatia >= quantum) {
                if (execucao_existe_concorrente(processo, por_prioridade)) {
                    execucao_devolver_cpu(processo, 1);
                    if (por_prioridade)
                        snprintf(motivo, sizeof motivo, "Quantum %ldms expirou -> Volta p/ Fila P%d", quantum, processo->prioridade);
                    else
                        snprintf(motivo, sizeof motivo, "Quantum %ldms expirou -> Volta p/ Fila de Prontos", quantum);
                    segmento_terminou = 1;
                } else {
                    fatia = 0;   
                }
            }
        }

        imprimir_segmento(inicio, execucao_tempo_atual(), processo, motivo);
        execucao_imprimir_eventos();
    }
}

int cmd_schedule(int argc, char **argv) {
    const char *texto_algoritmo = argumento_valor(argc, argv, "--algorithm");
    const char *texto_quantum = argumento_valor(argc, argv, "--quantum");
    int trace = argumento_existe(argc, argv, "--trace");
    AlgoritmoCpu algoritmo = ALGORITMO_CPU_PRIORIDADE_RR;
    long quantum = QUANTUM_PADRAO_MS;

    if (texto_algoritmo) {
        if (strcmp(texto_algoritmo, "Priority_RR") == 0) algoritmo = ALGORITMO_CPU_PRIORIDADE_RR;
        else if (strcmp(texto_algoritmo, "RR") == 0) algoritmo = ALGORITMO_CPU_RR;
        else if (strcmp(texto_algoritmo, "FCFS") == 0) algoritmo = ALGORITMO_CPU_FCFS;
        else return reg_erro("Algoritmo desconhecido: '%s' (use Priority_RR, RR ou FCFS)", texto_algoritmo);
    }
    if (texto_quantum && argumento_tempo_ms(texto_quantum, &quantum) != 0) return -1;
    if (quantum < 1) return reg_erro("O quantum precisa ser de pelo menos 1ms.");
    if (!execucao_ha_processos_vivos())
        return reg_erro("Nenhum processo para escalonar (crie com create_process).");

    seguranca_registrar("ESCALONAMENTO: algoritmo %s, quantum %ldms", nome_do_algoritmo(algoritmo), quantum);
    if (algoritmo == ALGORITMO_CPU_FCFS) printf("[ESCALONADOR] Algoritmo: FCFS (sem quantum) | t inicial = %ldms\n", execucao_tempo_atual());
    else printf("[ESCALONADOR] Algoritmo: %s | Quantum: %ldms | t inicial = %ldms\n",
                nome_do_algoritmo(algoritmo), quantum, execucao_tempo_atual());
    imprimir_fila_inicial(algoritmo);
    executar_escalonamento(algoritmo, quantum, trace);
    cpu_imprimir_metricas();
    return 0;
}

void cpu_imprimir_metricas(void) {
    const char *linha = "+------+----------------+------------+---------+-------+-----------+----------+---------+-----------+-----------+";
    int concluidos = 0;
    double soma_espera = 0, soma_retorno = 0, soma_resposta = 0;
    long total_referencias = 0, total_faults = 0;

    printf("\n================ METRICAS FINAIS DO ESCALONADOR ================\n");
    printf("%s\n", linha);
    printf("| PID  | Nome           | Prioridade | Chegada | Burst | Espera WT | Resposta | E/S     | Conclusão | Turnaround|\n");
    printf("%s\n", linha);
    const Processo *todos[MAX_PROCESSOS];
    int quantidade_todos = processo_listar_ordenados(todos, MAX_PROCESSOS);
    for (int i = 0; i < quantidade_todos; i++) {
        const Processo *p = todos[i];
        if (p->estado != ESTADO_CONCLUIDO) continue;
        long retorno = p->conclusao_ms - p->chegada_ms;       
        long resposta = p->primeira_execucao_ms - p->chegada_ms;
        printf("| %4d | %-14.14s | %d (%-6s) | %5ldms | %3ldms | %7ldms | %6ldms | %5ldms | %7ldms | %7ldms |\n",
               p->identificador, p->nome, p->prioridade, execucao_nome_prioridade(p->prioridade),
               p->chegada_ms, p->burst_ms, p->espera_ms, resposta, p->bloqueado_ms, p->conclusao_ms, retorno);
        soma_espera += (double)p->espera_ms;
        soma_retorno += (double)retorno;
        soma_resposta += (double)resposta;
        total_referencias += p->referencias;
        total_faults += p->page_faults;
        concluidos++;
    }
    printf("%s\n", linha);

    if (concluidos == 0) {
        printf("Nenhum processo concluído ainda (rode 'schedule' ou 'run_simulation').\n");
        return;
    }

    long total_ms = execucao_tempo_atual();
    long ocupada = execucao_cpu_ocupada_ms();
    printf("* Tempo Médio de Espera (AWT)    : %.2f ms\n", soma_espera / concluidos);
    printf("* Tempo Médio de Retorno (ATT)   : %.2f ms\n", soma_retorno / concluidos);
    printf("* Tempo Médio de Resposta        : %.2f ms\n", soma_resposta / concluidos);
    printf("* Trocas de Contexto Totais      : %ld trocas\n", execucao_trocas_de_contexto());
    printf("* Utilização Global da CPU       : %.0f%% (%ldms ocupada, %ldms ociosa)\n",
           total_ms > 0 ? 100.0 * (double)ocupada / (double)total_ms : 0.0, ocupada, execucao_cpu_ociosa_ms());
    printf("* Vazão (Throughput)             : %d processo(s) em %ldms\n", concluidos, total_ms);
    printf("* Referências a Páginas          : %ld (page faults: %ld", total_referencias, total_faults);
    if (total_referencias > 0) printf(", %.1f%%", 100.0 * (double)total_faults / (double)total_referencias);
    printf(")\n");
    printf("========================================================================\n");
}

int cmd_metrics(int argc, char **argv) {
    (void)argc; (void)argv;
    cpu_imprimir_metricas();
    return 0;
}