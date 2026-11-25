#include <stdio.h>
#include <string.h>
#include "memoria/paginacao.h"
#include "memoria/swap.h"

typedef struct {
    int ocupada;
    int identificador;       
    int pagina;
    long ordem_fila;         
    long carregada_em_ms;
    long ultimo_acesso_ms;   
    int referenciada;        
} Moldura;

typedef struct {
    long acessos;
    long acertos;
    long page_faults;
    long substituicoes;
    long descartes;          
} Estatisticas;

static Moldura molduras[TOTAL_MOLDURAS];
static AlgoritmoSubstituicao algoritmo = ALGORITMO_LRU;
static long relogio_ms;
static long contador_ordem;
static Estatisticas estatisticas;

void paginacao_iniciar(void) {
    memset(molduras, 0, sizeof molduras);
    memset(&estatisticas, 0, sizeof estatisticas);
    algoritmo = ALGORITMO_LRU;
    relogio_ms = 0;
    contador_ordem = 0;
}

const char *paginacao_nome_algoritmo(void) {
    switch (algoritmo) {
        case ALGORITMO_FIFO: return "FIFO";
        case ALGORITMO_LRU: return "LRU";
        default: return "Segunda Chance";
    }
}

int paginacao_definir_algoritmo(const char *nome) {
    if (strcmp(nome, "fifo") == 0) algoritmo = ALGORITMO_FIFO;
    else if (strcmp(nome, "lru") == 0) algoritmo = ALGORITMO_LRU;
    else if (strcmp(nome, "segunda_chance") == 0) algoritmo = ALGORITMO_SEGUNDA_CHANCE;
    else return reg_erro("Algoritmo desconhecido: '%s' (use fifo, lru ou segunda_chance)", nome);
    return 0;
}

int paginacao_paginas_alocadas(void) {
    int total = 0;
    for (int i = 0; i < MAX_PROCESSOS; i++) {
        const Processo *processo = processo_na_posicao(i);
        if (processo && processo->tipo == PROCESSO_NORMAL) total += processo->total_paginas;
    }
    return total;
}

int paginacao_ha_capacidade(int paginas_novas) {
    return paginacao_paginas_alocadas() + paginas_novas <= TOTAL_POSICOES_SWAP;
}

int paginacao_molduras_ocupadas(void) {
    int total = 0;
    for (int i = 0; i < TOTAL_MOLDURAS; i++)
        if (molduras[i].ocupada) total++;
    return total;
}

static void descrever_paginas(int quantidade, char *saida, size_t capacidade) {
    if (quantidade == 1) {
        snprintf(saida, capacidade, "Página 0");
    } else if (quantidade <= 6) {
        size_t usado = (size_t)snprintf(saida, capacidade, "Páginas ");
        for (int p = 0; p < quantidade && usado < capacidade; p++) {
            const char *separador = p == 0 ? "" : (p == quantidade - 1 ? " e " : ", ");
            usado += (size_t)snprintf(saida + usado, capacidade - usado, "%s%d", separador, p);
        }
    } else {
        snprintf(saida, capacidade, "Páginas 0 a %d", quantidade - 1);
    }
}

void paginacao_imprimir_criacao(const Processo *processo) {
    long bytes_ultima_pagina = processo->tamanho_bytes - (long)(processo->total_paginas - 1) * TAM_PAGINA;
    char lista[96];
    descrever_paginas(processo->total_paginas, lista, sizeof lista);

    printf("[CRIAÇÃO DE PROCESSO NÃO CRÍTICO - MEMÓRIA VIRTUAL]\n");
    printf("Processo            : %s (PID %d)\n", processo->nome, processo->identificador);
    if (processo->tamanho_bytes % 1024 == 0)
        printf("Tamanho do Processo : %ld KB (%ld Bytes)\n", processo->tamanho_bytes / 1024, processo->tamanho_bytes);
    else
        printf("Tamanho do Processo : %ld Bytes\n", processo->tamanho_bytes);
    printf("Tamanho da Página   : %d KB (%d Bytes)\n", TAM_PAGINA / 1024, TAM_PAGINA);
    printf("Cálculo de Páginas  : %ld Bytes / %d Bytes = %.2f -> Requer %d Página(s) Lógica(s) (%s).\n",
           processo->tamanho_bytes, TAM_PAGINA, (double)processo->tamanho_bytes / TAM_PAGINA,
           processo->total_paginas, lista);

    printf("--- ANÁLISE DE FRAGMENTAÇÃO INTERNA ---\n");
    if (processo->total_paginas <= 8) {
        for (int p = 0; p < processo->total_paginas; p++) {
            long ocupados = p == processo->total_paginas - 1 ? bytes_ultima_pagina : TAM_PAGINA;
            printf("Página Lógica %d : %ld Bytes ocupados (%ld%%%s)\n", p, ocupados,
                   ocupados * 100 / TAM_PAGINA, ocupados == TAM_PAGINA ? "" : " de uso");
        }
    } else {
        printf("Páginas Lógicas 0 a %d : %d Bytes ocupados (100%%)\n", processo->total_paginas - 2, TAM_PAGINA);
        printf("Página Lógica %d : %ld Bytes ocupados (%ld%% de uso)\n", processo->total_paginas - 1,
               bytes_ultima_pagina, bytes_ultima_pagina * 100 / TAM_PAGINA);
    }
    long desperdicio = (long)processo->total_paginas * TAM_PAGINA - processo->tamanho_bytes;
    printf("Fragmentação Interna Total: %ld Bytes de desperdício na Página Lógica %d.\n",
           desperdicio, processo->total_paginas - 1);
    printf("Status: Tabela de Páginas gerada sem alocar molduras na RAM (Alocação por demanda).\n");
}

static int primeira_moldura_livre(void) {
    for (int i = 0; i < TOTAL_MOLDURAS; i++)
        if (!molduras[i].ocupada) return i;
    return -1;
}

static int moldura_mais_antiga_na_fila(void) {
    int escolhida = -1;
    for (int i = 0; i < TOTAL_MOLDURAS; i++)
        if (molduras[i].ocupada && (escolhida < 0 || molduras[i].ordem_fila < molduras[escolhida].ordem_fila))
            escolhida = i;
    return escolhida;
}

static int escolher_vitima(void) {
    int vitima = -1;

    if (algoritmo == ALGORITMO_LRU) {
        for (int i = 0; i < TOTAL_MOLDURAS; i++)
            if (molduras[i].ocupada && (vitima < 0 || molduras[i].ultimo_acesso_ms < molduras[vitima].ultimo_acesso_ms))
                vitima = i;
        printf("- Analisando histórico de acesso: Página Lógica %d do PID %d não é usada há %ld ms (Mais antiga).\n",
               molduras[vitima].pagina, molduras[vitima].identificador,
               relogio_ms - molduras[vitima].ultimo_acesso_ms);
    } else if (algoritmo == ALGORITMO_FIFO) {
        vitima = moldura_mais_antiga_na_fila();
        printf("- Fila de chegada (FIFO): Página Lógica %d do PID %d está há mais tempo na RAM (%ld ms).\n",
               molduras[vitima].pagina, molduras[vitima].identificador,
               relogio_ms - molduras[vitima].carregada_em_ms);
    } else {
        for (;;) {
            vitima = moldura_mais_antiga_na_fila();
            Moldura *candidata = &molduras[vitima];
            if (!candidata->referenciada) {
                printf("- Segunda Chance: Página Lógica %d do PID %d está no início da fila com bit de referência 0 -> será a vítima.\n",
                       candidata->pagina, candidata->identificador);
                break;
            }
            printf("- Segunda Chance: Página Lógica %d do PID %d está no início da fila com bit de referência 1 -> ganha outra chance (bit zerado, vai para o fim da fila).\n",
                   candidata->pagina, candidata->identificador);
            candidata->referenciada = 0;
            candidata->ordem_fila = ++contador_ordem;
        }
    }
    return vitima;
}

static void retirar_da_ram(int moldura) {
    Moldura *ocupada = &molduras[moldura];
    Processo *dono = processo_por_identificador(ocupada->identificador);
    EntradaTabelaPaginas *entrada = &dono->tabela_paginas[ocupada->pagina];

    printf("- Removendo Página Lógica %d do PID %d (%s) da RAM -> ", ocupada->pagina, dono->identificador, dono->nome);
    if (entrada->modificada || entrada->posicao_swap < 0) {
        if (entrada->posicao_swap < 0) entrada->posicao_swap = swap_reservar_posicao();
        swap_gravar(entrada->posicao_swap, dono->identificador, ocupada->pagina);
        char local[96];
        swap_descrever_posicao(entrada->posicao_swap, local, sizeof local);
        printf("Salvando no Swap (Disco) [%s].\n", local);
    } else {
        estatisticas.descartes++;
        printf("página sem alterações e com cópia em dia no Swap: descartada sem gravar.\n");
    }

    entrada->presente = 0;
    entrada->moldura = -1;
    entrada->modificada = 0;
    ocupada->ocupada = 0;
}

static void carregar_na_ram(Processo *processo, int pagina, int moldura) {
    EntradaTabelaPaginas *entrada = &processo->tabela_paginas[pagina];

    if (entrada->posicao_swap >= 0) {
        swap_ler(entrada->posicao_swap, processo->identificador, pagina);
        char local[96];
        swap_descrever_posicao(entrada->posicao_swap, local, sizeof local);
        printf("- Trazendo Página Lógica %d do Swap (Disco) [%s].\n", pagina, local);
    } else {
        printf("- Página Lógica %d ainda não tem cópia no disco: nasce preenchida com zeros.\n", pagina);
    }

    Moldura *destino = &molduras[moldura];
    destino->ocupada = 1;
    destino->identificador = processo->identificador;
    destino->pagina = pagina;
    destino->ordem_fila = ++contador_ordem;
    destino->carregada_em_ms = relogio_ms;
    destino->ultimo_acesso_ms = relogio_ms;
    destino->referenciada = 1;

    entrada->presente = 1;
    entrada->moldura = moldura;
    entrada->modificada = 0;
    printf("- Carregando Página Lógica %d de %s na Moldura Física %d (RAM Dinâmica).\n", pagina, processo->nome, moldura);
    printf("- Tabela de Páginas atualizada: Página Lógica %d ---> Moldura Física %d.\n", pagina, moldura);
}

void paginacao_acessar(Processo *processo, int pagina, int escrita) {
    EntradaTabelaPaginas *entrada = &processo->tabela_paginas[pagina];

    relogio_ms += MS_POR_REFERENCIA;
    estatisticas.acessos++;
    printf("[CPU - REFERÊNCIA DE MEMÓRIA EM EXECUÇÃO]\n");
    printf("%s requisita acesso à Página Lógica %d (%s)...\n", processo->nome, pagina,
           escrita ? "escrita" : "leitura");

    if (entrada->presente) {
        estatisticas.acertos++;
        Moldura *moldura = &molduras[entrada->moldura];
        moldura->ultimo_acesso_ms = relogio_ms;
        moldura->referenciada = 1;
        if (escrita) entrada->modificada = 1;
        printf("- Consulta Tabela de Páginas -> Página Lógica %d está na RAM (Moldura Física %d) [HIT]\n",
               pagina, entrada->moldura);
        return;
    }

    estatisticas.page_faults++;
    printf("- Consulta Tabela de Páginas -> Página Lógica %d NÃO ESTÁ NA RAM!\n", pagina);
    printf("[PAGE FAULT DETECTADO]: Página Lógica %d ausente na RAM!\n", pagina);

    int moldura = primeira_moldura_livre();
    if (moldura >= 0) {
        printf("Há moldura livre na RAM (Moldura Física %d): nenhuma substituição necessária.\n", moldura);
    } else {
        estatisticas.substituicoes++;
        printf("Memória RAM cheia. Executando Substituição de Página (Algoritmo %s):\n", paginacao_nome_algoritmo());
        moldura = escolher_vitima();
        retirar_da_ram(moldura);
    }

    carregar_na_ram(processo, pagina, moldura);
    if (escrita) entrada->modificada = 1;
}

void paginacao_traduzir(const Processo *processo, int pagina, long deslocamento) {
    const EntradaTabelaPaginas *entrada = &processo->tabela_paginas[pagina];
    printf("[TRADUÇÃO DE ENDEREÇO: PÁGINA LÓGICA -> MOLDURA FÍSICA]\n");
    if (entrada->presente) {
        printf("Solicitação: Página Lógica %d -> Mapeada na Moldura Física %d (RAM Dinâmica) [HIT]\n",
               pagina, entrada->moldura);
        printf("Endereço lógico %ld (página %d, deslocamento %ld) -> endereço físico %ld (moldura %d x %d + %ld)\n",
               (long)pagina * TAM_PAGINA + deslocamento, pagina, deslocamento,
               (long)entrada->moldura * TAM_PAGINA + deslocamento, entrada->moldura, TAM_PAGINA, deslocamento);
    } else {
        printf("Solicitação: Página Lógica %d -> NÃO está em nenhuma moldura da RAM [PAGE FAULT]\n", pagina);
        printf("A tradução só consulta a tabela. Um acesso (execute_step) dispara o tratamento do page fault.\n");
    }
}

void paginacao_liberar_processo(Processo *processo, int *molduras_liberadas, int *posicoes_swap_liberadas) {
    *molduras_liberadas = 0;
    *posicoes_swap_liberadas = 0;
    for (int p = 0; p < processo->total_paginas; p++) {
        EntradaTabelaPaginas *entrada = &processo->tabela_paginas[p];
        if (entrada->presente) {
            molduras[entrada->moldura].ocupada = 0;
            (*molduras_liberadas)++;
        }
        if (entrada->posicao_swap >= 0) {
            swap_liberar_posicao(entrada->posicao_swap);
            (*posicoes_swap_liberadas)++;
        }
    }
}

void paginacao_imprimir_molduras(void) {
    printf("--- RAM DINÂMICA (Memória Virtual): %d molduras de %d KB (%d KB) | Algoritmo: %s ---\n",
           TOTAL_MOLDURAS, TAM_PAGINA / 1024, TOTAL_MOLDURAS * TAM_PAGINA / 1024, paginacao_nome_algoritmo());
    for (int i = 0; i < TOTAL_MOLDURAS; i++) {
        const Moldura *m = &molduras[i];
        if (!m->ocupada) {
            printf("Moldura %02d : LIVRE\n", i);
            continue;
        }
        const Processo *dono = processo_por_identificador(m->identificador);
        printf("Moldura %02d : PID %d (%s) Página %d | referência=%d | modificada=%s | último acesso há %ld ms\n",
               i, m->identificador, dono ? dono->nome : "?", m->pagina, m->referenciada,
               dono && dono->tabela_paginas[m->pagina].modificada ? "SIM" : "NÃO",
               relogio_ms - m->ultimo_acesso_ms);
    }
}

void paginacao_imprimir_tabela(const Processo *processo) {
    printf("--- TABELA DE PÁGINAS: %s (PID %d) ---\n", processo->nome, processo->identificador);
    for (int p = 0; p < processo->total_paginas; p++) {
        const EntradaTabelaPaginas *e = &processo->tabela_paginas[p];
        if (e->presente)
            printf("Página %2d -> Moldura %02d [NA RAM%s]\n", p, e->moldura, e->modificada ? ", modificada" : "");
        else if (e->posicao_swap >= 0)
            printf("Página %2d -> [NO SWAP, posição %d]\n", p, e->posicao_swap);
        else
            printf("Página %2d -> [AINDA NÃO CARREGADA]\n", p);
    }
}

void paginacao_imprimir_estatisticas(void) {
    printf("--- ESTATÍSTICAS DA MEMÓRIA VIRTUAL ---\n");
    printf("Acessos: %ld | Acertos (HIT): %ld | Page faults: %ld", estatisticas.acessos,
           estatisticas.acertos, estatisticas.page_faults);
    if (estatisticas.acessos > 0)
        printf(" (%.1f%% dos acessos)", 100.0 * estatisticas.page_faults / estatisticas.acessos);
    printf("\nSubstituições de página: %ld | Gravações no swap: %ld | Leituras do swap: %ld | Descartes sem gravar: %ld\n",
           estatisticas.substituicoes, swap_total_gravacoes(), swap_total_leituras(), estatisticas.descartes);
}

long paginacao_fragmentacao_interna_total(int *processos_afetados) {
    long total = 0;
    int afetados = 0;
    for (int i = 0; i < MAX_PROCESSOS; i++) {
        const Processo *processo = processo_na_posicao(i);
        if (!processo || processo->tipo != PROCESSO_NORMAL) continue;
        long desperdicio = (long)processo->total_paginas * TAM_PAGINA - processo->tamanho_bytes;
        if (desperdicio > 0) afetados++;
        total += desperdicio;
    }
    if (processos_afetados) *processos_afetados = afetados;
    return total;
}

void paginacao_imprimir_fragmentacao_interna(void) {
    printf("--- FRAGMENTAÇÃO INTERNA (páginas subutilizadas) ---\n");
    int encontrou = 0;
    for (int i = 0; i < MAX_PROCESSOS; i++) {
        const Processo *processo = processo_na_posicao(i);
        if (!processo || processo->tipo != PROCESSO_NORMAL) continue;
        encontrou = 1;
        long desperdicio = (long)processo->total_paginas * TAM_PAGINA - processo->tamanho_bytes;
        long ocupados = TAM_PAGINA - desperdicio;
        printf("%s (PID %d): %ld Bytes em %d página(s) -> %ld Bytes de desperdício na Página %d (%ld%% de uso)\n",
               processo->nome, processo->identificador, processo->tamanho_bytes, processo->total_paginas,
               desperdicio, processo->total_paginas - 1, ocupados * 100 / TAM_PAGINA);
    }
    if (!encontrou) printf("Nenhum processo normal em execução.\n");
    printf("Fragmentação Interna Total: %ld Bytes\n", paginacao_fragmentacao_interna_total(NULL));
}
