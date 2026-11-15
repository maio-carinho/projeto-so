#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "comum/utils.h"
#include "armazenamento/disco.h"
#include "sistema_arquivos/fs.h"
#include "seguranca/seguranca.h"
#include "antivirus/antivirus.h"

#define MAX_ASSINATURAS     8
#define TAM_ASSINATURA      40

static char assinaturas[MAX_ASSINATURAS][TAM_ASSINATURA] = {
    "VIRUS_TESTE_SIMULADO",
    "MALWARE_SIM_TROJAN",
    "RANSOM_SIM_LOCKER"
};
static int n_assinaturas = 3;

static unsigned char conteudo[TOTAL_BLOCOS * TAM_BLOCO];

static int contem(const unsigned char *dados, long tamanho, const char *padrao) {
    long n = (long)strlen(padrao);
    if (n == 0 || n > tamanho) return 0;
    for (long i = 0; i + n <= tamanho; i++)
        if (memcmp(dados + i, padrao, (size_t)n) == 0) return 1;
    return 0;
}

static const char *procurar_assinatura(const InfoArquivo *arquivo) {
    unsigned char buffer[TAM_BLOCO];
    for (int k = 0; k < arquivo->n_blocos; k++) {
        if (disco_ler_bloco(arquivo->blocos[k], buffer) != 0) return NULL;
        long offset = (long)k * TAM_BLOCO;
        long ocupado = arquivo->tamanho - offset;
        if (ocupado > TAM_BLOCO) ocupado = TAM_BLOCO;
        memcpy(conteudo + offset, buffer, (size_t)ocupado);
    }
    for (int i = 0; i < n_assinaturas; i++)
        if (contem(conteudo, arquivo->tamanho, assinaturas[i])) return assinaturas[i];
    return NULL;
}

int cmd_antivirus_scan(int argc, char **argv) {
    const char *caminho = argc >= 1 ? argv[0] : "/";
    InfoArquivo arquivos[TOTAL_BLOCOS];
    int total = fs_listar_arquivos(caminho, arquivos, TOTAL_BLOCOS);
    if (total < 0) return -1;

    printf("[MÓDULO DE SEGURANÇA - VARREDURA DE INTEGRIDADE]\n");
    printf("Analisando %d arquivos em busca de assinaturas maliciosas e hashes corrompidos...\n", total);

    int com_adulteracao = 0, com_assinatura = 0;
    for (int a = 0; a < total; a++) {
        const InfoArquivo *arquivo = &arquivos[a];

        int bloco_alterado = -1, nao_verificado = -1;
        for (int k = -1; k < arquivo->n_blocos; k++) {
            int bloco = (k < 0) ? arquivo->inode : arquivo->blocos[k];
            int resultado = disco_verificar_integridade(bloco);
            if (resultado == 0 && bloco_alterado < 0) bloco_alterado = bloco;
            if (resultado < 0 && nao_verificado < 0) nao_verificado = bloco;
        }

        const char *assinatura = NULL;
        if (!arquivo->criptografado && arquivo->tamanho > 0)
            assinatura = procurar_assinatura(arquivo);

        if (bloco_alterado >= 0) {
            printf("- %s : [ALERTA] Hash do bloco difere do registro oficial! (bloco %02d)\n",
                   arquivo->caminho, bloco_alterado);
            com_adulteracao++;
        }
        if (assinatura) {
            printf("- %s : [ALERTA] Assinatura maliciosa detectada: '%s'\n", arquivo->caminho, assinatura);
            com_assinatura++;
        }
        if (nao_verificado >= 0 && bloco_alterado < 0)
            printf("- %s : [AVISO] Não foi possível verificar o bloco %02d\n", arquivo->caminho, nao_verificado);
        if (bloco_alterado < 0 && !assinatura && nao_verificado < 0)
            printf("- %s : [OK - %s]\n", arquivo->caminho,
                   arquivo->criptografado ? "Criptografado e Íntegro" : "Íntegro, sem assinaturas maliciosas");
    }

    if (com_adulteracao > 0) printf("Resultado: Possível modificação direta não autorizada no bloco do disco.\n");
    if (com_assinatura > 0) printf("Resultado: %d arquivo(s) com assinatura maliciosa.\n", com_assinatura);
    if (com_adulteracao == 0 && com_assinatura == 0) printf("Resultado: nenhuma ameaça encontrada.\n");

    seguranca_registrar("ANTIVÍRUS: varredura de '%s': %d arquivo(s), %d com bloco alterado, %d com assinatura",
                        caminho, total, com_adulteracao, com_assinatura);
    return 0;
}

int cmd_antivirus_sig(int argc, char **argv) {
    if (argc >= 1 && strcmp(argv[0], "add") != 0) return reg_erro("Uso: antivirus_sig [add <texto>]");
    if (argc >= 1) {
        if (argc < 2 || argv[1][0] == '\0') return reg_erro("Uso: antivirus_sig add <texto>");
        if (strlen(argv[1]) >= TAM_ASSINATURA) return reg_erro("Assinatura muito longa (máximo de %d caracteres).", TAM_ASSINATURA - 1);
        if (n_assinaturas >= MAX_ASSINATURAS) return reg_erro("Limite de %d assinaturas atingido.", MAX_ASSINATURAS);
        snprintf(assinaturas[n_assinaturas++], TAM_ASSINATURA, "%s", argv[1]);
        seguranca_registrar("ANTIVÍRUS: assinatura '%s' adicionada", argv[1]);
        printf("Assinatura '%s' adicionada.\n", argv[1]);
        return 0;
    }
    printf("[ASSINATURAS MALICIOSAS CONHECIDAS]\n");
    for (int i = 0; i < n_assinaturas; i++) printf("%d. %s\n", i + 1, assinaturas[i]);
    return 0;
}

int cmd_adulterar_bloco(int argc, char **argv) {
    if (argc < 1) return reg_erro("Uso: adulterar_bloco <arquivo> [--bloco N]");
    int numero = 1;
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], "--bloco") == 0) numero = atoi(argv[i + 1]);

    InfoArquivo arquivo;
    if (fs_info_arquivo(argv[0], &arquivo) != 0) return -1;
    if (arquivo.n_blocos == 0) return reg_erro("O arquivo não tem blocos de dados.");
    if (numero < 1 || numero > arquivo.n_blocos)
        return reg_erro("Bloco inválido: o arquivo tem %d bloco(s) de dados.", arquivo.n_blocos);

    int bloco = arquivo.blocos[numero - 1];
    if (disco_adulterar_bloco(bloco, 0) != 0) return -1;
    seguranca_registrar("ADULTERAÇÃO SIMULADA: bloco %02d de '%s' alterado direto no disco", bloco, arquivo.caminho);
    printf("[SIMULAÇÃO] Bloco lógico %02d de '%s' alterado diretamente no disco (sem passar pelo sistema de arquivos).\n",
           bloco, arquivo.caminho);
    return 0;
}