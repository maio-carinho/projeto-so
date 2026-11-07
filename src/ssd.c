#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "ssd.h"

typedef enum { PAGINA_LIVRE, PAGINA_VALIDA, PAGINA_INVALIDA } EstadoPagina;

#define MAX_EVENTOS        64
#define LIMITE_DESGASTE     2   

static unsigned char dados_fisicos[SSD_TOTAL_PAGINAS][TAM_BLOCO];
static EstadoPagina estado[SSD_TOTAL_PAGINAS];
static int logica_da_fisica[SSD_TOTAL_PAGINAS];
static int fisica_da_logica[TOTAL_BLOCOS];         
static int apagamentos[SSD_BLOCOS];

static long escritas_logicas, escritas_fisicas, leituras, total_apagamentos;
static int nivelamento = 1;

static char eventos[MAX_EVENTOS][100];
static int quantidade_eventos, eventos_omitidos;

static void registrar_evento(const char *formato, ...) {
    if (quantidade_eventos >= MAX_EVENTOS) { eventos_omitidos++; return; }
    va_list args;
    va_start(args, formato);
    vsnprintf(eventos[quantidade_eventos++], sizeof eventos[0], formato, args);
    va_end(args);
}

void ssd_limpar_eventos(void) {
    quantidade_eventos = 0;
    eventos_omitidos = 0;
}

void ssd_zerar_estatisticas(void) {
    escritas_logicas = escritas_fisicas = leituras = 0;
    ssd_limpar_eventos();
}

void ssd_init(void) {
    memset(dados_fisicos, 0, sizeof dados_fisicos);
    memset(estado, 0, sizeof estado);
    memset(logica_da_fisica, 0, sizeof logica_da_fisica);
    memset(apagamentos, 0, sizeof apagamentos);
    for (int i = 0; i < TOTAL_BLOCOS; i++) fisica_da_logica[i] = -1;
    total_apagamentos = 0;
    ssd_zerar_estatisticas();
}

void ssd_definir_nivelamento(int ligado) { nivelamento = ligado; }
int ssd_nivelamento_ligado(void) { return nivelamento; }

static int bloco_da_pagina(int pagina) { return pagina / SSD_PAGINAS_POR_BLOCO; }

static int contar(int bloco, EstadoPagina procurado) {
    int total = 0;
    for (int i = 0; i < SSD_PAGINAS_POR_BLOCO; i++)
        if (estado[bloco * SSD_PAGINAS_POR_BLOCO + i] == procurado) total++;
    return total;
}

static int paginas_livres(void) {
    int total = 0;
    for (int b = 0; b < SSD_BLOCOS; b++) total += contar(b, PAGINA_LIVRE);
    return total;
}

static int escolher_pagina_livre(int excluido, int preferir_gasto) {
    int melhor_bloco = -1;
    for (int b = 0; b < SSD_BLOCOS; b++) {
        if (b == excluido || contar(b, PAGINA_LIVRE) == 0) continue;
        if (!nivelamento) { melhor_bloco = b; break; }
        if (melhor_bloco < 0 ||
            (!preferir_gasto && apagamentos[b] < apagamentos[melhor_bloco]) ||
            (preferir_gasto && apagamentos[b] > apagamentos[melhor_bloco]))
            melhor_bloco = b;
    }
    if (melhor_bloco < 0) return -1;
    for (int i = 0; i < SSD_PAGINAS_POR_BLOCO; i++) {
        int pagina = melhor_bloco * SSD_PAGINAS_POR_BLOCO + i;
        if (estado[pagina] == PAGINA_LIVRE) return pagina;
    }
    return -1;
}

static void copiar_pagina_valida(int origem, int destino) {
    int logica = logica_da_fisica[origem];
    memcpy(dados_fisicos[destino], dados_fisicos[origem], TAM_BLOCO);
    estado[destino] = PAGINA_VALIDA;
    logica_da_fisica[destino] = logica;
    fisica_da_logica[logica] = destino;
    estado[origem] = PAGINA_INVALIDA;
    escritas_fisicas++;
}

static void apagar_bloco(int bloco) {
    for (int i = 0; i < SSD_PAGINAS_POR_BLOCO; i++) {
        int pagina = bloco * SSD_PAGINAS_POR_BLOCO + i;
        estado[pagina] = PAGINA_LIVRE;
        memset(dados_fisicos[pagina], 0, TAM_BLOCO);
    }
    apagamentos[bloco]++;
    total_apagamentos++;
}

static int coletar_lixo(void) {
    int menor = apagamentos[0], maior = apagamentos[0];
    for (int b = 1; b < SSD_BLOCOS; b++) {
        if (apagamentos[b] < menor) menor = apagamentos[b];
        if (apagamentos[b] > maior) maior = apagamentos[b];
    }

    int vitima = -1, estatico = 0;

    if (nivelamento && maior - menor >= LIMITE_DESGASTE) {
        for (int b = 0; b < SSD_BLOCOS; b++) {
            if (apagamentos[b] != menor || contar(b, PAGINA_VALIDA) == 0) continue;
            int livres_fora = paginas_livres() - contar(b, PAGINA_LIVRE);
            if (livres_fora >= contar(b, PAGINA_VALIDA)) { vitima = b; estatico = 1; break; }
        }
    }

    if (vitima < 0) {
        int mais_invalidas = 0;
        for (int b = 0; b < SSD_BLOCOS; b++) {
            int invalidas = contar(b, PAGINA_INVALIDA);
            if (invalidas > mais_invalidas ||
                (invalidas == mais_invalidas && invalidas > 0 && vitima >= 0 &&
                 apagamentos[b] < apagamentos[vitima])) {
                mais_invalidas = invalidas;
                vitima = b;
            }
        }
        if (vitima < 0) return -1;
    }

    int movidas = 0;
    for (int i = 0; i < SSD_PAGINAS_POR_BLOCO; i++) {
        int origem = vitima * SSD_PAGINAS_POR_BLOCO + i;
        if (estado[origem] != PAGINA_VALIDA) continue;
        int destino = escolher_pagina_livre(vitima, estatico);
        if (destino < 0) return -1;
        copiar_pagina_valida(origem, destino);
        movidas++;
    }
    apagar_bloco(vitima);
    registrar_evento("%s: bloco %d apagado (%d página(s) válida(s) movida(s); apagamentos do bloco: %d)",
                     estatico ? "Nivelamento estático" : "Coleta de lixo", vitima, movidas,
                     apagamentos[vitima]);
    return 0;
}

int ssd_ler_pagina(int pagina_logica, unsigned char *dados) {
    if (pagina_logica < 0 || pagina_logica >= TOTAL_BLOCOS)
        return reg_erro("Página lógica inválida: %d", pagina_logica);
    int fisica = fisica_da_logica[pagina_logica];
    if (fisica < 0) {
        memset(dados, 0, TAM_BLOCO);
        registrar_evento("Leitura   da página lógica %02d -> sem dados gravados (zeros)", pagina_logica);
    } else {
        memcpy(dados, dados_fisicos[fisica], TAM_BLOCO);
        registrar_evento("Leitura   da página lógica %02d -> página física %02d (bloco de apagamento %d)",
                         pagina_logica, fisica, bloco_da_pagina(fisica));
    }
    leituras++;
    return 0;
}

int ssd_gravar_pagina(int pagina_logica, const unsigned char *dados) {
    if (pagina_logica < 0 || pagina_logica >= TOTAL_BLOCOS)
        return reg_erro("Página lógica inválida: %d", pagina_logica);

    if (paginas_livres() <= 2 * SSD_PAGINAS_POR_BLOCO) coletar_lixo();

    int pagina = escolher_pagina_livre(-1, 0);
    if (pagina < 0 && coletar_lixo() == 0) pagina = escolher_pagina_livre(-1, 0);
    if (pagina < 0) return reg_erro("SSD sem páginas livres.");

    int antiga = fisica_da_logica[pagina_logica];
    if (antiga >= 0) estado[antiga] = PAGINA_INVALIDA;     

    memcpy(dados_fisicos[pagina], dados, TAM_BLOCO);
    estado[pagina] = PAGINA_VALIDA;
    logica_da_fisica[pagina] = pagina_logica;
    fisica_da_logica[pagina_logica] = pagina;
    escritas_logicas++;
    escritas_fisicas++;

    if (antiga >= 0)
        registrar_evento("Gravação da página lógica %02d -> página física %02d (bloco %d); página %02d ficou inválida",
                         pagina_logica, pagina, bloco_da_pagina(pagina), antiga);
    else
        registrar_evento("Gravação da página lógica %02d -> página física %02d (bloco de apagamento %d)",
                         pagina_logica, pagina, bloco_da_pagina(pagina));
    return 0;
}

void ssd_descartar_pagina(int pagina_logica) {
    if (pagina_logica < 0 || pagina_logica >= TOTAL_BLOCOS) return;
    int fisica = fisica_da_logica[pagina_logica];
    if (fisica < 0) return;
    estado[fisica] = PAGINA_INVALIDA;
    fisica_da_logica[pagina_logica] = -1;
    registrar_evento("TRIM     da página lógica %02d -> página física %02d marcada como inválida",
                     pagina_logica, fisica);
}

void ssd_imprimir_eventos(void) {
    if (quantidade_eventos == 0) return;
    printf("\n[E/S - SSD] Operações por página (sem braço mecânico, sem escalonamento de trilhas)\n");
    for (int i = 0; i < quantidade_eventos; i++) printf("  %s\n", eventos[i]);
    if (eventos_omitidos > 0) printf("  ... e mais %d operação(ões)\n", eventos_omitidos);
}

void ssd_imprimir_estado(void) {
    printf("SSD: %d blocos de apagamento x %d páginas de %d KB (%d KB físicos, %d KB lógicos)\n",
           SSD_BLOCOS, SSD_PAGINAS_POR_BLOCO, TAM_BLOCO / 1024,
           SSD_TOTAL_PAGINAS * TAM_BLOCO / 1024, TOTAL_BLOCOS * TAM_BLOCO / 1024);
    printf("Nivelamento de desgaste (wear leveling): %s\n", nivelamento ? "LIGADO" : "DESLIGADO");

    printf("\n[MAPA DAS PÁGINAS]  (. = livre, V = válida, x = inválida)\n");
    int menor = apagamentos[0], maior = apagamentos[0], maximo_barra = 1;
    for (int b = 0; b < SSD_BLOCOS; b++) {
        if (apagamentos[b] < menor) menor = apagamentos[b];
        if (apagamentos[b] > maior) maior = apagamentos[b];
    }
    if (maior > maximo_barra) maximo_barra = maior;

    for (int b = 0; b < SSD_BLOCOS; b++) {
        printf("Bloco %d  ", b);
        for (int i = 0; i < SSD_PAGINAS_POR_BLOCO; i++) {
            EstadoPagina e = estado[b * SSD_PAGINAS_POR_BLOCO + i];
            printf("%c ", e == PAGINA_LIVRE ? '.' : e == PAGINA_VALIDA ? 'V' : 'x');
        }
        int tamanho_barra = apagamentos[b] * 30 / maximo_barra;
        printf(" | apagamentos: %4d ", apagamentos[b]);
        for (int k = 0; k < tamanho_barra; k++) putchar('#');
        printf("\n");
    }

    double media = (double)total_apagamentos / SSD_BLOCOS;
    printf("\n--- DESGASTE ---\n");
    printf("Apagamentos: total %ld | mínimo %d | máximo %d | média %.1f | diferença máx-mín %d\n",
           total_apagamentos, menor, maior, media, maior - menor);
    printf("Escritas lógicas: %ld | escritas físicas: %ld | leituras: %ld\n",
           escritas_logicas, escritas_fisicas, leituras);
    if (escritas_logicas > 0)
        printf("Amplificação de escrita: %.2f (escritas físicas / escritas lógicas)\n",
               (double)escritas_fisicas / (double)escritas_logicas);
}