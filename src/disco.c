#include <string.h>
#include "disco.h"

typedef struct {
    int ocupado;                        
    unsigned char dados[TAM_BLOCO];   
} Bloco;

static Bloco blocos[TOTAL_BLOCOS];

static int existe(int bloco)
{
    return bloco >= 0 && bloco < TOTAL_BLOCOS;
}

void disco_init(void)
{
    memset(blocos, 0, sizeof blocos);
}

int disco_alocar_bloco(void)
{
    for (int i = 0; i < TOTAL_BLOCOS; i++) {
        if (!blocos[i].ocupado) {
            blocos[i].ocupado = 1;
            return i;
        }
    }
    return -1;
}

void disco_liberar_bloco(int bloco)
{
    if (!existe(bloco)) return;
    memset(&blocos[bloco], 0, sizeof blocos[bloco]);
}

int disco_livre(int bloco)
{
    return existe(bloco) && !blocos[bloco].ocupado;
}

int livres(void)
{
    int n = 0;
    for (int i = 0; i < TOTAL_BLOCOS; i++)
        if (!blocos[i].ocupado) n++;
    return n;
}

int disco_ler_bloco(int bloco, unsigned char *buf)
{
    if (!existe(bloco)) return reg_erro("Bloco inválido: %d", bloco);
    memcpy(buf, blocos[bloco].dados, TAM_BLOCO);
    return 0;
}

int disco_gravar_bloco(int bloco, const unsigned char *buf)
{
    if (!existe(bloco)) return reg_erro("Bloco inválido: %d", bloco);
    if (!blocos[bloco].ocupado)
        return reg_erro("Gravação em bloco não reservado: %d", bloco);
    memcpy(blocos[bloco].dados, buf, TAM_BLOCO);
    return 0;
}