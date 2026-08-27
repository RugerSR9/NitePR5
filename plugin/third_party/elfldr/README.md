# Johns elfldr (websrv)

Vendored from [ps5-payload-dev/websrv](https://github.com/ps5-payload-dev/websrv) `src/ps5/{pt.c,pt.h,elfldr.c,elfldr.h}` (GPL-3.0, Copyright John Törnblom).

NitePR5 uses **`pt_attach` + `elfldr_inject`**: map overlay.elf, run a tiny **int3 stager** that calls `scePthreadCreate`, restore the game thread, `pt_detach`. Do not `pt_call` pthread_create (it follows the new thread). `elfldr_exec` is unused for overlay.

`elfldr_spawn` (new process via SceSpZeroConf) is compiled out (`NITEPR5_ELFLDR_INJECT_ONLY`). Do not send overlay.elf to elfldr **9021**.
