#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "raid.h"
#include "escalonador.h"

typedef struct {
    const char *nome;
    int discos;
    int discos_de_dados;    
    int tolerancia;         
} Configuracao;

static const Configuracao configuracoes[] = {
    [RAID_NENHUM] = { "Sem RAID", 1, 1, 0 },
    [RAID_0]      = { "RAID 0", 2, 2, 0 },
    [RAID_1]      = { "RAID 1", 2, 1, 1 },
    [RAID_4]      = { "RAID 4", 3, 2, 1 },
    [RAID_5]      = { "RAID 5", 3, 2, 1 },
};

static NivelRaid nivel = RAID_NENHUM;
static unsigned char conteudo[MAX_DISCOS][TOTAL_BLOCOS][TAM_BLOCO];
static int falho[MAX_DISCOS];
static long leituras_disco[MAX_DISCOS];
static long escritas_disco[MAX_DISCOS];

int raid_quantidade_discos(void) { return configuracoes[nivel].discos; }
NivelRaid raid_nivel(void) { return nivel; }

int raid_blocos_por_disco(void) {
    return TOTAL_BLOCOS / configuracoes[nivel].discos_de_dados;
}

const char *raid_nome(NivelRaid n) { return configuracoes[n].nome; }

int raid_nivel_por_nome(const char *texto, NivelRaid *saida) {
    char minusculo[16];
    size_t i = 0;
    for (; texto[i] && i < sizeof minusculo - 1; i++)
        minusculo[i] = (char)tolower((unsigned char)texto[i]);
    minusculo[i] = '\0';

    if (strcmp(minusculo, "nenhum") == 0 || strcmp(minusculo, "off") == 0) *saida = RAID_NENHUM;
    else if (strcmp(minusculo, "0") == 0 || strcmp(minusculo, "raid0") == 0) *saida = RAID_0;
    else if (strcmp(minusculo, "1") == 0 || strcmp(minusculo, "raid1") == 0) *saida = RAID_1;
    else if (strcmp(minusculo, "4") == 0 || strcmp(minusculo, "raid4") == 0) *saida = RAID_4;
    else if (strcmp(minusculo, "5") == 0 || strcmp(minusculo, "raid5") == 0) *saida = RAID_5;
    else return reg_erro("Nível desconhecido: '%s' (use nenhum, 0, 1, 4 ou 5)", texto);
    return 0;
}

static int trilha_do_bloco(int bloco_fisico) {
    return bloco_fisico * NUM_TRILHAS / raid_blocos_por_disco();
}

static int disco_da_paridade(int faixa) {
    int discos = configuracoes[nivel].discos;
    if (nivel == RAID_4) return discos - 1;
    return (discos - 1) - (faixa % discos);        
}

typedef struct {
    int disco;              
    int bloco;              
    int faixa;              
    int disco_paridade;     
} Local;

static Local localizar(int bloco_logico) {
    Local local = { 0, bloco_logico, 0, -1 };
    if (nivel == RAID_0) {
        local.disco = bloco_logico % 2;
        local.bloco = bloco_logico / 2;
        local.faixa = local.bloco;
    } else if (nivel == RAID_4 || nivel == RAID_5) {
        int dados_por_faixa = configuracoes[nivel].discos_de_dados;
        int indice = bloco_logico % dados_por_faixa;
        local.faixa = bloco_logico / dados_por_faixa;
        local.bloco = local.faixa;
        local.disco_paridade = disco_da_paridade(local.faixa);
        int contador = 0, disco = 0;
        for (; disco < configuracoes[nivel].discos; disco++) {
            if (disco == local.disco_paridade) continue;
            if (contador == indice) break;
            contador++;
        }
        local.disco = disco;
    } else {
        local.faixa = bloco_logico;
    }
    return local;
}

static void ler_fisico(int disco, int bloco, unsigned char *destino, const char *rotulo) {
    memcpy(destino, conteudo[disco][bloco], TAM_BLOCO);
    leituras_disco[disco]++;
    escalonador_enfileirar(disco, trilha_do_bloco(bloco), OP_LEITURA, bloco, rotulo);
}

static void gravar_fisico(int disco, int bloco, const unsigned char *origem, const char *rotulo) {
    memcpy(conteudo[disco][bloco], origem, TAM_BLOCO);
    escritas_disco[disco]++;
    escalonador_enfileirar(disco, trilha_do_bloco(bloco), OP_ESCRITA, bloco, rotulo);
}

static void xor_bloco(unsigned char *acumulado, const unsigned char *outro) {
    for (int i = 0; i < TAM_BLOCO; i++) acumulado[i] ^= outro[i];
}

static int discos_falhos(void) {
    int total = 0;
    for (int d = 0; d < configuracoes[nivel].discos; d++) total += falho[d];
    return total;
}

static int reconstruir_por_xor(int disco_perdido, int bloco, unsigned char *saida, const char *rotulo) {
    unsigned char temporario[TAM_BLOCO];
    memset(saida, 0, TAM_BLOCO);
    for (int d = 0; d < configuracoes[nivel].discos; d++) {
        if (d == disco_perdido) continue;
        if (falho[d]) return reg_erro("Mais de um disco falhou: não é possível reconstruir.");
        ler_fisico(d, bloco, temporario, rotulo);
        xor_bloco(saida, temporario);
    }
    return 0;
}

void raid_init(void) {
    raid_reiniciar(RAID_NENHUM);
}

void raid_reiniciar(NivelRaid novo_nivel) {
    nivel = novo_nivel;
    memset(conteudo, 0, sizeof conteudo);
    memset(falho, 0, sizeof falho);
    memset(leituras_disco, 0, sizeof leituras_disco);
    memset(escritas_disco, 0, sizeof escritas_disco);
    escalonador_reiniciar_discos();     
}

int raid_ler_bloco(int bloco_logico, unsigned char *dados) {
    if (bloco_logico < 0 || bloco_logico >= TOTAL_BLOCOS)
        return reg_erro("Bloco inválido: %d", bloco_logico);

    Local local = localizar(bloco_logico);
    char rotulo[40];
    snprintf(rotulo, sizeof rotulo, "dado L%02d", bloco_logico);

    switch (nivel) {
    case RAID_NENHUM:
    case RAID_0:
        if (falho[local.disco])
            return reg_erro("O Disco %d falhou e este nível não tem redundância: dados perdidos.",
                            local.disco + 1);
        ler_fisico(local.disco, local.bloco, dados, rotulo);
        return 0;

    case RAID_1: {
        int preferido = bloco_logico % 2;          
        int escolhido = !falho[preferido] ? preferido : 1 - preferido;
        if (falho[escolhido]) return reg_erro("Os dois discos do espelho falharam: dados perdidos.");
        ler_fisico(escolhido, local.bloco, dados, rotulo);
        return 0;
    }

    default:    
        if (!falho[local.disco]) {
            ler_fisico(local.disco, local.bloco, dados, rotulo);
            return 0;
        }
        snprintf(rotulo, sizeof rotulo, "recalculo L%02d", bloco_logico);
        return reconstruir_por_xor(local.disco, local.bloco, dados, rotulo);
    }
}

int raid_gravar_bloco(int bloco_logico, const unsigned char *dados) {
    if (bloco_logico < 0 || bloco_logico >= TOTAL_BLOCOS)
        return reg_erro("Bloco inválido: %d", bloco_logico);

    Local local = localizar(bloco_logico);
    char rotulo[40];
    snprintf(rotulo, sizeof rotulo, "dado L%02d", bloco_logico);

    switch (nivel) {
    case RAID_NENHUM:
    case RAID_0:
        if (falho[local.disco])
            return reg_erro("O Disco %d falhou e este nível não tem redundância: gravação impossível.",
                            local.disco + 1);
        gravar_fisico(local.disco, local.bloco, dados, rotulo);
        return 0;

    case RAID_1:
        if (falho[0] && falho[1]) return reg_erro("Os dois discos do espelho falharam.");
        for (int d = 0; d < 2; d++) {
            if (falho[d]) continue;
            snprintf(rotulo, sizeof rotulo, "%s L%02d", d == 0 ? "dado" : "espelho", bloco_logico);
            gravar_fisico(d, local.bloco, dados, rotulo);
        }
        return 0;

    default: {  
        int paridade = local.disco_paridade;
        if (falho[local.disco] && falho[paridade])
            return reg_erro("Mais de um disco falhou: gravação impossível.");

        char rotulo_paridade[40];
        snprintf(rotulo_paridade, sizeof rotulo_paridade, "paridade faixa %d", local.faixa);

        if (falho[paridade]) {
            gravar_fisico(local.disco, local.bloco, dados, rotulo);   /* sem paridade para atualizar */
            return 0;
        }

        unsigned char nova_paridade[TAM_BLOCO];
        if (!falho[local.disco]) {
            unsigned char antigo[TAM_BLOCO];
            char rotulo_antigo[40];
            snprintf(rotulo_antigo, sizeof rotulo_antigo, "dado antigo L%02d", bloco_logico);
            ler_fisico(local.disco, local.bloco, antigo, rotulo_antigo);
            ler_fisico(paridade, local.bloco, nova_paridade, "paridade antiga");
            for (int i = 0; i < TAM_BLOCO; i++) nova_paridade[i] ^= antigo[i] ^ dados[i];
            gravar_fisico(local.disco, local.bloco, dados, rotulo);
        } else {
            memcpy(nova_paridade, dados, TAM_BLOCO);
            unsigned char temporario[TAM_BLOCO];
            for (int d = 0; d < configuracoes[nivel].discos; d++) {
                if (d == local.disco || d == paridade) continue;
                if (falho[d]) return reg_erro("Mais de um disco falhou: gravação impossível.");
                ler_fisico(d, local.bloco, temporario, "dado da faixa");
                xor_bloco(nova_paridade, temporario);
            }
        }
        gravar_fisico(paridade, local.bloco, nova_paridade, rotulo_paridade);
        return 0;
    }
    }
}

int raid_falhar_disco(int disco) {
    if (disco < 0 || disco >= configuracoes[nivel].discos)
        return reg_erro("Disco inválido (o arranjo atual tem %d disco(s)).", configuracoes[nivel].discos);
    if (falho[disco]) return reg_erro("O Disco %d já está falho.", disco + 1);
    falho[disco] = 1;
    memset(conteudo[disco], 0, sizeof conteudo[disco]);      
    return 0;
}

int raid_reconstruir_disco(int disco, int *blocos_reconstruidos) {
    if (disco < 0 || disco >= configuracoes[nivel].discos)
        return reg_erro("Disco inválido (o arranjo atual tem %d disco(s)).", configuracoes[nivel].discos);
    if (!falho[disco]) return reg_erro("O Disco %d não está falho.", disco + 1);
    if (configuracoes[nivel].tolerancia == 0)
        return reg_erro("%s não tem redundância: os dados do disco não podem ser reconstruídos.",
                        configuracoes[nivel].nome);

    unsigned char bloco[TAM_BLOCO];
    int quantidade = raid_blocos_por_disco();
    for (int b = 0; b < quantidade; b++) {
        if (nivel == RAID_1) {
            int espelho = 1 - disco;
            if (falho[espelho]) return reg_erro("Os dois discos do espelho falharam.");
            ler_fisico(espelho, b, bloco, "cópia do espelho");
        } else if (reconstruir_por_xor(disco, b, bloco, "reconstrução") != 0) {
            return -1;
        }
        gravar_fisico(disco, b, bloco, "reconstrução");
    }
    falho[disco] = 0;
    if (blocos_reconstruidos) *blocos_reconstruidos = quantidade;
    return 0;
}

int raid_testar_falha(int disco, const int *ocupados) {
    if (disco < 0 || disco >= configuracoes[nivel].discos)
        return reg_erro("Disco inválido (o arranjo atual tem %d disco(s)).", configuracoes[nivel].discos);
    if (configuracoes[nivel].tolerancia == 0)
        return reg_erro("%s não tem redundância: o teste de falha só faz sentido em RAID 1, 4 ou 5.",
                        configuracoes[nivel].nome);
    if (discos_falhos() > 0) return reg_erro("Já existe um disco falho; reconstrua-o antes de testar.");

    static unsigned char copia[TOTAL_BLOCOS][TAM_BLOCO];
    memcpy(copia, conteudo[disco], sizeof copia);

    int registro_anterior = escalonador_ativar(0);      

    printf("\n[TESTE DE FALHA SIMULADO]\n");
    printf("* Falha forçada no Disco %d!\n", disco + 1);
    raid_falhar_disco(disco);

    int total_ok = 1;
    for (int logico = 0; logico < TOTAL_BLOCOS; logico++) {
        if (!ocupados[logico]) continue;
        Local local = localizar(logico);
        int em_disco_falho = (nivel == RAID_1) ? 1 : (local.disco == disco);
        if (!em_disco_falho) continue;

        unsigned char lido[TAM_BLOCO];
        int resultado;
        if (nivel == RAID_1) {
            resultado = raid_ler_bloco(logico, lido);
            if (resultado == 0)
                printf("* Recuperando Bloco %02d copiando do Disco %d (espelho) -> ",
                       logico, 2 - disco);
        } else {
            resultado = raid_ler_bloco(logico, lido);
            if (resultado == 0) {
                char expressao[64] = "";
                for (int d = 0; d < configuracoes[nivel].discos; d++) {
                    if (d == disco) continue;
                    size_t usado = strlen(expressao);
                    snprintf(expressao + usado, sizeof expressao - usado, "%sDisco %d",
                             usado ? " XOR " : "", d + 1);
                }
                printf("* Recuperando Bloco %02d via operação XOR: (%s) -> ", logico, expressao);
            }
        }
        int conferido = (resultado == 0) && memcmp(lido, copia[local.bloco], TAM_BLOCO) == 0;
        if (resultado == 0) printf("Reconstrução [%s]\n", conferido ? "OK" : "FALHOU");
        else printf("* Bloco %02d: %s\n", logico, reg_ultimo_erro());
        if (!conferido) total_ok = 0;
    }

    int reconstruidos = 0;
    if (raid_reconstruir_disco(disco, &reconstruidos) == 0) {
        int identico = memcmp(conteudo[disco], copia, sizeof copia) == 0;
        printf("* Disco %d substituído e reconstruído por completo (%d blocos físicos, dados e paridade): [%s]\n",
               disco + 1, reconstruidos, identico ? "OK" : "FALHOU");
        if (!identico) total_ok = 0;
    } else {
        printf("* Reconstrução do disco: %s\n", reg_ultimo_erro());
        total_ok = 0;
    }

    escalonador_ativar(registro_anterior);
    return total_ok ? 0 : reg_erro("O teste de falha encontrou divergências.");
}

void raid_zerar_contadores(void) {
    memset(leituras_disco, 0, sizeof leituras_disco);
    memset(escritas_disco, 0, sizeof escritas_disco);
}

static void descrever_bloco(int disco, int bloco, const int *ocupados, char *saida, size_t capacidade) {
    int logico = -1;
    int paridade = 0;
    switch (nivel) {
    case RAID_NENHUM: logico = bloco; break;
    case RAID_0: logico = bloco * 2 + disco; break;
    case RAID_1: logico = bloco; break;
    default: {
        int faixa = bloco;
        if (disco == disco_da_paridade(faixa)) { paridade = 1; break; }
        int indice = 0;
        for (int d = 0; d < disco; d++) if (d != disco_da_paridade(faixa)) indice++;
        logico = faixa * configuracoes[nivel].discos_de_dados + indice;
    }
    }
    if (paridade) snprintf(saida, capacidade, "P(faixa %d)", bloco);
    else snprintf(saida, capacidade, "%sL%02d%s", (nivel == RAID_1 && disco == 1) ? "=" : "",
                  logico, ocupados[logico] ? "*" : "");
}

void raid_imprimir_estado(const int *ocupados) {
    const Configuracao *config = &configuracoes[nivel];
    int blocos = raid_blocos_por_disco();

    printf("Arranjo ativo: %s (%d disco(s) físico(s), blocos de %d KB, %d bloco(s) por disco)\n",
           config->nome, config->discos, TAM_BLOCO / 1024, blocos);
    printf("Capacidade lógica: %d blocos (%d KB) | Tolerância a falhas: %d disco(s)\n",
           TOTAL_BLOCOS, TOTAL_BLOCOS * TAM_BLOCO / 1024, config->tolerancia);

    for (int d = 0; d < config->discos; d++)
        printf("Disco %d: %-5s | leituras: %3ld | escritas: %3ld | cabeça na trilha %d\n",
               d + 1, falho[d] ? "FALHO" : "OK", leituras_disco[d], escritas_disco[d],
               escalonador_cabeca(d));

    printf("\n[MAPA FÍSICO]  (L = bloco lógico, * = em uso, P = paridade");
    if (nivel == RAID_1) printf(", = = cópia do espelho");
    printf(")\nBloco Trilha ");
    for (int d = 0; d < config->discos; d++) printf(" Disco %d    ", d + 1);
    printf("\n");
    for (int b = 0; b < blocos; b++) {
        printf("%02d    %3d    ", b, trilha_do_bloco(b));
        for (int d = 0; d < config->discos; d++) {
            char texto[32];
            if (falho[d]) snprintf(texto, sizeof texto, "XXXX");
            else descrever_bloco(d, b, ocupados, texto, sizeof texto);
            printf(" %-10s ", texto);
        }
        printf("\n");
    }
}