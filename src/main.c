#include <stdio.h>
#include <string.h>

#include "comum/utils.h"
#include "sistema_arquivos/fs.h"
#include "armazenamento/es.h"
#include "seguranca/seguranca.h"
#include "antivirus/antivirus.h"
#include "memoria/memoria.h"

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
           "\nSegurança e proteção:\n"
           "  login --user <nome>               abre uma sessão (pede a senha)\n"
           "  logout                            encerra a sessão\n"
           "  whoami                            mostra o usuário, o grupo e o modo\n"
           "  sudo <comando>                    executa como root (só para o grupo admin)\n"
           "  useradd <nome> --group <grupo>    cria um usuário (kernel)\n"
           "  groupadd <nome>                   cria um grupo (kernel)\n"
           "  usermod <usuário> --add-group <g> coloca o usuário em outro grupo (kernel)\n"
           "  passwd [usuário]                  troca a senha\n"
           "  users                             lista usuários e grupos (kernel)\n"
           "  chmod <octal> <caminho>           altera as permissões rwx (ex.: 750)\n"
           "  chown <usuário>[:<grupo>] <cam.>  altera o proprietário (kernel)\n"
           "  stat <caminho>                    dono, permissões e criptografia\n"
           "  read_file <caminho> [--key \"<chave>\"]   mostra o conteúdo (decifra com a chave)\n"
           "  encrypt_file <caminho> --key \"<chave>\"  criptografa o arquivo nos blocos\n"
           "  decrypt_file <caminho> --key \"<chave>\"  descriptografa o arquivo\n"
           "  antivirus_scan [caminho]          varre integridade e assinaturas (kernel)\n"
           "  antivirus_sig [add <texto>]       lista ou adiciona assinaturas (kernel)\n"
           "  adulterar_bloco <arq> [--bloco N] simula alteração direta no disco (kernel)\n"
           "  audit_log [n]                     registro de auditoria (kernel)\n"
           "\nGerenciamento de memória:\n"
           "  create_process --name <nome> --size <tam> [--type critical|normal]\n"
           "                                    cria processo (critical = Área Reservada, kernel)\n"
           "  create_process --name <nome> --file <caminho>   usa o tamanho de um arquivo (exige x)\n"
           "  terminate_process --name <nome>   encerra o processo e libera a memória\n"
           "  memory_status [--reserved|--virtual]   mapa, ocupação e fragmentação (--reserved: kernel)\n"
           "  memory_status --process <nome>    tabela de páginas do processo\n"
           "  memory_compact --reserved         compacta a Área Reservada (kernel)\n"
           "  translate_address --process <nome> --page <N> [--offset <bytes>]\n"
           "                                    página lógica -> moldura física\n"
           "  execute_step --process <nome> --instruction \"LOAD_DATA Página <N>\"\n"
           "                                    acessa uma página (STORE_DATA escreve)\n"
           "  memory_algoritmo [fifo|lru|segunda_chance]   troca o algoritmo de substituição (kernel)\n"
           "  memory_relatorio [n]              relatório de fragmentação a cada n operações; 0 desliga (kernel)\n"
           "\n(kernel) = exige MODO_KERNEL (root ou sudo); fs_status, io, raid, ssd e meio também.\n"
           "  exit                              sai\n");
    return 0;
}

typedef struct { const char *name; int (*fn)(int, char **); int critico; } Command;

static const Command comandos[] = {
    { "mkdir", cmd_mkdir, 0 }, { "create", cmd_create, 0 }, { "ls", cmd_ls, 0 },
    { "cd", cmd_cd, 0 }, { "tree", cmd_tree, 0 }, { "read", cmd_read, 0 },
    { "update", cmd_update, 0 }, { "rm", cmd_rm, 0 },
    { "fs_status", cmd_fs_status, 1 }, { "help", cmd_help, 0 },
    { "write_file", cmd_write_file, 0 },
    { "io", cmd_io, 1 }, { "raid", cmd_raid, 1 }, { "ssd", cmd_ssd, 1 }, { "meio", cmd_meio, 1 },
    { "login", cmd_login, 0 }, { "logout", cmd_logout, 0 }, { "whoami", cmd_whoami, 0 },
    { "useradd", cmd_useradd, 1 }, { "groupadd", cmd_groupadd, 1 }, { "usermod", cmd_usermod, 1 },
    { "passwd", cmd_passwd, 0 }, { "users", cmd_users, 1 }, { "audit_log", cmd_audit_log, 1 },
    { "chmod", cmd_chmod, 0 }, { "chown", cmd_chown, 1 }, { "stat", cmd_stat, 0 },
    { "read_file", cmd_read_file, 0 }, { "encrypt_file", cmd_encrypt_file, 0 },
    { "decrypt_file", cmd_decrypt_file, 0 },
    { "antivirus_scan", cmd_antivirus_scan, 1 }, { "antivirus_sig", cmd_antivirus_sig, 1 },
    { "adulterar_bloco", cmd_adulterar_bloco, 1 },
    { "create_process", cmd_create_process, 0 }, { "terminate_process", cmd_terminate_process, 0 },
    { "memory_status", cmd_memory_status, 0 }, { "memory_compact", cmd_memory_compact, 1 },
    { "translate_address", cmd_translate_address, 0 }, { "execute_step", cmd_execute_step, 0 },
    { "memory_algoritmo", cmd_memory_algoritmo, 1 }, { "memory_relatorio", cmd_memory_relatorio, 1 },
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

static const Command *buscar_comando(const char *nome) {
    for (size_t i = 0; i < sizeof comandos / sizeof comandos[0]; i++)
        if (strcmp(nome, comandos[i].name) == 0) return &comandos[i];
    return NULL;
}

static void executar_comando(int argc, char **argv);

static void executar_sudo(int argc, char **argv) {
    if (argc < 2) { printf("Erro: Uso: sudo <comando> [argumentos]\n"); return; }
    if (strcmp(argv[1], "sudo") == 0) { printf("Erro: o sudo não pode ser repetido.\n"); return; }
    if (seguranca_elevar() != 0) { printf("Erro: %s\n", reg_ultimo_erro()); return; }
    seguranca_registrar("SUDO: '%s' executado com privilégios de root", argv[1]);
    executar_comando(argc - 1, argv + 1);
    seguranca_baixar();
}

static void executar_comando(int argc, char **argv) {
    if (strcmp(argv[0], "sudo") == 0) { executar_sudo(argc, argv); return; }

    const Command *comando = buscar_comando(argv[0]);
    if (!comando) {
        printf("Comando desconhecido: %s (digite 'help')\n", argv[0]);
        return;
    }

    int sem_login = strcmp(comando->name, "login") == 0 || strcmp(comando->name, "whoami") == 0 ||
                    strcmp(comando->name, "help") == 0;
    if (!seguranca_ha_sessao() && !sem_login) {
        printf("Erro: Nenhuma sessão ativa. Use: login --user <nome>\n");
        return;
    }
    if (comando->critico && seguranca_exigir_kernel(comando->name) != 0) return;

    int resultado = comando->fn(argc - 1, argv + 1);
    if (resultado != 0 && resultado != ERRO_JA_EXIBIDO)
        printf("Erro: %s\n", reg_ultimo_erro());
    es_fim_do_comando();
}

int main(void) {
    seguranca_init();
    fs_init();
    es_iniciar();
    memoria_iniciar();

    char linha[TAM_LINHA];
    for (;;) {
        char p[TAM_PATH];
        fs_cwd_path(p, sizeof p);
        printf("%s%s > ", seguranca_prefixo_prompt(), p);
        fflush(stdout);
        if (!fgets(linha, sizeof linha, stdin)) break;

        char *argv[MAX_ARGS];
        int argc = dividir_linha(linha, argv, MAX_ARGS);
        if (argc == 0) continue;

        if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) break;
        executar_comando(argc, argv);
    }

    fs_shutdown();
    return 0;
}