#ifndef SEGURANCA_H
#define SEGURANCA_H

#include "comum/utils.h"

typedef enum { MODO_USUARIO, MODO_KERNEL } ModoExecucao;

#define GID_ADMIN 0     

void seguranca_init(void);

typedef int (*GanchoNovoUsuario)(const char *nome, int uid, int gid);
void seguranca_registrar_gancho_novo_usuario(GanchoNovoUsuario gancho);

int seguranca_ha_sessao(void);
int seguranca_uid_atual(void);
int seguranca_gid_atual(void);
ModoExecucao seguranca_modo(void);
const char *seguranca_nome_modo(void);
const char *seguranca_prefixo_prompt(void);

const char *seguranca_nome_usuario(int uid);
const char *seguranca_nome_grupo(int gid);
int seguranca_buscar_usuario(const char *nome);
int seguranca_buscar_grupo(const char *nome);
int seguranca_usuario_no_grupo(int uid, int gid);
int seguranca_tem_permissao(int id_dono, int id_grupo, int permissoes, char acao);
void seguranca_formatar_permissoes(int permissoes, char saida[10]);
void seguranca_registrar(const char *formato, ...);
int seguranca_exigir_kernel(const char *operacao);
int seguranca_elevar(void);
void seguranca_baixar(void);

int cmd_login(int argc, char **argv);
int cmd_logout(int argc, char **argv);
int cmd_whoami(int argc, char **argv);
int cmd_useradd(int argc, char **argv);
int cmd_groupadd(int argc, char **argv);
int cmd_usermod(int argc, char **argv);
int cmd_passwd(int argc, char **argv);
int cmd_users(int argc, char **argv);
int cmd_audit_log(int argc, char **argv);

#endif