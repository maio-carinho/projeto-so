#include <stdio.h>
#include <string.h>

#include "utils.h"
#include "fs.h"

#define TAM_LINHA 512    
#define MAX_ARGS 8      

static int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Comandos:\n"
           "  mkdir <caminho>                   cria diretório\n"
           "  create <caminho> --size <tam>     cria arquivo (ex.: 512B, 10KB, 1MB)\n"
           "  ls [caminho]                      lista o conteúdo\n"
           "  cd [caminho]                      muda de diretório\n"
           "  tree                              mostra a árvore\n"
           "  read <arquivo>                    lê i-node e começo do conteúdo\n"
           "  update <arquivo> --size <tam>     muda o tamanho do arquivo\n"
           "  rm <caminho> [-r]                 remove arquivo/diretório\n"
           "  fs_status                         mapa do disco e fragmentação\n"
           "  exit                              sai\n");
    return 0;
}

typedef struct { const char *name; int (*fn)(int, char **); } Command;

static const Command comandos[] = {
    { "mkdir", cmd_mkdir }, { "create", cmd_create }, { "ls", cmd_ls },
    { "cd", cmd_cd }, { "tree", cmd_tree }, { "read", cmd_read },
    { "update", cmd_update }, { "rm", cmd_rm },
    { "fs_status", cmd_fs_status }, { "help", cmd_help },
};

int main(void) {
    fs_init();

    char linha[TAM_LINHA];
    for (;;) {
        char p[TAM_PATH];
        fs_cwd_path(p, sizeof p);
        printf("%s > ", p);
        fflush(stdout);
        if (!fgets(linha, sizeof linha, stdin)) break;     

        char *argv[MAX_ARGS];
        int argc = 0;
        for (char *token = strtok(linha, " \t\r\n"); token && argc < MAX_ARGS;
             token = strtok(NULL, " \t\r\n"))
            argv[argc++] = token;
        if (argc == 0) continue;

        if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) break;

        int flag = 0;
        for (size_t i = 0; i < sizeof comandos / sizeof comandos[0]; i++) {
            if (strcmp(argv[0], comandos[i].name) == 0) {
                flag = 1;
                if (comandos[i].fn(argc - 1, argv + 1) != 0)
                    printf("Erro: %s\n", reg_ultimo_erro());
                break;
            }
        }
        if (!flag) printf("Comando desconhecido: %s (digite 'help')\n", argv[0]);
    }

    fs_shutdown();                               
    return 0;
}