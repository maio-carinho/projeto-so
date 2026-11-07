#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "disco.h"
#include "escalonador.h"
#include "raid.h"
#include "ssd.h"
#include "es.h"

typedef enum { FIM_EXIBIR, FIM_PRESERVAR, FIM_SILENCIAR } TratamentoFim;

static int exibir_automatico = 0;
static TratamentoFim tratamento_fim = FIM_EXIBIR;

static int quantidade_discos(void) {
    return disco_meio() == MEIO_SSD ? 1 : raid_quantidade_discos();
}

static int interpretar_inteiro(const char *texto, int *saida) {
    char *final;
    long valor = strtol(texto, &final, 10);
    if (final == texto || *final != '\0') return reg_erro("Número inválido: '%s'", texto);
    *saida = (int)valor;
    return 0;
}

static int obter_opcao_disco(int argc, char **argv, int *disco) {
    *disco = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--disco") != 0) continue;
        int numero;
        if (i + 1 >= argc || interpretar_inteiro(argv[i + 1], &numero) != 0)
            return reg_erro("Faltou o número do disco: --disco <N>");
        if (numero < 1 || numero > quantidade_discos())
            return reg_erro("Disco %d não existe (o arranjo atual tem %d disco(s)).",
                            numero, quantidade_discos());
        *disco = numero - 1;
    }
    return 0;
}

static int exigir_hdd(void) {
    if (disco_meio() != MEIO_HDD)
        return reg_erro("O meio atual é SSD; este comando vale para discos rígidos (use 'meio hdd').");
    return 0;
}

void es_iniciar(void) {
    escalonador_limpar_filas();
    raid_zerar_contadores();
    ssd_zerar_estatisticas();
    exibir_automatico = 0;
    tratamento_fim = FIM_EXIBIR;
}

static void imprimir_pendentes(int disco) {
    int quantidade = escalonador_quantidade(disco);
    if (quantidade_discos() > 1) printf("[E/S - Disco %d] ", disco + 1);
    else printf("[E/S] ");
    printf("Requisições de leitura/escrita pendentes nas trilhas: [");
    for (int i = 0; i < quantidade; i++)
        printf("%s%d", i ? ", " : "", escalonador_requisicao(disco, i)->trilha);
    printf("]\nPosição atual da cabeça de leitura: Trilha %d\n", escalonador_cabeca(disco));
}

static void escalonar_disco(int disco, Algoritmo algoritmo) {
    imprimir_pendentes(disco);
    printf("\n");
    escalonador_executar(disco, algoritmo, 1, quantidade_discos() > 1);
}

static int total_pendentes(void) {
    int total = 0;
    for (int d = 0; d < quantidade_discos(); d++) total += escalonador_quantidade(d);
    return total;
}

static void mostrar_distribuicao(void) {
    int ha_escritas = 0;
    for (int d = 0; d < quantidade_discos() && !ha_escritas; d++)
        for (int i = 0; i < escalonador_quantidade(d); i++)
            if (escalonador_requisicao(d, i)->tipo == OP_ESCRITA) { ha_escritas = 1; break; }
    if (!ha_escritas) return;

    printf("[E/S - DISTRIBUIÇÃO EM DISCO]  Arranjo: %s\n", raid_nome(raid_nivel()));
    for (int d = 0; d < quantidade_discos(); d++) {
        for (int i = 0; i < escalonador_quantidade(d); i++) {
            const Requisicao *r = escalonador_requisicao(d, i);
            if (r->tipo != OP_ESCRITA) continue;
            printf("Disco %d: [Bloco %02d | Trilha %3d | %s (4 KB)]\n",
                   d + 1, r->bloco_fisico, r->trilha, r->rotulo);
        }
    }
    printf("\n");
}

static void mostrar_escalonamento_automatico(void) {
    if (total_pendentes() == 0) return;
    printf("\n");
    mostrar_distribuicao();
    for (int d = 0; d < quantidade_discos(); d++) {
        if (escalonador_quantidade(d) == 0) continue;
        escalonar_disco(d, escalonador_algoritmo());
        printf("\n");
    }
}

void es_fim_do_comando(void) {
    TratamentoFim tratamento = tratamento_fim;
    tratamento_fim = FIM_EXIBIR;

    if (exibir_automatico && tratamento != FIM_PRESERVAR) {
        if (disco_meio() == MEIO_SSD) {
            if (tratamento == FIM_EXIBIR) ssd_imprimir_eventos();
        } else if (tratamento == FIM_EXIBIR) {
            mostrar_escalonamento_automatico();
        } else {
            for (int d = 0; d < quantidade_discos(); d++)
                escalonador_executar(d, escalonador_algoritmo(), 0, 0);
        }
    }
    ssd_limpar_eventos();
}

static int io_fila(int argc, char **argv) {
    (void)argc; (void)argv;
    if (total_pendentes() == 0) { printf("Nenhuma requisição pendente.\n"); return 0; }
    for (int d = 0; d < quantidade_discos(); d++) {
        if (escalonador_quantidade(d) == 0) continue;
        imprimir_pendentes(d);
        for (int i = 0; i < escalonador_quantidade(d); i++) {
            const Requisicao *r = escalonador_requisicao(d, i);
            if (r->bloco_fisico >= 0)
                printf("  %2d. %-8s trilha %3d (bloco físico %02d: %s)\n", i + 1,
                       r->tipo == OP_LEITURA ? "Leitura" : "Escrita", r->trilha,
                       r->bloco_fisico, r->rotulo);
            else
                printf("  %2d. %-8s trilha %3d (%s)\n", i + 1,
                       r->tipo == OP_LEITURA ? "Leitura" : "Escrita", r->trilha, r->rotulo);
        }
    }
    return 0;
}

static int io_adicionar(int argc, char **argv) {
    int disco;
    if (obter_opcao_disco(argc, argv, &disco) != 0) return -1;

    int adicionadas = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--disco") == 0) { i++; continue; }
        int trilha;
        if (interpretar_inteiro(argv[i], &trilha) != 0) return -1;
        if (trilha < 0 || trilha >= NUM_TRILHAS)
            return reg_erro("Trilha inválida: %d (use 0 a %d)", trilha, NUM_TRILHAS - 1);
        escalonador_enfileirar(disco, trilha, OP_LEITURA, -1, "requisição manual");
        adicionadas++;
    }
    if (adicionadas == 0) return reg_erro("Uso: io add <trilha> [<trilha> ...] [--disco N]");
    printf("%d requisição(ões) enfileirada(s) no Disco %d.\n", adicionadas, disco + 1);
    return 0;
}

static int io_cabeca(int argc, char **argv) {
    int disco, trilha;
    if (argc < 1 || interpretar_inteiro(argv[0], &trilha) != 0)
        return reg_erro("Uso: io cabeca <trilha> [--disco N]");
    if (obter_opcao_disco(argc, argv, &disco) != 0) return -1;
    if (escalonador_definir_cabeca(disco, trilha) != 0) return -1;
    printf("Cabeça do Disco %d posicionada na trilha %d.\n", disco + 1, trilha);
    return 0;
}

static int io_algoritmo(int argc, char **argv) {
    Algoritmo algoritmo;
    if (argc < 1) return reg_erro("Uso: io algoritmo <fcfs|sstf|scan|cscan> [sobe|desce]");
    if (escalonador_algoritmo_por_nome(argv[0], &algoritmo) != 0) return -1;
    if (argc >= 2) {
        if (strcmp(argv[1], "sobe") == 0) escalonador_definir_direcao(1);
        else if (strcmp(argv[1], "desce") == 0) escalonador_definir_direcao(0);
        else return reg_erro("Direção inválida: '%s' (use sobe ou desce)", argv[1]);
    }
    escalonador_definir_algoritmo(algoritmo);
    printf("Algoritmo ativo: %s", escalonador_nome_completo(algoritmo));
    if (algoritmo == ALG_SCAN || algoritmo == ALG_CSCAN)
        printf(" | direção: %s", escalonador_direcao_sobe() ? "sobe (para trilhas maiores)"
                                                            : "desce (para trilhas menores)");
    printf("\n");
    return 0;
}

static int io_escalonar(int argc, char **argv) {
    Algoritmo algoritmo = escalonador_algoritmo();
    if (argc >= 1 && escalonador_algoritmo_por_nome(argv[0], &algoritmo) != 0) return -1;
    if (total_pendentes() == 0) { printf("Nenhuma requisição pendente.\n"); return 0; }

    int primeiro = 1;
    for (int d = 0; d < quantidade_discos(); d++) {
        if (escalonador_quantidade(d) == 0) continue;
        if (!primeiro) printf("\n");
        primeiro = 0;
        escalonar_disco(d, algoritmo);
    }
    return 0;
}

static int io_comparar(int argc, char **argv) {
    (void)argc; (void)argv;
    if (total_pendentes() == 0) { printf("Nenhuma requisição pendente.\n"); return 0; }
    for (int d = 0; d < quantidade_discos(); d++) {
        if (escalonador_quantidade(d) == 0) continue;
        imprimir_pendentes(d);
        printf("\n--- COMPARAÇÃO DOS ALGORITMOS ---\n");
        long melhor = -1;
        Algoritmo melhor_algoritmo = ALG_FCFS;
        for (int a = ALG_FCFS; a <= ALG_CSCAN; a++) {
            long total = escalonador_simular(d, (Algoritmo)a);
            printf("%-7s: %4ld trilhas", escalonador_nome((Algoritmo)a), total);
            if (a == ALG_SCAN || a == ALG_CSCAN)
                printf("  (direção: %s)", escalonador_direcao_sobe() ? "sobe" : "desce");
            printf("\n");
            if (melhor < 0 || total < melhor) { melhor = total; melhor_algoritmo = (Algoritmo)a; }
        }
        printf("Menor deslocamento: %s (%ld trilhas)\n", escalonador_nome(melhor_algoritmo), melhor);
    }
    return 0;
}

static int io_status(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("Escalonamento automático após cada comando: %s\n", exibir_automatico ? "LIGADO" : "DESLIGADO");
    printf("Algoritmo ativo: %s", escalonador_nome_completo(escalonador_algoritmo()));
    printf(" | direção do SCAN/C-SCAN: %s\n", escalonador_direcao_sobe() ? "sobe" : "desce");
    if (disco_meio() == MEIO_SSD) {
        printf("Meio atual: SSD (sem cabeça nem trilhas). Veja 'ssd status'.\n");
        return 0;
    }
    for (int d = 0; d < quantidade_discos(); d++)
        printf("Disco %d: cabeça na trilha %3d | pendentes: %3d | atendidas: %4ld | deslocamento acumulado: %5ld trilhas\n",
               d + 1, escalonador_cabeca(d), escalonador_quantidade(d),
               escalonador_total_atendidas(d), escalonador_total_movimento(d));
    return 0;
}

int cmd_io(int argc, char **argv) {
    tratamento_fim = FIM_PRESERVAR;
    if (argc < 1)
        return reg_erro("Uso: io <auto|algoritmo|fila|add|cabeca|escalonar|comparar|status> ...");

    const char *sub = argv[0];
    if (strcmp(sub, "auto") == 0) {
        if (argc < 2 || (strcmp(argv[1], "on") != 0 && strcmp(argv[1], "off") != 0))
            return reg_erro("Uso: io auto <on|off>");
        exibir_automatico = strcmp(argv[1], "on") == 0;
        printf("Escalonamento automático após cada comando: %s\n", exibir_automatico ? "LIGADO" : "DESLIGADO");
        return 0;
    }
    if (strcmp(sub, "algoritmo") == 0) return io_algoritmo(argc - 1, argv + 1);
    if (strcmp(sub, "fila") == 0) return io_fila(argc - 1, argv + 1);
    if (strcmp(sub, "add") == 0) return io_adicionar(argc - 1, argv + 1);
    if (strcmp(sub, "cabeca") == 0) return io_cabeca(argc - 1, argv + 1);
    if (strcmp(sub, "escalonar") == 0) return io_escalonar(argc - 1, argv + 1);
    if (strcmp(sub, "comparar") == 0) return io_comparar(argc - 1, argv + 1);
    if (strcmp(sub, "status") == 0) return io_status(argc - 1, argv + 1);
    return reg_erro("Subcomando desconhecido: '%s'", sub);
}

static int numero_do_disco(const char *texto, int *disco) {
    int numero;
    if (interpretar_inteiro(texto, &numero) != 0) return -1;
    if (numero < 1 || numero > raid_quantidade_discos())
        return reg_erro("Disco %d não existe (o arranjo atual tem %d disco(s)).",
                        numero, raid_quantidade_discos());
    *disco = numero - 1;
    return 0;
}

int cmd_raid(int argc, char **argv) {
    tratamento_fim = FIM_SILENCIAR;
    if (argc < 1 || strcmp(argv[0], "status") == 0) {
        if (exigir_hdd() != 0) return -1;
        disco_imprimir_raid();
        return 0;
    }

    const char *sub = argv[0];
    int disco;

    if (strcmp(sub, "falhar") == 0) {
        if (exigir_hdd() != 0) return -1;
        if (argc < 2) return reg_erro("Uso: raid falhar <disco>");
        if (numero_do_disco(argv[1], &disco) != 0 || raid_falhar_disco(disco) != 0) return -1;
        printf("Falha forçada no Disco %d! O conteúdo dele foi perdido.\n", disco + 1);
        if (raid_nivel() == RAID_NENHUM || raid_nivel() == RAID_0)
            printf("Este nível não tem redundância: os dados que estavam nele estão perdidos (reinicie o simulador para continuar).\n");
        else
            printf("As leituras continuam funcionando em modo degradado (recalculadas por XOR/espelho).\n");
        return 0;
    }
    if (strcmp(sub, "reconstruir") == 0) {
        if (exigir_hdd() != 0) return -1;
        if (argc < 2) return reg_erro("Uso: raid reconstruir <disco>");
        int blocos;
        if (numero_do_disco(argv[1], &disco) != 0 || raid_reconstruir_disco(disco, &blocos) != 0) return -1;
        printf("Disco %d substituído e reconstruído: %d blocos físicos regravados.\n", disco + 1, blocos);
        return 0;
    }
    if (strcmp(sub, "testar") == 0) {
        if (argc < 2) return reg_erro("Uso: raid testar <disco>");
        if (numero_do_disco(argv[1], &disco) != 0) return -1;
        return disco_testar_falha_raid(disco);
    }

    NivelRaid nivel;
    if (raid_nivel_por_nome(sub, &nivel) != 0) return -1;
    if (disco_definir_raid(nivel) != 0) return -1;
    printf("Arranjo ativo: %s (%d disco(s) físico(s), blocos de %d KB)\n",
           raid_nome(nivel), raid_quantidade_discos(), TAM_BLOCO / 1024);
    return 0;
}

static int ssd_estresse(int argc, char **argv) {
    int vezes;
    if (argc < 1 || interpretar_inteiro(argv[0], &vezes) != 0 || vezes < 1 || vezes > 100000)
        return reg_erro("Uso: ssd estresse <quantidade de regravações, 1 a 100000>");

    unsigned char bloco[TAM_BLOCO];
    if (disco_ler_bloco(0, bloco) != 0) return -1;
    for (int i = 0; i < vezes; i++)
        if (disco_gravar_bloco(0, bloco) != 0) return -1;

    ssd_limpar_eventos();
    printf("Bloco lógico 00 regravado %d vez(es).\n\n", vezes);
    ssd_imprimir_estado();
    return 0;
}

int cmd_ssd(int argc, char **argv) {
    tratamento_fim = FIM_SILENCIAR;
    if (argc < 1) return reg_erro("Uso: ssd <status|wear|estresse> ...");
    const char *sub = argv[0];

    if (strcmp(sub, "wear") == 0) {
        if (argc < 2 || (strcmp(argv[1], "on") != 0 && strcmp(argv[1], "off") != 0))
            return reg_erro("Uso: ssd wear <on|off>");
        ssd_definir_nivelamento(strcmp(argv[1], "on") == 0);
        printf("Nivelamento de desgaste (wear leveling): %s\n",
               ssd_nivelamento_ligado() ? "LIGADO" : "DESLIGADO");
        return 0;
    }
    if (disco_meio() != MEIO_SSD)
        return reg_erro("O meio atual é HDD; use 'meio ssd' primeiro.");
    if (strcmp(sub, "status") == 0) { ssd_imprimir_estado(); return 0; }
    if (strcmp(sub, "estresse") == 0) return ssd_estresse(argc - 1, argv + 1);
    return reg_erro("Subcomando desconhecido: '%s'", sub);
}

int cmd_meio(int argc, char **argv) {
    tratamento_fim = FIM_SILENCIAR;
    if (argc >= 1) {
        Meio novo;
        if (strcmp(argv[0], "hdd") == 0) novo = MEIO_HDD;
        else if (strcmp(argv[0], "ssd") == 0) novo = MEIO_SSD;
        else return reg_erro("Meio desconhecido: '%s' (use hdd ou ssd)", argv[0]);
        if (disco_definir_meio(novo) != 0) return -1;
    }
    if (disco_meio() == MEIO_SSD)
        printf("Meio de armazenamento: SSD (%d blocos de apagamento x %d páginas)\n",
               SSD_BLOCOS, SSD_PAGINAS_POR_BLOCO);
    else
        printf("Meio de armazenamento: HDD (%s, %d disco(s) físico(s))\n",
               raid_nome(raid_nivel()), raid_quantidade_discos());
    return 0;
}