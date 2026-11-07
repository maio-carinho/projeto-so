#ifndef SSD_H
#define SSD_H

#include "utils.h"

#define SSD_PAGINAS_POR_BLOCO 4
#define SSD_BLOCOS            8
#define SSD_TOTAL_PAGINAS     (SSD_PAGINAS_POR_BLOCO * SSD_BLOCOS)

void ssd_init(void);

int ssd_ler_pagina(int pagina_logica, unsigned char *dados);
int ssd_gravar_pagina(int pagina_logica, const unsigned char *dados);

void ssd_descartar_pagina(int pagina_logica);

void ssd_definir_nivelamento(int ligado);
int ssd_nivelamento_ligado(void);

void ssd_zerar_estatisticas(void);
void ssd_limpar_eventos(void);
void ssd_imprimir_eventos(void);
void ssd_imprimir_estado(void);

#endif