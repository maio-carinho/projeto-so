#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "armazenamento/disco.h"
#include "armazenamento/escalonador.h"
#include "processos/execucao.h"

#define MAX_EVENTOS 24
#define TAM_EVENTO  160

static long relogio_ms;
static long contador_fila;
static long trocas_de_contexto;
static long cpu_ocupada_ms;
static long cpu_ociosa_ms;
static int ultimo_executado;          
static unsigned int semente_sorteio = 2026;

static char eventos[MAX_EVENTOS][TAM_EVENTO];
static int quantidade_eventos;

void execucao_iniciar(void) {
    relogio_ms = 0;
    contador_fila = 0;
    trocas_de_contexto = 0;
    cpu_ocupada_ms = 0;
    cpu_ociosa_ms = 0;
    ultimo_executado = 0;
    semente_sorteio = 2026;
    quantidade_eventos = 0;
}

long execucao_tempo_atual(void) { return relogio_ms; }
long execucao_trocas_de_contexto(void) { return trocas_de_contexto; }
long execucao_cpu_ocupada_ms(void) { return cpu_ocupada_ms; }
long execucao_cpu_ociosa_ms(void) { return cpu_ociosa_ms; }

const char *execucao_nome_estado(EstadoProcesso estado) {
    switch (estado) {
        case ESTADO_NOVO: return "NOVO";
        case ESTADO_PRONTO: return "PRONTO";
        case ESTADO_EXECUTANDO: return "EXECUTANDO";
        case ESTADO_BLOQUEADO: return "BLOQUEADO";
        default: return "CONCLUÍDO";
    }
}

const char *execucao_nome_prioridade(int prioridade) {
    switch (prioridade) {
        case 1: return "Alta";
        case 2: return "Baixa";
        default: return "Mínima";
    }
}

static unsigned int proximo_aleatorio(unsigned int *estado) {
    *estado = *estado * 1103515245u + 12345u;
    return (*estado >> 16) & 0x7fff;
}

long execucao_sortear(long maximo) {
    if (maximo <= 0) return 0;
    return (long)(proximo_aleatorio(&semente_sorteio) % (unsigned int)maximo);
}

static void registrar_evento(const char *formato, ...) {
    if (quantidade_eventos >= MAX_EVENTOS) return;
    va_list argumentos;
    va_start(argumentos, formato);
    vsnprintf(eventos[quantidade_eventos++], TAM_EVENTO, formato, argumentos);
    va_end(argumentos);
}

void execucao_imprimir_eventos(void) {
    for (int i = 0; i < quantidade_eventos; i++) printf("%s\n", eventos[i]);
    quantidade_eventos = 0;
}

void execucao_marcar_pronto(Processo *processo) {
    processo->estado = ESTADO_PRONTO;
    processo->ordem_na_fila = ++contador_fila;
}

void execucao_despachar(Processo *processo) {
    if (processo->identificador != ultimo_executado) trocas_de_contexto++;
    ultimo_executado = processo->identificador;
    processo->estado = ESTADO_EXECUTANDO;
    if (processo->primeira_execucao_ms < 0) processo->primeira_execucao_ms = relogio_ms;
}

void execucao_devolver_cpu(Processo *processo, int reenfileirar) {
    if (processo->estado != ESTADO_EXECUTANDO) return;
    if (reenfileirar) execucao_marcar_pronto(processo);
    else processo->estado = ESTADO_PRONTO;
}

Processo *execucao_escolher_proximo(int usar_prioridades) {
    Processo *escolhido = NULL;
    for (int i = 0; i < MAX_PROCESSOS; i++) {
        Processo *candidato = processo_na_posicao(i);
        if (!candidato || candidato->estado != ESTADO_PRONTO) continue;
        if (!escolhido) { escolhido = candidato; continue; }
        if (usar_prioridades && candidato->prioridade != escolhido->prioridade) {
            if (candidato->prioridade < escolhido->prioridade) escolhido = candidato;
        } else if (candidato->ordem_na_fila < escolhido->ordem_na_fila) {
            escolhido = candidato;
        }
    }
    return escolhido;
}

int execucao_existe_concorrente(const Processo *processo, int usar_prioridades) {
    for (int i = 0; i < MAX_PROCESSOS; i++) {
        const Processo *outro = processo_na_posicao(i);
        if (!outro || outro == processo || outro->estado != ESTADO_PRONTO) continue;
        if (!usar_prioridades || outro->prioridade <= processo->prioridade) return 1;
    }
    return 0;
}

int execucao_existe_pronto_com_prioridade_maior(int prioridade) {
    for (int i = 0; i < MAX_PROCESSOS; i++) {
        const Processo *outro = processo_na_posicao(i);
        if (outro && outro->estado == ESTADO_PRONTO && outro->prioridade < prioridade) return 1;
    }
    return 0;
}

int execucao_ha_processos_vivos(void) {
    for (int i = 0; i < MAX_PROCESSOS; i++) {
        const Processo *p = processo_na_posicao(i);
        if (p && p->estado != ESTADO_CONCLUIDO) return 1;
    }
    return 0;
}

static int eh_syscall_de_disco(const char *nome) {
    return strstr(nome, "disk") != NULL;
}

void execucao_descrever_syscall(const SyscallAgendada *syscall, char *saida, size_t capacidade) {
    if (eh_syscall_de_disco(syscall->nome)) snprintf(saida, capacidade, "E/S de Disco");
    else snprintf(saida, capacidade, "Syscall %s", syscall->nome);
}

static void enfileirar_requisicao_de_disco(const Processo *processo, const SyscallAgendada *syscall) {
    if (!eh_syscall_de_disco(syscall->nome) || disco_meio() != MEIO_HDD) return;
    char rotulo[40];
    snprintf(rotulo, sizeof rotulo, "%.20s PID %d", syscall->nome, processo->identificador);
    int trilha = (processo->identificador * 37 + (int)syscall->instante_ms * 13) % NUM_TRILHAS;
    TipoOperacao tipo = strstr(syscall->nome, "write") ? OP_ESCRITA : OP_LEITURA;
    escalonador_enfileirar(0, trilha, tipo, processo->identificador % TOTAL_BLOCOS, rotulo);
}

static void avancar_relogio(const Processo *executando) {
    relogio_ms++;

    for (int i = 0; i < MAX_PROCESSOS; i++) {
        Processo *p = processo_na_posicao(i);
        if (!p || p == executando) continue;
        if (p->estado == ESTADO_PRONTO) p->espera_ms++;
        else if (p->estado == ESTADO_BLOQUEADO) p->bloqueado_ms++;
    }

    for (int i = 0; i < MAX_PROCESSOS; i++) {
        Processo *p = processo_na_posicao(i);
        if (!p || p == executando) continue;
        if (p->estado == ESTADO_NOVO && p->chegada_ms <= relogio_ms) {
            execucao_marcar_pronto(p);
            registrar_evento("[%ldms] Chegada: PID %d (%s) NOVO -> PRONTO", relogio_ms, p->identificador, p->nome);
        } else if (p->estado == ESTADO_BLOQUEADO && p->desbloqueio_em_ms <= relogio_ms) {
            execucao_marcar_pronto(p);
            registrar_evento("[%ldms] Evento E/S: PID %d (%s) encerra a E/S -> BLOQUEADO -> PRONTO",
                             relogio_ms, p->identificador, p->nome);
        }
    }
}

void execucao_passo_ocioso(void) {
    cpu_ociosa_ms++;
    avancar_relogio(NULL);
}

static int escolher_pagina(Processo *processo, int *escrita) {
    int total = processo->total_paginas;
    int pagina;
    if (processo->referencias > 0 && proximo_aleatorio(&processo->semente_paginas) % 100 < 70) {
        int desvio = (int)(proximo_aleatorio(&processo->semente_paginas) % 3) - 1;
        pagina = (processo->ultima_pagina + desvio + total) % total;
    } else {
        pagina = (int)(proximo_aleatorio(&processo->semente_paginas) % (unsigned int)total);
    }
    *escrita = proximo_aleatorio(&processo->semente_paginas) % 100 < 30;
    return pagina;
}

static SyscallAgendada *syscall_vencida(Processo *processo) {
    for (int i = 0; i < processo->total_syscalls; i++) {
        SyscallAgendada *s = &processo->syscalls[i];
        if (!s->ja_disparada && s->instante_ms == processo->executado_ms) return s;
    }
    return NULL;
}

void execucao_executar_passo(Processo *processo, ResultadoPasso *resultado) {
    memset(resultado, 0, sizeof *resultado);
    long instante = relogio_ms + 1;

    if (processo->tipo == PROCESSO_NORMAL && processo->total_paginas > 0) {
        resultado->pagina = escolher_pagina(processo, &resultado->escrita);
        resultado->referenciou_pagina = 1;
        paginacao_referenciar(processo, resultado->pagina, resultado->escrita, instante, 0, &resultado->acesso);
        processo->ultima_pagina = resultado->pagina;
        processo->referencias++;
        if (resultado->acesso.acerto) processo->acertos++;
        else processo->page_faults++;
    }

    processo->executado_ms++;
    cpu_ocupada_ms++;
    avancar_relogio(processo);

    if (processo->executado_ms >= processo->burst_ms) {
        processo->estado = ESTADO_CONCLUIDO;
        processo->conclusao_ms = relogio_ms;
        memoria_liberar_processo(processo, &resultado->liberacao);
        resultado->evento = EVENTO_CONCLUIDO;
        return;
    }

    SyscallAgendada *syscall = syscall_vencida(processo);
    if (syscall) {
        syscall->ja_disparada = 1;
        processo->estado = ESTADO_BLOQUEADO;
        processo->desbloqueio_em_ms = relogio_ms + syscall->duracao_ms;
        enfileirar_requisicao_de_disco(processo, syscall);
        resultado->evento = EVENTO_SYSCALL;
        resultado->syscall = syscall;
    }
}