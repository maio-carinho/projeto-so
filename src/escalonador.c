#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "escalonador.h"

typedef struct {
    Requisicao fila[MAX_REQUISICOES];
    int quantidade;
    int cabeca;
    long total_movimento;
    long total_atendidas;
} FilaDisco;

static FilaDisco filas[MAX_DISCOS];
static Algoritmo algoritmo_atual = ALG_SSTF;
static int direcao_sobe = 1;
static int registro_ativo = 1;

typedef enum { PASSO_INICIO, PASSO_ATENDE, PASSO_EXTREMO, PASSO_RETORNO } TipoPasso;
typedef struct { int trilha; TipoPasso tipo; } Passo;
#define MAX_PASSOS (MAX_REQUISICOES + 4)

void escalonador_init(void) {
    memset(filas, 0, sizeof filas);
    algoritmo_atual = ALG_SSTF;
    direcao_sobe = 1;
    registro_ativo = 1;
}

int escalonador_ativar(int ativo) {
    int anterior = registro_ativo;
    registro_ativo = ativo;
    return anterior;
}

void escalonador_reiniciar_discos(void) {
    memset(filas, 0, sizeof filas);
}

static int disco_valido(int disco) {
    return disco >= 0 && disco < MAX_DISCOS;
}

static void ordenar_crescente(int *valores, int quantidade) {
    for (int i = 1; i < quantidade; i++) {
        int chave = valores[i];
        int j = i - 1;
        while (j >= 0 && valores[j] > chave) {
            valores[j + 1] = valores[j];
            j--;
        }
        valores[j + 1] = chave;
    }
}

static void adicionar_passo(Passo *passos, int *total, int trilha, TipoPasso tipo) {
    passos[*total].trilha = trilha;
    passos[*total].tipo = tipo;
    (*total)++;
}

static int montar_percurso(const FilaDisco *fila, Algoritmo algoritmo, Passo *passos) {
    int quantidade = fila->quantidade, total = 0;
    int cabeca = fila->cabeca;
    int trilhas[MAX_REQUISICOES];
    for (int i = 0; i < quantidade; i++) trilhas[i] = fila->fila[i].trilha;

    adicionar_passo(passos, &total, cabeca, PASSO_INICIO);

    if (algoritmo == ALG_FCFS) {
        for (int i = 0; i < quantidade; i++)
            adicionar_passo(passos, &total, trilhas[i], PASSO_ATENDE);

    } else if (algoritmo == ALG_SSTF) {
        int usada[MAX_REQUISICOES] = {0};
        int posicao = cabeca;
        for (int k = 0; k < quantidade; k++) {
            int melhor = -1;
            for (int i = 0; i < quantidade; i++) {
                if (usada[i]) continue;
                if (melhor < 0 || abs(trilhas[i] - posicao) < abs(trilhas[melhor] - posicao))
                    melhor = i;
            }
            usada[melhor] = 1;
            posicao = trilhas[melhor];
            adicionar_passo(passos, &total, posicao, PASSO_ATENDE);
        }

    } else {
        int ultima = NUM_TRILHAS - 1;
        ordenar_crescente(trilhas, quantidade);
        int restantes = 0;

        if (direcao_sobe) {
            for (int i = 0; i < quantidade; i++) {
                if (trilhas[i] >= cabeca) adicionar_passo(passos, &total, trilhas[i], PASSO_ATENDE);
                else restantes++;
            }
            if (passos[total - 1].trilha != ultima)
                adicionar_passo(passos, &total, ultima, PASSO_EXTREMO);
            if (algoritmo == ALG_SCAN) {
                for (int i = quantidade - 1; i >= 0; i--)
                    if (trilhas[i] < cabeca) adicionar_passo(passos, &total, trilhas[i], PASSO_ATENDE);
            } else if (restantes > 0) {
                adicionar_passo(passos, &total, 0, PASSO_RETORNO);
                for (int i = 0; i < quantidade; i++)
                    if (trilhas[i] < cabeca) adicionar_passo(passos, &total, trilhas[i], PASSO_ATENDE);
            }
        } else {
            for (int i = quantidade - 1; i >= 0; i--) {
                if (trilhas[i] <= cabeca) adicionar_passo(passos, &total, trilhas[i], PASSO_ATENDE);
                else restantes++;
            }
            if (passos[total - 1].trilha != 0)
                adicionar_passo(passos, &total, 0, PASSO_EXTREMO);
            if (algoritmo == ALG_SCAN) {
                for (int i = 0; i < quantidade; i++)
                    if (trilhas[i] > cabeca) adicionar_passo(passos, &total, trilhas[i], PASSO_ATENDE);
            } else if (restantes > 0) {
                adicionar_passo(passos, &total, ultima, PASSO_RETORNO);
                for (int i = quantidade - 1; i >= 0; i--)
                    if (trilhas[i] > cabeca) adicionar_passo(passos, &total, trilhas[i], PASSO_ATENDE);
            }
        }
    }
    return total;
}

static long deslocamento_total(const Passo *passos, int total) {
    long soma = 0;
    for (int i = 1; i < total; i++) soma += labs((long)passos[i].trilha - passos[i - 1].trilha);
    return soma;
}

static void atender_fila(int disco, const Passo *passos, int total_passos) {
    FilaDisco *fila = &filas[disco];
    fila->total_movimento += deslocamento_total(passos, total_passos);
    fila->total_atendidas += fila->quantidade;
    fila->cabeca = passos[total_passos - 1].trilha;
    fila->quantidade = 0;
}

void escalonador_enfileirar(int disco, int trilha, TipoOperacao tipo,
                            int bloco_fisico, const char *rotulo) {
    if (!registro_ativo || !disco_valido(disco)) return;
    if (trilha < 0) trilha = 0;
    if (trilha >= NUM_TRILHAS) trilha = NUM_TRILHAS - 1;

    if (filas[disco].quantidade >= MAX_REQUISICOES)
        escalonador_executar(disco, algoritmo_atual, 0, 0);

    FilaDisco *fila = &filas[disco];
    Requisicao *nova = &fila->fila[fila->quantidade++];
    nova->trilha = trilha;
    nova->tipo = tipo;
    nova->bloco_fisico = bloco_fisico;
    snprintf(nova->rotulo, sizeof nova->rotulo, "%s", rotulo ? rotulo : "");
}

int escalonador_quantidade(int disco) {
    return disco_valido(disco) ? filas[disco].quantidade : 0;
}

const Requisicao *escalonador_requisicao(int disco, int indice) {
    if (!disco_valido(disco) || indice < 0 || indice >= filas[disco].quantidade) return NULL;
    return &filas[disco].fila[indice];
}

void escalonador_limpar_filas(void) {
    for (int d = 0; d < MAX_DISCOS; d++) {
        filas[d].quantidade = 0;
        filas[d].total_movimento = 0;
        filas[d].total_atendidas = 0;
    }
}

int escalonador_cabeca(int disco) {
    return disco_valido(disco) ? filas[disco].cabeca : 0;
}

int escalonador_definir_cabeca(int disco, int trilha) {
    if (!disco_valido(disco)) return reg_erro("Disco inválido.");
    if (trilha < 0 || trilha >= NUM_TRILHAS)
        return reg_erro("Trilha inválida: %d (use 0 a %d)", trilha, NUM_TRILHAS - 1);
    filas[disco].cabeca = trilha;
    return 0;
}

void escalonador_definir_algoritmo(Algoritmo algoritmo) { algoritmo_atual = algoritmo; }
Algoritmo escalonador_algoritmo(void) { return algoritmo_atual; }
void escalonador_definir_direcao(int sobe) { direcao_sobe = sobe; }
int escalonador_direcao_sobe(void) { return direcao_sobe; }

const char *escalonador_nome(Algoritmo algoritmo) {
    static const char *nomes[] = { "FCFS", "SSTF", "SCAN", "C-SCAN" };
    return nomes[algoritmo];
}

const char *escalonador_nome_completo(Algoritmo algoritmo) {
    static const char *nomes[] = {
        "FCFS (First-Come, First-Served)",
        "SSTF (Shortest Seek Time First)",
        "SCAN (elevador)",
        "C-SCAN (elevador circular)"
    };
    return nomes[algoritmo];
}

int escalonador_algoritmo_por_nome(const char *texto, Algoritmo *saida) {
    char minusculo[16];
    size_t i = 0;
    for (; texto[i] && i < sizeof minusculo - 1; i++)
        minusculo[i] = (char)tolower((unsigned char)texto[i]);
    minusculo[i] = '\0';

    if (strcmp(minusculo, "fcfs") == 0) *saida = ALG_FCFS;
    else if (strcmp(minusculo, "sstf") == 0) *saida = ALG_SSTF;
    else if (strcmp(minusculo, "scan") == 0) *saida = ALG_SCAN;
    else if (strcmp(minusculo, "cscan") == 0 || strcmp(minusculo, "c-scan") == 0) *saida = ALG_CSCAN;
    else return reg_erro("Algoritmo desconhecido: '%s' (use fcfs, sstf, scan ou cscan)", texto);
    return 0;
}

static void acrescentar(char *destino, size_t capacidade, const char *formato, ...) {
    size_t usado = strlen(destino);
    if (usado + 1 >= capacidade) return;
    va_list args;
    va_start(args, formato);
    vsnprintf(destino + usado, capacidade - usado, formato, args);
    va_end(args);
}

long escalonador_simular(int disco, Algoritmo algoritmo) {
    if (!disco_valido(disco) || filas[disco].quantidade == 0) return 0;
    Passo passos[MAX_PASSOS];
    int total = montar_percurso(&filas[disco], algoritmo, passos);
    return deslocamento_total(passos, total);
}

long escalonador_executar(int disco, Algoritmo algoritmo, int imprimir, int rotular_disco) {
    if (!disco_valido(disco)) return 0;
    FilaDisco *fila = &filas[disco];
    if (fila->quantidade == 0) {
        if (imprimir) printf("Nenhuma requisição pendente.\n");
        return 0;
    }

    Passo passos[MAX_PASSOS];
    int total = montar_percurso(fila, algoritmo, passos);
    long movimento = deslocamento_total(passos, total);

    if (imprimir) {
        char ordem[4096] = "", formula[4096] = "", parcelas[4096] = "";
        int tem_extremo = 0, tem_retorno = 0, primeira_parcela = 1;

        for (int i = 0; i < total; i++) {
            if (passos[i].tipo == PASSO_INICIO || passos[i].tipo == PASSO_ATENDE)
                acrescentar(ordem, sizeof ordem, "%s%d", i ? " -> " : "", passos[i].trilha);
            if (passos[i].tipo == PASSO_EXTREMO) tem_extremo = 1;
            if (passos[i].tipo == PASSO_RETORNO) tem_retorno = 1;
            if (i > 0) {
                long distancia = labs((long)passos[i].trilha - passos[i - 1].trilha);
                acrescentar(formula, sizeof formula, "%s|%d-%d|", i > 1 ? " + " : "",
                            passos[i - 1].trilha, passos[i].trilha);
                acrescentar(parcelas, sizeof parcelas, "%s%ld", primeira_parcela ? "" : " + ", distancia);
                primeira_parcela = 0;
            }
        }

        if (rotular_disco)
            printf("--- ESCALONAMENTO DE DISCO: %s - Disco %d ---\n",
                   escalonador_nome_completo(algoritmo), disco + 1);
        else
            printf("--- ESCALONAMENTO DE DISCO: %s ---\n", escalonador_nome_completo(algoritmo));
        printf("Ordem de Atendimento : %s\n", ordem);
        printf("Deslocamento da Cabeça: %s\n", formula);
        printf("Total de Movimento : %s = %ld trilhas\n", parcelas, movimento);
        if (tem_extremo)
            printf("Obs.: a cabeça vai até a extremidade do disco antes de inverter a direção.\n");
        if (tem_retorno)
            printf("Obs.: o retorno à outra extremidade não atende requisições, mas conta como deslocamento.\n");
    }

    atender_fila(disco, passos, total);
    return movimento;
}

long escalonador_total_movimento(int disco) {
    return disco_valido(disco) ? filas[disco].total_movimento : 0;
}

long escalonador_total_atendidas(int disco) {
    return disco_valido(disco) ? filas[disco].total_atendidas : 0;
}