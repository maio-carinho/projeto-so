#include <stdio.h>
#include <string.h>
#include "seguranca/seguranca.h"
#include "memoria/memoria.h"
#include "memoria/reservada.h"
#include "processos/argumentos.h"
#include "processos/execucao.h"
#include "processos/processos.h"

#define TAMANHO_PADRAO_NORMAL_BYTES  (32L * 1024)     /* 8 páginas */
#define TAMANHO_PADRAO_CRITICO_BYTES (512L * 1024)
#define MAX_BURST_MS                 10000
#define MAX_PASSOS                   200
#define DURACAO_PADRAO_SYSCALL_MS    2

void processos_iniciar(void) {
    execucao_iniciar();
}

/* ---------- create_process ---------- */

/* Lê uma syscall do texto: "read_disk at 3ms (duration: 4ms)" ou "random". */
static int interpretar_syscall(const char *texto, long burst_ms, SyscallAgendada *syscall) {
    char copia[128];
    char *palavras[8];
    int quantidade = 0;

    memset(syscall, 0, sizeof *syscall);
    snprintf(copia, sizeof copia, "%s", texto);
    for (char *palavra = strtok(copia, " \t"); palavra && quantidade < 8; palavra = strtok(NULL, " \t"))
        palavras[quantidade++] = palavra;
    if (quantidade == 0) return reg_erro("Syscall vazia: use \"read_disk at 3ms (duration: 4ms)\" ou \"random\"");

    if (strcmp(palavras[0], "random") == 0) {
        /* Evento aleatório: o sistema sorteia o instante e a duração, sempre do mesmo jeito. */
        snprintf(syscall->nome, sizeof syscall->nome, "io_aleatoria");
        syscall->aleatoria = 1;
        syscall->instante_ms = burst_ms >= 2 ? 1 + execucao_sortear(burst_ms - 1) : burst_ms;
        syscall->duracao_ms = 1 + execucao_sortear(4);
        return 0;
    }

    snprintf(syscall->nome, sizeof syscall->nome, "%.60s", palavras[0]);
    syscall->duracao_ms = DURACAO_PADRAO_SYSCALL_MS;
    int achou_instante = 0;
    for (int i = 1; i < quantidade; i++) {
        if (strcmp(palavras[i], "at") == 0 && i + 1 < quantidade) {
            if (argumento_tempo_ms(palavras[++i], &syscall->instante_ms) != 0) return -1;
            achou_instante = 1;
        } else if (strncmp(palavras[i], "(duration:", 10) == 0 || strcmp(palavras[i], "duration:") == 0) {
            /* O valor pode vir colado ("(duration:4ms)") ou na palavra seguinte. */
            const char *valor = palavras[i] + (palavras[i][0] == '(' ? 10 : 9);
            if (*valor == '\0' && i + 1 < quantidade) valor = palavras[++i];
            char numero[32];
            snprintf(numero, sizeof numero, "%s", valor);
            size_t tamanho = strlen(numero);
            if (tamanho > 0 && numero[tamanho - 1] == ')') numero[tamanho - 1] = '\0';
            if (argumento_tempo_ms(numero, &syscall->duracao_ms) != 0) return -1;
        }
    }
    if (!achou_instante) return reg_erro("Falta o instante da syscall: \"%s at <N>ms (duration: <N>ms)\"", syscall->nome);
    if (syscall->instante_ms < 1 || syscall->instante_ms >= burst_ms)
        return reg_erro("A syscall '%s' precisa disparar entre 1ms e %ldms (o burst do processo é %ldms).",
                        syscall->nome, burst_ms - 1, burst_ms);
    if (syscall->duracao_ms < 1) return reg_erro("A duração do bloqueio precisa ser de pelo menos 1ms.");
    return 0;
}

static void imprimir_criacao(const Processo *processo) {
    printf("[CRIAÇÃO DE PROCESSO] PID: %d | Nome: %s | Prioridade: %d (%s)\n", processo->identificador,
           processo->nome, processo->prioridade, execucao_nome_prioridade(processo->prioridade));
    printf("Burst       : %ldms | Tipo: %s\n", processo->burst_ms, processo->tipo == PROCESSO_CRITICO ? "critical" : "normal");
    if (processo->tipo == PROCESSO_CRITICO) {
        char de[32], ate[32];
        reservada_formatar_mb(processo->inicio_kb, de, sizeof de);
        reservada_formatar_mb(processo->inicio_kb + processo->tamanho_bytes / 1024, ate, sizeof ate);
        printf("Alocação    : Área Reservada, bloco contíguo [%s até %s] (%ld KB, sem paginação)\n", de, ate,
               processo->tamanho_bytes / 1024);
    } else {
        printf("Alocação    : Memória Virtual Paginada (Tabela de Páginas criada)\n");
        printf("Páginas     : %d página(s) de %d KB\n", processo->total_paginas, TAM_PAGINA / 1024);
    }

    if (processo->total_syscalls == 0) printf("Syscall/I/O : Nenhuma\n");
    for (int i = 0; i < processo->total_syscalls; i++) {
        const SyscallAgendada *s = &processo->syscalls[i];
        if (s->aleatoria) printf("Syscall/I/O : Evento Aleatório Habilitado\n");
        else printf("Syscall/I/O : Agendada para t=%ldms (Duração do Bloqueio: %ldms) [%s]\n", s->instante_ms, s->duracao_ms, s->nome);
    }

    if (processo->estado == ESTADO_NOVO) printf("Estado Inicial: NOVO (chega em t=%ldms)\n", processo->chegada_ms);
    else printf("Estado Inicial: PRONTO\n");
}

/* create_process aceita dois formatos:
 *   Etapa 4: --size ou --file (mostra o relatório de memória);
 *   Etapa 5: --burst, --priority, --syscall, --arrival (mostra o perfil de CPU).
 * No formato da Etapa 5 o --size é opcional. */
int cmd_create_process(int argc, char **argv) {
    const char *nome = argumento_valor(argc, argv, "--name");
    const char *texto_tamanho = argumento_valor(argc, argv, "--size");
    const char *arquivo = argumento_valor(argc, argv, "--file");
    const char *texto_tipo = argumento_valor(argc, argv, "--type");
    const char *texto_burst = argumento_valor(argc, argv, "--burst");
    const char *texto_prioridade = argumento_valor(argc, argv, "--priority");
    const char *texto_chegada = argumento_valor(argc, argv, "--arrival");

    SyscallAgendada syscalls[MAX_SYSCALLS_POR_PROCESSO];
    const char *textos_syscall[MAX_SYSCALLS_POR_PROCESSO];
    int total_syscalls = 0;
    for (int i = 0; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--syscall") != 0) continue;
        if (total_syscalls >= MAX_SYSCALLS_POR_PROCESSO)
            return reg_erro("No máximo %d syscalls por processo.", MAX_SYSCALLS_POR_PROCESSO);
        textos_syscall[total_syscalls++] = argv[i + 1];
    }

    int perfil_de_cpu = texto_burst || texto_prioridade || texto_chegada || total_syscalls > 0;
    if (!nome || (texto_tamanho && arquivo) || (!perfil_de_cpu && !texto_tamanho && !arquivo))
        return reg_erro("Uso: create_process --name <nome> [--size <tam> | --file <caminho>] [--type critical|normal]\n"
                        "       [--burst <N>ms] [--priority <1-%d>] [--syscall \"read_disk at <N>ms (duration: <N>ms)\"|random] [--arrival <N>ms]",
                        MAX_PRIORIDADES);

    TipoProcesso tipo = PROCESSO_NORMAL;
    if (texto_tipo && strcmp(texto_tipo, "critical") == 0) tipo = PROCESSO_CRITICO;
    else if (texto_tipo && strcmp(texto_tipo, "normal") != 0)
        return reg_erro("Tipo inválido: '%s' (use critical ou normal)", texto_tipo);

    long burst = BURST_PADRAO_MS;
    if (texto_burst) {
        if (argumento_tempo_ms(texto_burst, &burst) != 0) return -1;
        if (burst < 1 || burst > MAX_BURST_MS) return reg_erro("O burst precisa estar entre 1ms e %dms.", MAX_BURST_MS);
    }

    long prioridade = PRIORIDADE_PADRAO;
    if (texto_prioridade && (!argumento_numero(texto_prioridade, &prioridade) || prioridade < 1 || prioridade > MAX_PRIORIDADES))
        return reg_erro("Prioridade inválida: '%s' (use de 1 a %d; 1 = Alta)", texto_prioridade, MAX_PRIORIDADES);
    if (prioridade == 1) {
        /* Como o 'nice' negativo do Unix: só o kernel dá prioridade máxima a um processo. */
        int negado = seguranca_exigir_kernel("create_process --priority 1 (Alta)");
        if (negado != 0) return negado;
    }

    long chegada = execucao_tempo_atual();
    if (texto_chegada) {
        if (argumento_tempo_ms(texto_chegada, &chegada) != 0) return -1;
        if (chegada < execucao_tempo_atual())
            return reg_erro("A chegada (%ldms) já passou: o relógio está em t=%ldms.", chegada, execucao_tempo_atual());
    }

    for (int i = 0; i < total_syscalls; i++) {
        if (interpretar_syscall(textos_syscall[i], burst, &syscalls[i]) != 0) return -1;
        if (i > 0 && !syscalls[i].aleatoria && !syscalls[i - 1].aleatoria && syscalls[i].instante_ms <= syscalls[i - 1].instante_ms)
            return reg_erro("As syscalls devem ser informadas em ordem crescente de instante.");
    }

    long tamanho_bytes;
    if (arquivo) {
        int resultado = memoria_tamanho_do_arquivo(arquivo, &tamanho_bytes);
        if (resultado != 0) return resultado;
    } else if (texto_tamanho) {
        if (memoria_interpretar_tamanho(texto_tamanho, &tamanho_bytes) != 0) return -1;
    } else {
        tamanho_bytes = tipo == PROCESSO_CRITICO ? TAMANHO_PADRAO_CRITICO_BYTES : TAMANHO_PADRAO_NORMAL_BYTES;
    }

    Processo *processo;
    int resultado = memoria_criar_processo(nome, tipo, tamanho_bytes, !perfil_de_cpu, &processo);
    if (resultado != 0) return resultado;

    processo->prioridade = (int)prioridade;
    processo->burst_ms = burst;
    processo->chegada_ms = chegada;
    processo->total_syscalls = total_syscalls;
    for (int i = 0; i < total_syscalls; i++) processo->syscalls[i] = syscalls[i];

    if (chegada > execucao_tempo_atual()) processo->estado = ESTADO_NOVO;
    else execucao_marcar_pronto(processo);

    if (perfil_de_cpu) imprimir_criacao(processo);
    memoria_registrar_operacao();
    return 0;
}

/* ---------- run_simulation ---------- */

typedef struct {
    int moldura;
    int identificador;
    int pagina;
    int page_fault;
    int do_swap;
    long instante_ms;
} RegistroMoldura;

static void imprimir_acesso(const ResultadoPasso *r) {
    if (!r->referenciou_pagina) {
        printf("CPU executa uma instrução do processo crítico na Área Reservada (sem referências a páginas)\n");
        return;
    }
    const ResultadoAcesso *a = &r->acesso;
    printf("CPU acessa: Página Lógica %02d (%s) -> ", r->pagina, r->escrita ? "escrita" : "leitura");
    if (a->acerto) {
        printf("[RAM: Moldura %02d - HIT] | ", a->moldura);
        if (strcmp(paginacao_nome_algoritmo(), "LRU") == 0)
            printf("Histórico LRU: Pág %02d atualizada (t=%ldms)\n", r->pagina, a->instante_ms);
        else
            printf("Bit de referência da Pág %02d = 1 (t=%ldms)\n", r->pagina, a->instante_ms);
        return;
    }

    printf("[PAGE FAULT!]\n");
    if (a->substituiu) {
        printf("Substituição %s: Página Lógica %02d do PID %d ", paginacao_nome_algoritmo(), a->vitima_pagina, a->vitima_identificador);
        if (strcmp(paginacao_nome_algoritmo(), "LRU") == 0) printf("(não usada há %ldms) ", a->vitima_inativa_ms);
        printf(a->vitima_gravada_no_swap ? "removida para o Swap.\n" : "removida (cópia no Swap já estava em dia).\n");
    }
    printf("Página Lógica %02d alocada na Moldura %02d%s.\n", r->pagina, a->moldura,
           a->veio_do_swap ? " (trazida do Swap)" : "");
}

static void imprimir_evento_do_passo(const Processo *processo, const ResultadoPasso *r) {
    if (r->evento == EVENTO_SYSCALL) {
        char descricao[100];
        execucao_descrever_syscall(r->syscall, descricao, sizeof descricao);
        printf("--- CONTINUAÇÃO DA EXECUÇÃO (t=%ldms) ---\n", execucao_tempo_atual());
        printf("[SYSCALL DETECTADA]: PID %d atingiu a syscall '%s'%s agendada para t=%ldms!\n", processo->identificador,
               r->syscall->nome, r->syscall->aleatoria ? " (evento aleatório)" : "", r->syscall->instante_ms);
        printf("- Mudança de Estado: EXECUTANDO -> BLOQUEADO (%s).\n", descricao);
        printf("- Tempo de Retorno Previsto do Bloqueio: %ldms (Desbloqueio em t=%ldms).\n", r->syscall->duracao_ms,
               processo->desbloqueio_em_ms);
        printf("- CPU liberada -> Escalonador seleciona próximo processo da Fila de Pronto.\n");
    } else if (r->evento == EVENTO_CONCLUIDO) {
        printf("[PROCESSO CONCLUÍDO]: PID %d (%s) terminou em t=%ldms (EXECUTANDO -> CONCLUÍDO).\n",
               processo->identificador, processo->nome, execucao_tempo_atual());
        if (r->liberacao.molduras > 0 || r->liberacao.posicoes_swap > 0)
            printf("- Memória devolvida: %d moldura(s) da RAM e %d posição(ões) do Swap.\n",
                   r->liberacao.molduras, r->liberacao.posicoes_swap);
        else if (r->liberacao.kb_reservados > 0)
            printf("- Memória devolvida: %ld KB da Área Reservada.\n", r->liberacao.kb_reservados);
    }
}

static void imprimir_resumo_das_molduras(const RegistroMoldura *registros, int quantidade) {
    int ocultas = 0;
    printf("[RESUMO DA GERÊNCIA DE MEMÓRIA (RAM DINÂMICA)]\n");
    printf("+-----------------+---------------+----------------------+---------------------+\n");
    printf("| Moldura RAM (F) | Processo/Pág  | Estado de Acesso     | Último Acesso (LRU) |\n");
    printf("+-----------------+---------------+----------------------+---------------------+\n");
    for (int i = 0; i < quantidade; i++) {
        const RegistroMoldura *r = &registros[i];
        const Processo *dono = processo_por_identificador(r->identificador);
        if (!dono || dono->memoria_liberada) { ocultas++; continue; }   /* a moldura já foi devolvida */

        char processo_pagina[24], estado[24], ultimo[24];
        snprintf(processo_pagina, sizeof processo_pagina, "PID %d / P%d", r->identificador, r->pagina);
        if (!r->page_fault) snprintf(estado, sizeof estado, "HIT");
        else snprintf(estado, sizeof estado, "%s", r->do_swap ? "PAGE FAULT (SWAP-IN)" : "PAGE FAULT (NEW)");
        snprintf(ultimo, sizeof ultimo, "t = %ld ms", r->instante_ms);
        printf("| Moldura %02d      | %-13s | %-20s | %-19s |\n", r->moldura, processo_pagina, estado, ultimo);
    }
    printf("+-----------------+---------------+----------------------+---------------------+\n");
    if (ocultas > 0)
        printf("(%d moldura(s) de processos já CONCLUÍDOS foram devolvidas e não aparecem na tabela)\n", ocultas);
}

/* Guarda a última situação de cada moldura tocada durante o comando. */
static void anotar_moldura(RegistroMoldura *registros, int *quantidade, const Processo *processo, const ResultadoPasso *r) {
    if (!r->referenciou_pagina) return;
    RegistroMoldura novo = { r->acesso.moldura, processo->identificador, r->pagina, !r->acesso.acerto,
                             r->acesso.veio_do_swap, r->acesso.instante_ms };
    for (int i = 0; i < *quantidade; i++) {
        if (registros[i].moldura == novo.moldura) { registros[i] = novo; return; }
    }
    registros[(*quantidade)++] = novo;
}

/* Executa a CPU passo a passo (1 passo = 1ms). Com --process roda só aquele processo;
 * sem --process, a cada passo roda o processo que o escalonador escolheria. */
int cmd_run_simulation(int argc, char **argv) {
    const char *nome = argumento_valor(argc, argv, "--process");
    const char *texto_passos = argumento_valor(argc, argv, "--steps");
    long passos = 1;
    if (texto_passos && (!argumento_numero(texto_passos, &passos) || passos < 1 || passos > MAX_PASSOS))
        return reg_erro("Passos inválidos: '%s' (use de 1 a %d)", texto_passos, MAX_PASSOS);

    Processo *alvo = NULL;
    if (nome) {
        alvo = processo_por_nome(nome);
        if (!alvo) return reg_erro("Processo '%s' não encontrado.", nome);
        int negado = memoria_exigir_dono_ou_kernel(alvo, "executar o processo");
        if (negado != 0) return negado;
        if (alvo->estado == ESTADO_CONCLUIDO) return reg_erro("O processo '%s' já está CONCLUÍDO.", nome);
        if (alvo->estado == ESTADO_NOVO) return reg_erro("O processo '%s' ainda não chegou (chega em t=%ldms).", nome, alvo->chegada_ms);
        if (alvo->estado == ESTADO_BLOQUEADO)
            return reg_erro("O processo '%s' está BLOQUEADO até t=%ldms. Rode 'run_simulation --steps N' (sem --process) para o tempo andar.",
                            nome, alvo->desbloqueio_em_ms);
    } else {
        int negado = seguranca_exigir_kernel("run_simulation (sem --process)");
        if (negado != 0) return negado;
        if (!execucao_ha_processos_vivos()) return reg_erro("Nenhum processo para executar (crie com create_process).");
    }

    RegistroMoldura registros[MAX_PASSOS];
    int quantidade_registros = 0;

    for (long passo = 1; passo <= passos; passo++) {
        Processo *processo = alvo ? alvo : execucao_escolher_proximo(1);
        if (!processo) {
            execucao_passo_ocioso();
            printf("--- STEP %ld (t=%ldms) | CPU ociosa ---\n", passo, execucao_tempo_atual());
            execucao_imprimir_eventos();
            continue;
        }

        if (processo->estado != ESTADO_EXECUTANDO) execucao_despachar(processo);
        ResultadoPasso resultado;
        execucao_executar_passo(processo, &resultado);
        printf("--- STEP %ld (t=%ldms) | Executando PID %d ---\n", passo, execucao_tempo_atual(), processo->identificador);
        imprimir_acesso(&resultado);
        anotar_moldura(registros, &quantidade_registros, processo, &resultado);
        imprimir_evento_do_passo(processo, &resultado);
        execucao_imprimir_eventos();

        if (processo->estado == ESTADO_EXECUTANDO && !alvo) execucao_devolver_cpu(processo, 0);
        if (alvo && alvo->estado != ESTADO_EXECUTANDO) break;    /* bloqueou ou terminou */
    }

    if (alvo) execucao_devolver_cpu(alvo, 0);
    if (quantidade_registros > 0) imprimir_resumo_das_molduras(registros, quantidade_registros);
    return 0;
}

/* ---------- process_status ---------- */

/* printf conta bytes, e letras acentuadas têm 2 bytes em UTF-8; aqui o preenchimento conta caracteres. */
static void imprimir_preenchido(const char *texto, int largura) {
    int caracteres = 0;
    for (const char *c = texto; *c; c++)
        if (((unsigned char)*c & 0xC0) != 0x80) caracteres++;
    printf("%s%*s", texto, caracteres < largura ? largura - caracteres : 0, "");
}

int cmd_process_status(int argc, char **argv) {
    (void)argc; (void)argv;
    const char *linha = "+------+----------------+----------+------+------------+-------+-------+---------+-----------+";
    int existe = 0;

    printf("[PROCESSOS] t=%ldms | Trocas de contexto: %ld | CPU ocupada: %ldms\n", execucao_tempo_atual(),
           execucao_trocas_de_contexto(), execucao_cpu_ocupada_ms());
    printf("%s\n| PID  | Nome           | Tipo     | Prio | Estado     | Burst | Exec. | Chegada | Syscalls  |\n%s\n", linha, linha);
    const Processo *todos[MAX_PROCESSOS];
    int quantidade_todos = processo_listar_ordenados(todos, MAX_PROCESSOS);
    for (int i = 0; i < quantidade_todos; i++) {
        const Processo *p = todos[i];
        existe = 1;
        char syscalls[24] = "-";
        if (p->total_syscalls > 0) snprintf(syscalls, sizeof syscalls, "%d %s", p->total_syscalls, p->syscalls[0].aleatoria ? "random" : "agend.");
        printf("| %4d | %-14.14s | %-8s | %4d | ", p->identificador, p->nome,
               p->tipo == PROCESSO_CRITICO ? "critical" : "normal", p->prioridade);
        imprimir_preenchido(execucao_nome_estado(p->estado), 10);
        printf(" | %3ldms | %3ldms | %5ldms | %-9.9s |\n", p->burst_ms, p->executado_ms, p->chegada_ms, syscalls);
    }
    printf("%s\n", linha);
    if (!existe) printf("Nenhum processo criado.\n");

    for (int i = 0; i < quantidade_todos; i++) concorrencia_imprimir(todos[i]);
    return 0;
}