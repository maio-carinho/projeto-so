#include <stdio.h>
#include <string.h>

#include "utils.h"
#include "fs.h"
#include "es.h"

#define TAM_LINHA 512    
#define MAX_ARGS 16     

static int cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("Sistema de arquivos:\n"
           "  mkdir <caminho>                   cria diretório\n"
           "  create <caminho> --size <tam>     cria arquivo (ex.: 512B, 10KB, 1MB)\n"
           "  write_file <caminho> \"<texto>\"    grava texto (cria ou sobrescreve o arquivo)\n"
           "  write_file <caminho> --size <tam> grava conteúdo simulado de <tam> bytes\n"
           "  ls [caminho]                      lista o conteúdo\n"
           "  cd [caminho]                      muda de diretório\n"
           "  tree                              mostra a árvore\n"
           "  read <arquivo>                    lê i-node e começo do conteúdo\n"
           "  update <arquivo> --size <tam>     muda o tamanho do arquivo\n"
           "  rm <caminho> [-r]                 remove arquivo/diretório\n"
           "  fs_status                         mapa do disco e fragmentação\n"
           "\nEntrada/Saída:\n"
           "  io auto <on|off>                  mostra o escalonamento após cada comando\n"
           "  io algoritmo <fcfs|sstf|scan|cscan> [sobe|desce]\n"
           "  io fila                           requisições pendentes\n"
           "  io add <trilha...> [--disco N]    enfileira requisições manuais\n"
           "  io cabeca <trilha> [--disco N]    posiciona a cabeça de leitura\n"
           "  io escalonar [algoritmo]          atende a fila e mostra o deslocamento\n"
           "  io comparar                       compara os 4 algoritmos na fila\n"
           "  io status                         estatísticas acumuladas\n"
           "  raid <nenhum|0|1|4|5>             escolhe o arranjo de discos (migra os dados)\n"
           "  raid status                       discos, contadores e mapa físico\n"
           "  raid falhar <disco>               simula a falha de um disco\n"
           "  raid reconstruir <disco>          substitui e reconstrói o disco\n"
           "  raid testar <disco>               teste completo de falha e recuperação\n"
           "  meio <hdd|ssd>                    escolhe o dispositivo de armazenamento\n"
           "  ssd status                        desgaste dos blocos do SSD\n"
           "  ssd wear <on|off>                 liga/desliga o nivelamento de desgaste\n"
           "  ssd estresse <n>                  regrava n vezes um bloco quente\n"
           "\n"
           "  exit                              sai\n");
    return 0;
}

typedef struct { const char *name; int (*fn)(int, char **); } Command;

static const Command comandos[] = {
    { "mkdir", cmd_mkdir }, { "create", cmd_create }, { "ls", cmd_ls },
    { "cd", cmd_cd }, { "tree", cmd_tree }, { "read", cmd_read },
    { "update", cmd_update }, { "rm", cmd_rm },
    { "fs_status", cmd_fs_status }, { "help", cmd_help },
    { "write_file", cmd_write_file },
    { "io", cmd_io }, { "raid", cmd_raid }, { "ssd", cmd_ssd }, { "meio", cmd_meio },
};

static int dividir_linha(char *linha, char **palavras, int maximo) {
    int quantidade = 0;
    linha[strcspn(linha, "\r\n")] = '\0';
    char *p = linha;
    while (*p && quantidade < maximo) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') {
            palavras[quantidade++] = ++p;
            while (*p && *p != '"') p++;
        } else {
            palavras[quantidade++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
        }
        if (*p) *p++ = '\0';
    }
    return quantidade;
}

int main(void) {
    fs_init();
    es_iniciar();

    char linha[TAM_LINHA];
    for (;;) {
        char p[TAM_PATH];
        fs_cwd_path(p, sizeof p);
        printf("%s > ", p);
        fflush(stdout);
        if (!fgets(linha, sizeof linha, stdin)) break;     

        char *argv[MAX_ARGS];
        int argc = dividir_linha(linha, argv, MAX_ARGS);
        if (argc == 0) continue;

        if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) break;

        int flag = 0;
        for (size_t i = 0; i < sizeof comandos / sizeof comandos[0]; i++) {
            if (strcmp(argv[0], comandos[i].name) == 0) {
                flag = 1;
                if (comandos[i].fn(argc - 1, argv + 1) != 0)
                    printf("Erro: %s\n", reg_ultimo_erro());
                es_fim_do_comando();
                break;
            }
        }
        if (!flag) printf("Comando desconhecido: %s (digite 'help')\n", argv[0]);
    }

    fs_shutdown();                               
    return 0;
}