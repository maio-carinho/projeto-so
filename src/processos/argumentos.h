#ifndef ARGUMENTOS_H
#define ARGUMENTOS_H

const char *argumento_valor(int argc, char **argv, const char *opcao);

int argumento_existe(int argc, char **argv, const char *opcao);
int argumento_tempo_ms(const char *texto, long *milissegundos);
int argumento_numero(const char *texto, long *valor);

#endif