# Johns elfldr (websrv)

Vendored from [ps5-payload-dev/websrv](https://github.com/ps5-payload-dev/websrv) `src/ps5/{pt.c,pt.h,elfldr.c,elfldr.h}` (GPL-3.0, Copyright John Törnblom).

NitePR5 uses **`pt_attach` + `elfldr_inject`**: map overlay.elf into a live `eboot.bin`, remote `scePthreadCreate` of Johns CRT, then `pt_detach`. The game thread RIP is not hijacked. `elfldr_exec` (push RIP) is kept but unused for overlay.

`elfldr_spawn` (new process via SceSpZeroConf) is compiled out (`NITEPR5_ELFLDR_INJECT_ONLY`). Do not send overlay.elf to elfldr **9021**.
