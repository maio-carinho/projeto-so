#ifndef FS_H
#define FS_H

#include <stddef.h>

void fs_init(void);
void fs_shutdown(void);
void fs_cwd_path(char *out, size_t cap);

int cmd_mkdir(int argc, char **argv);
int cmd_create(int argc, char **argv);
int cmd_ls(int argc, char **argv);
int cmd_cd(int argc, char **argv);
int cmd_tree(int argc, char **argv);
int cmd_read(int argc, char **argv);
int cmd_update(int argc, char **argv);
int cmd_rm(int argc, char **argv);
int cmd_fs_status(int argc, char **argv);

#endif