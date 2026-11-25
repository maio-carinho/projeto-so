#include <stdio.h>
#include <string.h>
#include "memoria/processo.h"
#include "memoria/reservada.h"

static Segmento segmentos[MAX_SEGMENTOS];
static int quantidade_segmentos;

void reservada_formatar_mb(long kb, char *saida, size_t capacidade) {
    char numero[32];
    snprintf(numero, sizeof numero, "%.2f", kb / 1024.0);
    size_t tamanho = strlen(numero);
    if (numero[tamanho - 1] == '0') numero[tamanho - 1] = '\0';     
    snprintf(saida, capacidade, "%s MB", numero);
}

void reservada_iniciar(void) {
    memset(segmentos, 0, sizeof segmentos);

    segmentos[0].inicio_kb = 0;
    segmentos[0].tamanho_kb = NUCLEO_BASE_KB;
    segmentos[0].livre = 0;
    segmentos[0].identificador = IDENTIFICADOR_NUCLEO;
    snprintf(segmentos[0].nome, sizeof segmentos[0].nome, "Núcleo Base do SO");

    segmentos[1].inicio_kb = NUCLEO_BASE_KB;
    segmentos[1].tamanho_kb = RESERVADA_TOTAL_KB - NUCLEO_BASE_KB;
    segmentos[1].livre = 1;

    quantidade_segmentos = 2;
}

void reservada_diagnosticar(DiagnosticoReservada *diagnostico) {
    memset(diagnostico, 0, sizeof *diagnostico);
    for (int i = 0; i < quantidade_segmentos; i++) {
        if (!segmentos[i].livre) {
            diagnostico->ocupado_kb += segmentos[i].tamanho_kb;
            continue;
        }
        diagnostico->livre_total_kb += segmentos[i].tamanho_kb;
        diagnostico->fragmentos_livres++;
        if (segmentos[i].tamanho_kb > diagnostico->maior_bloco_kb)
            diagnostico->maior_bloco_kb = segmentos[i].tamanho_kb;
    }
    diagnostico->fragmentacao_externa = diagnostico->fragmentos_livres > 1;
}

int reservada_alocar(const char *nome, int identificador, long tamanho_kb, long *inicio_kb) {
    for (int i = 0; i < quantidade_segmentos; i++) {
        if (!segmentos[i].livre || segmentos[i].tamanho_kb < tamanho_kb) continue;

        if (segmentos[i].tamanho_kb > tamanho_kb) {
            if (quantidade_segmentos >= MAX_SEGMENTOS)
                return reg_erro("Mapa da Área Reservada cheio (máximo de %d segmentos).", MAX_SEGMENTOS);
            memmove(&segmentos[i + 2], &segmentos[i + 1],
                    (size_t)(quantidade_segmentos - i - 1) * sizeof(Segmento));
            memset(&segmentos[i + 1], 0, sizeof(Segmento));
            segmentos[i + 1].inicio_kb = segmentos[i].inicio_kb + tamanho_kb;
            segmentos[i + 1].tamanho_kb = segmentos[i].tamanho_kb - tamanho_kb;
            segmentos[i + 1].livre = 1;
            quantidade_segmentos++;
            segmentos[i].tamanho_kb = tamanho_kb;
        }

        segmentos[i].livre = 0;
        segmentos[i].identificador = identificador;
        snprintf(segmentos[i].nome, sizeof segmentos[i].nome, "%s", nome);
        *inicio_kb = segmentos[i].inicio_kb;
        return 0;
    }

    DiagnosticoReservada diagnostico;
    reservada_diagnosticar(&diagnostico);
    if (diagnostico.livre_total_kb >= tamanho_kb)
        return reg_erro("Fragmentação externa: há %ld KB livres no total, mas o maior bloco contíguo tem só "
                        "%ld KB (pedido: %ld KB). Use 'memory_compact --reserved'.",
                        diagnostico.livre_total_kb, diagnostico.maior_bloco_kb, tamanho_kb);
    return reg_erro("Área Reservada sem espaço: pedido de %ld KB, livres %ld KB.",
                    tamanho_kb, diagnostico.livre_total_kb);
}

int reservada_liberar(int identificador, long *inicio_kb, long *tamanho_kb) {
    for (int i = 0; i < quantidade_segmentos; i++) {
        if (segmentos[i].livre || segmentos[i].identificador != identificador) continue;

        *inicio_kb = segmentos[i].inicio_kb;
        *tamanho_kb = segmentos[i].tamanho_kb;
        segmentos[i].livre = 1;
        segmentos[i].identificador = 0;
        segmentos[i].nome[0] = '\0';

        int posicao = 0;
        while (posicao + 1 < quantidade_segmentos) {
            if (segmentos[posicao].livre && segmentos[posicao + 1].livre) {
                segmentos[posicao].tamanho_kb += segmentos[posicao + 1].tamanho_kb;
                memmove(&segmentos[posicao + 1], &segmentos[posicao + 2],
                        (size_t)(quantidade_segmentos - posicao - 2) * sizeof(Segmento));
                quantidade_segmentos--;
            } else {
                posicao++;
            }
        }
        return 0;
    }
    return reg_erro("Nenhum bloco da Área Reservada pertence ao processo %d.", identificador);
}

void reservada_imprimir_mapa(void) {
    DiagnosticoReservada diagnostico;
    reservada_diagnosticar(&diagnostico);
    char inicio[32], fim[32], livre[32], maior[32];

    printf("--- MAPA DA ÁREA CRÍTICA/RESERVADA DA RAM (Total: %d MB - Alocação Fixa) ---\n",
           RESERVADA_TOTAL_KB / 1024);
    int numero_lacuna = 0;
    for (int i = 0; i < quantidade_segmentos; i++) {
        const Segmento *s = &segmentos[i];
        reservada_formatar_mb(s->inicio_kb, inicio, sizeof inicio);
        reservada_formatar_mb(s->inicio_kb + s->tamanho_kb, fim, sizeof fim);

        if (!s->livre) {
            printf("[%s - %s] : %s (%ld KB) [OCUPADO]\n", inicio, fim, s->nome, s->tamanho_kb);
        } else if (diagnostico.fragmentacao_externa) {
            printf("[%s - %s] : FRAGMENTO LIVRE %d (%ld KB) [LIVRE - LACUNA]\n",
                   inicio, fim, ++numero_lacuna, s->tamanho_kb);
        } else {
            printf("[%s - %s] : Bloco Livre Contíguo (%ld KB) [LIVRE]\n", inicio, fim, s->tamanho_kb);
        }
    }

    reservada_formatar_mb(diagnostico.livre_total_kb, livre, sizeof livre);
    reservada_formatar_mb(diagnostico.maior_bloco_kb, maior, sizeof maior);
    printf("--- DIAGNÓSTICO DE FRAGMENTAÇÃO EXTERNA ---\n");
    printf("Total de Memória Livre na Área Reservada : %ld KB (%s)\n", diagnostico.livre_total_kb, livre);
    printf("Maior Bloco Contíguo Disponível : %ld KB (%s)\n", diagnostico.maior_bloco_kb, maior);
    if (diagnostico.fragmentacao_externa) {
        printf("Fragmentação Externa Detectada : SIM (%d fragmentos não contíguos)\n",
               diagnostico.fragmentos_livres);
        printf("Status: Uma requisição contígua maior que %ld KB (e até %ld KB) falhará por ausência de bloco único.\n",
               diagnostico.maior_bloco_kb, diagnostico.livre_total_kb);
    } else {
        printf("Fragmentação Externa Detectada : NÃO\n");
        if (diagnostico.livre_total_kb == 0)
            printf("Status: Área Reservada totalmente ocupada.\n");
        else
            printf("Status: Todo o espaço livre está em um único bloco contíguo.\n");
    }
}

int reservada_compactar(long *kb_copiados) {
    Segmento novos[MAX_SEGMENTOS];
    int quantidade = 0, movidos = 0;
    long proximo_inicio = 0;
    char antes_inicio[32], antes_fim[32], depois_inicio[32], depois_fim[32];

    *kb_copiados = 0;
    for (int i = 0; i < quantidade_segmentos; i++) {
        if (segmentos[i].livre) continue;

        novos[quantidade] = segmentos[i];
        if (segmentos[i].inicio_kb != proximo_inicio) {
            reservada_formatar_mb(segmentos[i].inicio_kb, antes_inicio, sizeof antes_inicio);
            reservada_formatar_mb(segmentos[i].inicio_kb + segmentos[i].tamanho_kb, antes_fim, sizeof antes_fim);
            reservada_formatar_mb(proximo_inicio, depois_inicio, sizeof depois_inicio);
            reservada_formatar_mb(proximo_inicio + segmentos[i].tamanho_kb, depois_fim, sizeof depois_fim);
            printf("- Mapeamento de '%s': [%s a %s] -> Ajustado para: [%s a %s]\n",
                   segmentos[i].nome, antes_inicio, antes_fim, depois_inicio, depois_fim);

            novos[quantidade].inicio_kb = proximo_inicio;
            Processo *dono = processo_por_identificador(segmentos[i].identificador);
            if (dono) dono->inicio_kb = proximo_inicio;     
            *kb_copiados += segmentos[i].tamanho_kb;
            movidos++;
        }
        proximo_inicio += segmentos[i].tamanho_kb;
        quantidade++;
    }

    if (proximo_inicio < RESERVADA_TOTAL_KB) {
        memset(&novos[quantidade], 0, sizeof(Segmento));
        novos[quantidade].inicio_kb = proximo_inicio;
        novos[quantidade].tamanho_kb = RESERVADA_TOTAL_KB - proximo_inicio;
        novos[quantidade].livre = 1;
        quantidade++;
    }

    memcpy(segmentos, novos, (size_t)quantidade * sizeof(Segmento));
    quantidade_segmentos = quantidade;
    return movidos;
}
