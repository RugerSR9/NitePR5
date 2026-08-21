/* NitePR5 Phase 1 UI — fetch('/api/...') only. No second protocol. */
(function () {
  "use strict";

  const HEX_N = 512;
  const ROW_BYTES = 16;
  const ROW_COUNT = HEX_N / ROW_BYTES; // 32
  const HEX_MS = 250; // 4 Hz

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

  function stopHexPoll() {
    if (state.timer != null) {
      clearInterval(state.timer);
      state.timer = null;
    }
  }

  function startHexPoll() {
    stopHexPoll();
    if (state.paused || document.hidden || state.attachedPid == null) return;
    tickHex();
    state.timer = setInterval(tickHex, HEX_MS);
  }

  function resetTargetUi() {
    /* Match Session.connect -> disconnect: drop logical attach, maps, peephole. */
    stopHexPoll();
    state.attachedPid = null;
    state.peepholeAddr = 0;
    state.paused = false;
    el.btnPause.textContent = "Pause";
    el.btnPause.disabled = true;
    el.mapsBody.innerHTML = "";
    el.hexBody.innerHTML = "";
    el.hexMeta.textContent = "addr — · 512 bytes";
    setStatus(el.attachStatus, "");
    setStatus(el.hexStatus, "");
  }

  async function tickHex() {
    if (state.inflight || state.paused || document.hidden || state.attachedPid == null) {
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
      setStatus(el.hexStatus, state.paused ? "paused" : "~4 Hz", false);
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
  prefillHost();
})();
