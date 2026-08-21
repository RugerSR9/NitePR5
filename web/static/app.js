/* NitePR5 Phase 1–2 UI — fetch('/api/...') only. No second protocol. */
(function () {
  "use strict";

  const HEX_N = 512;
  const ROW_BYTES = 16;
  const ROW_COUNT = HEX_N / ROW_BYTES; // 32
  const HEX_MS = 250; // 4 Hz
  const RESULTS_MAX = 256;

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
  };

  const state = {
    connected: false,
    selectedPid: null,
    foregroundPid: null,
    attachedPid: null,
    peepholeAddr: 0,
    paused: false,
    timer: null,
    inflight: false,
    hasScan: false,
    scanBusy: false,
    scanGen: 0,
    scanCount: null,
  };

  function setStatus(node, text, isErr) {
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

  function renderHex(addr, hexStr, n) {
    const bytes = bytesFromHex(hexStr);
    const rows = [];
    for (let r = 0; r < ROW_COUNT; r++) {
      const rowAddr = addr + r * ROW_BYTES;
      const slice = bytes.slice(r * ROW_BYTES, r * ROW_BYTES + ROW_BYTES);
      const hexParts = [];
      let ascii = "";
      for (let i = 0; i < ROW_BYTES; i++) {
        const b = slice[i];
        hexParts.push(b == null ? "  " : b.toString(16).toUpperCase().padStart(2, "0"));
        ascii += b == null ? " " : asciiChar(b);
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

  function syncScanButtons() {
    const attached = state.connected && state.attachedPid != null;
    el.btnScanFirst.disabled = !attached || state.scanBusy;
    el.btnScanNext.disabled = !state.hasScan || state.scanBusy;
    el.btnScanUndo.disabled = !state.hasScan || state.scanBusy;
    if (el.btnAttach) {
      el.btnAttach.disabled = !attached || state.selectedPid == null || state.scanBusy;
    }
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
        "<td class=\"mono\">" + u32leFromHex(row.current) + "</td>";
      tr.addEventListener("click", function () {
        if (state.attachedPid == null) return;
        state.peepholeAddr = addr;
        if (!state.paused && !document.hidden) {
          tickHex();
        }
      });
      el.scanBody.appendChild(tr);
    });
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
    state.attachedPid = null;
    state.peepholeAddr = 0;
    state.paused = false;
    state.scanGen += 1;
    state.scanBusy = false;
    el.btnPause.textContent = "Pause";
    el.btnPause.disabled = true;
    el.mapsBody.innerHTML = "";
    el.hexBody.innerHTML = "";
    el.hexMeta.textContent = "addr — · 512 bytes";
    setStatus(el.attachStatus, "");
    setStatus(el.hexStatus, "");
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

  async function loadProcessList() {
    const procsBody = await api("/api/processes");
    const fg = await api("/api/foreground");
    const processes = procsBody.processes || [];
    state.foregroundPid = fg.pid;
    state.selectedPid = preferPid(processes, fg);
    renderProcesses(processes, fg.pid, state.selectedPid);
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
      const base = firstWritable(maps);
      state.peepholeAddr = base ? base.start : 0;
      renderMaps(maps, state.peepholeAddr);
      setStatus(
        el.attachStatus,
        "Attached pid " + att.pid + (base ? " · peephole " + hexAddr(base.start) : "")
      );
      state.paused = false;
      el.btnPause.textContent = "Pause";
      el.btnPause.disabled = false;
      syncScanButtons();
      startHexPoll();
    } catch (err) {
      setStatus(el.attachStatus, String(err.message || err), true);
    }
  }

  function onPause() {
    state.paused = !state.paused;
    el.btnPause.textContent = state.paused ? "Resume" : "Pause";
    if (state.paused) {
      stopHexPoll();
      setStatus(el.hexStatus, "paused");
    } else {
      startHexPoll();
    }
  }

  document.addEventListener("visibilitychange", function () {
    if (document.hidden) {
      stopHexPoll();
    } else if (!state.paused && state.attachedPid != null) {
      startHexPoll();
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
  prefillHost();
  syncScanButtons();
})();
