#ifndef FS_H
#define FS_H

#include <stddef.h>
#include "comum/utils.h"

typedef struct {
    char caminho[TAM_PATH];
    long tamanho;
    int inode;
    int n_blocos;
    int blocos[TOTAL_BLOCOS];
    int criptografado;
} InfoArquivo;

void fs_init(void);
void fs_shutdown(void);
void fs_cwd_path(char *out, size_t cap);

int fs_listar_arquivos(const char *caminho, InfoArquivo *saida, int maximo);
int fs_info_arquivo(const char *caminho, InfoArquivo *info);
int fs_criar_diretorio_pessoal(const char *nome_usuario, int uid, int gid);

int cmd_mkdir(int argc, char **argv);
int cmd_create(int argc, char **argv);
int cmd_write_file(int argc, char **argv);
int cmd_ls(int argc, char **argv);
int cmd_cd(int argc, char **argv);
int cmd_tree(int argc, char **argv);
int cmd_read(int argc, char **argv);
int cmd_update(int argc, char **argv);
int cmd_rm(int argc, char **argv);
int cmd_fs_status(int argc, char **argv);

int cmd_read_file(int argc, char **argv);
int cmd_stat(int argc, char **argv);
int cmd_chmod(int argc, char **argv);
int cmd_chown(int argc, char **argv);
int cmd_encrypt_file(int argc, char **argv);
int cmd_decrypt_file(int argc, char **argv);

#endif