#ifndef PROCESSOS_H
#define PROCESSOS_H

#include "processos/concorrencia.h"
#include "processos/cpu.h"

void processos_iniciar(void);

int cmd_create_process(int argc, char **argv);
int cmd_run_simulation(int argc, char **argv);
int cmd_process_status(int argc, char **argv);

#endif