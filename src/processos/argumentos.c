#include <stdlib.h>
#include <string.h>
#include "comum/utils.h"
#include "processos/argumentos.h"

const char *argumento_valor(int argc, char **argv, const char *opcao) {
    for (int i = 0; i + 1 < argc; i++)
        if (strcmp(argv[i], opcao) == 0) return argv[i + 1];
    return NULL;
}

int argumento_existe(int argc, char **argv, const char *opcao) {
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], opcao) == 0) return 1;
    return 0;
}

int argumento_numero(const char *texto, long *valor) {
    char *final;
    *valor = strtol(texto, &final, 10);
    return final != texto && *final == '\0' && *valor >= 0;
}

int argumento_tempo_ms(const char *texto, long *milissegundos) {
    char *final;
    long valor = strtol(texto, &final, 10);
    if (final == texto || valor < 0 || valor > 1000000L)
        return reg_erro("Tempo inválido: '%s' (use, por exemplo, 8ms)", texto);
    if (*final != '\0' && strcmp(final, "ms") != 0)
        return reg_erro("Unidade de tempo inválida em '%s' (use ms)", texto);
    *milissegundos = valor;
    return 0;
}