#ifndef RESERVADA_H
#define RESERVADA_H

#include <stddef.h>
#include "comum/utils.h"

#define RESERVADA_TOTAL_KB   4096
#define NUCLEO_BASE_KB       1024
#define IDENTIFICADOR_NUCLEO 0      
#define MAX_SEGMENTOS        32

typedef struct {
    long inicio_kb;
    long tamanho_kb;
    int livre;
    int identificador;
    char nome[TAM_NOME];
} Segmento;

typedef struct {
    long ocupado_kb;
    long livre_total_kb;
    long maior_bloco_kb;
    int fragmentos_livres;      
    int fragmentacao_externa;   
} DiagnosticoReservada;

void reservada_iniciar(void);

int reservada_alocar(const char *nome, int identificador, long tamanho_kb, long *inicio_kb);
int reservada_liberar(int identificador, long *inicio_kb, long *tamanho_kb);

void reservada_diagnosticar(DiagnosticoReservada *diagnostico);
void reservada_imprimir_mapa(void);

int reservada_compactar(long *kb_copiados);

void reservada_formatar_mb(long kb, char *saida, size_t capacidade);

#endif
