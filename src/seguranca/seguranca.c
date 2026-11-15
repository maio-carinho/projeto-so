#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L
#endif
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <conio.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "comum/utils.h"
#include "criptografia/cripto.h"
#include "seguranca/seguranca.h"

#define MAX_USUARIOS      8
#define MAX_GRUPOS        8
#define MAX_GRUPOS_EXTRAS 4
#define MAX_REGISTROS     64
#define TAM_REGISTRO      320
#define TAM_MIN_SENHA     4

typedef struct {
    int ativo;
    char nome[TAM_NOME];
    int gid_primario;
    int extras[MAX_GRUPOS_EXTRAS];
    int n_extras;
    unsigned char sal[TAM_SAL];
    unsigned char hash[TAM_HASH];
} Usuario;

typedef struct {
    int ativo;
    char nome[TAM_NOME];
} Grupo;

static Usuario usuarios[MAX_USUARIOS];
static Grupo grupos[MAX_GRUPOS];
static int uid_real = -1;       
static int uid_efetivo = -1;    
static GanchoNovoUsuario gancho_novo_usuario = NULL;

static char registros[MAX_REGISTROS][TAM_REGISTRO];
static int total_registros = 0;

static int usuario_valido(int uid) {
    return uid >= 0 && uid < MAX_USUARIOS && usuarios[uid].ativo;
}

static int grupo_valido(int gid) {
    return gid >= 0 && gid < MAX_GRUPOS && grupos[gid].ativo;
}

void seguranca_registrar(const char *formato, ...) {
    char mensagem[200];
    va_list args;
    va_start(args, formato);
    vsnprintf(mensagem, sizeof mensagem, formato, args);
    va_end(args);

    char quem[TAM_NOME + 8];
    if (!usuario_valido(uid_real)) snprintf(quem, sizeof quem, "-");
    else if (uid_efetivo != uid_real) snprintf(quem, sizeof quem, "%s(sudo)", usuarios[uid_real].nome);
    else snprintf(quem, sizeof quem, "%s", usuarios[uid_real].nome);

    total_registros++;
    snprintf(registros[(total_registros - 1) % MAX_REGISTROS], TAM_REGISTRO,
             "#%03d %-12s %s", total_registros, quem, mensagem);
}

static int entrada_interativa(void) {
#ifdef _WIN32
    return _isatty(0);              
#else
    return isatty(fileno(stdin));
#endif
}

static void ler_senha(const char *pergunta, char *destino, size_t capacidade) {
    printf("%s", pergunta);
    fflush(stdout);
    destino[0] = '\0';

    if (entrada_interativa()) {
#ifdef _WIN32
        size_t tamanho = 0;
        int c;
        while ((c = _getch()) != '\r' && c != '\n' && c != EOF) {
            if (c == '\b') {
                if (tamanho > 0) { tamanho--; printf("\b \b"); }
            } else if (tamanho + 1 < capacidade && c >= 32) {
                destino[tamanho++] = (char)c;
                putchar('*');
            }
            fflush(stdout);
        }
        destino[tamanho] = '\0';
        printf("\n");
        return;
#else
        struct termios original, silencioso;
        tcgetattr(STDIN_FILENO, &original);
        silencioso = original;
        silencioso.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &silencioso);
        if (fgets(destino, (int)capacidade, stdin)) destino[strcspn(destino, "\r\n")] = '\0';
        tcsetattr(STDIN_FILENO, TCSANOW, &original);
#endif
    } else if (fgets(destino, (int)capacidade, stdin)) {
        destino[strcspn(destino, "\r\n")] = '\0';
    }

    for (size_t i = 0; i < strlen(destino); i++) putchar('*');
    printf("\n");
}

static void definir_senha(Usuario *usuario, const char *senha) {
    cripto_sal_aleatorio(usuario->sal);
    cripto_derivar_chave(senha, usuario->sal, usuario->hash);
}

static int senha_confere(int uid, const char *senha) {
    unsigned char hash[TAM_HASH];
    cripto_derivar_chave(senha, usuarios[uid].sal, hash);
    return cripto_hashes_iguais(hash, usuarios[uid].hash);
}

static int nome_valido(const char *nome) {
    size_t tamanho = strlen(nome);
    if (tamanho == 0 || tamanho > 31) return 0;
    for (size_t i = 0; i < tamanho; i++)
        if (!isalnum((unsigned char)nome[i]) && nome[i] != '_' && nome[i] != '-') return 0;
    return 1;
}

void seguranca_init(void) {
    memset(usuarios, 0, sizeof usuarios);
    memset(grupos, 0, sizeof grupos);
    memset(registros, 0, sizeof registros);
    total_registros = 0;

    grupos[GID_ADMIN].ativo = 1;
    snprintf(grupos[GID_ADMIN].nome, TAM_NOME, "admin");

    usuarios[0].ativo = 1;
    snprintf(usuarios[0].nome, TAM_NOME, "root");
    usuarios[0].gid_primario = GID_ADMIN;
    definir_senha(&usuarios[0], "root");

    uid_real = uid_efetivo = 0;
}

int seguranca_ha_sessao(void) { return usuario_valido(uid_real); }
int seguranca_uid_atual(void) { return uid_efetivo < 0 ? 0 : uid_efetivo; }

int seguranca_gid_atual(void) {
    return usuario_valido(uid_efetivo) ? usuarios[uid_efetivo].gid_primario : GID_ADMIN;
}

ModoExecucao seguranca_modo(void) { return uid_efetivo == 0 ? MODO_KERNEL : MODO_USUARIO; }

const char *seguranca_nome_modo(void) {
    return seguranca_modo() == MODO_KERNEL ? "MODO_KERNEL" : "MODO_USUARIO";
}

const char *seguranca_prefixo_prompt(void) {
    static char prefixo[TAM_NOME + 4];
    if (!usuario_valido(uid_real)) return "(sem sessão) ";
    if (uid_real == 0) return "";
    snprintf(prefixo, sizeof prefixo, "%s:", usuarios[uid_real].nome);
    return prefixo;
}

const char *seguranca_nome_usuario(int uid) { return usuario_valido(uid) ? usuarios[uid].nome : "?"; }
const char *seguranca_nome_grupo(int gid) { return grupo_valido(gid) ? grupos[gid].nome : "?"; }

int seguranca_buscar_usuario(const char *nome) {
    for (int i = 0; i < MAX_USUARIOS; i++)
        if (usuarios[i].ativo && strcmp(usuarios[i].nome, nome) == 0) return i;
    return -1;
}

int seguranca_buscar_grupo(const char *nome) {
    for (int i = 0; i < MAX_GRUPOS; i++)
        if (grupos[i].ativo && strcmp(grupos[i].nome, nome) == 0) return i;
    return -1;
}

int seguranca_usuario_no_grupo(int uid, int gid) {
    if (!usuario_valido(uid)) return 0;
    if (usuarios[uid].gid_primario == gid) return 1;
    for (int i = 0; i < usuarios[uid].n_extras; i++)
        if (usuarios[uid].extras[i] == gid) return 1;
    return 0;
}

int seguranca_tem_permissao(int id_dono, int id_grupo, int permissoes, char acao) {
    if (uid_efetivo == 0) return 1;                
    if (!usuario_valido(uid_efetivo)) return 0;

    int bit = (acao == 'r') ? 4 : (acao == 'w') ? 2 : 1;
    int classe;                                     
    if (uid_efetivo == id_dono) classe = (permissoes >> 6) & 7;
    else if (seguranca_usuario_no_grupo(uid_efetivo, id_grupo)) classe = (permissoes >> 3) & 7;
    else classe = permissoes & 7;
    return (classe & bit) != 0;
}

void seguranca_formatar_permissoes(int permissoes, char saida[10]) {
    const char letras[] = "rwx";
    for (int i = 0; i < 9; i++)
        saida[i] = (permissoes & (1 << (8 - i))) ? letras[i % 3] : '-';
    saida[9] = '\0';
}

int seguranca_exigir_kernel(const char *operacao) {
    if (seguranca_modo() == MODO_KERNEL) return 0;
    const char *nome = seguranca_nome_usuario(uid_efetivo);
    printf("[SISTEMA DE SEGURANÇA - OPERAÇÃO CRÍTICA]\n"
           "Operação : %s\n"
           "Exige    : MODO_KERNEL (superusuário)\n"
           "Atual    : '%s' (Grupo: %s) | %s\n\n"
           "[NEGADO]: O usuário '%s' não pode executar '%s' sem privilégios elevados.\n"
           "Tentativa não autorizada registrada nos logs do sistema.\n",
           operacao, nome, seguranca_nome_grupo(seguranca_gid_atual()), seguranca_nome_modo(),
           nome, operacao);
    seguranca_registrar("OPERAÇÃO CRÍTICA NEGADA: '%s' tentou executar '%s'", nome, operacao);
    return ERRO_JA_EXIBIDO;
}

int seguranca_elevar(void) {
    if (!usuario_valido(uid_real)) return reg_erro("Nenhuma sessão ativa.");
    if (uid_real == 0) return 0;                    
    if (!seguranca_usuario_no_grupo(uid_real, GID_ADMIN)) {
        seguranca_registrar("SUDO NEGADO: '%s' não pertence ao grupo admin", usuarios[uid_real].nome);
        return reg_erro("O usuário '%s' não pertence ao grupo 'admin' e não pode usar sudo.",
                        usuarios[uid_real].nome);
    }
    char pergunta[TAM_NOME + 32], senha[128];
    snprintf(pergunta, sizeof pergunta, "Senha de '%s': ", usuarios[uid_real].nome);
    ler_senha(pergunta, senha, sizeof senha);
    if (!senha_confere(uid_real, senha)) {
        seguranca_registrar("SUDO FALHOU: senha incorreta de '%s'", usuarios[uid_real].nome);
        return reg_erro("Senha incorreta.");
    }
    uid_efetivo = 0;
    return 0;
}

void seguranca_baixar(void) {
    uid_efetivo = uid_real;
}

int cmd_login(int argc, char **argv) {
    const char *nome = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) nome = argv[++i];
        else if (argv[i][0] != '-' && !nome) nome = argv[i];
    }
    if (!nome) return reg_erro("Uso: login --user <nome>");

    int uid = seguranca_buscar_usuario(nome);
    char senha[128];
    ler_senha("Senha: ", senha, sizeof senha);
    if (uid < 0 || !senha_confere(uid, senha)) {
        seguranca_registrar("LOGIN FALHOU: usuário '%s'", nome);
        return reg_erro("Usuário ou senha inválidos.");
    }

    uid_real = uid_efetivo = uid;
    seguranca_registrar("LOGIN: usuário '%s' (%s)", nome, seguranca_nome_modo());
    printf("Autenticado com sucesso. Usuário: '%s' | Grupo: '%s' | Modo: %s\n",
           nome, seguranca_nome_grupo(usuarios[uid].gid_primario), seguranca_nome_modo());
    return 0;
}

int cmd_logout(int argc, char **argv) {
    (void)argc; (void)argv;
    seguranca_registrar("LOGOUT");
    uid_real = uid_efetivo = -1;
    printf("Sessão encerrada. Use: login --user <nome>\n");
    return 0;
}

int cmd_whoami(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!usuario_valido(uid_real)) {
        printf("Nenhuma sessão ativa.\n");
        return 0;
    }
    printf("Usuário: '%s' | Grupo: '%s' | Modo: %s\n", usuarios[uid_real].nome,
           seguranca_nome_grupo(usuarios[uid_real].gid_primario), seguranca_nome_modo());
    return 0;
}

int cmd_groupadd(int argc, char **argv) {
    if (argc < 1) return reg_erro("Uso: groupadd <nome>");
    if (!nome_valido(argv[0])) return reg_erro("Nome inválido (use letras, números, '_' ou '-', até 31 caracteres).");
    if (seguranca_buscar_grupo(argv[0]) >= 0) return reg_erro("O grupo '%s' já existe.", argv[0]);
    for (int gid = 0; gid < MAX_GRUPOS; gid++) {
        if (grupos[gid].ativo) continue;
        grupos[gid].ativo = 1;
        snprintf(grupos[gid].nome, TAM_NOME, "%s", argv[0]);
        seguranca_registrar("GRUPO CRIADO: '%s' (GID %d)", argv[0], gid);
        printf("Grupo '%s' criado (GID %d).\n", argv[0], gid);
        return 0;
    }
    return reg_erro("Limite de %d grupos atingido.", MAX_GRUPOS);
}

int cmd_useradd(int argc, char **argv) {
    const char *nome = NULL, *nome_grupo = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--group") == 0 && i + 1 < argc) nome_grupo = argv[++i];
        else if (argv[i][0] != '-' && !nome) nome = argv[i];
    }
    if (!nome || !nome_grupo) return reg_erro("Uso: useradd <nome> --group <grupo>");
    if (!nome_valido(nome)) return reg_erro("Nome inválido (use letras, números, '_' ou '-', até 31 caracteres).");
    if (seguranca_buscar_usuario(nome) >= 0) return reg_erro("O usuário '%s' já existe.", nome);
    int gid = seguranca_buscar_grupo(nome_grupo);
    if (gid < 0) return reg_erro("O grupo '%s' não existe (crie com: groupadd %s).", nome_grupo, nome_grupo);

    int uid = -1;
    for (int i = 0; i < MAX_USUARIOS && uid < 0; i++)
        if (!usuarios[i].ativo) uid = i;
    if (uid < 0) return reg_erro("Limite de %d usuários atingido.", MAX_USUARIOS);

    char senha[128];
    ler_senha("Senha: ", senha, sizeof senha);
    if (strlen(senha) < TAM_MIN_SENHA)
        return reg_erro("Senha muito curta (mínimo de %d caracteres).", TAM_MIN_SENHA);

    memset(&usuarios[uid], 0, sizeof usuarios[uid]);
    usuarios[uid].ativo = 1;
    snprintf(usuarios[uid].nome, TAM_NOME, "%s", nome);
    usuarios[uid].gid_primario = gid;
    definir_senha(&usuarios[uid], senha);

    if (gancho_novo_usuario) gancho_novo_usuario(nome, uid, gid);
    seguranca_registrar("USUÁRIO CRIADO: '%s' (UID %d, grupo '%s')", nome, uid, nome_grupo);
    printf("Usuário '%s' criado (UID %d, grupo '%s'). Diretório pessoal: /home/%s\n",
           nome, uid, nome_grupo, nome);
    return 0;
}

int cmd_usermod(int argc, char **argv) {
    const char *nome = NULL, *nome_grupo = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--add-group") == 0 && i + 1 < argc) nome_grupo = argv[++i];
        else if (argv[i][0] != '-' && !nome) nome = argv[i];
    }
    if (!nome || !nome_grupo) return reg_erro("Uso: usermod <usuário> --add-group <grupo>");
    int uid = seguranca_buscar_usuario(nome);
    if (uid < 0) return reg_erro("O usuário '%s' não existe.", nome);
    int gid = seguranca_buscar_grupo(nome_grupo);
    if (gid < 0) return reg_erro("O grupo '%s' não existe.", nome_grupo);
    if (seguranca_usuario_no_grupo(uid, gid)) return reg_erro("'%s' já pertence ao grupo '%s'.", nome, nome_grupo);
    if (usuarios[uid].n_extras >= MAX_GRUPOS_EXTRAS)
        return reg_erro("Limite de %d grupos extras por usuário atingido.", MAX_GRUPOS_EXTRAS);

    usuarios[uid].extras[usuarios[uid].n_extras++] = gid;
    seguranca_registrar("GRUPO ADICIONADO: '%s' agora também está em '%s'", nome, nome_grupo);
    printf("Usuário '%s' adicionado ao grupo '%s'.\n", nome, nome_grupo);
    return 0;
}

int cmd_passwd(int argc, char **argv) {
    int uid = (argc >= 1) ? seguranca_buscar_usuario(argv[0]) : uid_real;
    if (uid < 0) return reg_erro("O usuário '%s' não existe.", argv[0]);
    if (uid != uid_real && seguranca_modo() != MODO_KERNEL)
        return seguranca_exigir_kernel("passwd (de outro usuário)");

    char senha[128], nova[128];
    if (seguranca_modo() != MODO_KERNEL) {
        ler_senha("Senha atual: ", senha, sizeof senha);
        if (!senha_confere(uid, senha)) {
            seguranca_registrar("TROCA DE SENHA FALHOU: senha atual incorreta de '%s'", usuarios[uid].nome);
            return reg_erro("Senha atual incorreta.");
        }
    }
    ler_senha("Nova senha: ", nova, sizeof nova);
    if (strlen(nova) < TAM_MIN_SENHA)
        return reg_erro("Senha muito curta (mínimo de %d caracteres).", TAM_MIN_SENHA);

    definir_senha(&usuarios[uid], nova);
    seguranca_registrar("SENHA ALTERADA: usuário '%s'", usuarios[uid].nome);
    printf("Senha de '%s' alterada.\n", usuarios[uid].nome);
    return 0;
}

int cmd_users(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("%-4s %-12s %-12s %-16s %s\n", "UID", "Usuário", "Grupo", "Grupos extras",
           "Hash da senha (SHA-256 com sal)");
    for (int uid = 0; uid < MAX_USUARIOS; uid++) {
        if (!usuarios[uid].ativo) continue;
        char extras[TAM_NOME * 2] = "-", hash[TAM_HASH * 2 + 1];
        for (int i = 0; i < usuarios[uid].n_extras; i++) {
            size_t usado = (i == 0) ? 0 : strlen(extras);
            snprintf(extras + usado, sizeof extras - usado, "%s%s", i ? "," : "",
                     seguranca_nome_grupo(usuarios[uid].extras[i]));
        }
        sha256_hex(usuarios[uid].hash, hash);
        printf("%-4d %-12s %-12s %-16s %.16s...\n", uid, usuarios[uid].nome,
               seguranca_nome_grupo(usuarios[uid].gid_primario), extras, hash);
    }
    printf("\nGrupos:");
    for (int gid = 0; gid < MAX_GRUPOS; gid++)
        if (grupos[gid].ativo) printf(" %s(%d)", grupos[gid].nome, gid);
    printf("\n");
    return 0;
}

int cmd_audit_log(int argc, char **argv) {
    int quantidade = 20;
    if (argc >= 1) {
        quantidade = atoi(argv[0]);
        if (quantidade < 1) return reg_erro("Uso: audit_log [quantidade]");
    }
    int guardados = total_registros < MAX_REGISTROS ? total_registros : MAX_REGISTROS;
    if (quantidade > guardados) quantidade = guardados;
    if (quantidade == 0) {
        printf("(registro de auditoria vazio)\n");
        return 0;
    }
    printf("[REGISTRO DE AUDITORIA] (últimos %d de %d eventos)\n", quantidade, total_registros);
    for (int n = total_registros - quantidade + 1; n <= total_registros; n++)
        printf("%s\n", registros[(n - 1) % MAX_REGISTROS]);
    return 0;
}

void seguranca_registrar_gancho_novo_usuario(GanchoNovoUsuario gancho) {
    gancho_novo_usuario = gancho;
}