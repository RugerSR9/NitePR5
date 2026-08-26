#ifndef NITEPR5_CHEATS_H
#define NITEPR5_CHEATS_H

#include <stddef.h>

void cheats_clear(void);

/* Parse a GoldHEN object (name,id,version,process,mods). Stores a copy of json. */
int cheats_load_json(const char *json, size_t n);

/* Basename only under /data/nitepr5/cheats/. */
int cheats_load_filename(const char *filename);

int cheats_toggle(const char *name, int enabled);
int cheats_apply_enabled(void);
int cheats_replace_enabled(const char *const *names, int count);
void cheats_save_goldhen_file(void);

int cheats_find_mod(const char *name);

#endif
