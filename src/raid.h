#ifndef RAID_H
#define RAID_H

#include "utils.h"

typedef enum { RAID_NENHUM, RAID_0, RAID_1, RAID_4, RAID_5 } NivelRaid;

void raid_init(void);

void raid_reiniciar(NivelRaid nivel);

NivelRaid raid_nivel(void);
const char *raid_nome(NivelRaid nivel);
int raid_nivel_por_nome(const char *texto, NivelRaid *saida);
int raid_quantidade_discos(void);
int raid_blocos_por_disco(void);

int raid_ler_bloco(int bloco_logico, unsigned char *dados);
int raid_gravar_bloco(int bloco_logico, const unsigned char *dados);

int raid_falhar_disco(int disco);
int raid_reconstruir_disco(int disco, int *blocos_reconstruidos);

int raid_testar_falha(int disco, const int *ocupados);

void raid_imprimir_estado(const int *ocupados);

void raid_zerar_contadores(void);

#endif