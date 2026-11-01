#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

#define TAM_BLOCO    4096
#define TOTAL_BLOCOS 16
#define TAM_NOME     64
#define TAM_PATH     512

int reg_erro(const char *fmt, ...);

const char *reg_ultimo_erro(void);

#endif 