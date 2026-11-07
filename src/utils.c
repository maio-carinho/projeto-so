#include <stdio.h>
#include <stdarg.h>
#include "utils.h"

static char mensagem_erro[256];

int reg_erro(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(mensagem_erro, sizeof mensagem_erro, fmt, args);
    va_end(args);
    return -1;
}

const char *reg_ultimo_erro(void) {
    return mensagem_erro;
}