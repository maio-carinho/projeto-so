#ifndef ES_H
#define ES_H

void es_iniciar(void);

void es_fim_do_comando(void);

int cmd_io(int argc, char **argv);
int cmd_raid(int argc, char **argv);
int cmd_ssd(int argc, char **argv);
int cmd_meio(int argc, char **argv);

#endif