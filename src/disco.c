#include <stdio.h>
#include <string.h>
#include "disco.h"
#include "escalonador.h"
#include "ssd.h"

static int ocupado[TOTAL_BLOCOS];
static Meio meio = MEIO_HDD;

static int existe(int bloco) {
    return bloco >= 0 && bloco < TOTAL_BLOCOS;
}

void disco_init(void) {
    memset(ocupado, 0, sizeof ocupado);
    meio = MEIO_HDD;
    escalonador_init();
    raid_init();
    ssd_init();
}

int disco_alocar_bloco(void) {
    for (int i = 0; i < TOTAL_BLOCOS; i++) {
        if (!ocupado[i]) {
            ocupado[i] = 1;
            return i;
        }
    }
    return -1;
}

void disco_liberar_bloco(int bloco) {
    if (!existe(bloco)) return;
    ocupado[bloco] = 0;
    if (meio == MEIO_SSD) ssd_descartar_pagina(bloco);
}

int disco_livre(int bloco) {
    return existe(bloco) && !ocupado[bloco];
}

int livres(void) {
    int n = 0;
    for (int i = 0; i < TOTAL_BLOCOS; i++)
        if (!ocupado[i]) n++;
    return n;
}

static int ler_meio(int bloco, unsigned char *buf) {
    return meio == MEIO_SSD ? ssd_ler_pagina(bloco, buf) : raid_ler_bloco(bloco, buf);
}

static int gravar_meio(int bloco, const unsigned char *buf) {
    return meio == MEIO_SSD ? ssd_gravar_pagina(bloco, buf) : raid_gravar_bloco(bloco, buf);
}

int disco_ler_bloco(int bloco, unsigned char *buf) {
    if (!existe(bloco)) return reg_erro("Bloco inválido: %d", bloco);
    return ler_meio(bloco, buf);
}

int disco_gravar_bloco(int bloco, const unsigned char *buf) {
    if (!existe(bloco)) return reg_erro("Bloco inválido: %d", bloco);
    if (!ocupado[bloco])
        return reg_erro("Gravação em bloco não reservado: %d", bloco);
    return gravar_meio(bloco, buf);
}

static unsigned char copia_dos_dados[TOTAL_BLOCOS][TAM_BLOCO];

static int migrar(void (*reconfigurar)(const void *), const void *argumento) {
    int registro_anterior = escalonador_ativar(0);

    for (int i = 0; i < TOTAL_BLOCOS; i++) {
        if (ocupado[i] && ler_meio(i, copia_dos_dados[i]) != 0) {
            char motivo[256];           
            snprintf(motivo, sizeof motivo, "%s", reg_ultimo_erro());
            escalonador_ativar(registro_anterior);
            return reg_erro("Migração cancelada, nada foi alterado. Motivo: %s", motivo);
        }
    }

    reconfigurar(argumento);

    for (int i = 0; i < TOTAL_BLOCOS; i++)
        if (ocupado[i]) gravar_meio(i, copia_dos_dados[i]);

    ssd_zerar_estatisticas();           
    escalonador_ativar(registro_anterior);
    return 0;
}

static void reconfigurar_raid(const void *argumento) {
    raid_reiniciar(*(const NivelRaid *)argumento);
}

static void reconfigurar_meio(const void *argumento) {
    meio = *(const Meio *)argumento;
    if (meio == MEIO_SSD) ssd_init();
    else raid_reiniciar(raid_nivel());
}

Meio disco_meio(void) {
    return meio;
}

int disco_definir_meio(Meio novo_meio) {
    if (novo_meio == meio) return 0;
    return migrar(reconfigurar_meio, &novo_meio);
}

int disco_definir_raid(NivelRaid nivel) {
    if (meio != MEIO_HDD)
        return reg_erro("O meio atual é SSD; RAID só vale para discos rígidos (use 'meio hdd').");
    return migrar(reconfigurar_raid, &nivel);
}

void disco_imprimir_raid(void) {
    raid_imprimir_estado(ocupado);
}

int disco_testar_falha_raid(int disco) {
    if (meio != MEIO_HDD)
        return reg_erro("O meio atual é SSD; RAID só vale para discos rígidos (use 'meio hdd').");
    return raid_testar_falha(disco, ocupado);
}