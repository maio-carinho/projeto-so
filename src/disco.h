#ifndef DISCO_H
#define DISCO_H

#include "utils.h"
#include "raid.h"

typedef enum { MEIO_HDD, MEIO_SSD } Meio;

void disco_init(void);

int disco_alocar_bloco(void);

void disco_liberar_bloco(int bloco);

int disco_livre(int bloco);

int livres(void);

int disco_ler_bloco(int bloco, unsigned char *buf);

int disco_gravar_bloco(int bloco, const unsigned char *buf);

Meio disco_meio(void);
int disco_definir_meio(Meio meio);
int disco_definir_raid(NivelRaid nivel);
void disco_imprimir_raid(void);
int disco_testar_falha_raid(int disco);

#endif