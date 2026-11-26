#ifndef CONCORRENCIA_H
#define CONCORRENCIA_H

#include "memoria/processo.h"

int cmd_create_thread(int argc, char **argv);
int cmd_mutex_lock(int argc, char **argv);
int cmd_mutex_unlock(int argc, char **argv);

void concorrencia_imprimir(const Processo *processo);

#endif