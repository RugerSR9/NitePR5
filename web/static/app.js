/* NitePR5 Phase 1–3 UI — fetch('/api/...') only. No second protocol. */
(function () {
  "use strict";

  const HEX_N = 512;
  const ROW_BYTES = 16;
  const ROW_COUNT = HEX_N / ROW_BYTES; // 32
  const HEX_MS = 250; // 4 Hz
  const RESULTS_MAX = 256;
  const WATCH_MAX = 64;
  const WATCH_MS = 100; // 10 Hz
  const WATCH_N = 4;
  const FREEZE_MAX = 32;
  const FREEZE_MS = 67; // ~15 Hz
  const FREEZE_N = 4;

  const el = {
    host: document.getElementById("host"),
    connectForm: document.getElementById("connect-form"),
    btnConnect: document.getElementById("btn-connect"),
    btnDiscover: document.getElementById("btn-discover"),
    btnDisconnect: document.getElementById("btn-disconnect"),
    connectStatus: document.getElementById("connect-status"),
    fgStatus: document.getElementById("fg-status"),
    procBody: document.getElementById("proc-body"),
    btnAttach: document.getElementById("btn-attach"),
    attachStatus: document.getElementById("attach-status"),
    mapsBody: document.getElementById("maps-body"),
    hexMeta: document.getElementById("hex-meta"),
    btnPause: document.getElementById("btn-pause"),
    hexStatus: document.getElementById("hex-status"),
    hexBody: document.getElementById("hex-body"),
    hexSel: document.getElementById("hex-sel"),
    hexPokeU32: document.getElementById("hex-poke-u32"),
    btnWatchHex: document.getElementById("btn-watch-hex"),
    btnFreezeHex: document.getElementById("btn-freeze-hex"),
    btnPokeU32: document.getElementById("btn-poke-u32"),
    scanValue: document.getElementById("scan-value"),
    scanUnknown: document.getElementById("scan-unknown"),
    scanNextCompare: document.getElementById("scan-next-compare"),
    btnScanFirst: document.getElementById("btn-scan-first"),
    btnScanNext: document.getElementById("btn-scan-next"),
    btnScanUndo: document.getElementById("btn-scan-undo"),
    scanCount: document.getElementById("scan-count"),
    scanStatus: document.getElementById("scan-status"),
    scanHint: document.getElementById("scan-hint"),
    scanBody: document.getElementById("scan-body"),
    btnWatchAdd: document.getElementById("btn-watch-add"),
    watchCap: document.getElementById("watch-cap"),
    watchHint: document.getElementById("watch-hint"),
    watchStatus: document.getElementById("watch-status"),
    watchBody: document.getElementById("watch-body"),
    freezeCap: document.getElementById("freeze-cap"),
    freezeHint: document.getElementById("freeze-hint"),
    freezeStatus: document.getElementById("freeze-status"),
    freezeBody: document.getElementById("freeze-body"),
    cheatFile: document.getElementById("cheat-file"),
    btnCheatLoad: document.getElementById("btn-cheat-load"),
    btnCheatSave: document.getElementById("btn-cheat-save"),
    btnCheatSaveFreezes: document.getElementById("btn-cheat-save-freezes"),
    cheatFilename: document.getElementById("cheat-filename"),
    cheatId: document.getElementById("cheat-id"),
    cheatVersion: document.getElementById("cheat-version"),
    cheatGameName: document.getElementById("cheat-game-name"),
    cheatStatus: document.getElementById("cheat-status"),
    cheatMods: document.getElementById("cheat-mods"),
  };

  const state = {
    connected: false,
    selectedPid: null,
    foregroundPid: null,
    foreground: null,
    attachedPid: null,
    maps: [],
    peepholeAddr: 0,
    selectedAddr: null,
    hexData: "",
    paused: false,
    timer: null,
    inflight: false,
    hasScan: false,
    scanBusy: false,
    scanGen: 0,
    scanCount: null,
    watches: [],
    watchValues: {},
    watchTimer: null,
    watchInflight: false,
    freezes: [],
    freezeTimer: null,
    freezeInflight: false,
    cheatLoaded: false,
  };

  function setStatus(node, text, isErr) {
    if (!node) return;
    node.textContent = text || "";
    node.classList.toggle("err", !!isErr);
  }

  function hexAddr(n) {
    return "0x" + n.toString(16).toUpperCase().padStart(10, "0");
  }

  async function api(path, opts) {
    const res = await fetch(path, opts);
    const text = await res.text();
    let body = null;
    if (text) {
      try {
        body = JSON.parse(text);
      } catch {
        body = { detail: text };
      }
    }
    if (!res.ok) {
      const detail = body && (body.detail || body.message);
      throw new Error(typeof detail === "string" ? detail : "HTTP " + res.status);
    }
    return body;
  }

  function apiPost(path, obj) {
    return api(path, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(obj),
    });
  }

  function isScanActiveError(err) {
    const m = String(err && err.message ? err.message : err);
    return (
      m.indexOf("ScanActive") !== -1 ||
      m.indexOf("scan in flight") !== -1 ||
      m.indexOf("scan operation already") !== -1
    );
  }

  function preferPid(processes, fg) {
    const eboot = processes.find(function (p) { return p.name === "eboot.bin"; });
    if (eboot) return eboot.pid;
    const cusa = processes.find(function (p) {
      return String(p.titleid || "").indexOf("CUSA") === 0;
    });
    if (cusa) return cusa.pid;
    if (fg && fg.pid) return fg.pid;
    return processes.length ? processes[0].pid : null;
  }

  function firstWritable(maps) {
    return maps.find(function (m) {
      return (m.prot & 2) || (m.perms && m.perms.indexOf("w") !== -1);
    }) || maps[0] || null;
  }

  function ebootBase(maps) {
    let start = null;
    (maps || []).forEach(function (m) {
      if (m.name === "eboot.bin") {
        if (start == null || m.start < start) start = m.start;
      }
    });
    return start;
  }

  function renderProcesses(processes, fgPid, selectedPid) {
    el.procBody.innerHTML = "";
    processes.forEach(function (p) {
      const tr = document.createElement("tr");
      if (p.pid === fgPid) tr.classList.add("foreground");
      if (p.pid === selectedPid) tr.classList.add("selected");
      tr.dataset.pid = String(p.pid);
      tr.innerHTML =
        "<td>" + p.pid + "</td>" +
        "<td>" + escapeHtml(p.name || "") + "</td>" +
        "<td>" + escapeHtml(p.titleid || "") + "</td>";
      tr.addEventListener("click", function () {
        state.selectedPid = p.pid;
        renderProcesses(processes, fgPid, p.pid);
        el.btnAttach.disabled = !state.connected;
      });
      el.procBody.appendChild(tr);
    });
  }

  function escapeHtml(s) {
    return String(s)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  function renderMaps(maps, baseStart) {
    el.mapsBody.innerHTML = "";
    maps.forEach(function (m) {
      const tr = document.createElement("tr");
      if (baseStart != null && m.start === baseStart) tr.classList.add("selected");
      tr.innerHTML =
        "<td>" + escapeHtml(m.name || "") + "</td>" +
        "<td class=\"mono\">" + hexAddr(m.start) + "</td>" +
        "<td class=\"mono\">" + hexAddr(m.end) + "</td>" +
        "<td>" + escapeHtml(m.perms || "") + "</td>" +
        "<td>" + m.size + "</td>";
      el.mapsBody.appendChild(tr);
    });
  }

  function bytesFromHex(hex) {
    const out = [];
    const s = hex || "";
    for (let i = 0; i + 1 < s.length; i += 2) {
      out.push(parseInt(s.slice(i, i + 2), 16));
    }
    return out;
  }

  function asciiChar(b) {
    return b >= 32 && b < 127 ? String.fromCharCode(b) : ".";
  }

  function bytesToHex(bytes) {
    return (bytes || []).map(function (b) {
      return (b & 0xff).toString(16).toUpperCase().padStart(2, "0");
    }).join("");
  }

  function u32ToLeHex(n) {
    n = n >>> 0;
    return bytesToHex([n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, (n >>> 24) & 0xff]);
  }

  function parsePokeHex(raw) {
    let s = String(raw || "").trim().replace(/\s+/g, "");
    if (/^0x/i.test(s)) s = s.slice(2);
    if (!/^[0-9a-fA-F]{2,8}$/.test(s) || s.length % 2 !== 0) return null;
    return s.toUpperCase();
  }

  function peekHexAt(addr, n) {
    const data = state.hexData || "";
    const off = addr - state.peepholeAddr;
    if (off < 0 || n <= 0) return "";
    const start = off * 2;
    const end = start + n * 2;
    if (start >= data.length || start < 0) return "";
    return data.slice(start, Math.min(end, data.length)).toUpperCase();
  }

  function pokeTargetAddr() {
    if (state.selectedAddr != null) return state.selectedAddr;
    return state.peepholeAddr;
  }

  function formatLive(hex, n) {
    const h = String(hex || "").toUpperCase();
    if (!h) return "—";
    if (n >= 4 && h.length >= 8) {
      return h + " / " + u32leFromHex(h);
    }
    return h;
  }

  function updateHexSel() {
    if (!el.hexSel) return;
    if (state.selectedAddr == null) {
      el.hexSel.textContent = "";
      return;
    }
    el.hexSel.textContent = "sel " + hexAddr(state.selectedAddr);
  }

  function renderHex(addr, hexStr, n) {
    const bytes = bytesFromHex(hexStr);
    state.hexData = hexStr || "";
    const rows = [];
    for (let r = 0; r < ROW_COUNT; r++) {
      const rowAddr = addr + r * ROW_BYTES;
      const slice = bytes.slice(r * ROW_BYTES, r * ROW_BYTES + ROW_BYTES);
      const hexParts = [];
      let ascii = "";
      for (let i = 0; i < ROW_BYTES; i++) {
        const b = slice[i];
        const cellAddr = rowAddr + i;
        if (b == null) {
          hexParts.push("  ");
          ascii += " ";
        } else {
          const hx = b.toString(16).toUpperCase().padStart(2, "0");
          const sel = state.selectedAddr === cellAddr ? " selected" : "";
          hexParts.push(
            "<span class=\"byte" + sel + "\" data-addr=\"" + cellAddr + "\">" + hx + "</span>"
          );
          ascii += asciiChar(b);
        }
      }
      rows.push(
        "<tr>" +
          "<td class=\"addr\">" + hexAddr(rowAddr) + "</td>" +
          "<td class=\"bytes\">" + hexParts.join(" ") + "</td>" +
          "<td class=\"ascii\">" + escapeHtml(ascii) + "</td>" +
        "</tr>"
      );
    }
    el.hexBody.innerHTML = rows.join("");
    el.hexMeta.textContent = hexAddr(addr) + " · " + n + " bytes · 16×" + ROW_COUNT;
    updateHexSel();
  }

  function parseScanValue(raw) {
    const s = String(raw || "").trim();
    if (!s) return null;
    if (/^0x[0-9a-fA-F]+$/i.test(s)) {
      const n = parseInt(s, 16);
      return Number.isFinite(n) ? n >>> 0 : null;
    }
    if (/^\d+$/.test(s)) {
      const n = parseInt(s, 10);
      return Number.isFinite(n) ? n : null;
    }
    return null;
  }

  function u32leFromHex(hex) {
    const bytes = bytesFromHex(hex);
    if (bytes.length < 4) return 0;
    return (bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24)) >>> 0;
  }

  function syncWatchFreezeButtons() {
    const attached = state.connected && state.attachedPid != null;
    const nW = (state.watches || []).length;
    const nF = (state.freezes || []).length;
    const fullW = nW >= WATCH_MAX;
    const fullF = nF >= FREEZE_MAX;
    const busy = state.scanBusy;
    if (el.watchCap) el.watchCap.textContent = nW + "/" + WATCH_MAX;
    if (el.freezeCap) el.freezeCap.textContent = nF + "/" + FREEZE_MAX;
    if (el.watchHint) {
      el.watchHint.textContent = fullW ? "64/64 — remove one to add" : "";
    }
    if (el.freezeHint) {
      el.freezeHint.textContent = fullF ? "32/32 — remove one to add" : "";
    }
    if (el.btnWatchAdd) el.btnWatchAdd.disabled = !attached || busy || fullW;
    if (el.btnWatchHex) el.btnWatchHex.disabled = !attached || busy || fullW;
    if (el.btnFreezeHex) el.btnFreezeHex.disabled = !attached || busy || fullF;
    if (el.btnPokeU32) el.btnPokeU32.disabled = !attached || busy;
    if (el.btnCheatLoad) el.btnCheatLoad.disabled = !state.connected;
    if (el.btnCheatSave) el.btnCheatSave.disabled = !state.connected;
    if (el.btnCheatSaveFreezes) {
      el.btnCheatSaveFreezes.disabled = !attached || nF === 0;
    }
    const watchBtns = el.scanBody ? el.scanBody.querySelectorAll("button.btn-watch-hit") : [];
    for (let i = 0; i < watchBtns.length; i++) {
      watchBtns[i].disabled = !attached || busy || fullW;
    }
  }

  function syncScanButtons() {
    const attached = state.connected && state.attachedPid != null;
    el.btnScanFirst.disabled = !attached || state.scanBusy;
    el.btnScanNext.disabled = !state.hasScan || state.scanBusy;
    el.btnScanUndo.disabled = !state.hasScan || state.scanBusy;
    if (el.btnAttach) {
      el.btnAttach.disabled = !state.connected || state.selectedPid == null || state.scanBusy;
    }
    syncWatchFreezeButtons();
  }

  function clearScanUi() {
    state.hasScan = false;
    state.scanCount = null;
    el.scanCount.textContent = "Count: —";
    el.scanHint.textContent = "";
    el.scanBody.innerHTML = "";
    setStatus(el.scanStatus, "");
    syncScanButtons();
  }

  function renderScanResults(results) {
    el.scanBody.innerHTML = "";
    (results || []).forEach(function (row) {
      const addr = row.addr;
      const tr = document.createElement("tr");
      tr.innerHTML =
        "<td class=\"mono\">" + hexAddr(addr) + "</td>" +
        "<td class=\"mono\">" + u32leFromHex(row.current) + "</td>" +
        "<td><button type=\"button\" class=\"btn-row btn-watch-hit\">Watch</button></td>";
      tr.addEventListener("click", function () {
        if (state.attachedPid == null) return;
        state.peepholeAddr = addr;
        state.selectedAddr = addr;
        updateHexSel();
        if (!state.paused && !document.hidden) {
          tickHex();
        }
      });
      const btn = tr.querySelector("button");
      btn.addEventListener("click", function (ev) {
        ev.stopPropagation();
        addWatch(addr, WATCH_N, "");
      });
      el.scanBody.appendChild(tr);
    });
    syncWatchFreezeButtons();
  }

  async function applyScanCount(count, ended, gen) {
    if (ended || count == null) {
      clearScanUi();
      setStatus(el.scanStatus, "Scan ended");
      return;
    }
    state.hasScan = true;
    state.scanCount = count;
    el.scanCount.textContent = "Count: " + count;
    syncScanButtons();
    if (count > RESULTS_MAX) {
      el.scanBody.innerHTML = "";
      el.scanHint.textContent =
        count + " matches — narrow in-game, then Next Scan";
      setStatus(el.scanStatus, "");
      return;
    }
    el.scanHint.textContent = "";
    if (count === 0) {
      el.scanBody.innerHTML = "";
      setStatus(el.scanStatus, "");
      return;
    }
    const body = await api("/api/scan/results?limit=" + RESULTS_MAX);
    if (gen !== state.scanGen) return;
    renderScanResults(body.results || []);
    setStatus(el.scanStatus, "");
  }

  function scanPayload(kind) {
    if (kind === "first") {
      const unknown = !!el.scanUnknown.checked;
      const payload = { compare: "exact", unknown: unknown };
      if (!unknown) {
        const value = parseScanValue(el.scanValue.value);
        if (value == null) return null;
        payload.value = value;
      }
      return payload;
    }
    if (kind === "next") {
      const compare = el.scanNextCompare.value || "exact";
      const payload = { compare: compare };
      if (compare === "exact") {
        const value = parseScanValue(el.scanValue.value);
        if (value == null) return null;
        payload.value = value;
      }
      return payload;
    }
    return {};
  }

  async function runScan(kind) {
    if (state.scanBusy) return;
    const payload = scanPayload(kind);
    if (kind !== "undo" && payload == null) {
      setStatus(el.scanStatus, "Enter a value (decimal or 0x hex)", true);
      return;
    }
    const gen = state.scanGen;
    state.scanBusy = true;
    syncScanButtons();
    stopHexPoll();
    stopWatchPoll();
    stopFreezeTick();
    setStatus(el.hexStatus, "paused (scan)");
    setStatus(el.scanStatus, kind === "undo" ? "Undoing…" : "Scanning…");
    try {
      let body;
      if (kind === "first") {
        body = await api("/api/scan/start", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(payload),
        });
      } else if (kind === "next") {
        body = await api("/api/scan/next", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(payload),
        });
      } else {
        body = await api("/api/scan/undo", { method: "POST" });
      }
      if (gen !== state.scanGen) return;
      await applyScanCount(body.count, kind === "undo" && !!body.ended, gen);
    } catch (err) {
      if (gen !== state.scanGen) return;
      setStatus(el.scanStatus, String(err.message || err), true);
    } finally {
      if (gen === state.scanGen) {
        state.scanBusy = false;
        syncScanButtons();
        if (!state.paused && state.attachedPid != null && !document.hidden) {
          startHexPoll();
          startWatchPoll();
          startFreezeTick();
        }
      }
    }
  }

  function stopHexPoll() {
    if (state.timer != null) {
      clearInterval(state.timer);
      state.timer = null;
    }
  }

  function startHexPoll() {
    stopHexPoll();
    if (state.paused || state.scanBusy || document.hidden || state.attachedPid == null) return;
    tickHex();
    state.timer = setInterval(tickHex, HEX_MS);
  }

  function resetTargetUi() {
    /* Match Session.connect -> disconnect: drop logical attach, maps, peephole. */
    stopHexPoll();
    stopWatchPoll();
    stopFreezeTick();
    state.attachedPid = null;
    state.peepholeAddr = 0;
    state.selectedAddr = null;
    state.hexData = "";
    state.maps = [];
    state.paused = false;
    state.scanGen += 1;
    state.scanBusy = false;
    state.watches = [];
    state.watchValues = {};
    state.freezes = [];
    state.cheatLoaded = false;
    el.btnPause.textContent = "Pause";
    el.btnPause.disabled = true;
    el.mapsBody.innerHTML = "";
    el.hexBody.innerHTML = "";
    el.hexMeta.textContent = "addr — · 512 bytes";
    if (el.watchBody) el.watchBody.innerHTML = "";
    if (el.freezeBody) el.freezeBody.innerHTML = "";
    if (el.cheatMods) el.cheatMods.innerHTML = "";
    updateHexSel();
    setStatus(el.attachStatus, "");
    setStatus(el.hexStatus, "");
    setStatus(el.watchStatus, "");
    setStatus(el.freezeStatus, "");
    clearScanUi();
  }

  async function tickHex() {
    if (state.inflight || state.paused || state.scanBusy || document.hidden || state.attachedPid == null) {
      return;
    }
    const pid = state.attachedPid;
    const addr = state.peepholeAddr;
    state.inflight = true;
    try {
      const q =
        "/api/read?addr=" + encodeURIComponent(String(addr)) +
        "&n=" + HEX_N +
        "&pid=" + encodeURIComponent(String(pid));
      const body = await api(q);
      if (state.attachedPid !== pid || state.peepholeAddr !== addr) {
        return;
      }
      renderHex(body.addr, body.data, body.n);
      setStatus(el.hexStatus, state.paused ? "paused" : "peephole ~4 Hz", false);
    } catch (err) {
      if (state.attachedPid !== pid) {
        return;
      }
      if (isScanActiveError(err)) {
        return;
      }
      setStatus(el.hexStatus, String(err.message || err), true);
    } finally {
      state.inflight = false;
    }
  }

  async function onDiscover() {
    setStatus(el.connectStatus, "Discovering…");
    try {
      const body = await api("/api/discover", { method: "POST" });
      const hosts = body.hosts || [];
      if (!hosts.length) {
        setStatus(el.connectStatus, "No hosts on UDP 1010", true);
        return;
      }
      el.host.value = hosts[0];
      setStatus(el.connectStatus, "Found " + hosts.join(", "));
    } catch (err) {
      setStatus(el.connectStatus, String(err.message || err), true);
    }
  }

  function prefillCheatMeta(fg) {
    if (!fg) return;
    if (el.cheatId && fg.titleid) el.cheatId.value = fg.titleid;
    if (el.cheatVersion && fg.app_ver) el.cheatVersion.value = fg.app_ver;
    if (el.cheatGameName && fg.name) el.cheatGameName.value = fg.name;
    if (el.cheatFilename && fg.titleid) {
      const ver = String(fg.app_ver || "00.00").replace(/\s/g, "") || "00.00";
      el.cheatFilename.value = fg.titleid + "_" + ver + ".json";
    }
  }

  async function loadProcessList() {
    const procsBody = await api("/api/processes");
    const fg = await api("/api/foreground");
    const processes = procsBody.processes || [];
    state.foregroundPid = fg.pid;
    state.foreground = fg;
    state.selectedPid = preferPid(processes, fg);
    renderProcesses(processes, fg.pid, state.selectedPid);
    prefillCheatMeta(fg);
    const where = fg.pid === 0
      ? "home (pid 0)"
      : (fg.name || "") + " pid " + fg.pid + " " + (fg.titleid || "");
    el.fgStatus.textContent = "Foreground: " + where + ". eboot.bin / CUSA preferred.";
    el.btnAttach.disabled = state.selectedPid == null;
  }

  async function onConnect(ev) {
    if (ev) ev.preventDefault();
    const host = (el.host.value || "").trim();
    if (!host) {
      setStatus(el.connectStatus, "Enter a PS5 IP", true);
      return;
    }
    /* Server connect() already disconnect()s and drops target_pid / maps cache.
       Drop client attach + hex-poll first so we cannot keep reading the old pid. */
    resetTargetUi();
    el.btnAttach.disabled = true;
    setStatus(el.connectStatus, "Connecting…");
    try {
      const body = await api("/api/connect", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ host: host }),
      });
      state.connected = true;
      setStatus(el.connectStatus, "Connected to " + body.host);
      await loadProcessList();
      await refreshCheatFiles();
      syncScanButtons();
    } catch (err) {
      state.connected = false;
      el.btnAttach.disabled = true;
      setStatus(el.connectStatus, String(err.message || err), true);
    }
  }

  async function onDisconnect() {
    resetTargetUi();
    state.connected = false;
    state.selectedPid = null;
    state.foregroundPid = null;
    state.foreground = null;
    el.btnAttach.disabled = true;
    try {
      await api("/api/disconnect", { method: "POST" });
    } catch (_) { /* still drop local state */ }
    setStatus(el.connectStatus, "Disconnected");
    el.fgStatus.textContent = "Connect to list processes. Foreground is highlighted.";
    el.procBody.innerHTML = "";
    syncScanButtons();
  }

  async function onAttach() {
    if (state.selectedPid == null) return;
    setStatus(el.attachStatus, "Attaching…");
    try {
      const att = await api("/api/attach_target", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ pid: state.selectedPid }),
      });
      state.attachedPid = att.pid;
      const mapsBody = await api("/api/maps?refresh=false");
      const maps = mapsBody.maps || [];
      state.maps = maps;
      const base = firstWritable(maps);
      state.peepholeAddr = base ? base.start : 0;
      state.selectedAddr = state.peepholeAddr;
      renderMaps(maps, state.peepholeAddr);
      setStatus(
        el.attachStatus,
        "Attached pid " + att.pid + (base ? " · peephole " + hexAddr(base.start) : "")
      );
      state.paused = false;
      el.btnPause.textContent = "Pause";
      el.btnPause.disabled = false;
      syncScanButtons();
      await refreshWatchFreezeLists();
      startHexPoll();
      startWatchPoll();
      startFreezeTick();
    } catch (err) {
      setStatus(el.attachStatus, String(err.message || err), true);
    }
  }

  function onPause() {
    state.paused = !state.paused;
    el.btnPause.textContent = state.paused ? "Resume" : "Pause";
    if (state.paused) {
      stopHexPoll();
      stopWatchPoll();
      stopFreezeTick();
      setStatus(el.hexStatus, "paused");
    } else {
      startHexPoll();
      startWatchPoll();
      startFreezeTick();
    }
  }

  async function refreshWatchFreezeLists() {
    try {
      const w = await api("/api/watch");
      state.watches = w.watches || [];
      state.watchValues = {};
      renderWatchTable();
    } catch (_) { /* sibling route may still be landing */ }
    try {
      const f = await api("/api/freeze");
      state.freezes = f.freezes || [];
      renderFreezeTable();
    } catch (_) { /* sibling route may still be landing */ }
  }

  function stopWatchPoll() {
    if (state.watchTimer != null) {
      clearInterval(state.watchTimer);
      state.watchTimer = null;
    }
  }

  function startWatchPoll() {
    stopWatchPoll();
    if (state.paused || state.scanBusy || document.hidden || state.attachedPid == null) return;
    if (!(state.watches || []).length) return;
    tickWatch();
    state.watchTimer = setInterval(tickWatch, WATCH_MS);
  }

  function stopFreezeTick() {
    if (state.freezeTimer != null) {
      clearInterval(state.freezeTimer);
      state.freezeTimer = null;
    }
  }

  function startFreezeTick() {
    stopFreezeTick();
    if (state.paused || state.scanBusy || document.hidden || state.attachedPid == null) return;
    if (!(state.freezes || []).length) return;
    tickFreeze();
    state.freezeTimer = setInterval(tickFreeze, FREEZE_MS);
  }

  function renderWatchTable() {
    if (!el.watchBody) return;
    el.watchBody.innerHTML = "";
    (state.watches || []).forEach(function (w) {
      const tr = document.createElement("tr");
      const live = formatLive(state.watchValues[w.id], w.n);
      tr.innerHTML =
        "<td class=\"mono\">" + w.id + "</td>" +
        "<td class=\"mono\">" + hexAddr(w.addr) + "</td>" +
        "<td class=\"mono\">" + w.n + "</td>" +
        "<td class=\"mono\" id=\"watch-live-" + w.id + "\">" + escapeHtml(live) + "</td>" +
        "<td><button type=\"button\" class=\"btn-row btn-watch-rm\">Remove</button></td>" +
        "<td><button type=\"button\" class=\"btn-row btn-watch-fz\">Freeze</button></td>";
      tr.addEventListener("click", function () {
        if (state.attachedPid == null) return;
        state.peepholeAddr = w.addr;
        state.selectedAddr = w.addr;
        updateHexSel();
        if (!state.paused && !document.hidden && !state.scanBusy) {
          tickHex();
        }
      });
      tr.querySelector(".btn-watch-rm").addEventListener("click", function (ev) {
        ev.stopPropagation();
        removeWatch(w.id);
      });
      tr.querySelector(".btn-watch-fz").addEventListener("click", function (ev) {
        ev.stopPropagation();
        freezeFromWatch(w);
      });
      el.watchBody.appendChild(tr);
    });
    syncWatchFreezeButtons();
  }

  function applyWatchValues(values) {
    (values || []).forEach(function (v) {
      state.watchValues[v.id] = v.data;
      const cell = document.getElementById("watch-live-" + v.id);
      if (!cell) return;
      let n = WATCH_N;
      const found = (state.watches || []).find(function (w) { return w.id === v.id; });
      if (found) n = found.n;
      cell.textContent = formatLive(v.data, n);
    });
  }

  async function tickWatch() {
    if (state.watchInflight || state.paused || state.scanBusy || document.hidden || state.attachedPid == null) {
      return;
    }
    if (!(state.watches || []).length) {
      stopWatchPoll();
      return;
    }
    state.watchInflight = true;
    try {
      const body = await api("/api/watch/poll");
      applyWatchValues(body.values || []);
      setStatus(el.watchStatus, "watch ~10 Hz");
    } catch (err) {
      if (isScanActiveError(err)) {
        return;
      }
      setStatus(el.watchStatus, String(err.message || err), true);
    } finally {
      state.watchInflight = false;
    }
  }

  async function addWatch(addr, n, label) {
    if (state.attachedPid == null || state.scanBusy) return;
    if ((state.watches || []).length >= WATCH_MAX) {
      setStatus(el.watchStatus, "Watch list full (64)", true);
      syncWatchFreezeButtons();
      return;
    }
    try {
      const body = await apiPost("/api/watch", {
        addr: addr,
        n: n || WATCH_N,
        label: label || "",
      });
      state.watches.push({
        id: body.id,
        pid: body.pid,
        addr: body.addr,
        n: body.n,
        label: body.label || "",
      });
      renderWatchTable();
      setStatus(el.watchStatus, "Watch " + body.id + " @ " + hexAddr(body.addr));
      if (!state.paused && !document.hidden && !state.scanBusy) {
        startWatchPoll();
      }
    } catch (err) {
      setStatus(el.watchStatus, String(err.message || err), true);
    }
  }

  async function removeWatch(id) {
    try {
      await api("/api/watch/" + encodeURIComponent(String(id)), { method: "DELETE" });
      state.watches = (state.watches || []).filter(function (w) { return w.id !== id; });
      delete state.watchValues[id];
      renderWatchTable();
      if (!(state.watches || []).length) {
        stopWatchPoll();
        setStatus(el.watchStatus, "");
      }
    } catch (err) {
      setStatus(el.watchStatus, String(err.message || err), true);
    }
  }

  function onAddWatchClick() {
    if (state.attachedPid == null) return;
    addWatch(pokeTargetAddr(), WATCH_N, "");
  }

  function renderFreezeTable() {
    if (!el.freezeBody) return;
    el.freezeBody.innerHTML = "";
    (state.freezes || []).forEach(function (f) {
      const tr = document.createElement("tr");
      tr.innerHTML =
        "<td class=\"mono\">" + f.id + "</td>" +
        "<td class=\"mono\">" + hexAddr(f.addr) + "</td>" +
        "<td class=\"mono\">" + escapeHtml(String(f.data || "").toUpperCase()) + "</td>" +
        "<td><button type=\"button\" class=\"btn-row btn-freeze-rm\">Remove</button></td>";
      tr.querySelector(".btn-freeze-rm").addEventListener("click", function (ev) {
        ev.stopPropagation();
        removeFreeze(f.id);
      });
      el.freezeBody.appendChild(tr);
    });
    syncWatchFreezeButtons();
  }

  async function tickFreeze() {
    if (state.freezeInflight || state.paused || state.scanBusy || document.hidden || state.attachedPid == null) {
      return;
    }
    if (!(state.freezes || []).length) {
      stopFreezeTick();
      return;
    }
    state.freezeInflight = true;
    try {
      await apiPost("/api/freeze/tick", {});
    } catch (err) {
      if (isScanActiveError(err)) {
        return;
      }
      setStatus(el.freezeStatus, String(err.message || err), true);
    } finally {
      state.freezeInflight = false;
    }
  }

  async function addFreeze(addr, dataHex) {
    if (state.attachedPid == null || state.scanBusy) return;
    if ((state.freezes || []).length >= FREEZE_MAX) {
      setStatus(el.freezeStatus, "Freeze list full (32)", true);
      syncWatchFreezeButtons();
      return;
    }
    const data = String(dataHex || "").replace(/\s+/g, "").toUpperCase();
    if (!data) {
      setStatus(el.freezeStatus, "No bytes to freeze yet", true);
      return;
    }
    const msg = "Freeze " + hexAddr(addr) + " as " + data + "?\nWrites at ~15 Hz until removed.";
    if (!window.confirm(msg)) return;
    try {
      const body = await apiPost("/api/freeze", { addr: addr, data: data });
      state.freezes.push({
        id: body.id,
        pid: body.pid,
        addr: body.addr,
        data: body.data,
      });
      renderFreezeTable();
      setStatus(el.freezeStatus, "Freeze " + body.id + " @ " + hexAddr(body.addr));
      if (!state.paused && !document.hidden && !state.scanBusy) {
        startFreezeTick();
      }
    } catch (err) {
      setStatus(el.freezeStatus, String(err.message || err), true);
    }
  }

  async function removeFreeze(id) {
    try {
      await api("/api/freeze/" + encodeURIComponent(String(id)), { method: "DELETE" });
      state.freezes = (state.freezes || []).filter(function (f) { return f.id !== id; });
      renderFreezeTable();
      if (!(state.freezes || []).length) {
        stopFreezeTick();
        setStatus(el.freezeStatus, "");
      }
    } catch (err) {
      setStatus(el.freezeStatus, String(err.message || err), true);
    }
  }

  function freezeFromWatch(w) {
    const hex = state.watchValues[w.id];
    if (!hex) {
      setStatus(el.freezeStatus, "Wait for a watch poll, then Freeze", true);
      return;
    }
    addFreeze(w.addr, hex);
  }

  function freezeFromHex() {
    const addr = pokeTargetAddr();
    let data = peekHexAt(addr, FREEZE_N);
    if (!data || data.length < FREEZE_N * 2) {
      setStatus(el.freezeStatus, "Need " + FREEZE_N + " live peephole bytes at " + hexAddr(addr), true);
      return;
    }
    data = data.slice(0, FREEZE_N * 2);
    addFreeze(addr, data);
  }

  async function writeBytes(addr, newHex) {
    const n = newHex.length / 2;
    const oldHex = peekHexAt(addr, n) || "(unknown)";
    const msg = hexAddr(addr) + "\n" + oldHex + " → " + newHex;
    if (!window.confirm(msg)) return false;
    try {
      await apiPost("/api/write", { addr: addr, data: newHex });
      setStatus(el.hexStatus, "wrote " + n + " B @ " + hexAddr(addr));
      if (!state.paused && !document.hidden && !state.scanBusy) {
        tickHex();
      }
      return true;
    } catch (err) {
      setStatus(el.hexStatus, String(err.message || err), true);
      return false;
    }
  }

  function onHexClick(ev) {
    const span = ev.target && ev.target.closest ? ev.target.closest("span.byte") : null;
    if (!span || state.attachedPid == null || state.scanBusy) return;
    const addr = parseInt(span.getAttribute("data-addr"), 10);
    if (!Number.isFinite(addr)) return;
    state.selectedAddr = addr;
    updateHexSel();
    const current = span.textContent || "00";
    const raw = window.prompt("New hex (1–4 bytes, no separators)", current);
    if (raw == null) {
      renderHex(state.peepholeAddr, state.hexData, HEX_N);
      return;
    }
    const newHex = parsePokeHex(raw);
    if (!newHex) {
      setStatus(el.hexStatus, "Enter 2–8 hex chars (1–4 bytes)", true);
      renderHex(state.peepholeAddr, state.hexData, HEX_N);
      return;
    }
    writeBytes(addr, newHex);
    renderHex(state.peepholeAddr, state.hexData, HEX_N);
  }

  function onPokeU32() {
    if (state.attachedPid == null || state.scanBusy) return;
    const value = parseScanValue(el.hexPokeU32.value);
    if (value == null) {
      setStatus(el.hexStatus, "Enter a u32 (decimal or 0x hex)", true);
      return;
    }
    writeBytes(pokeTargetAddr(), u32ToLeHex(value));
  }

  function safeCheatFilename(name) {
    const s = String(name || "").trim();
    if (!s) return null;
    if (/[\\/]/.test(s) || /\.\./.test(s) || /etahen/i.test(s)) return null;
    return s;
  }

  async function refreshCheatFiles() {
    if (!el.cheatFile) return;
    try {
      const body = await api("/api/cheats");
      const files = body.files || [];
      const prev = el.cheatFile.value;
      el.cheatFile.innerHTML = "";
      const blank = document.createElement("option");
      blank.value = "";
      blank.textContent = files.length ? "—" : "(no files)";
      el.cheatFile.appendChild(blank);
      files.forEach(function (name) {
        const opt = document.createElement("option");
        opt.value = name;
        opt.textContent = name;
        el.cheatFile.appendChild(opt);
      });
      if (prev) el.cheatFile.value = prev;
      syncWatchFreezeButtons();
    } catch (err) {
      setStatus(el.cheatStatus, String(err.message || err), true);
    }
    try {
      const cur = await api("/api/cheat");
      if (cur && cur.cheat) {
        state.cheatLoaded = true;
        renderCheatMods(cur.cheat, cur.enabled || []);
      }
    } catch (_) { /* sibling route may still be landing */ }
  }

  function renderCheatMods(cheat, enabled) {
    if (!el.cheatMods) return;
    el.cheatMods.innerHTML = "";
    const on = enabled || [];
    const mods = (cheat && cheat.mods) || [];
    if (!mods.length) {
      el.cheatMods.textContent = cheat ? "No mods in this file." : "";
      return;
    }
    mods.forEach(function (mod) {
      const label = document.createElement("label");
      label.className = "check";
      const cb = document.createElement("input");
      cb.type = "checkbox";
      cb.checked = on.indexOf(mod.name) !== -1;
      cb.addEventListener("change", function () {
        toggleCheatMod(mod.name, cb.checked, cb);
      });
      label.appendChild(cb);
      label.appendChild(document.createTextNode(" " + (mod.name || "")));
      if (mod.description) {
        const desc = document.createElement("span");
        desc.className = "mod-desc";
        desc.textContent = mod.description;
        label.appendChild(desc);
      }
      el.cheatMods.appendChild(label);
    });
  }

  async function toggleCheatMod(name, enabled, cb) {
    try {
      await apiPost("/api/cheat/toggle", { name: name, enabled: !!enabled });
      setStatus(el.cheatStatus, (enabled ? "on" : "off") + ": " + name);
    } catch (err) {
      if (cb) cb.checked = !enabled;
      setStatus(el.cheatStatus, String(err.message || err), true);
    }
  }

  async function onCheatLoad() {
    const filename = (el.cheatFile && el.cheatFile.value) || "";
    if (!filename) {
      setStatus(el.cheatStatus, "Choose a cheat file", true);
      return;
    }
    try {
      const body = await apiPost("/api/cheat/load", { filename: filename });
      state.cheatLoaded = !!body.cheat;
      if (el.cheatFilename) el.cheatFilename.value = filename;
      if (body.cheat) {
        if (el.cheatId && body.cheat.id) el.cheatId.value = body.cheat.id;
        if (el.cheatVersion && body.cheat.version) el.cheatVersion.value = body.cheat.version;
        if (el.cheatGameName && body.cheat.name) el.cheatGameName.value = body.cheat.name;
      }
      renderCheatMods(body.cheat, body.enabled || []);
      setStatus(el.cheatStatus, "Loaded " + filename);
    } catch (err) {
      setStatus(el.cheatStatus, String(err.message || err), true);
    }
  }

  async function onCheatSave() {
    const filename = safeCheatFilename(el.cheatFilename && el.cheatFilename.value);
    if (!filename) {
      setStatus(el.cheatStatus, "Enter a filename (no path, not etaHEN)", true);
      return;
    }
    try {
      const body = await apiPost("/api/cheat/save", { filename: filename });
      setStatus(el.cheatStatus, "Saved " + (body.filename || filename));
      if (el.cheatFile) {
        await refreshCheatFiles();
        el.cheatFile.value = body.filename || filename;
      }
    } catch (err) {
      setStatus(el.cheatStatus, String(err.message || err), true);
    }
  }

  function buildFreezeCheat() {
    const base = ebootBase(state.maps);
    if (base == null) {
      setStatus(el.cheatStatus, "No eboot.bin map — cannot compute offsets", true);
      return null;
    }
    const fg = state.foreground || {};
    const id = ((el.cheatId && el.cheatId.value) || fg.titleid || "CUSA00000").trim();
    const version = ((el.cheatVersion && el.cheatVersion.value) || fg.app_ver || "00.00").trim();
    const name = ((el.cheatGameName && el.cheatGameName.value) || fg.name || "Game").trim();
    const mods = [];
    (state.freezes || []).forEach(function (f) {
      const off = f.addr - base;
      if (off < 0) return;
      const onHex = String(f.data || "").toLowerCase();
      const offHex = (new Array(onHex.length + 1).join("0")).slice(0, onHex.length) || "00";
      mods.push({
        name: "Freeze " + hexAddr(f.addr),
        description: "",
        type: "checkbox",
        memory: [{ offset: off.toString(16), on: onHex, off: offHex }],
      });
    });
    if (!mods.length) {
      setStatus(el.cheatStatus, "No freezes with offsets inside eboot.bin", true);
      return null;
    }
    return {
      name: name || "Game",
      id: id || "CUSA00000",
      version: version || "00.00",
      process: "eboot.bin",
      mods: mods,
      credits: ["NitePR5"],
    };
  }

  async function onCheatSaveFreezes() {
    const filename = safeCheatFilename(el.cheatFilename && el.cheatFilename.value);
    if (!filename) {
      setStatus(el.cheatStatus, "Enter a filename (no path, not etaHEN)", true);
      return;
    }
    const cheat = buildFreezeCheat();
    if (!cheat) return;
    try {
      const body = await apiPost("/api/cheat/save", { filename: filename, cheat: cheat });
      state.cheatLoaded = true;
      renderCheatMods(cheat, []);
      setStatus(el.cheatStatus, "Saved " + (body.filename || filename));
      if (el.cheatFile) {
        await refreshCheatFiles();
        el.cheatFile.value = body.filename || filename;
      }
    } catch (err) {
      setStatus(el.cheatStatus, String(err.message || err), true);
    }
  }

  document.addEventListener("visibilitychange", function () {
    if (document.hidden) {
      stopHexPoll();
      stopWatchPoll();
      stopFreezeTick();
    } else if (!state.paused && state.attachedPid != null) {
      startHexPoll();
      startWatchPoll();
      startFreezeTick();
    }
  });

  async function prefillHost() {
    try {
      const body = await api("/api/defaults");
      if (body && body.host && !el.host.value) {
        el.host.value = body.host;
      }
    } catch (_) {
      /* placeholder is enough; UI works without this route */
    }
  }

  el.connectForm.addEventListener("submit", onConnect);
  el.btnDiscover.addEventListener("click", onDiscover);
  el.btnDisconnect.addEventListener("click", onDisconnect);
  el.btnAttach.addEventListener("click", onAttach);
  el.btnPause.addEventListener("click", onPause);
  el.btnScanFirst.addEventListener("click", function () { runScan("first"); });
  el.btnScanNext.addEventListener("click", function () { runScan("next"); });
  el.btnScanUndo.addEventListener("click", function () { runScan("undo"); });
  if (el.hexBody) el.hexBody.addEventListener("click", onHexClick);
  if (el.btnWatchAdd) el.btnWatchAdd.addEventListener("click", onAddWatchClick);
  if (el.btnWatchHex) el.btnWatchHex.addEventListener("click", onAddWatchClick);
  if (el.btnFreezeHex) el.btnFreezeHex.addEventListener("click", freezeFromHex);
  if (el.btnPokeU32) el.btnPokeU32.addEventListener("click", onPokeU32);
  if (el.btnCheatLoad) el.btnCheatLoad.addEventListener("click", onCheatLoad);
  if (el.btnCheatSave) el.btnCheatSave.addEventListener("click", onCheatSave);
  if (el.btnCheatSaveFreezes) {
    el.btnCheatSaveFreezes.addEventListener("click", onCheatSaveFreezes);
  }
  prefillHost();
  syncScanButtons();
})();
