#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comum/utils.h"
#include "armazenamento/disco.h"
#include "criptografia/cripto.h"
#include "seguranca/seguranca.h"
#include "sistema_arquivos/fs.h"

typedef struct No {
    char nome[TAM_NOME];
    int pasta;
    struct No *pai;
    struct No *filho;
    struct No *proximo;
    long tamanho;
    int inode;
    int blocos[TOTAL_BLOCOS];
    int n_blocos;
    int id_dono;                    
    int id_grupo;                   
    int permissoes;                 /* rwx de dono, grupo e outros (ex.: 0750) */
    int criptografado;              /* 1 se o conteúdo gravado está cifrado */
    unsigned char sal[TAM_SAL];
    unsigned char verificador[TAM_HASH];
} No;

static No *raiz;
static No *atual;

static No *novo_no(const char *nome, int pasta, No *pai, long tamanho) {
    No *no = calloc(1, sizeof *no);
    if (!no) { fprintf(stderr, "Sem memória!\n"); exit(1); }
    snprintf(no->nome, TAM_NOME, "%s", nome);
    no->pasta = pasta;
    no->pai = pai;
    no->tamanho = tamanho;
    no->inode = -1;
    no->id_dono = seguranca_uid_atual();
    no->id_grupo = seguranca_gid_atual();
    no->permissoes = pasta ? 0755 : 0644;
    return no;
}

static void path_no(const No *no, char *saida, size_t capacidade) {
    if (!no->pai) { snprintf(saida, capacidade, "/"); return; }
    path_no(no->pai, saida, capacidade);
    size_t tam = strlen(saida);
    snprintf(saida + tam, capacidade - tam, "%s%s", tam > 1 ? "/" : "", no->nome);
}

static int pode(const No *no, char acao) {
    return seguranca_tem_permissao(no->id_dono, no->id_grupo, no->permissoes, acao);
}

static int exigir_permissao(const No *no, char acao) {
    if (pode(no, acao)) return 0;

    char caminho[TAM_PATH], modo[10];
    path_no(no, caminho, sizeof caminho);
    seguranca_formatar_permissoes(no->permissoes, modo);
    const char *pedida = (acao == 'r') ? "LEITURA" : (acao == 'w') ? "ESCRITA" : "EXECUÇÃO";
    const char *direito = (acao == 'r') ? "leitura" : (acao == 'w') ? "escrita" : "execução (acesso)";
    const char *usuario = seguranca_nome_usuario(seguranca_uid_atual());

    printf("[SISTEMA DE SEGURANÇA - VERIFICAÇÃO DE ACL]\n"
           "Objeto : %s\n"
           "Proprietário: %s (Grupo: %s)\n"
           "Permissões : %s (%03o)\n"
           "Ação Pedida : %s por '%s' (Grupo: %s)\n\n"
           "[NEGADO]: O usuário '%s' não possui permissão de %s no objeto '%s'.\n"
           "Tentativa não autorizada registrada nos logs do sistema.\n",
           caminho, seguranca_nome_usuario(no->id_dono), seguranca_nome_grupo(no->id_grupo),
           modo, no->permissoes, pedida, usuario, seguranca_nome_grupo(seguranca_gid_atual()),
           usuario, direito, caminho);
    seguranca_registrar("ACESSO NEGADO: '%s' tentou %s em '%s' (%s %03o)", usuario, pedida,
                        caminho, modo, no->permissoes);
    return ERRO_JA_EXIBIDO;
}

static No *achar_filho(const No *diretorio, const char *nome) {
    for (No *c = diretorio->filho; c; c = c->proximo)
        if (strcmp(c->nome, nome) == 0) return c;
    return NULL;
}

static int no_anterior(const No *a, const No *b) {
    if (a->pasta != b->pasta) return a->pasta;
    return strcmp(a->nome, b->nome) < 0;
}

static void inserir_filho(No *pai, No *no) {
    No **pp = &pai->filho;
    while (*pp && !no_anterior(no, *pp)) pp = &(*pp)->proximo;
    no->proximo = *pp;
    *pp = no;
}

static void remover_filho(No *no) {
    No **pp = &no->pai->filho;
    while (*pp != no) pp = &(*pp)->proximo;
    *pp = no->proximo;                       
}

static int parse_tamanho(const char *texto, long *saida) {
    char *final;
    long valor = strtol(texto, &final, 10);           
    if (final == texto || valor < 0)
        return reg_erro("Tamanho inválido: '%s' (use ex.: 512B, 10KB, 1MB)", texto);

    char sufixo[4] = "";                            
    size_t i = 0;
    for (; final[i] && i < sizeof sufixo - 1; i++)
        sufixo[i] = (final[i] >= 'a' && final[i] <= 'z') ? (char)(final[i] - 32) : final[i];
    sufixo[i] = '\0';
    if (final[i] != '\0') sufixo[0] = '?';            

    long mult;
    if (sufixo[0] == '\0' || strcmp(sufixo, "B") == 0) mult = 1;
    else if (strcmp(sufixo, "KB") == 0)             mult = 1024;
    else if (strcmp(sufixo, "MB") == 0)             mult = 1024L * 1024L;
    else return reg_erro("Tamanho inválido: '%s' (use ex.: 512B, 10KB, 1MB)", texto);

    *saida = valor * mult;
    return 0;
}

static void formata(long tamanho, char *saida, size_t capacidade) {
    if (tamanho && tamanho % 1024 == 0) snprintf(saida, capacidade, "%ld KB", tamanho / 1024);
    else                                snprintf(saida, capacidade, "%ld B", tamanho);
}

static int get_tamanho_arg(int argc, char **argv, long *saida) {
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], "--size") == 0 && i + 1 < argc)
            return parse_tamanho(argv[i + 1], saida);
    return reg_erro("Faltou o parâmetro: --size <tamanho>");
}

static int resolve(const char *path, No **saida) {
    No *no_atual = (path[0] == '/') ? raiz : atual;
    const char *p = path;
    while (*p) {
        while (*p == '/') p++;                   
        if (!*p) break;
        char token[TAM_NOME];
        size_t tam_token = 0;
        while (*p && *p != '/') {
            if (tam_token >= TAM_NOME - 1) return reg_erro("Nome muito longo.");
            token[tam_token++] = *p++;
        }
        token[tam_token] = '\0';

        if (strcmp(token, ".") == 0) continue;     
        if (strcmp(token, "..") == 0) {            
            if (no_atual->pai) no_atual = no_atual->pai;
            continue;
        }
        No *proximo = no_atual->pasta ? achar_filho(no_atual, token) : NULL;
        if (!proximo) return reg_erro("Caminho não encontrado: %s", path);
        no_atual = proximo;
    }
    *saida = no_atual;
    return 0;
}

static int separar_pai(const char *path, No **pai, char *nome) {
    char buffer[TAM_PATH];
    if (strlen(path) >= sizeof buffer) return reg_erro("Caminho muito longo.");
    strcpy(buffer, path);

    size_t tam = strlen(buffer);
    while (tam > 0 && buffer[tam - 1] == '/') buffer[--tam] = '\0';   
    if (tam == 0) return reg_erro("Caminho inválido.");

    char *barra = strrchr(buffer, '/');             
    const char *base;
    if (barra) {
        base = barra + 1;
        *barra = '\0';
        if (resolve(buffer[0] ? buffer : "/", pai) != 0) return -1;
    } else {
        base = buffer;
        *pai = atual;
    }
    if (!(*pai)->pasta) return reg_erro("O caminho pai não é um diretório.");
    if (strcmp(base, ".") == 0 || strcmp(base, "..") == 0 || !*base)
        return reg_erro("Nome inválido.");
    if (strlen(base) >= TAM_NOME) return reg_erro("Nome muito longo.");
    strcpy(nome, base);
    return 0;
}

static long blocos_precisos(long tamanho) {
    return (tamanho + TAM_BLOCO - 1) / TAM_BLOCO;         
}

static void gravar_blocos_arquivo(const No *no, const char *conteudo) {
    unsigned char buffer[TAM_BLOCO];

    memset(buffer, 0, sizeof buffer);                     
    memcpy(buffer, &no->n_blocos, sizeof(int));
    memcpy(buffer + sizeof(int), no->blocos, sizeof(int) * (size_t)no->n_blocos);
    disco_gravar_bloco(no->inode, buffer);

    for (int k = 0; k < no->n_blocos; k++) {              
        long offset = (long)k * TAM_BLOCO;
        long ocupado = no->tamanho - offset;
        if (ocupado > TAM_BLOCO) ocupado = TAM_BLOCO;
        memset(buffer, 0, sizeof buffer);
        if (conteudo)
            memcpy(buffer, conteudo + offset, (size_t)ocupado);
        else
            for (long j = 0; j < ocupado; j++)
                buffer[j] = (unsigned char)('a' + (offset + j) % 26);
        disco_gravar_bloco(no->blocos[k], buffer);
    }
}

static void alocar_arquivo(No *no, const char *conteudo) {
    no->n_blocos = (int)blocos_precisos(no->tamanho);
    no->inode = disco_alocar_bloco();
    for (int k = 0; k < no->n_blocos; k++)
        no->blocos[k] = disco_alocar_bloco();
    gravar_blocos_arquivo(no, conteudo);
}

static void liberar_arquivo(No *no) {
    if (no->inode >= 0) disco_liberar_bloco(no->inode);
    for (int k = 0; k < no->n_blocos; k++) disco_liberar_bloco(no->blocos[k]);
    no->inode = -1;
    no->n_blocos = 0;
}

static void apagar_no(No *no) {
    while (no->filho) apagar_no(no->filho);
    if (!no->pasta) liberar_arquivo(no);             
    remover_filho(no);
    free(no);
}

void fs_init(void) {
    disco_init();
    seguranca_registrar_gancho_novo_usuario(fs_criar_diretorio_pessoal);
    raiz = novo_no("/", 1, NULL, 0);
    atual = raiz;

    raiz->inode = disco_alocar_bloco();            
    unsigned char buffer[TAM_BLOCO] = {0};         
    disco_gravar_bloco(raiz->inode, buffer);
}

void fs_shutdown(void) {
    while (raiz->filho) apagar_no(raiz->filho);
    free(raiz);
    raiz = atual = NULL;
}

void fs_cwd_path(char *saida, size_t capacidade) {
    path_no(atual, saida, capacidade);
}

int cmd_mkdir(int argc, char **argv) {
    if (argc < 1) return reg_erro("Uso: mkdir <caminho>");
    No *pai; char nome[TAM_NOME];
    if (separar_pai(argv[0], &pai, nome) != 0) return -1;
    int negado = exigir_permissao(pai, 'w');
    if (negado != 0) return negado;
    if (achar_filho(pai, nome)) return reg_erro("'%s' já existe.", nome);
    inserir_filho(pai, novo_no(nome, 1, pai, 0));
    return 0;
}

int cmd_create(int argc, char **argv) {
    if (argc < 1) return reg_erro("Uso: create <caminho> --size <tamanho>");
    long tamanho;
    if (get_tamanho_arg(argc, argv, &tamanho) != 0) return -1;
    No *pai; char nome[TAM_NOME];
    if (separar_pai(argv[0], &pai, nome) != 0) return -1;
    int negado = exigir_permissao(pai, 'w');
    if (negado != 0) return negado;
    if (achar_filho(pai, nome)) return reg_erro("'%s' já existe.", nome);

    if (livres() < blocos_precisos(tamanho) + 1)     
        return reg_erro("Disco cheio: não há blocos livres suficientes.");

    No *no = novo_no(nome, 0, pai, tamanho);
    alocar_arquivo(no, NULL);
    inserir_filho(pai, no);
    return 0;
}

int cmd_write_file(int argc, char **argv) {
    if (argc < 2)
        return reg_erro("Uso: write_file <caminho> \"<texto>\"   ou   write_file <caminho> --size <tamanho>");

    const char *conteudo = NULL;
    long tamanho;
    if (strcmp(argv[1], "--size") == 0) {
        if (get_tamanho_arg(argc, argv, &tamanho) != 0) return -1;
    } else {
        conteudo = argv[1];
        tamanho = (long)strlen(conteudo);
    }

    No *existente;
    if (resolve(argv[0], &existente) == 0) {
        if (existente->pasta) return reg_erro("É um diretório; não é possível gravar nele.");
        int negado = exigir_permissao(existente, 'w');
        if (negado != 0) return negado;
        if (existente->criptografado)
            return reg_erro("O arquivo está criptografado; use decrypt_file antes de gravar nele.");
        long disponivel = livres() + existente->n_blocos + 1;
        if (disponivel < blocos_precisos(tamanho) + 1)
            return reg_erro("Disco cheio: não há blocos livres suficientes.");
        liberar_arquivo(existente);
        existente->tamanho = tamanho;
        alocar_arquivo(existente, conteudo);
        return 0;
    }

    No *pai; char nome[TAM_NOME];
    if (separar_pai(argv[0], &pai, nome) != 0) return -1;
    int negado = exigir_permissao(pai, 'w');
    if (negado != 0) return negado;
    if (livres() < blocos_precisos(tamanho) + 1)
        return reg_erro("Disco cheio: não há blocos livres suficientes.");
    No *no = novo_no(nome, 0, pai, tamanho);
    alocar_arquivo(no, conteudo);
    inserir_filho(pai, no);
    return 0;
}

int cmd_ls(int argc, char **argv) {
    No *diretorio = atual;
    if (argc >= 1 && resolve(argv[0], &diretorio) != 0) return -1;
    if (!diretorio->pasta) {
        char p[TAM_PATH]; path_no(diretorio, p, sizeof p);
        printf("%s\n", p);
        return 0;
    }
    int negado = exigir_permissao(diretorio, 'r');
    if (negado != 0) return negado;
    for (No *c = diretorio->filho; c; c = c->proximo) {
        char tam[32]; formata(c->tamanho, tam, sizeof tam);
        if (c->pasta) printf("%s/\n", c->nome);
        else           printf("%s  (%s)\n", c->nome, tam);
    }
    return 0;
}

int cmd_cd(int argc, char **argv) {
    No *diretorio = raiz;
    if (argc >= 1 && resolve(argv[0], &diretorio) != 0) return -1;
    if (!diretorio->pasta) return reg_erro("Não é um diretório.");
    int negado = exigir_permissao(diretorio, 'x');
    if (negado != 0) return negado;
    atual = diretorio;
    return 0;
}

int cmd_read(int argc, char **argv) {
    if (argc < 1) return reg_erro("Uso: read <arquivo>");
    No *no;
    if (resolve(argv[0], &no) != 0) return -1;
    if (no->pasta) return reg_erro("É um diretório; use 'ls'.");
    int negado = exigir_permissao(no, 'r');
    if (negado != 0) return negado;

    unsigned char buffer[TAM_BLOCO];
    if (disco_ler_bloco(no->inode, buffer) != 0) return -1;
    int quant;
    memcpy(&quant, buffer, sizeof quant);
    if (quant < 0 || quant > TOTAL_BLOCOS) return reg_erro("i-node corrompido.");
    int table[TOTAL_BLOCOS];
    memcpy(table, buffer + sizeof(int), sizeof(int) * (size_t)quant);

    char p[TAM_PATH]; path_no(no, p, sizeof p);
    printf("Arquivo: %s | %ld bytes\n", p, no->tamanho);
    printf("  i-node (lido do bloco %02d): [", no->inode);
    for (int k = 0; k < quant; k++) printf("%s%d", k ? ", " : "", table[k]);
    printf("]\n");

    if (quant > 0) {
        if (disco_ler_bloco(table[0], buffer) != 0) return -1;
        long tam = no->tamanho < 16 ? no->tamanho : 16;
        if (no->criptografado) {
            printf("  conteúdo cifrado (início): 0x");
            for (long i = 0; i < tam; i++) printf("%02X", buffer[i]);
            printf("\n");
        } else {
            char preview[17];
            memcpy(preview, buffer, (size_t)tam);
            preview[tam] = '\0';
            printf("  conteúdo (início): \"%s\"\n", preview);
        }
    }
    return 0;
}

static void imprimir_arvore(const No *diretorio, const char *prefixo) {
    for (const No *c = diretorio->filho; c; c = c->proximo) {
        int ultimo = (c->proximo == NULL);
        char rotulo[128], tam[32];
        formata(c->tamanho, tam, sizeof tam);
        if (c->pasta) snprintf(rotulo, sizeof rotulo, "%s/ [DIR]", c->nome);
        else           snprintf(rotulo, sizeof rotulo, "%s (%s) [ARQ]", c->nome, tam);

        printf("%s%s%s\n", prefixo, ultimo ? "└── " : "├── ", rotulo);
        if (c->pasta) {
            char proximo_prefixo[TAM_PATH];
            snprintf(proximo_prefixo, sizeof proximo_prefixo, "%s%s", prefixo,
                     ultimo ? "    " : "│   ");
            if (pode(c, 'r')) imprimir_arvore(c, proximo_prefixo);
            else printf("%s└── [sem permissão de leitura]\n", proximo_prefixo);
        }
    }
}

int cmd_tree(int argc, char **argv) {
    (void)argc; (void)argv;                      
    printf("[ÁRVORE DO SISTEMA DE ARQUIVOS]\n/\n");
    imprimir_arvore(raiz, "");
    return 0;
}

int cmd_update(int argc, char **argv) {
    if (argc < 1) return reg_erro("Uso: update <arquivo> --size <novo tamanho>");
    long novo_tamanho;
    if (get_tamanho_arg(argc, argv, &novo_tamanho) != 0) return -1;
    No *no;
    if (resolve(argv[0], &no) != 0) return -1;
    if (no->pasta) return reg_erro("Só é possível atualizar arquivos.");
    int negado = exigir_permissao(no, 'w');
    if (negado != 0) return negado;
    if (no->criptografado)
        return reg_erro("O arquivo está criptografado; use decrypt_file antes de alterá-lo.");

    long disponivel = livres() + no->n_blocos + 1;   
    if (disponivel < blocos_precisos(novo_tamanho) + 1)
        return reg_erro("Disco cheio: não há blocos livres suficientes.");

    liberar_arquivo(no);
    no->tamanho = novo_tamanho;
    alocar_arquivo(no, NULL);
    return 0;
}

static int dentro(const No *no, const No *ancestral) {
    for (; no; no = no->pai) if (no == ancestral) return 1;
    return 0;
}

int cmd_rm(int argc, char **argv) {
    int recursivo = 0;
    const char *path = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) recursivo = 1;
        else if (!path) path = argv[i];
    }
    if (!path) return reg_erro("Uso: rm <caminho> [-r]   (-r para diretórios com conteúdo)");

    No *no;
    if (resolve(path, &no) != 0) return -1;
    if (no == raiz) return reg_erro("Não é possível remover a raiz.");
    int negado = exigir_permissao(no->pai, 'w');
    if (negado != 0) return negado;
    if (no->pasta && no->filho && !recursivo)
        return reg_erro("Diretório não vazio. Use 'rm <diretorio> -r'.");

    if (dentro(atual, no)) atual = no->pai;      
    apagar_no(no);
    return 0;
}

static int collect_files(const No *diretorio, No **arquivos, int quant) {
    for (No *c = diretorio->filho; c; c = c->proximo) {
        if (c->pasta) quant = collect_files(c, arquivos, quant);
        else           arquivos[quant++] = c;
    }
    return quant;
}

int cmd_fs_status(int argc, char **argv) {
    (void)argc; (void)argv;

    printf("Tamanho do Bloco de Disco: %d KB | Total de Blocos: %d (%d KB) | "
           "Alocação: Indexada\n",
           TAM_BLOCO / 1024, TOTAL_BLOCOS, TOTAL_BLOCOS * TAM_BLOCO / 1024);

    No *arquivos[TOTAL_BLOCOS];                   
    int n_arquivos = collect_files(raiz, arquivos, 0);
    No *dono[TOTAL_BLOCOS] = {0};
    dono[raiz->inode] = raiz;
    for (int a = 0; a < n_arquivos; a++) {
        dono[arquivos[a]->inode] = arquivos[a];
        for (int k = 0; k < arquivos[a]->n_blocos; k++)
            dono[arquivos[a]->blocos[k]] = arquivos[a];
    }

    printf("\n[MAPA VISUAL]  ");
    for (int i = 0; i < TOTAL_BLOCOS; i++) {
        if (disco_livre(i))        printf("[·] ");
        else if (dono[i] == raiz)  printf("[R] ");
        else {
            int a = 0;
            while (arquivos[a] != dono[i]) a++;
            char letra = (char)('A' + a % 26);
            if (dono[i]->inode == i) letra = (char)(letra + 32);  
            printf("[%c] ", letra);
        }
    }
    printf("\n");
    for (int a = 0; a < n_arquivos; a++) {
        char p[TAM_PATH]; path_no(arquivos[a], p, sizeof p);
        printf("   %c = %s\n", 'A' + a % 26, p);
    }

    printf("\n[MAPA DE ALOCAÇÃO DO DISCO]\n");
    for (int i = 0; i < TOTAL_BLOCOS; ) {
        if (disco_livre(i)) {                   
            int j = i;
            while (j + 1 < TOTAL_BLOCOS && disco_livre(j + 1)) j++;
            char range[32];
            if (i == j) snprintf(range, sizeof range, "%02d", i);
            else        snprintf(range, sizeof range, "%02d..%02d", i, j);
            printf("Bloco %-7s: [LIVRE]\n", range);
            i = j + 1;
            continue;
        }
        No *b = dono[i];
        char p[TAM_PATH];
        if (b == raiz) {
            printf("Bloco %02d     : [INODE_RAIZ] : Diretório '/'\n", i);
        } else if (b->inode == i) {
            path_no(b, p, sizeof p);
            printf("Bloco %02d     : [INODE] : Arquivo '%s'\n", i, p);
        } else {
            int k = 0;
            while (b->blocos[k] != i) k++;       
            long ocupado = b->tamanho - (long)k * TAM_BLOCO;
            if (ocupado > TAM_BLOCO) ocupado = TAM_BLOCO;
            path_no(b, p, sizeof p);
            printf("Bloco %02d     : [DADO]  : '%s' (Bloco %d - %ld bytes ocupados)\n",
                   i, p, k + 1, ocupado);
        }
        i++;
    }

    printf("\n--- RELATÓRIO DE FRAGMENTAÇÃO ---\n");
    long total_interno = 0;
    for (int a = 0; a < n_arquivos; a++) {
        No *no = arquivos[a];
        long alocado = (long)no->n_blocos * TAM_BLOCO;
        long interno = alocado - no->tamanho;     
        total_interno += interno;
        double percentual = alocado ? (double)interno * 100.0 / (double)alocado : 0.0;
        char p[TAM_PATH]; path_no(no, p, sizeof p);
        printf("'%s': real %ld B | alocado %ld B (%d bloco(s) de dados) | "
               "frag. interna %ld B (%.0f%%)\n",
               p, no->tamanho, alocado, no->n_blocos, interno, percentual);
    }

    int inicios[TOTAL_BLOCOS], tamanhos[TOTAL_BLOCOS], n_runs = 0;
    for (int i = 0; i < TOTAL_BLOCOS; i++) {
        if (!disco_livre(i)) continue;
        if (n_runs > 0 && inicios[n_runs - 1] + tamanhos[n_runs - 1] == i) tamanhos[n_runs - 1]++;
        else { inicios[n_runs] = i; tamanhos[n_runs] = 1; n_runs++; }
    }
    char runs[TAM_PATH] = "";
    for (int r = 0; r < n_runs; r++) {
        size_t tam = strlen(runs);
        snprintf(runs + tam, sizeof runs - tam, "%s(%d, %d)", r ? ", " : "",
                 inicios[r], tamanhos[r]);
    }

    printf("Fragmentação Interna (total) : %ld bytes\n", total_interno);
    printf("Fragmentação Externa         : 0 bytes (%d trecho(s) livre(s): [%s])\n",
           n_runs, runs);
    printf("Necessidade de Compactação   : NÃO\n");
    return 0;
}

static unsigned char conteudo_temporario[TOTAL_BLOCOS * TAM_BLOCO + 1];

static int ler_conteudo(const No *no, unsigned char *saida) {
    unsigned char buffer[TAM_BLOCO];
    for (int k = 0; k < no->n_blocos; k++) {
        if (disco_ler_bloco(no->blocos[k], buffer) != 0) return -1;
        long offset = (long)k * TAM_BLOCO;
        long ocupado = no->tamanho - offset;
        if (ocupado > TAM_BLOCO) ocupado = TAM_BLOCO;
        memcpy(saida + offset, buffer, (size_t)ocupado);
    }
    return 0;
}

static const char *obter_opcao(int argc, char **argv, const char *opcao) {
    for (int i = 0; i + 1 < argc; i++)
        if (strcmp(argv[i], opcao) == 0) return argv[i + 1];
    return NULL;
}

static int verificar_chave(const No *no, const char *chave_texto, unsigned char chave[TAM_HASH]) {
    unsigned char verificador[TAM_HASH];
    cripto_derivar_chave(chave_texto, no->sal, chave);
    cripto_verificador(chave, verificador);
    if (!cripto_hashes_iguais(verificador, no->verificador)) {
        char caminho[TAM_PATH];
        path_no(no, caminho, sizeof caminho);
        seguranca_registrar("CHAVE INCORRETA: tentativa de decifrar '%s'", caminho);
        return reg_erro("Chave incorreta.");
    }
    return 0;
}

static void imprimir_hex(const unsigned char *dados, long quantidade) {
    for (long i = 0; i < quantidade; i++) printf("%02X", dados[i]);
}

int cmd_read_file(int argc, char **argv) {
    if (argc < 1) return reg_erro("Uso: read_file <caminho> [--key \"<chave>\"]");
    No *no;
    if (resolve(argv[0], &no) != 0) return -1;
    if (no->pasta) return reg_erro("É um diretório; use 'ls'.");
    int negado = exigir_permissao(no, 'r');
    if (negado != 0) return negado;

    if (ler_conteudo(no, conteudo_temporario) != 0) return -1;
    char caminho[TAM_PATH];
    path_no(no, caminho, sizeof caminho);

    if (no->criptografado) {
        const char *chave_texto = obter_opcao(argc, argv, "--key");
        if (!chave_texto) {
            long previa = no->tamanho < 24 ? no->tamanho : 24;
            printf("[ARQUIVO CRIPTOGRAFADO] %s (%ld bytes)\n", caminho, no->tamanho);
            printf("Conteúdo cifrado (início): 0x");
            imprimir_hex(conteudo_temporario, previa);
            printf("...\nUse --key \"<chave>\" para decifrar.\n");
            return 0;
        }
        unsigned char chave[TAM_HASH];
        if (verificar_chave(no, chave_texto, chave) != 0) return -1;
        cripto_aplicar(chave, no->sal, conteudo_temporario, (size_t)no->tamanho); 
    }

    long exibir = no->tamanho < 2048 ? no->tamanho : 2048;
    printf("--- %s (%ld bytes) ---\n", caminho, no->tamanho);
    for (long i = 0; i < exibir; i++) {
        unsigned char c = conteudo_temporario[i];
        putchar((c >= 32 && c != 127) || c == '\n' || c == '\t' ? c : '.');
    }
    printf("\n");
    if (exibir < no->tamanho) printf("... (+%ld bytes não exibidos)\n", no->tamanho - exibir);
    printf("--- fim ---\n");
    return 0;
}

int cmd_stat(int argc, char **argv) {
    if (argc < 1) return reg_erro("Uso: stat <caminho>");
    No *no;
    if (resolve(argv[0], &no) != 0) return -1;

    char caminho[TAM_PATH], modo[10];
    path_no(no, caminho, sizeof caminho);
    seguranca_formatar_permissoes(no->permissoes, modo);
    printf("Objeto      : %s\n", caminho);
    printf("Tipo        : %s\n", no->pasta ? "diretório" : "arquivo");
    printf("Proprietário: %s (Grupo: %s)\n", seguranca_nome_usuario(no->id_dono),
           seguranca_nome_grupo(no->id_grupo));
    printf("Permissões  : %s (%03o)\n", modo, no->permissoes);
    if (!no->pasta) {
        printf("Tamanho     : %ld bytes\n", no->tamanho);
        printf("Criptografia: %s\n", no->criptografado ? "SIM" : "NÃO");
        printf("i-node      : bloco %02d | blocos de dados: [", no->inode);
        for (int k = 0; k < no->n_blocos; k++) printf("%s%d", k ? ", " : "", no->blocos[k]);
        printf("]\n");
    }
    return 0;
}

int cmd_chmod(int argc, char **argv) {
    if (argc < 2) return reg_erro("Uso: chmod <octal> <caminho>   (ex.: chmod 750 /sistema/config.sys)");
    if (strlen(argv[0]) != 3) return reg_erro("Permissões inválidas: '%s' (use 3 dígitos de 0 a 7, ex.: 750)", argv[0]);
    int permissoes = 0;
    for (int i = 0; i < 3; i++) {
        if (argv[0][i] < '0' || argv[0][i] > '7')
            return reg_erro("Permissões inválidas: '%s' (use 3 dígitos de 0 a 7, ex.: 750)", argv[0]);
        permissoes = permissoes * 8 + (argv[0][i] - '0');
    }

    No *no;
    if (resolve(argv[1], &no) != 0) return -1;
    char caminho[TAM_PATH];
    path_no(no, caminho, sizeof caminho);
    if (seguranca_modo() != MODO_KERNEL && seguranca_uid_atual() != no->id_dono) {
        seguranca_registrar("CHMOD NEGADO: '%s' tentou alterar '%s'",
                            seguranca_nome_usuario(seguranca_uid_atual()), caminho);
        return reg_erro("Somente o proprietário ('%s') ou o superusuário pode alterar as permissões.",
                        seguranca_nome_usuario(no->id_dono));
    }

    no->permissoes = permissoes;
    char modo[10];
    seguranca_formatar_permissoes(permissoes, modo);
    seguranca_registrar("CHMOD: '%s' agora é %s (%03o)", caminho, modo, permissoes);
    printf("Permissões de '%s' alteradas para %s (%03o).\n", caminho, modo, permissoes);
    return 0;
}

int cmd_chown(int argc, char **argv) {
    if (argc < 2) return reg_erro("Uso: chown <usuário>[:<grupo>] <caminho>");
    char especificacao[TAM_NOME * 2];
    snprintf(especificacao, sizeof especificacao, "%s", argv[0]);
    char *separador = strchr(especificacao, ':');
    const char *nome_grupo = NULL;
    if (separador) { *separador = '\0'; nome_grupo = separador + 1; }

    int uid = seguranca_buscar_usuario(especificacao);
    if (uid < 0) return reg_erro("O usuário '%s' não existe.", especificacao);
    int gid = -1;
    if (nome_grupo && *nome_grupo) {
        gid = seguranca_buscar_grupo(nome_grupo);
        if (gid < 0) return reg_erro("O grupo '%s' não existe.", nome_grupo);
    }

    No *no;
    if (resolve(argv[1], &no) != 0) return -1;
    no->id_dono = uid;
    if (gid >= 0) no->id_grupo = gid;
    char caminho[TAM_PATH];
    path_no(no, caminho, sizeof caminho);
    seguranca_registrar("CHOWN: '%s' agora pertence a '%s' (grupo '%s')", caminho,
                        seguranca_nome_usuario(no->id_dono), seguranca_nome_grupo(no->id_grupo));
    printf("Proprietário de '%s' alterado para %s (Grupo: %s).\n", caminho,
           seguranca_nome_usuario(no->id_dono), seguranca_nome_grupo(no->id_grupo));
    return 0;
}

int cmd_encrypt_file(int argc, char **argv) {
    const char *chave_texto = obter_opcao(argc, argv, "--key");
    if (argc < 1 || !chave_texto) return reg_erro("Uso: encrypt_file <caminho> --key \"<chave>\"");
    No *no;
    if (resolve(argv[0], &no) != 0) return -1;
    if (no->pasta) return reg_erro("Só é possível criptografar arquivos.");
    int negado = exigir_permissao(no, 'r');
    if (negado == 0) negado = exigir_permissao(no, 'w');
    if (negado != 0) return negado;
    if (no->criptografado) return reg_erro("O arquivo já está criptografado.");
    if (no->tamanho == 0) return reg_erro("O arquivo está vazio; não há o que criptografar.");
    if (strlen(chave_texto) < 4) return reg_erro("Chave muito curta (mínimo de 4 caracteres).");

    if (ler_conteudo(no, conteudo_temporario) != 0) return -1;
    unsigned char chave[TAM_HASH];
    cripto_sal_aleatorio(no->sal);
    cripto_derivar_chave(chave_texto, no->sal, chave);
    cripto_verificador(chave, no->verificador);
    cripto_aplicar(chave, no->sal, conteudo_temporario, (size_t)no->tamanho);
    gravar_blocos_arquivo(no, (const char *)conteudo_temporario);
    no->criptografado = 1;

    char caminho[TAM_PATH];
    path_no(no, caminho, sizeof caminho);
    seguranca_registrar("CRIPTOGRAFIA: '%s' criptografado", caminho);
    printf("Arquivo '%s' criptografado com sucesso.\n", caminho);
    printf("Conteúdo gravado nos blocos físicos: \"0x");
    imprimir_hex(conteudo_temporario, 4);
    printf("... (Encrypted Ciphertext)\"\n");
    return 0;
}

int cmd_decrypt_file(int argc, char **argv) {
    const char *chave_texto = obter_opcao(argc, argv, "--key");
    if (argc < 1 || !chave_texto) return reg_erro("Uso: decrypt_file <caminho> --key \"<chave>\"");
    No *no;
    if (resolve(argv[0], &no) != 0) return -1;
    if (no->pasta) return reg_erro("Só é possível descriptografar arquivos.");
    int negado = exigir_permissao(no, 'r');
    if (negado == 0) negado = exigir_permissao(no, 'w');
    if (negado != 0) return negado;
    if (!no->criptografado) return reg_erro("O arquivo não está criptografado.");

    unsigned char chave[TAM_HASH];
    if (verificar_chave(no, chave_texto, chave) != 0) return -1;
    if (ler_conteudo(no, conteudo_temporario) != 0) return -1;
    cripto_aplicar(chave, no->sal, conteudo_temporario, (size_t)no->tamanho);
    gravar_blocos_arquivo(no, (const char *)conteudo_temporario);
    no->criptografado = 0;

    char caminho[TAM_PATH];
    path_no(no, caminho, sizeof caminho);
    seguranca_registrar("CRIPTOGRAFIA: '%s' descriptografado", caminho);
    printf("Arquivo '%s' descriptografado com sucesso.\n", caminho);
    return 0;
}

static int coletar_info(const No *no, InfoArquivo *saida, int maximo, int quant) {
    if (!no->pasta) {
        if (quant >= maximo) return quant;
        InfoArquivo *info = &saida[quant];
        path_no(no, info->caminho, sizeof info->caminho);
        info->tamanho = no->tamanho;
        info->inode = no->inode;
        info->n_blocos = no->n_blocos;
        memcpy(info->blocos, no->blocos, sizeof info->blocos);
        info->criptografado = no->criptografado;
        return quant + 1;
    }
    for (const No *c = no->filho; c; c = c->proximo) quant = coletar_info(c, saida, maximo, quant);
    return quant;
}

int fs_listar_arquivos(const char *caminho, InfoArquivo *saida, int maximo) {
    No *no;
    if (resolve(caminho, &no) != 0) return -1;
    return coletar_info(no, saida, maximo, 0);
}

int fs_info_arquivo(const char *caminho, InfoArquivo *info) {
    No *no;
    if (resolve(caminho, &no) != 0) return -1;
    if (no->pasta) return reg_erro("'%s' é um diretório; informe um arquivo.", caminho);
    coletar_info(no, info, 1, 0);
    return 0;
}

int fs_exigir_execucao(const char *caminho) {
    No *no;
    if (resolve(caminho, &no) != 0) return -1;
    if (no->pasta) return reg_erro("'%s' é um diretório; informe um arquivo.", caminho);
    return exigir_permissao(no, 'x');
}

int fs_criar_diretorio_pessoal(const char *nome_usuario, int uid, int gid) {
    No *home = achar_filho(raiz, "home");
    if (!home) {
        home = novo_no("home", 1, raiz, 0);
        inserir_filho(raiz, home);
    }
    if (achar_filho(home, nome_usuario)) return 0;
    No *pessoal = novo_no(nome_usuario, 1, home, 0);
    pessoal->id_dono = uid;
    pessoal->id_grupo = gid;
    pessoal->permissoes = 0750;
    inserir_filho(home, pessoal);
    return 0;
}