# Johns elfldr (websrv)

Vendored from [ps5-payload-dev/websrv](https://github.com/ps5-payload-dev/websrv) `src/ps5/{pt.c,pt.h,elfldr.c,elfldr.h}` (GPL-3.0, Copyright John Törnblom).

NitePR5 uses **`pt_attach` + `elfldr_exec`** only: map overlay.elf into a live `eboot.bin`, push the original RIP, jump to Johns CRT, then `pt_detach`. That is the etaHEN FPS / Inject_Toolbox path.

`elfldr_spawn` (new process via SceSpZeroConf) is compiled out (`NITEPR5_ELFLDR_INJECT_ONLY`). Do not send overlay.elf to elfldr **9021**.
