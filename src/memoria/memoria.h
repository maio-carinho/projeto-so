#ifndef MEMORIA_H
#define MEMORIA_H

void memoria_iniciar(void);

int cmd_create_process(int argc, char **argv);
int cmd_terminate_process(int argc, char **argv);
int cmd_memory_status(int argc, char **argv);
int cmd_memory_compact(int argc, char **argv);
int cmd_translate_address(int argc, char **argv);
int cmd_execute_step(int argc, char **argv);
int cmd_memory_algoritmo(int argc, char **argv);
int cmd_memory_relatorio(int argc, char **argv);

#endif
