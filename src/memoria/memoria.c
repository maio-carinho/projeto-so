#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comum/utils.h"
#include "seguranca/seguranca.h"
#include "sistema_arquivos/fs.h"
#include "memoria/memoria.h"
#include "memoria/paginacao.h"
#include "memoria/processo.h"
#include "memoria/reservada.h"
#include "memoria/swap.h"

#define INTERVALO_RELATORIO_PADRAO 5
#define LIMITE_TAMANHO_BYTES       (1024L * 1024L * 1024L)

static int intervalo_relatorio = INTERVALO_RELATORIO_PADRAO;
static int operacoes_de_memoria;

void memoria_iniciar(void) {
    processo_iniciar();
    reservada_iniciar();
    swap_iniciar();
    paginacao_iniciar();
    intervalo_relatorio = INTERVALO_RELATORIO_PADRAO;
    operacoes_de_memoria = 0;
}

static const char *valor_da_opcao(int argc, char **argv, const char *opcao) {
    for (int i = 0; i + 1 < argc; i++)
        if (strcmp(argv[i], opcao) == 0) return argv[i + 1];
    return NULL;
}

static int tem_opcao(int argc, char **argv, const char *opcao) {
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], opcao) == 0) return 1;
    return 0;
}

static int iguais_ignorando_maiusculas(const char *a, const char *b) {
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int interpretar_tamanho(const char *texto, long *bytes) {
    char *final;
    long valor = strtol(texto, &final, 10);
    long multiplicador = 1;

    if (final == texto || valor <= 0)
        return reg_erro("Tamanho inválido: '%s' (use, por exemplo, 512B, 10KB ou 1MB)", texto);
    if (iguais_ignorando_maiusculas(final, "KB")) multiplicador = 1024;
    else if (iguais_ignorando_maiusculas(final, "MB")) multiplicador = 1024 * 1024;
    else if (!iguais_ignorando_maiusculas(final, "B") && *final != '\0')
        return reg_erro("Unidade inválida em '%s' (use B, KB ou MB)", texto);
    if (valor > LIMITE_TAMANHO_BYTES / multiplicador) return reg_erro("Tamanho grande demais: '%s'", texto);

    *bytes = valor * multiplicador;
    return 0;
}

static int exigir_dono_ou_kernel(const Processo *processo, const char *acao) {
    if (seguranca_modo() == MODO_KERNEL || processo->dono_uid == seguranca_uid_atual()) return 0;

    const char *usuario = seguranca_nome_usuario(seguranca_uid_atual());
    const char *dono = seguranca_nome_usuario(processo->dono_uid);
    printf("[SISTEMA DE SEGURANÇA - PROTEÇÃO DE MEMÓRIA]\n"
           "Processo    : %s (PID %d) | Proprietário: %s\n"
           "Ação Pedida : %s, por '%s' (Grupo: %s)\n\n"
           "[NEGADO]: O usuário '%s' não pode %s '%s', que pertence a outro usuário.\n"
           "Tentativa não autorizada registrada nos logs do sistema.\n",
           processo->nome, processo->identificador, dono, acao, usuario,
           seguranca_nome_grupo(seguranca_gid_atual()), usuario, acao, processo->nome);
    seguranca_registrar("MEMÓRIA NEGADA: '%s' tentou '%s' no PID %d ('%s', dono '%s')",
                        usuario, acao, processo->identificador, processo->nome, dono);
    return ERRO_JA_EXIBIDO;
}

static void imprimir_relatorio_periodico(void) {
    int processos_afetados;
    long interna = paginacao_fragmentacao_interna_total(&processos_afetados);

    printf("\n[RELATÓRIO PERIÓDICO DE FRAGMENTAÇÃO - a cada %d operações de memória]\n", intervalo_relatorio);
    printf("Fragmentação Interna (páginas subutilizadas): %ld Bytes em %d processo(s)\n", interna, processos_afetados);
    if (seguranca_modo() != MODO_KERNEL) return;

    DiagnosticoReservada diagnostico;
    reservada_diagnosticar(&diagnostico);
    printf("Fragmentação Externa (Área Reservada): %s | livre: %ld KB | maior bloco contíguo: %ld KB | lacunas: %d\n",
           diagnostico.fragmentacao_externa ? "SIM" : "NÃO", diagnostico.livre_total_kb,
           diagnostico.maior_bloco_kb, diagnostico.fragmentos_livres);
}

static void contar_operacao(void) {
    operacoes_de_memoria++;
    if (intervalo_relatorio > 0 && operacoes_de_memoria % intervalo_relatorio == 0)
        imprimir_relatorio_periodico();
}

static int criar_processo_critico(const char *nome, long tamanho_bytes) {
    int negado = seguranca_exigir_kernel("create_process --type critical");
    if (negado != 0) return negado;

    long tamanho_kb = (tamanho_bytes + 1023) / 1024;        
    long inicio_kb;
    int identificador = processo_proximo_identificador();
    if (reservada_alocar(nome, identificador, tamanho_kb, &inicio_kb) != 0) return -1;

    Processo *processo = processo_criar(nome, PROCESSO_CRITICO, tamanho_kb * 1024, seguranca_uid_atual());
    processo->inicio_kb = inicio_kb;

    char de[32], ate[32];
    reservada_formatar_mb(inicio_kb, de, sizeof de);
    reservada_formatar_mb(inicio_kb + tamanho_kb, ate, sizeof ate);
    printf("[CRIAÇÃO DE PROCESSO CRÍTICO - ÁREA RESERVADA]\n");
    printf("- Alocado de forma contígua na Área Reservada: [%s até %s] (%ld KB)\n", de, ate, tamanho_kb);
    seguranca_registrar("PROCESSO CRÍTICO CRIADO: '%s' (PID %d), %ld KB em [%s até %s]",
                        nome, identificador, tamanho_kb, de, ate);
    return 0;
}

static int criar_processo_normal(const char *nome, long tamanho_bytes) {
    int paginas = (int)((tamanho_bytes + TAM_PAGINA - 1) / TAM_PAGINA);
    if (paginas > MAX_PAGINAS_POR_PROCESSO)
        return reg_erro("Processo grande demais: %d páginas (máximo de %d páginas, %d KB).",
                        paginas, MAX_PAGINAS_POR_PROCESSO, MAX_PAGINAS_POR_PROCESSO * TAM_PAGINA / 1024);
    if (!paginacao_ha_capacidade(paginas))
        return reg_erro("Memória virtual insuficiente: o swap comporta %d páginas no total e já há %d alocadas.",
                        TOTAL_POSICOES_SWAP, paginacao_paginas_alocadas());

    Processo *processo = processo_criar(nome, PROCESSO_NORMAL, tamanho_bytes, seguranca_uid_atual());
    paginacao_imprimir_criacao(processo);
    return 0;
}

int cmd_create_process(int argc, char **argv) {
    const char *nome = valor_da_opcao(argc, argv, "--name");
    const char *texto_tamanho = valor_da_opcao(argc, argv, "--size");
    const char *arquivo = valor_da_opcao(argc, argv, "--file");
    const char *texto_tipo = valor_da_opcao(argc, argv, "--type");

    if (!nome || (!texto_tamanho && !arquivo) || (texto_tamanho && arquivo))
        return reg_erro("Uso: create_process --name <nome> (--size <tam> | --file <caminho>) [--type critical|normal]");

    TipoProcesso tipo = PROCESSO_NORMAL;
    if (texto_tipo && strcmp(texto_tipo, "critical") == 0) tipo = PROCESSO_CRITICO;
    else if (texto_tipo && strcmp(texto_tipo, "normal") != 0)
        return reg_erro("Tipo inválido: '%s' (use critical ou normal)", texto_tipo);

    long tamanho_bytes;
    if (arquivo) {
        InfoArquivo info;
        if (fs_info_arquivo(arquivo, &info) != 0) return -1;
        int negado = fs_exigir_execucao(arquivo);
        if (negado != 0) return negado;
        if (info.tamanho <= 0) return reg_erro("O arquivo '%s' está vazio: não há o que carregar.", arquivo);
        tamanho_bytes = info.tamanho;
    } else if (interpretar_tamanho(texto_tamanho, &tamanho_bytes) != 0) {
        return -1;
    }

    if (processo_validar_novo(nome) != 0) return -1;
    int resultado = tipo == PROCESSO_CRITICO ? criar_processo_critico(nome, tamanho_bytes)
                                             : criar_processo_normal(nome, tamanho_bytes);
    if (resultado == 0) contar_operacao();
    return resultado;
}

int cmd_terminate_process(int argc, char **argv) {
    const char *nome = valor_da_opcao(argc, argv, "--name");
    if (!nome) return reg_erro("Uso: terminate_process --name <nome>");

    Processo *processo = processo_por_nome(nome);
    if (!processo) return reg_erro("Processo '%s' não encontrado.", nome);

    if (processo->tipo == PROCESSO_CRITICO) {
        int negado = seguranca_exigir_kernel("terminate_process (processo crítico)");
        if (negado != 0) return negado;

        long inicio_kb, tamanho_kb;
        if (reservada_liberar(processo->identificador, &inicio_kb, &tamanho_kb) != 0) return -1;
        char de[32], ate[32];
        reservada_formatar_mb(inicio_kb, de, sizeof de);
        reservada_formatar_mb(inicio_kb + tamanho_kb, ate, sizeof ate);
        printf("[ENCERRAMENTO DE PROCESSO CRÍTICO] Processo '%s' finalizado.\n", processo->nome);
        printf("Área de %ld KB no intervalo [%s a %s] liberada na Área Reservada da RAM.\n", tamanho_kb, de, ate);
        seguranca_registrar("PROCESSO CRÍTICO ENCERRADO: '%s' (PID %d)", processo->nome, processo->identificador);
    } else {
        int negado = exigir_dono_ou_kernel(processo, "encerrar o processo");
        if (negado != 0) return negado;

        int molduras, posicoes_swap;
        paginacao_liberar_processo(processo, &molduras, &posicoes_swap);
        printf("[ENCERRAMENTO DE PROCESSO] Processo '%s' (PID %d) finalizado.\n", processo->nome, processo->identificador);
        printf("Molduras liberadas na RAM Dinâmica: %d | Posições liberadas no Swap: %d\n", molduras, posicoes_swap);
    }

    processo_remover(processo);
    contar_operacao();
    return 0;
}

static int exigir_processo_paginado(const Processo *processo) {
    if (processo->tipo == PROCESSO_NORMAL) return 0;
    char de[32], ate[32];
    reservada_formatar_mb(processo->inicio_kb, de, sizeof de);
    reservada_formatar_mb(processo->inicio_kb + processo->tamanho_bytes / 1024, ate, sizeof ate);
    return reg_erro("'%s' é um processo crítico: vive na Área Reservada [%s até %s], contígua e sem paginação.",
                    processo->nome, de, ate);
}

static int interpretar_numero(const char *texto, long *valor) {
    char *final;
    *valor = strtol(texto, &final, 10);
    return final != texto && *final == '\0' && *valor >= 0;
}

static int exigir_pagina_valida(const Processo *processo, long pagina) {
    if (pagina < processo->total_paginas) return 0;
    printf("[VIOLAÇÃO DE ACESSO À MEMÓRIA]\n"
           "Processo : %s (PID %d) | Página Lógica pedida: %ld | Páginas válidas: 0 a %d\n\n"
           "[NEGADO]: Endereço fora do espaço de endereçamento do processo (falha de segmentação).\n"
           "Tentativa registrada nos logs do sistema.\n",
           processo->nome, processo->identificador, pagina, processo->total_paginas - 1);
    seguranca_registrar("VIOLAÇÃO DE MEMÓRIA: PID %d ('%s') pediu a página %ld (válidas: 0 a %d)",
                        processo->identificador, processo->nome, pagina, processo->total_paginas - 1);
    return ERRO_JA_EXIBIDO;
}

int cmd_translate_address(int argc, char **argv) {
    const char *nome = valor_da_opcao(argc, argv, "--process");
    const char *texto_pagina = valor_da_opcao(argc, argv, "--page");
    const char *texto_deslocamento = valor_da_opcao(argc, argv, "--offset");
    long pagina, deslocamento = 0;

    if (!nome || !texto_pagina || !interpretar_numero(texto_pagina, &pagina))
        return reg_erro("Uso: translate_address --process <nome> --page <N> [--offset <bytes>]");
    if (texto_deslocamento && (!interpretar_numero(texto_deslocamento, &deslocamento) || deslocamento >= TAM_PAGINA))
        return reg_erro("Deslocamento inválido: use um valor de 0 a %d.", TAM_PAGINA - 1);

    Processo *processo = processo_por_nome(nome);
    if (!processo) return reg_erro("Processo '%s' não encontrado.", nome);
    if (exigir_processo_paginado(processo) != 0) return -1;
    int negado = exigir_dono_ou_kernel(processo, "consultar a tradução de endereços do processo");
    if (negado != 0) return negado;
    negado = exigir_pagina_valida(processo, pagina);
    if (negado != 0) return negado;

    paginacao_traduzir(processo, (int)pagina, deslocamento);
    return 0;
}

static int interpretar_instrucao(const char *texto, int *escrita, long *pagina) {
    char copia[128];
    char *palavras[8];
    int quantidade = 0;

    snprintf(copia, sizeof copia, "%s", texto);
    for (char *palavra = strtok(copia, " \t"); palavra && quantidade < 8; palavra = strtok(NULL, " \t"))
        palavras[quantidade++] = palavra;

    if (quantidade < 2 || !interpretar_numero(palavras[quantidade - 1], pagina))
        return reg_erro("Instrução inválida: use \"LOAD_DATA Página <N>\" ou \"STORE_DATA Página <N>\"");
    if (iguais_ignorando_maiusculas(palavras[0], "LOAD_DATA")) *escrita = 0;
    else if (iguais_ignorando_maiusculas(palavras[0], "STORE_DATA")) *escrita = 1;
    else return reg_erro("Instrução desconhecida: '%s' (use LOAD_DATA ou STORE_DATA)", palavras[0]);
    return 0;
}

int cmd_execute_step(int argc, char **argv) {
    const char *nome = valor_da_opcao(argc, argv, "--process");
    const char *instrucao = valor_da_opcao(argc, argv, "--instruction");
    int escrita;
    long pagina;

    if (!nome || !instrucao)
        return reg_erro("Uso: execute_step --process <nome> --instruction \"LOAD_DATA Página <N>\"");
    if (interpretar_instrucao(instrucao, &escrita, &pagina) != 0) return -1;

    Processo *processo = processo_por_nome(nome);
    if (!processo) return reg_erro("Processo '%s' não encontrado.", nome);
    if (exigir_processo_paginado(processo) != 0) return -1;
    int negado = exigir_dono_ou_kernel(processo, "acessar a memória do processo");
    if (negado != 0) return negado;
    negado = exigir_pagina_valida(processo, pagina);
    if (negado != 0) return negado;

    paginacao_acessar(processo, (int)pagina, escrita);
    contar_operacao();
    return 0;
}

static void imprimir_barra(const char *rotulo, long usado, long total, const char *unidade) {
    const int largura = 20;
    int cheio = total > 0 ? (int)(usado * largura / total) : 0;
    char barra[32];
    for (int i = 0; i < largura; i++) barra[i] = i < cheio ? '#' : '-';
    barra[largura] = '\0';
    printf("%-34s: %4ld %s de %ld %s (%.1f%%) [%s]\n", rotulo, usado, unidade, total, unidade,
           total > 0 ? 100.0 * usado / total : 0.0, barra);
}

static void imprimir_status_reservada(void) {
    DiagnosticoReservada diagnostico;
    reservada_imprimir_mapa();
    reservada_diagnosticar(&diagnostico);
    printf("--- OCUPAÇÃO DA RAM CRÍTICA ---\n");
    imprimir_barra("Área Reservada (alocação contígua)", diagnostico.ocupado_kb, RESERVADA_TOTAL_KB, "KB");
}

static void imprimir_status_virtual(void) {
    paginacao_imprimir_molduras();
    printf("--- OCUPAÇÃO DA MEMÓRIA VIRTUAL ---\n");
    imprimir_barra("RAM Dinâmica (molduras)", paginacao_molduras_ocupadas(), TOTAL_MOLDURAS, "molduras");
    imprimir_barra("Swap em disco (posições)", swap_posicoes_em_uso(), TOTAL_POSICOES_SWAP, "posições");
    imprimir_barra("Páginas lógicas alocadas / swap", paginacao_paginas_alocadas(), TOTAL_POSICOES_SWAP, "páginas");
    paginacao_imprimir_fragmentacao_interna();
    paginacao_imprimir_estatisticas();
}

int cmd_memory_status(int argc, char **argv) {
    const char *nome = valor_da_opcao(argc, argv, "--process");
    if (nome) {
        Processo *processo = processo_por_nome(nome);
        if (!processo) return reg_erro("Processo '%s' não encontrado.", nome);
        if (exigir_processo_paginado(processo) != 0) return -1;
        int negado = exigir_dono_ou_kernel(processo, "consultar a tabela de páginas do processo");
        if (negado != 0) return negado;
        paginacao_imprimir_tabela(processo);
        return 0;
    }

    int quer_reservada = tem_opcao(argc, argv, "--reserved");
    int quer_virtual = tem_opcao(argc, argv, "--virtual");
    if (!quer_reservada && !quer_virtual) {
        quer_virtual = 1;
        quer_reservada = seguranca_modo() == MODO_KERNEL;
    }
    if (quer_reservada) {
        int negado = seguranca_exigir_kernel("memory_status --reserved");
        if (negado != 0) return negado;
        imprimir_status_reservada();
    }
    if (quer_reservada && quer_virtual) printf("\n");
    if (quer_virtual) imprimir_status_virtual();
    return 0;
}

int cmd_memory_compact(int argc, char **argv) {
    (void)argc; (void)argv;
    DiagnosticoReservada antes;
    reservada_diagnosticar(&antes);
    if (!antes.fragmentacao_externa) {
        printf("Nada a compactar: a Área Reservada não tem fragmentação externa.\n");
        return 0;
    }

    printf("[EXECUTANDO COMPACTAÇÃO DA ÁREA RESERVADA DA RAM]\n");
    printf("Realocando blocos contíguos para eliminar a fragmentação externa...\n");
    long kb_copiados;
    int movidos = reservada_compactar(&kb_copiados);
    printf("Compactação concluída. Estado atual da Área Reservada:\n");

    DiagnosticoReservada depois;
    reservada_diagnosticar(&depois);
    reservada_imprimir_mapa();
    printf("Resultado: Fragmentação externa eliminada. Maior bloco contíguo: %ld KB.\n", depois.maior_bloco_kb);
    printf("Custo: %d bloco(s) movido(s), %ld KB copiados dentro da RAM.\n", movidos, kb_copiados);
    seguranca_registrar("COMPACTAÇÃO DA ÁREA RESERVADA: %d bloco(s) movido(s), maior bloco livre agora %ld KB",
                        movidos, depois.maior_bloco_kb);
    contar_operacao();
    return 0;
}

int cmd_memory_algoritmo(int argc, char **argv) {
    if (argc >= 1 && paginacao_definir_algoritmo(argv[0]) != 0) return -1;
    printf("Algoritmo de substituição de páginas: %s\n", paginacao_nome_algoritmo());
    return 0;
}

int cmd_memory_relatorio(int argc, char **argv) {
    long intervalo;
    if (argc >= 1) {
        if (!interpretar_numero(argv[0], &intervalo) || intervalo > 1000)
            return reg_erro("Uso: memory_relatorio <operações entre relatórios, 0 desliga>");
        intervalo_relatorio = (int)intervalo;
    }
    if (intervalo_relatorio == 0) printf("Relatório periódico de fragmentação: DESLIGADO\n");
    else printf("Relatório periódico de fragmentação: a cada %d operações de memória\n", intervalo_relatorio);
    return 0;
}
