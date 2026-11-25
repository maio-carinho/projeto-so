#include <stdio.h>
#include <string.h>
#include "memoria/processo.h"

static Processo processos[MAX_PROCESSOS];
static int proximo_identificador = PRIMEIRO_IDENTIFICADOR;

void processo_iniciar(void) {
    memset(processos, 0, sizeof processos);
    proximo_identificador = PRIMEIRO_IDENTIFICADOR;
}

int processo_proximo_identificador(void) {
    return proximo_identificador;
}

int processo_validar_novo(const char *nome) {
    if (!nome || nome[0] == '\0') return reg_erro("Informe o nome do processo: --name <nome>");
    if (strlen(nome) >= TAM_NOME) return reg_erro("Nome de processo muito longo (máximo %d caracteres).", TAM_NOME - 1);
    if (processo_por_nome(nome)) return reg_erro("Já existe um processo chamado '%s'.", nome);
    for (int i = 0; i < MAX_PROCESSOS; i++)
        if (!processos[i].em_uso) return 0;
    return reg_erro("Tabela de processos cheia (máximo de %d processos).", MAX_PROCESSOS);
}

Processo *processo_criar(const char *nome, TipoProcesso tipo, long tamanho_bytes, int dono_uid) {
    for (int i = 0; i < MAX_PROCESSOS; i++) {
        if (processos[i].em_uso) continue;

        Processo *novo = &processos[i];
        memset(novo, 0, sizeof *novo);
        novo->em_uso = 1;
        novo->identificador = proximo_identificador++;
        snprintf(novo->nome, sizeof novo->nome, "%s", nome);
        novo->tipo = tipo;
        novo->dono_uid = dono_uid;
        novo->tamanho_bytes = tamanho_bytes;

        if (tipo == PROCESSO_NORMAL) {
            novo->total_paginas = (int)((tamanho_bytes + TAM_PAGINA - 1) / TAM_PAGINA);
            for (int p = 0; p < novo->total_paginas; p++) {
                novo->tabela_paginas[p].presente = 0;
                novo->tabela_paginas[p].moldura = -1;
                novo->tabela_paginas[p].posicao_swap = -1;
            }
        }
        return novo;
    }
    return NULL;
}

void processo_remover(Processo *processo) {
    if (processo) processo->em_uso = 0;
}

Processo *processo_por_nome(const char *nome) {
    for (int i = 0; i < MAX_PROCESSOS; i++)
        if (processos[i].em_uso && strcmp(processos[i].nome, nome) == 0) return &processos[i];
    return NULL;
}

Processo *processo_por_identificador(int identificador) {
    for (int i = 0; i < MAX_PROCESSOS; i++)
        if (processos[i].em_uso && processos[i].identificador == identificador) return &processos[i];
    return NULL;
}

Processo *processo_na_posicao(int posicao) {
    if (posicao < 0 || posicao >= MAX_PROCESSOS || !processos[posicao].em_uso) return NULL;
    return &processos[posicao];
}
