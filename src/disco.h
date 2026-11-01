#ifndef DISCO_H
#define DISCO_H

#include "utils.h"

void disco_init(void);

int disco_alocar_bloco(void);

void disco_liberar_bloco(int bloco);

int disco_livre(int bloco);

int livres(void);

int disco_ler_bloco(int bloco, unsigned char *buf);

int disco_gravar_bloco(int bloco, const unsigned char *buf);

#endif