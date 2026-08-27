#include "overlay.h"

#include <dlfcn.h>

#ifndef RTLD_NOLOAD
#define RTLD_NOLOAD 0x0004
#endif
#ifndef RTLD_NOW
#define RTLD_NOW 0x0002
#endif
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)0)
#endif

void *dlopen(const char *name, int flags) __attribute__((weak));
void *dlsym(void *handle, const char *name) __attribute__((weak));

void *overlay_dlsym(const char *const *mods, const char *sym)
{
    int i;

    if (sym == NULL) {
        return NULL;
    }
    if (dlopen && dlsym && mods) {
        for (i = 0; mods[i]; i++) {
            void *h = dlopen(mods[i], RTLD_NOLOAD | RTLD_NOW);
            void *p;
            if (h == NULL) {
                continue;
            }
            p = dlsym(h, sym);
            if (p) {
                return p;
            }
        }
    }
    if (dlsym) {
        void *p = dlsym(RTLD_DEFAULT, sym);
        if (p) {
            return p;
        }
    }
    return NULL;
}
