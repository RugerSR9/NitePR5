#ifndef NITEPR5_FS_STATE_H
#define NITEPR5_FS_STATE_H

#include <stddef.h>

int fs_mkdirs(void);
int fs_state_load(void);
int fs_state_save(void);
int fs_read_file(const char *path, char **out, size_t *n);
int fs_write_file(const char *path, const void *data, size_t n);
int fs_cheat_join(const char *filename, char *out, size_t cap);

#endif
