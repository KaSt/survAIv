#pragma once

// Embedded HTML pages as C string literals.

namespace survaiv {

// ─── Dashboard ──────────────────────────────────────────────────────
static const char kDashboardHtml[] = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>survaiv</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:#fafafa;--card:#fff;--border:#e0e0e0;--text:#1a1a1a;--dim:#999;
--green:#0d9e50;--red:#d32f2f;--blue:#1565c0;--yellow:#e6a700;--purple:#7b1fa2;
--overlay:rgba(0,0,0,.45);--modal:#fff;--input-bg:#fff}
[data-theme="dark"]{--bg:#121212;--card:#1e1e1e;--border:#333;--text:#e0e0e0;--dim:#777;
--green:#4caf50;--red:#ef5350;--blue:#42a5f5;--yellow:#ffca28;--purple:#ab47bc;
--overlay:rgba(0,0,0,.7);--modal:#1e1e1e;--input-bg:#2a2a2a}
body{font-family:'SF Mono',Monaco,'Fira Code',monospace;background:var(--bg);color:var(--text);
font-size:13px;line-height:1.5;max-width:960px;margin:0 auto}
.error-banner{display:none;border:1px solid var(--red);color:var(--red);
border-radius:6px;padding:10px 16px;margin:8px 20px;font-size:12px;font-weight:600;
background:color-mix(in srgb, var(--red) 10%, var(--card))}
.error-banner.visible{display:block}
.top{display:flex;align-items:center;justify-content:space-between;padding:12px 20px;
border-bottom:1px solid var(--border);background:var(--card)}
.top h1{font-size:16px;font-weight:600;letter-spacing:2px;color:var(--green)}
.top .status{display:flex;align-items:center;gap:8px}
.dot{width:8px;height:8px;border-radius:50%;background:var(--green);animation:pulse 2s infinite}
.dot.err{background:var(--red);animation:none}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
@keyframes spin-r{to{transform:rotate(360deg)}}
.spin{display:inline-block;width:12px;height:12px;border:2px solid rgba(255,255,255,.3);
  border-top-color:#fff;border-radius:50%;animation:spin-r .8s linear infinite;vertical-align:middle;margin-right:6px}
.meta{color:var(--dim);font-size:11px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;padding:16px 20px}
.card{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:14px 16px}
.card .label{font-size:10px;text-transform:uppercase;letter-spacing:1px;color:var(--dim);margin-bottom:4px}
.card .value{font-size:22px;font-weight:700}
.card .sub{font-size:11px;color:var(--dim);margin-top:2px}
.pos{color:var(--green)}.neg{color:var(--red)}
.section{padding:0 20px 16px}
.section h2{font-size:12px;text-transform:uppercase;letter-spacing:1.5px;color:var(--dim);
margin:16px 0 8px;border-bottom:1px solid var(--border);padding-bottom:6px}
table{width:100%;border-collapse:collapse;font-size:12px}
th{text-align:left;color:var(--dim);font-weight:400;padding:6px 8px;
border-bottom:1px solid var(--border);font-size:10px;text-transform:uppercase;letter-spacing:1px}
td{padding:6px 8px;border-bottom:1px solid var(--border)}
.log{max-height:300px;overflow-y:auto;background:var(--card);border:1px solid var(--border);
border-radius:8px;padding:10px 14px;font-size:11px}
.log-entry{padding:4px 0;border-bottom:1px solid #1a1a24}
.log-entry:last-child{border:none}
.log-time{color:var(--dim);margin-right:8px}
.log-type{display:inline-block;padding:1px 6px;border-radius:3px;font-size:10px;font-weight:600;
margin-right:6px}
.log-type.hold{background:#f0f0f0;color:#888}
.log-type.buy{background:#e8f5e9;color:var(--green)}
.log-type.close{background:#ffebee;color:var(--red)}
.log-type.tool{background:#e3f2fd;color:var(--blue)}
.scout-grid{display:grid;grid-template-columns:1fr;gap:6px;max-height:320px;overflow-y:auto}
.scout-card{background:var(--card);border:1px solid var(--border);border-radius:6px;padding:10px 12px;
display:grid;grid-template-columns:auto 1fr auto;gap:8px;align-items:center}
.scout-signal{display:inline-block;width:10px;height:10px;border-radius:50%;flex-shrink:0}
.scout-signal.bullish{background:var(--green)}.scout-signal.bearish{background:var(--red)}
.scout-signal.neutral{background:var(--yellow)}.scout-signal.skip{background:var(--dim)}
.scout-q{font-size:11px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.scout-meta{font-size:10px;color:var(--dim);display:flex;gap:8px;flex-wrap:wrap}
.scout-right{text-align:right;white-space:nowrap}
.scout-price{font-size:14px;font-weight:700}
.wisdom-section{margin-top:16px}
.wisdom-stats{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:8px;margin-bottom:12px}
.wisdom-stat{background:var(--card);border:1px solid var(--border);border-radius:6px;padding:10px;text-align:center}
.wisdom-stat .val{font-size:20px;font-weight:700;color:var(--green)}
.wisdom-stat .lbl{font-size:10px;color:var(--dim);text-transform:uppercase;margin-top:2px}
.wisdom-rules{background:var(--card);border:1px solid var(--border);border-radius:6px;padding:12px;
font-size:11px;line-height:1.5;white-space:pre-wrap;color:var(--fg);max-height:200px;overflow-y:auto}
.freeze-toggle{display:flex;align-items:center;gap:6px;font-size:11px;color:var(--dim);cursor:pointer;float:right;margin-top:-2px}
.freeze-toggle input{accent-color:var(--blue);cursor:pointer}
.frozen-badge{display:inline-block;background:var(--blue);color:#fff;font-size:9px;padding:1px 6px;
border-radius:3px;margin-left:6px;vertical-align:middle;letter-spacing:.5px}
.scout-conf{font-size:10px;color:var(--dim)}
.chart-wrap{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:12px;
height:120px;position:relative}
canvas{width:100%!important;height:100%!important}
.badge{display:inline-block;padding:2px 8px;border-radius:10px;font-size:10px;font-weight:600}
.badge.live{background:#e8f5e9;color:var(--green);border:1px solid var(--green)}
.badge.paper{background:#fff8e1;color:var(--yellow);border:1px solid var(--yellow)}
.empty{text-align:center;padding:20px;color:var(--dim);font-style:italic}
.top-icons{display:flex;align-items:center;gap:6px}
.icon-btn{background:none;border:none;cursor:pointer;font-size:18px;padding:4px;line-height:1;
color:var(--dim);border-radius:4px;transition:color .2s}
.icon-btn:hover{color:var(--text)}
.modal-overlay{display:none;position:fixed;inset:0;background:var(--overlay);z-index:100;
justify-content:center;align-items:flex-start;padding-top:60px}
.modal-overlay.open{display:flex}
.modal{background:var(--modal);border:1px solid var(--border);border-radius:10px;
width:90%;max-width:560px;max-height:80vh;overflow-y:auto;padding:20px 24px;
box-shadow:0 8px 32px rgba(0,0,0,.25);position:relative}
.modal-close{position:absolute;top:10px;right:14px;background:none;border:none;font-size:20px;
cursor:pointer;color:var(--dim);line-height:1}
.modal-close:hover{color:var(--red)}
.modal h2{font-size:13px;text-transform:uppercase;letter-spacing:1.5px;color:var(--dim);
margin:0 0 14px;border-bottom:1px solid var(--border);padding-bottom:8px}
.modal .setting-group{margin-bottom:16px}
.modal .setting-group label{font-size:10px;color:var(--dim)}
</style>
</head>
<body>
<div id="auth-overlay" style="display:none;position:fixed;inset:0;background:var(--bg);z-index:200;align-items:center;justify-content:center">
  <div style="max-width:380px;padding:24px;text-align:center">
    <h1 style="font-size:24px;margin-bottom:8px">⟁ SURVAIV</h1>
    <div id="auth-claim" style="display:none">
      <p style="font-size:13px;color:var(--dim);margin-bottom:16px">This is your agent's secret PIN. Write it down — you'll need it to access this dashboard.</p>
      <div id="auth-pin-display" style="font-size:28px;font-weight:700;letter-spacing:2px;padding:16px;background:var(--card);border:2px solid var(--green);border-radius:8px;margin-bottom:16px;font-family:monospace"></div>
      <label style="font-size:12px;color:var(--dim)">Confirm PIN to claim ownership</label>
      <input id="auth-confirm-input" type="text" style="width:100%;padding:10px;font-size:16px;text-align:center;border:1px solid var(--border);border-radius:6px;margin-top:6px;background:var(--input-bg);color:var(--text)" placeholder="Type the PIN above">
      <button onclick="confirmClaim()" style="width:100%;margin-top:12px;padding:10px;background:var(--green);color:#000;border:none;border-radius:6px;font-size:14px;font-weight:600;cursor:pointer">Claim Agent</button>
      <div id="auth-claim-err" style="color:var(--red);font-size:12px;margin-top:8px"></div>
    </div>
    <div id="auth-login" style="display:none">
      <p style="font-size:13px;color:var(--dim);margin-bottom:16px">Enter your PIN to access the dashboard.</p>
      <input id="auth-login-input" type="text" style="width:100%;padding:10px;font-size:16px;text-align:center;border:1px solid var(--border);border-radius:6px;background:var(--input-bg);color:var(--text)" placeholder="Enter PIN">
      <button onclick="loginWithPin()" style="width:100%;margin-top:12px;padding:10px;background:var(--blue);color:#fff;border:none;border-radius:6px;font-size:14px;font-weight:600;cursor:pointer">Unlock</button>
      <div id="auth-login-err" style="color:var(--red);font-size:12px;margin-top:8px"></div>
    </div>
  </div>
</div>
<div class="top">
  <h1 id="agent-title">⟁ SURVAIV</h1>
  <div class="status">
    <span class="badge" id="mode">PAPER</span>
    <span class="dot" id="dot"></span>
    <span class="meta" id="status-text">connecting…</span>
    <div class="top-icons">
      <button class="icon-btn" id="theme-btn" onclick="toggleTheme()" title="Toggle theme">☀</button>
      <button class="icon-btn" onclick="openSettings()" title="Settings">⚙</button>
    </div>
  </div>
</div>

<div class="error-banner" id="error-banner"></div>

<div class="grid" id="cards">
  <div class="card"><div class="label">Cash</div><div class="value" id="v-cash">—</div>
    <div class="sub">USDC available</div></div>
  <div class="card"><div class="label">Equity</div><div class="value" id="v-equity">—</div>
    <div class="sub">Cash + positions</div></div>
  <div class="card"><div class="label">P&amp;L</div><div class="value" id="v-pnl">—</div>
    <div class="sub">Realized profit/loss</div></div>
  <div class="card"><div class="label">LLM Spend</div><div class="value" id="v-spend">—</div>
    <div class="sub">Cumulative inference</div></div>
  <div class="card"><div class="label">Daily Loss</div><div class="value" id="v-dloss">—</div>
    <div class="sub">Today's drawdown</div></div>
  <div class="card"><div class="label">Cycles</div><div class="value" id="v-cycles">—</div>
    <div class="sub" id="v-uptime">—</div></div>
</div>

<div class="section">
  <h2>Equity Chart</h2>
  <div class="chart-wrap"><canvas id="chart"></canvas></div>
</div>

<div class="section">
  <h2>P&amp;L per Cycle</h2>
  <div class="chart-wrap"><canvas id="pnl-chart"></canvas></div>
</div>

<div class="section">
  <h2>Open Positions</h2>
  <table>
    <thead><tr><th>Market</th><th>Side</th><th>Entry</th><th>Current</th><th>P&amp;L</th><th>Stake</th><th>Type</th></tr></thead>
    <tbody id="pos-body"><tr><td colspan="7" class="empty">No open positions</td></tr></tbody>
  </table>
</div>

<div class="section">
  <h2>Market Scanner</h2>
  <div class="scout-grid" id="scout-body"><div class="empty">Waiting for first scan…</div></div>
</div>

<div class="section wisdom-section">
  <h2>Learning <span id="w-frozen-badge" class="frozen-badge" style="display:none">FROZEN</span>
    <label class="freeze-toggle"><input type="checkbox" id="w-freeze" onchange="toggleFreeze(this.checked)"> Freeze learning</label>
  </h2>
  <div class="wisdom-stats" id="wisdom-stats">
    <div class="wisdom-stat"><div class="val" id="w-total">–</div><div class="lbl">Decisions</div></div>
    <div class="wisdom-stat"><div class="val" id="w-resolved">–</div><div class="lbl">Resolved</div></div>
    <div class="wisdom-stat"><div class="val" id="w-accuracy">–</div><div class="lbl">Accuracy</div></div>
    <div class="wisdom-stat"><div class="val" id="w-buys">–</div><div class="lbl">Buy Acc</div></div>
    <div class="wisdom-stat"><div class="val" id="w-holds">–</div><div class="lbl">Hold Acc</div></div>
  </div>
  <div id="w-models" style="font-size:10px;color:var(--dim);margin-bottom:8px"></div>
  <div class="wisdom-rules" id="w-rules">No learned rules yet…</div>
</div>

<div class="section">
  <h2>Decision Log</h2>
  <div class="log" id="log"><div class="empty">Waiting for first cycle…</div></div>
</div>

<div class="section" id="wallet-section" style="display:none">
  <h2>Wallet</h2>
  <div class="card">
    <div class="label">Address</div>
    <div class="value" id="v-wallet" style="font-size:13px;word-break:break-all">—</div>
    <div class="sub">USDC.e balance: <span id="v-balance">—</span></div>
  </div>
</div>

<div class="section">
  <div class="sub">
    Firmware: <span id="v-fw">—</span> · Model: <span id="v-model">—</span> (<span id="v-model-price">—</span>/req)
  </div>
</div>

<!-- Settings modal -->
<div class="modal-overlay" id="settings-overlay" onclick="if(event.target===this)closeSettings()">
  <div class="modal">
    <button class="modal-close" onclick="closeSettings()">&times;</button>
    <h2>Settings</h2>

    <div class="setting-group">
      <div style="display:flex;gap:10px;flex-wrap:wrap">
        <a class="badge" href="/api/backup?full=1" style="padding:6px 14px;text-decoration:none;background:#e3f2fd;color:var(--blue);border:1px solid var(--blue);cursor:pointer">⬇ Backup Config</a>
        <label class="badge" style="padding:6px 14px;background:#e8f5e9;color:var(--green);border:1px solid var(--green);cursor:pointer">
          ⬆ Restore Config
          <input type="file" id="restore-file" accept=".json" style="display:none"
            onchange="restoreConfig(this.files[0])">
        </label>
        <label class="badge" style="padding:6px 14px;background:#fff8e1;color:var(--yellow);border:1px solid var(--yellow);cursor:pointer">
          ⚡ OTA Update
          <input type="file" id="ota-file" accept=".bin" style="display:none"
            onchange="otaUpdate(this.files[0])">
        </label>
      </div>
      <div id="settings-msg" style="margin-top:8px;font-size:11px;color:var(--dim)"></div>
    </div>

    <div class="setting-group">
      <div style="font-size:12px;font-weight:600;margin-bottom:6px">Knowledge</div>
      <div style="display:flex;gap:10px;flex-wrap:wrap;align-items:center">
        <button onclick="downloadKnowledge()" class="badge" style="padding:6px 14px;background:#e8f5e9;color:var(--green);border:1px solid var(--green);cursor:pointer">⬇ Export Knowledge</button>
        <label class="badge" style="padding:6px 14px;background:#e3f2fd;color:var(--blue);border:1px solid var(--blue);cursor:pointer">
          ⬆ Import Knowledge
          <input type="file" accept=".json,.survaiv" id="kb-import" style="display:none" onchange="importKnowledge(this)">
        </label>
        <span id="kb-status" style="font-size:11px;color:var(--dim)"></span>
      </div>
    </div>

    <div class="setting-group">
      <div style="font-size:12px;font-weight:600;margin-bottom:6px">Trading Mode</div>
      <div style="display:flex;gap:10px;align-items:center">
        <button id="mode-paper-btn" onclick="setTradingMode(true)" class="badge" style="padding:6px 14px;cursor:pointer">📝 Paper</button>
        <button id="mode-real-btn" onclick="setTradingMode(false)" class="badge" style="padding:6px 14px;cursor:pointer">⟁ Real</button>
        <span id="mode-msg" style="font-size:11px;color:var(--dim)"></span>
      </div>
      <div style="font-size:10px;color:var(--dim);margin-top:4px">Open positions will continue to completion in their original mode.</div>
    </div>

    <div class="setting-group" id="llm-cfg" style="display:none">
      <div style="font-size:12px;font-weight:600;margin-bottom:6px">LLM Endpoint (paper mode)</div>
      <div style="display:flex;gap:4px;flex-wrap:wrap;margin-bottom:8px">
        <button onclick="setLlmPreset('local')" class="preset-btn" style="font-size:10px;padding:3px 8px;border:1px solid var(--border);border-radius:4px;background:var(--input-bg);color:var(--text);cursor:pointer">Local</button>
        <button onclick="setLlmPreset('openrouter')" class="preset-btn" style="font-size:10px;padding:3px 8px;border:1px solid var(--border);border-radius:4px;background:var(--input-bg);color:var(--text);cursor:pointer">OpenRouter</button>
        <button onclick="setLlmPreset('openai')" class="preset-btn" style="font-size:10px;padding:3px 8px;border:1px solid var(--border);border-radius:4px;background:var(--input-bg);color:var(--text);cursor:pointer">OpenAI</button>
        <button onclick="setLlmPreset('groq')" class="preset-btn" style="font-size:10px;padding:3px 8px;border:1px solid var(--border);border-radius:4px;background:var(--input-bg);color:var(--text);cursor:pointer">Groq</button>
        <button onclick="setLlmPreset('x402')" class="preset-btn" style="font-size:10px;padding:3px 8px;border:1px solid var(--border);border-radius:4px;background:var(--input-bg);color:var(--text);cursor:pointer">x402 (auto)</button>
      </div>
      <div style="display:flex;gap:6px;flex-wrap:wrap;align-items:end">
        <label style="font-size:10px;color:var(--dim);flex:2">Base URL<br>
          <input id="cfg-url" oninput="this.dataset.touched='1'" style="width:100%;font-size:11px;padding:4px 6px;border:1px solid var(--border);border-radius:4px;background:var(--input-bg);color:var(--text)" placeholder="https://…/v1">
        </label>
        <label style="font-size:10px;color:var(--dim);flex:1">Model<br>
          <input id="cfg-model" style="width:100%;font-size:11px;padding:4px 6px;border:1px solid var(--border);border-radius:4px;background:var(--input-bg);color:var(--text)" placeholder="model-id">
        </label>
        <label style="font-size:10px;color:var(--dim);flex:1">API Key<br>
          <input id="cfg-key" type="password" style="width:100%;font-size:11px;padding:4px 6px;border:1px solid var(--border);border-radius:4px;background:var(--input-bg);color:var(--text)" placeholder="sk-…">
        </label>
        <button onclick="saveLlmConfig()" style="background:var(--blue);color:#fff;border:none;padding:5px 12px;border-radius:4px;cursor:pointer;font-size:11px">Apply</button>
      </div>
      <div id="llm-cfg-msg" style="margin-top:4px;font-size:10px;color:var(--dim)"></div>
    </div>
    <div class="setting-group">
      <div style="font-size:12px;font-weight:600;margin-bottom:6px">News Search</div>
      <div style="display:flex;gap:6px;flex-wrap:wrap;align-items:end">
        <label style="font-size:10px;color:var(--dim);flex:1">Provider<br>
          <select id="cfg-news-prov" style="width:100%;font-size:11px;padding:4px 6px;border:1px solid var(--border);border-radius:4px;background:var(--input-bg);color:var(--text)">
            <option value="tavily">Tavily</option>
            <option value="brave">Brave Search</option>
          </select>
        </label>
        <label style="font-size:10px;color:var(--dim);flex:2">API Key<br>
          <input id="cfg-news-key" type="password" style="width:100%;font-size:11px;padding:4px 6px;border:1px solid var(--border);border-radius:4px;background:var(--input-bg);color:var(--text)" placeholder="tvly-xxx or BSA-xxx">
        </label>
        <button onclick="saveNewsConfig()" style="background:var(--blue);color:#fff;border:none;padding:5px 12px;border-radius:4px;cursor:pointer;font-size:11px">Save</button>
      </div>
      <div id="news-cfg-msg" style="margin-top:4px;font-size:10px;color:var(--dim)"></div>
    </div>
  </div>
</div>

<script>
const $ = id => document.getElementById(id);
const fmt = (n, d=4) => n < 0 ? n.toFixed(d) : n.toFixed(d);
const fmtUsd = n => '$' + Math.abs(n).toFixed(2);
const cls = n => n >= 0 ? 'pos' : 'neg';

// ── Auth ────────────────────────────────────────────────────────
let authToken = sessionStorage.getItem('survaiv-token') || '';

function authHeaders() {
  const h = {'Content-Type':'application/json'};
  if (authToken) h['X-Auth-Token'] = authToken;
  return h;
}

function checkAuth() {
  fetch('/api/auth').then(r => r.json()).then(d => {
    if (!d.claimed) {
      $('auth-overlay').style.display = 'flex';
      $('auth-claim').style.display = 'block';
      $('auth-pin-display').textContent = d.display_pin;
    } else if (!authToken) {
      $('auth-overlay').style.display = 'flex';
      $('auth-login').style.display = 'block';
    } else {
      fetch('/api/auth', {headers:{'X-Auth-Token': authToken}}).then(r => {
        if (r.ok) return r.json(); else return {needs_pin:true};
      }).then(v => {
        if (v.needs_pin === false) { $('auth-overlay').style.display = 'none'; initDashboard(); }
        else { authToken = ''; sessionStorage.removeItem('survaiv-token'); checkAuth(); }
      });
    }
  });
}

function confirmClaim() {
  const pin = $('auth-confirm-input').value.trim();
  fetch('/api/auth', {method:'POST', body:JSON.stringify({action:'claim',pin:pin}),
    headers:{'Content-Type':'application/json'}})
    .then(r => r.json()).then(d => {
      if (d.ok) {
        authToken = d.token;
        sessionStorage.setItem('survaiv-token', authToken);
        $('auth-overlay').style.display = 'none';
        initDashboard();
      } else { $('auth-claim-err').textContent = d.error || 'PIN mismatch'; }
    });
}

function loginWithPin() {
  const pin = $('auth-login-input').value.trim();
  fetch('/api/auth', {method:'POST', body:JSON.stringify({action:'login',pin:pin}),
    headers:{'Content-Type':'application/json'}})
    .then(r => r.json()).then(d => {
      if (d.ok) {
        authToken = d.token;
        sessionStorage.setItem('survaiv-token', authToken);
        $('auth-overlay').style.display = 'none';
        initDashboard();
      } else { $('auth-login-err').textContent = d.error || 'Wrong PIN'; }
    });
}

function setTradingMode(paper) {
  fetch('/api/config', {method:'POST', body:JSON.stringify({paper_only: paper ? 1 : 0}),
    headers: authHeaders()})
    .then(r => r.json()).then(d => {
      const msg = $('mode-msg');
      msg.textContent = paper ? '\u2713 Switched to Paper' : '\u2713 Switched to Real';
      msg.style.color = 'var(--green)';
      setTimeout(() => msg.textContent = '', 3000);
    }).catch(() => {
      $('mode-msg').textContent = '\u2717 Failed'; $('mode-msg').style.color = 'var(--red)';
    });
}

// Theme toggle
function toggleTheme() {
  const html = document.documentElement;
  const dark = html.getAttribute('data-theme') !== 'dark';
  html.setAttribute('data-theme', dark ? 'dark' : 'light');
  $('theme-btn').textContent = dark ? '🌙' : '☀';
  try { localStorage.setItem('survaiv-theme', dark ? 'dark' : 'light'); } catch(e) {}
}
(function initTheme() {
  try {
    const saved = localStorage.getItem('survaiv-theme');
    if (saved === 'dark') {
      document.documentElement.setAttribute('data-theme','dark');
      const btn = document.getElementById('theme-btn');
      if (btn) btn.textContent = '🌙';
    }
  } catch(e) {}
})();

// Settings modal
function openSettings() { $('settings-overlay').classList.add('open'); }
function closeSettings() { $('settings-overlay').classList.remove('open'); }
document.addEventListener('keydown', e => { if (e.key === 'Escape') closeSettings(); });

let equityData = [];

function drawChart() {
  const canvas = $('chart');
  const ctx = canvas.getContext('2d');
  const rect = canvas.parentElement.getBoundingClientRect();
  canvas.width = rect.width - 24;
  canvas.height = rect.height - 24;
  const w = canvas.width, h = canvas.height;

  ctx.clearRect(0, 0, w, h);
  if (equityData.length < 2) {
    ctx.fillStyle = '#999';
    ctx.font = '11px monospace';
    ctx.textAlign = 'center';
    ctx.fillText('Collecting data…', w/2, h/2);
    return;
  }

  const vals = equityData.map(d => d[1]);
  const mn = Math.min(...vals) * 0.995;
  const mx = Math.max(...vals) * 1.005;
  const range = mx - mn || 1;

  // Grid lines.
  ctx.strokeStyle = '#e0e0e0';
  ctx.lineWidth = 1;
  for (let i = 0; i < 4; i++) {
    const y = h * i / 3;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
  }

  // Line.
  const startVal = vals[0];
  ctx.beginPath();
  for (let i = 0; i < vals.length; i++) {
    const x = (i / (vals.length - 1)) * w;
    const y = h - ((vals[i] - mn) / range) * h;
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  const lastVal = vals[vals.length - 1];
  ctx.strokeStyle = lastVal >= startVal ? '#0d9e50' : '#d32f2f';
  ctx.lineWidth = 2;
  ctx.stroke();

  // Fill gradient.
  const grad = ctx.createLinearGradient(0, 0, 0, h);
  if (lastVal >= startVal) {
    grad.addColorStop(0, 'rgba(13,158,80,0.12)');
    grad.addColorStop(1, 'rgba(13,158,80,0)');
  } else {
    grad.addColorStop(0, 'rgba(211,47,47,0.12)');
    grad.addColorStop(1, 'rgba(211,47,47,0)');
  }
  ctx.lineTo(w, h); ctx.lineTo(0, h); ctx.closePath();
  ctx.fillStyle = grad;
  ctx.fill();

  // Labels.
  ctx.fillStyle = '#999';
  ctx.font = '10px monospace';
  ctx.textAlign = 'left';
  ctx.fillText(mx.toFixed(2), 4, 12);
  ctx.fillText(mn.toFixed(2), 4, h - 4);
  ctx.textAlign = 'right';
  ctx.fillText(lastVal.toFixed(2), w - 4, 12);
}

function drawPnlChart() {
  const canvas = $('pnl-chart');
  const ctx = canvas.getContext('2d');
  const rect = canvas.parentElement.getBoundingClientRect();
  canvas.width = rect.width - 24;
  canvas.height = rect.height - 24;
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  if (equityData.length < 2) {
    ctx.fillStyle = '#999'; ctx.font = '11px monospace'; ctx.textAlign = 'center';
    ctx.fillText('Collecting data…', w/2, h/2);
    return;
  }

  const baseline = equityData[0][1];
  const pnl = equityData.map(d => d[1] - baseline);
  const mn = Math.min(...pnl, 0);
  const mx = Math.max(...pnl, 0);
  const range = (mx - mn) || 0.01;
  const zeroY = h - ((-mn) / range) * h;

  // Grid + zero line.
  ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--border').trim() || '#e0e0e0';
  ctx.lineWidth = 1;
  for (let i = 0; i < 4; i++) {
    const y = h * i / 3;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
  }
  ctx.strokeStyle = '#999'; ctx.setLineDash([4,3]);
  ctx.beginPath(); ctx.moveTo(0, zeroY); ctx.lineTo(w, zeroY); ctx.stroke();
  ctx.setLineDash([]);

  // Bars.
  const barW = Math.max(1, (w / pnl.length) - 1);
  for (let i = 0; i < pnl.length; i++) {
    const x = (i / pnl.length) * w;
    const val = pnl[i];
    const barH = (Math.abs(val) / range) * h;
    if (val >= 0) {
      ctx.fillStyle = 'rgba(13,158,80,0.6)';
      ctx.fillRect(x, zeroY - barH, barW, barH);
    } else {
      ctx.fillStyle = 'rgba(211,47,47,0.6)';
      ctx.fillRect(x, zeroY, barW, barH);
    }
  }

  // Cumulative P&L line.
  ctx.beginPath();
  for (let i = 0; i < pnl.length; i++) {
    const x = (i / (pnl.length - 1)) * w;
    const y = h - ((pnl[i] - mn) / range) * h;
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  const last = pnl[pnl.length - 1];
  ctx.strokeStyle = last >= 0 ? '#0d9e50' : '#d32f2f';
  ctx.lineWidth = 2;
  ctx.stroke();

  // Labels.
  ctx.fillStyle = '#999'; ctx.font = '10px monospace';
  ctx.textAlign = 'left';
  ctx.fillText((mx >= 0 ? '+' : '') + mx.toFixed(2), 4, 12);
  ctx.fillText((mn >= 0 ? '+' : '') + mn.toFixed(2), 4, h - 4);
  ctx.textAlign = 'right';
  ctx.fillText((last >= 0 ? '+' : '') + last.toFixed(2), w - 4, 12);
  ctx.textAlign = 'center';
  ctx.fillText('0', 14, zeroY - 3);
}

function updateState(s) {
  $('v-cash').textContent = fmtUsd(s.budget.cash);
  $('v-equity').textContent = fmtUsd(s.budget.equity);

  const pnlEl = $('v-pnl');
  pnlEl.textContent = (s.budget.realized_pnl >= 0 ? '+' : '') + fmtUsd(s.budget.realized_pnl);
  pnlEl.className = 'value ' + cls(s.budget.realized_pnl);

  $('v-spend').textContent = fmtUsd(s.budget.llm_spend);
  if (s.inference_spent_usdc !== undefined && s.inference_spent_usdc > 0 &&
      Math.abs(s.inference_spent_usdc - s.budget.llm_spend) > 0.005) {
    $('v-spend').textContent += ' (' + fmtUsd(s.inference_spent_usdc) + ' x402)';
  }

  const dlEl = $('v-dloss');
  dlEl.textContent = fmtUsd(s.budget.daily_loss);
  dlEl.className = 'value ' + (s.budget.daily_loss > 0 ? 'neg' : '');

  $('v-cycles').textContent = s.cycle_count;
  const hrs = Math.floor(s.uptime_seconds / 3600);
  const mins = Math.floor((s.uptime_seconds % 3600) / 60);
  $('v-uptime').textContent = `Uptime: ${hrs}h ${mins}m`;

  const dot = $('dot');
  const statusText = $('status-text');
  statusText.textContent = s.status;
  dot.className = s.status === 'error' ? 'dot err' : 'dot';

  const modeEl = $('mode');
  if (s.live_mode) {
    modeEl.textContent = 'LIVE';
    modeEl.className = 'badge live';
  } else {
    modeEl.textContent = 'PAPER';
    modeEl.className = 'badge paper';
  }

  if (s.wallet) {
    $('v-wallet').textContent = s.wallet || '—';
    $('v-balance').textContent = s.usdc_balance >= 0 ? fmtUsd(s.usdc_balance) : '—';
  }
  const walletSec = $('wallet-section');
  if (walletSec) walletSec.style.display = s.live_mode ? 'block' : 'none';
  if (s.firmware) {
    $('v-fw').textContent = s.firmware;
  }
  if (s.active_model) {
    $('v-model').textContent = s.active_model;
    $('v-model-price').textContent = s.model_price > 0 ? fmtUsd(s.model_price) : '—';
  }

  // Show LLM config panel in paper mode only
  const llmCfg = $('llm-cfg');
  if (llmCfg) {
    llmCfg.style.display = s.live_mode ? 'none' : 'block';
    if (!s.live_mode && s.oai_url && !$('cfg-url').dataset.touched) {
      $('cfg-url').value = s.oai_url || '';
      $('cfg-model').value = s.oai_model || '';
    }
  }

  // Error banner
  const errEl = $('error-banner');
  if (s.last_error) {
    const retry = s.next_retry_sec > 0 ? ` · retrying in ${s.next_retry_sec}s` : '';
    errEl.textContent = '⚠ ' + s.last_error + retry;
    errEl.classList.add('visible');
  } else {
    errEl.classList.remove('visible');
  }

  // Agent name in header
  if (s.agent_name) $('agent-title').textContent = '⟁ ' + s.agent_name;

  // Trading mode buttons highlight
  const isPaper = s.paper_only === 1 || !s.live_mode;
  const pb = $('mode-paper-btn'), rb = $('mode-real-btn');
  if (pb && rb) {
    pb.style.background = isPaper ? 'var(--green)' : '';
    pb.style.color = isPaper ? '#000' : '';
    rb.style.background = !isPaper ? 'var(--green)' : '';
    rb.style.color = !isPaper ? '#000' : '';
  }

  // News search config
  if (s.news_provider) {
    const sel = $('cfg-news-prov');
    if (sel) sel.value = s.news_provider;
  }
}

function updatePositions(positions) {
  const body = $('pos-body');
  if (!positions || positions.length === 0) {
    body.innerHTML = '<tr><td colspan="7" class="empty">No open positions</td></tr>';
    return;
  }
  body.innerHTML = positions.map(p => {
    const pnlCls = p.unrealized_pnl >= 0 ? 'pos' : 'neg';
    const sign = p.unrealized_pnl >= 0 ? '+' : '';
    const q = p.question.length > 50 ? p.question.substring(0, 47) + '…' : p.question;
    return `<tr>
      <td title="${p.question}">${q}</td>
      <td>${p.side.toUpperCase()}</td>
      <td>${p.entry_price.toFixed(2)}</td>
      <td>${p.current_price.toFixed(2)}</td>
      <td class="${pnlCls}">${sign}${fmtUsd(p.unrealized_pnl)}</td>
      <td>${fmtUsd(p.stake_usdc)}</td>
      <td>${p.is_live ? '<span class="badge live">LIVE</span>' : '<span class="badge paper">PAPER</span>'}</td>
    </tr>`;
  }).join('');
}

function updateDecisions(decisions) {
  const log = $('log');
  if (!decisions || decisions.length === 0) {
    log.innerHTML = '<div class="empty">Waiting for first cycle…</div>';
    return;
  }
  log.innerHTML = decisions.slice(0, 30).map(d => {
    const dt = new Date(d.epoch * 1000);
    const time = dt.toLocaleTimeString();
    let typeCls = 'hold';
    if (d.type.includes('buy')) typeCls = 'buy';
    else if (d.type.includes('close')) typeCls = 'close';
    else if (d.type === 'tool_call') typeCls = 'tool';
    const q = d.question ? (d.question.length > 40 ? d.question.substring(0, 37) + '…' : d.question) : '';
    return `<div class="log-entry">
      <span class="log-time">${time}</span>
      <span class="log-type ${typeCls}">${d.type}</span>
      ${q ? '<span>' + q + '</span>' : ''}
      ${d.confidence ? ' <span style="color:var(--blue)">' + (d.confidence*100).toFixed(0) + '%</span>' : ''}
      ${d.rationale ? '<div style="color:var(--dim);margin-top:2px;font-size:10px">' + d.rationale.substring(0, 120) + '</div>' : ''}
    </div>`;
  }).join('');
}

function updateScouted(list) {
  const el = $('scout-body');
  if (!list || list.length === 0) {
    el.innerHTML = '<div class="empty">No markets scanned yet</div>';
    return;
  }
  el.innerHTML = list.map(s => {
    const q = s.question ? (s.question.length > 60 ? s.question.substring(0, 57) + '…' : s.question) : s.market_id;
    const sig = s.signal || 'neutral';
    const edge = s.edge_bps ? s.edge_bps.toFixed(0) + 'bp' : '—';
    const conf = s.confidence ? (s.confidence * 100).toFixed(0) + '%' : '—';
    const vol = s.volume > 1000 ? (s.volume / 1000).toFixed(0) + 'k' : s.volume.toFixed(0);
    const liq = s.liquidity > 1000 ? (s.liquidity / 1000).toFixed(0) + 'k' : s.liquidity.toFixed(0);
    const note = s.note ? s.note.substring(0, 80) : '';
    return `<div class="scout-card">
      <span class="scout-signal ${sig}" title="${sig}"></span>
      <div>
        <div class="scout-q" title="${s.question || ''}">${q}</div>
        <div class="scout-meta">
          <span>Edge: ${edge}</span>
          <span>Vol: $${vol}</span>
          <span>Liq: $${liq}</span>
          ${note ? '<span>— ' + note + '</span>' : ''}
        </div>
      </div>
      <div class="scout-right">
        <div class="scout-price">${s.yes_price ? (s.yes_price * 100).toFixed(0) + '¢' : '—'}</div>
        <div class="scout-conf">${conf}</div>
      </div>
    </div>`;
  }).join('');
}

function updateWisdom(w) {
  const el = (id) => document.getElementById(id);
  el('w-total').textContent = w.total_tracked || 0;
  el('w-resolved').textContent = w.total_resolved || 0;
  const acc = w.total_resolved > 0 ? ((w.total_correct / w.total_resolved) * 100).toFixed(0) + '%' : '–';
  el('w-accuracy').textContent = acc;
  const ba = w.buy_resolved > 0 ? ((w.buy_correct / w.buy_resolved) * 100).toFixed(0) + '%' : '–';
  el('w-buys').textContent = ba;
  const ha = w.hold_resolved > 0 ? ((w.hold_correct / w.hold_resolved) * 100).toFixed(0) + '%' : '–';
  el('w-holds').textContent = ha;
  el('w-rules').textContent = w.wisdom_text || 'No learned rules yet…';
  // Frozen state.
  el('w-freeze').checked = !!w.frozen;
  el('w-frozen-badge').style.display = w.frozen ? 'inline-block' : 'none';
  // Model history.
  if (w.models && w.models.length > 0) {
    el('w-models').innerHTML = '🧠 Models: ' + w.models.map(m =>
      '<b>' + m.name + '</b> (' + m.decisions + ' decisions)'
    ).join(' → ');
  }
}

function toggleFreeze(frozen) {
  fetch('/api/wisdom/freeze', {method:'POST', body:JSON.stringify({frozen:frozen}),
    headers: authHeaders()})
    .then(r => r.json())
    .then(d => {
      document.getElementById('w-frozen-badge').style.display = d.frozen ? 'inline-block' : 'none';
    })
    .catch(() => {});
}

function downloadKnowledge() {
  const a = document.createElement('a');
  a.href = '/api/knowledge';
  a.download = 'survaiv-knowledge.json';
  a.click();
}

function importKnowledge(input) {
  const file = input.files[0];
  if (!file) return;
  const status = document.getElementById('kb-status');
  status.textContent = 'Uploading…';
  status.style.color = 'var(--dim)';
  const reader = new FileReader();
  reader.onload = function(e) {
    fetch('/api/knowledge', {method:'POST', body:e.target.result,
      headers: authHeaders()})
      .then(r => r.json())
      .then(d => {
        if (d.ok) {
          status.textContent = '✓ ' + d.msg;
          status.style.color = 'var(--green)';
          poll();
        } else {
          status.textContent = '✗ Import failed';
          status.style.color = 'var(--red)';
        }
      })
      .catch(() => { status.textContent = '✗ Upload error'; status.style.color = 'var(--red)'; });
  };
  reader.readAsText(file);
  input.value = '';
}

function poll() {
  fetch('/api/state').then(r => r.json()).then(updateState).catch(() => {});
  fetch('/api/positions').then(r => r.json()).then(updatePositions).catch(() => {});
  fetch('/api/history').then(r => r.json()).then(updateDecisions).catch(() => {});
  fetch('/api/scouted').then(r => r.json()).then(updateScouted).catch(() => {});
  fetch('/api/wisdom').then(r => r.json()).then(updateWisdom).catch(() => {});
  fetch('/api/equity').then(r => r.json()).then(data => {
    equityData = data;
    drawChart();
    drawPnlChart();
  }).catch(() => {});
}

// SSE for real-time updates.
function connectSSE() {
  const es = new EventSource('/api/events');
  es.onmessage = function(e) {
    try {
      const data = JSON.parse(e.data);
      if (data.budget) updateState(data);
    } catch(err) {}
  };
  es.addEventListener('state', function(e) {
    try { updateState(JSON.parse(e.data)); } catch(err) {}
  });
  es.addEventListener('positions', function(e) {
    try { updatePositions(JSON.parse(e.data)); } catch(err) {}
  });
  es.addEventListener('scouted', function(e) {
    try { updateScouted(JSON.parse(e.data)); } catch(err) {}
  });
  es.addEventListener('wisdom', function(e) {
    try { updateWisdom(JSON.parse(e.data)); } catch(err) {}
  });
  es.addEventListener('decision', function(e) {
    try {
      const d = JSON.parse(e.data);
      // Prepend to log.
      const log = $('log');
      if (log.querySelector('.empty')) log.innerHTML = '';
      const dt = new Date(d.epoch * 1000);
      let typeCls = 'hold';
      if (d.type.includes('buy')) typeCls = 'buy';
      else if (d.type.includes('close')) typeCls = 'close';
      else if (d.type === 'tool_call') typeCls = 'tool';
      const entry = document.createElement('div');
      entry.className = 'log-entry';
      entry.innerHTML = `<span class="log-time">${dt.toLocaleTimeString()}</span>
        <span class="log-type ${typeCls}">${d.type}</span>
        ${d.rationale ? '<div style="color:var(--dim);margin-top:2px;font-size:10px">' + d.rationale.substring(0, 120) + '</div>' : ''}`;
      log.prepend(entry);
    } catch(err) {}
  });
  es.onerror = function() {
    $('dot').className = 'dot err';
    $('status-text').textContent = 'disconnected';
    es.close();
    setTimeout(connectSSE, 5000);
  };
}

function restoreConfig(file) {
  if (!file) return;
  const msg = $('settings-msg');
  msg.textContent = 'Restoring…';
  msg.style.color = 'var(--blue)';
  file.text().then(text => {
    return fetch('/api/restore', {method:'POST', headers: authHeaders(), body:text});
  }).then(r => {
    if (!r.ok) throw new Error('Restore failed');
    msg.textContent = 'Restored! Rebooting…';
    msg.style.color = 'var(--green)';
  }).catch(e => {
    msg.textContent = 'Error: ' + e.message;
    msg.style.color = 'var(--red)';
  });
}

function saveLlmConfig() {
  const msg = $('llm-cfg-msg');
  const body = {};
  const url = $('cfg-url').value.trim();
  const model = $('cfg-model').value.trim();
  const key = $('cfg-key').value.trim();
  if (url) body.oai_url = url;
  if (model) body.oai_model = model;
  if (key) body.api_key = key;
  if (!Object.keys(body).length) { msg.textContent = 'Nothing to change'; return; }
  msg.textContent = 'Saving…';
  msg.style.color = 'var(--blue)';
  fetch('/api/llm-config', {method:'POST', headers: authHeaders(), body:JSON.stringify(body)})
    .then(r => r.json())
    .then(d => {
      if (d.ok) {
        msg.textContent = 'Applied: ' + d.oai_model + ' @ ' + d.oai_url;
        msg.style.color = 'var(--green)';
        $('cfg-url').value = d.oai_url;
        $('cfg-model').value = d.oai_model;
        $('cfg-key').value = '';
        $('cfg-url').dataset.touched = '1';
      } else {
        msg.textContent = 'Error: ' + (d.error || 'unknown');
        msg.style.color = 'var(--red)';
      }
    }).catch(e => {
      msg.textContent = 'Error: ' + e.message;
      msg.style.color = 'var(--red)';
    });
}

const llmPresets = {
  local:      {url:'http://192.168.1.100:8080', model:'local-model', key:''},
  openrouter: {url:'https://openrouter.ai/api', model:'meta-llama/llama-4-scout', key:''},
  openai:     {url:'https://api.openai.com',    model:'gpt-4.1-mini', key:''},
  groq:       {url:'https://api.groq.com/openai', model:'llama-3.3-70b-versatile', key:''},
  x402:       {url:'https://tx402.ai',           model:'auto', key:''}
};
function setLlmPreset(name) {
  const p = llmPresets[name];
  if (!p) return;
  $('cfg-url').value = p.url;
  $('cfg-url').dataset.touched = '1';
  $('cfg-model').value = p.model;
  $('cfg-key').value = p.key;
  $('llm-cfg-msg').textContent = name + ' preset loaded — edit and Apply';
  $('llm-cfg-msg').style.color = 'var(--blue)';
}

function saveNewsConfig() {
  const msg = $('news-cfg-msg');
  const body = {news_prov: $('cfg-news-prov').value, news_key: $('cfg-news-key').value};
  msg.textContent = 'Saving\u2026';
  msg.style.color = 'var(--blue)';
  fetch('/api/config', {method:'POST', body:JSON.stringify(body),
    headers:{'Content-Type':'application/json','X-Auth-Token':authToken}})
    .then(r => r.json()).then(() => {
      msg.textContent = '\u2713 Saved';
      msg.style.color = 'var(--green)';
      $('cfg-news-key').value = '';
      setTimeout(() => msg.textContent = '', 3000);
    }).catch(() => {
      msg.textContent = '\u2717 Failed';
      msg.style.color = 'var(--red)';
    });
}

function otaUpdate(file) {
  if (!file) return;
  if (!confirm('Flash ' + file.name + ' (' + (file.size/1024).toFixed(0) + ' KB)? Device will reboot.')) return;
  const msg = $('settings-msg');
  msg.textContent = 'Uploading firmware… 0%';
  msg.style.color = 'var(--blue)';
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/ota');
  if (authToken) xhr.setRequestHeader('X-Auth-Token', authToken);
  xhr.upload.onprogress = e => {
    if (e.lengthComputable) msg.textContent = 'Uploading firmware… ' + Math.round(e.loaded/e.total*100) + '%';
  };
  xhr.onload = () => {
    if (xhr.status === 200) {
      msg.textContent = 'OTA success! Rebooting…';
      msg.style.color = 'var(--green)';
    } else {
      msg.textContent = 'OTA failed: ' + xhr.responseText;
      msg.style.color = 'var(--red)';
    }
  };
  xhr.onerror = () => { msg.textContent = 'Upload failed'; msg.style.color = 'var(--red)'; };
  xhr.send(file);
}

// Dashboard initialization (called after auth).
function initDashboard() {
  poll();
  connectSSE();
  setInterval(poll, 15000);
  window.addEventListener('resize', () => { drawChart(); drawPnlChart(); });
}

// Start with auth check.
checkAuth();
</script>
</body>
</html>)rawhtml";

// ─── Onboarding Wizard ──────────────────────────────────────────────
static const char kOnboardHtml[] = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>survaiv setup</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'SF Mono',Monaco,monospace;background:#fafafa;color:#1a1a1a;
font-size:14px;min-height:100vh;display:flex;align-items:center;justify-content:center}
.wizard{width:100%;max-width:440px;padding:24px}
h1{font-size:20px;color:#0d9e50;letter-spacing:2px;margin-bottom:4px}
.sub{color:#999;font-size:12px;margin-bottom:24px}
.step{display:none}.step.active{display:block}
label{display:block;font-size:11px;text-transform:uppercase;letter-spacing:1px;color:#888;
margin:14px 0 4px}
input,select{width:100%;padding:10px 12px;background:#fff;border:1px solid #e0e0e0;
border-radius:6px;color:#1a1a1a;font-family:inherit;font-size:13px;outline:none}
input:focus{border-color:#1565c0}
.btn{display:inline-block;padding:10px 24px;background:#0d9e50;color:#fff;border:none;
border-radius:6px;font-family:inherit;font-size:13px;font-weight:700;cursor:pointer;
margin-top:20px;letter-spacing:1px}
.btn:hover{background:#0a7a3e}
.btn.secondary{background:transparent;border:1px solid #e0e0e0;color:#888}
.btn.secondary:hover{border-color:#1565c0;color:#1a1a1a}
.btns{display:flex;gap:10px;margin-top:20px}
.networks{max-height:200px;overflow-y:auto;margin:8px 0}
.net{padding:8px 12px;border:1px solid #e0e0e0;border-radius:6px;margin:4px 0;cursor:pointer;
display:flex;justify-content:space-between}
.net:hover{border-color:#1565c0;background:#f5f5f5}
.net.selected{border-color:#0d9e50;background:#e8f5e9}
.signal{color:#999;font-size:11px}
.note{color:#888;font-size:11px;margin-top:8px;line-height:1.4}
.progress{display:flex;gap:6px;margin-bottom:20px}
.progress .dot{width:8px;height:8px;border-radius:50%;background:#e0e0e0}
.progress .dot.done{background:#0d9e50}
.progress .dot.current{background:#1565c0}
.spinner{display:none;text-align:center;padding:20px;color:#999}
.spinner.active{display:block}
.error{color:#d32f2f;font-size:12px;margin-top:8px;display:none}
.mode-card:hover{border-color:#1565c0;background:#f5f5f5}
.mode-card.selected{border-color:#0d9e50;background:#e8f5e9}
</style>
</head>
<body>
<div class="wizard">
  <h1>⟁ SURVAIV</h1>
  <div class="sub">Autonomous agent setup</div>

  <div class="progress" id="progress">
    <div class="dot current"></div><div class="dot"></div><div class="dot"></div><div class="dot"></div><div class="dot"></div>
  </div>

  <!-- Step 1: WiFi -->
  <div class="step active" id="step1">
    <h2 style="font-size:14px;margin-bottom:12px">Name Your Agent</h2>
    <div style="display:flex;gap:8px;align-items:end;margin-bottom:16px">
      <div style="flex:1">
        <label for="agent-name">Agent Name</label>
        <input type="text" id="agent-name" placeholder="Loading...">
      </div>
      <button class="btn secondary" onclick="randomName()" style="margin:0;padding:8px 12px;font-size:11px;white-space:nowrap">🎲 Random</button>
    </div>
    <hr style="border:none;border-top:1px solid #e0e0e0;margin-bottom:16px">
    <h2 style="font-size:14px;margin-bottom:12px">Wi-Fi Network</h2>
    <div class="spinner active" id="scan-spinner">Scanning networks…</div>
    <div class="networks" id="networks"></div>
    <label for="wifi-pass">Password</label>
    <input type="password" id="wifi-pass" placeholder="Enter Wi-Fi password">
    <div class="error" id="wifi-err"></div>
    <div class="btns">
      <button class="btn" onclick="nextStep(2)">Next →</button>
    </div>
  </div>

  <!-- Step 2: Trading Mode -->
  <div class="step" id="step2">
    <h2 style="font-size:14px;margin-bottom:12px">Trading Mode</h2>
    <div class="note" style="margin-bottom:14px">Choose how the agent trades. You can always switch later.</div>
    <div style="display:flex;gap:10px">
      <div class="mode-card" id="mode-real" onclick="selectMode('real')" style="flex:1;padding:16px;border:2px solid #e0e0e0;border-radius:8px;cursor:pointer;text-align:center">
        <div style="font-size:20px;margin-bottom:6px">⟁</div>
        <div style="font-weight:700;font-size:13px;margin-bottom:4px">Real Trading</div>
        <div style="font-size:11px;color:#888;line-height:1.4">Live on Polymarket.<br>LLM paid with USDC via x402.<br>Requires funded wallet.</div>
      </div>
      <div class="mode-card" id="mode-paper" onclick="selectMode('paper')" style="flex:1;padding:16px;border:2px solid #e0e0e0;border-radius:8px;cursor:pointer;text-align:center">
        <div style="font-size:20px;margin-bottom:6px">📝</div>
        <div style="font-weight:700;font-size:13px;margin-bottom:4px">Paper Trading</div>
        <div style="font-size:11px;color:#888;line-height:1.4">Simulated trades only.<br>Use your own LLM API.<br>Costs tracked virtually.</div>
      </div>
    </div>
    <div class="btns">
      <button class="btn secondary" onclick="nextStep(1)">← Back</button>
      <button class="btn" id="mode-next-btn" onclick="nextStep(3)" disabled style="opacity:0.5">Next →</button>
    </div>
  </div>

  <!-- Step 3: LLM Configuration -->
  <div class="step" id="step3">
    <h2 style="font-size:14px;margin-bottom:12px">LLM Configuration</h2>
    <div id="llm-real" style="display:none">
      <div style="display:flex;gap:8px;margin-bottom:12px">
        <button class="btn" id="x402-prov-tx402" onclick="selectX402Provider('tx402')"
          style="flex:1;margin:0;padding:8px;font-size:11px;background:#1565c0">tx402.ai</button>
        <button class="btn secondary" id="x402-prov-engine" onclick="selectX402Provider('engine')"
          style="flex:1;margin:0;padding:8px;font-size:11px">x402engine.app</button>
      </div>
      <div style="background:#e8f5e9;border:1px solid #0d9e50;border-radius:6px;padding:10px 12px;margin-bottom:12px;font-size:11px;line-height:1.5">
        <b>x402 — wallet-as-auth.</b> No API key needed. Your wallet pays per-request with USDC on Base.
      </div>
      <label for="oai-model-real">Model</label>
      <select id="oai-model-real" style="width:100%;padding:10px 12px;background:#fff;border:1px solid #e0e0e0;border-radius:6px;font-family:inherit;font-size:13px">
      </select>
      <div class="note" style="margin-top:8px">Recommended: <b>deepseek-v3.2</b> — best reasoning/cost ratio for trading.</div>
    </div>
    <div id="llm-paper" style="display:none">
      <div style="background:#fff8e1;border:1px solid #e6a700;border-radius:6px;padding:10px 12px;margin-bottom:12px;font-size:11px;line-height:1.5">
        <b>Paper mode</b> — provide your own OpenAI-compatible API. Inference costs will be <b>simulated</b> based on tx402.ai pricing (~$0.0005/request).
      </div>
      <label for="oai-url">API Base URL</label>
      <input type="url" id="oai-url" placeholder="https://api.openai.com/v1">
      <label for="oai-model">Model Name</label>
      <input type="text" id="oai-model" placeholder="gpt-4o-mini">
      <label for="api-key">API Key</label>
      <input type="password" id="api-key" placeholder="sk-...">
    </div>
    <div class="btns">
      <button class="btn secondary" onclick="nextStep(2)">← Back</button>
      <button class="btn" onclick="nextStep(tradingMode==='real'?4:5)">Next →</button>
    </div>
  </div>

  <!-- Step 4: Wallet -->
  <div class="step" id="step4">
    <h2 style="font-size:14px;margin-bottom:12px">Wallet</h2>
    <div class="note" style="margin-bottom:12px">Generate a fresh wallet on the device (recommended — key never leaves the board),
      or paste an existing private key.</div>
    <button class="btn" onclick="generateWallet()" id="gen-btn" style="margin-top:0;margin-bottom:12px">🔑 Generate on device</button>
    <div id="gen-result" style="display:none;background:#e8f5e9;border:1px solid #0d9e50;border-radius:6px;padding:10px 12px;margin-bottom:12px;font-size:12px">
      <div style="font-weight:700;margin-bottom:4px">Wallet generated!</div>
      <div style="word-break:break-all" id="gen-addr"></div>
      <div class="note" style="margin-top:6px">Fund this address with USDC on <b>Base</b> (for LLM inference) and USDC.e + MATIC on <b>Polygon</b> (for trading).</div>
    </div>
    <div id="gen-exists" style="display:none;background:#fff8e1;border:1px solid #e6a700;border-radius:6px;padding:10px 12px;margin-bottom:12px;font-size:12px">
      <div style="font-weight:700;margin-bottom:4px">⚠ Existing wallet found</div>
      <div style="word-break:break-all" id="exists-addr"></div>
      <div class="note" style="margin-top:6px">
        <button class="btn secondary" style="margin:0;padding:4px 12px;font-size:11px" onclick="generateWallet(true)">Overwrite with new key</button>
      </div>
    </div>
    <label for="wallet-pk">Or paste Private Key (hex)</label>
    <input type="password" id="wallet-pk" placeholder="64 hex chars (optional)">
    <label for="bankroll">Starting Bankroll (USDC)</label>
    <input type="number" id="bankroll" value="25" min="1" max="100000" step="0.01">
    <div class="btns">
      <button class="btn secondary" onclick="nextStep(3)">← Back</button>
      <button class="btn" onclick="nextStep(5)">Next →</button>
    </div>
  </div>

  <!-- Step 5: Confirm -->
  <div class="step" id="step5">
    <h2 style="font-size:14px;margin-bottom:12px">Confirm &amp; Launch</h2>
    <div id="summary" style="background:#f5f5f5;border:1px solid #e0e0e0;border-radius:8px;
      padding:14px;font-size:12px;line-height:1.8"></div>
    <div class="error" id="save-err"></div>
    <div class="spinner" id="save-spinner">Saving configuration…</div>
    <div class="btns">
      <button class="btn secondary" onclick="nextStep(tradingMode==='real'?4:3)">← Back</button>
      <button class="btn" id="save-btn" onclick="saveConfig()">Save &amp; Reboot ⟶</button>
    </div>
  </div>
</div>

<script>
let selectedSsid = '';
let generatedAddress = '';
let tradingMode = '';
let x402Provider = 'tx402';

const adjectives = ['swift','brave','calm','dark','eager','fair','glad','happy','keen','loud','neat','proud','quiet','rare','safe','tall','vast','warm','wise','young'];
const animals = ['bear','crow','deer','eagle','fox','hawk','lion','lynx','moth','newt','orca','puma','raven','seal','tiger','viper','whale','wolf','wren','yak'];
function randomName() {
  const a = adjectives[Math.floor(Math.random()*adjectives.length)];
  const b = animals[Math.floor(Math.random()*animals.length)];
  document.getElementById('agent-name').value = a + '-' + b;
}
randomName();

const tx402Models = [
  {id:'deepseek/deepseek-v3.2',name:'DeepSeek V3.2',price:0.0005},
  {id:'qwen/qwen3-235b-a22b-2507',name:'Qwen3 235B',price:0.000335},
  {id:'meta-llama/llama-4-maverick',name:'Llama 4 Maverick',price:0.000511},
  {id:'openai/gpt-oss-120b',name:'GPT-OSS 120B',price:0.00015},
  {id:'openai/gpt-oss-20b',name:'GPT-OSS 20B',price:0.000107},
  {id:'meta-llama/llama-3.3-70b-instruct',name:'Llama 3.3 70B',price:0.00026},
  {id:'qwen/qwen-2.5-72b-instruct',name:'Qwen 2.5 72B',price:0.000207},
  {id:'deepseek/deepseek-chat-v3.1',name:'DeepSeek Chat V3.1',price:0.000625},
  {id:'deepseek/deepseek-r1-0528',name:'DeepSeek R1',price:0.002038},
  {id:'moonshotai/kimi-k2.5',name:'Kimi K2.5',price:0.002063}
];

const engineModels = [
  {id:'llama',name:'Llama 3.3 70B',price:0.002},
  {id:'gpt-5-nano',name:'GPT-5 Nano',price:0.002},
  {id:'llama-4-maverick',name:'Llama 4 Maverick',price:0.003},
  {id:'gpt-4o-mini',name:'GPT-4o Mini',price:0.003},
  {id:'qwen',name:'Qwen3 235B',price:0.004},
  {id:'grok-4-fast',name:'Grok 4 Fast',price:0.004},
  {id:'deepseek',name:'DeepSeek V3',price:0.005},
  {id:'deepseek-v3.2',name:'DeepSeek V3.2',price:0.005},
  {id:'mistral',name:'Mistral Large 3',price:0.006},
  {id:'gemini-flash',name:'Gemini 2.5 Flash',price:0.009},
  {id:'deepseek-r1',name:'DeepSeek R1',price:0.01},
  {id:'claude-haiku',name:'Claude Haiku 4.5',price:0.02},
  {id:'gpt-4o',name:'GPT-4o',price:0.04}
];

const x402Prices = {};
tx402Models.forEach(m => x402Prices[m.id] = m.price);
engineModels.forEach(m => x402Prices[m.id] = m.price);
x402Prices['default'] = 0.0005;

function selectX402Provider(prov) {
  x402Provider = prov;
  document.getElementById('x402-prov-tx402').className = prov==='tx402' ? 'btn' : 'btn secondary';
  document.getElementById('x402-prov-tx402').style.background = prov==='tx402' ? '#1565c0' : '';
  document.getElementById('x402-prov-engine').className = prov==='engine' ? 'btn' : 'btn secondary';
  document.getElementById('x402-prov-engine').style.background = prov==='engine' ? '#1565c0' : '';
  const sel = document.getElementById('oai-model-real');
  sel.innerHTML = '';
  const models = prov==='tx402' ? tx402Models : engineModels;
  models.forEach(m => {
    const opt = document.createElement('option');
    opt.value = m.id;
    opt.textContent = m.name + ' \u2014 $' + m.price + '/req';
    sel.appendChild(opt);
  });
}

function selectMode(mode) {
  tradingMode = mode;
  document.getElementById('mode-real').classList.toggle('selected', mode === 'real');
  document.getElementById('mode-paper').classList.toggle('selected', mode === 'paper');
  const btn = document.getElementById('mode-next-btn');
  btn.disabled = false;
  btn.style.opacity = '1';
}

function nextStep(n) {
  if (n === 3) {
    document.getElementById('llm-real').style.display = tradingMode === 'real' ? 'block' : 'none';
    document.getElementById('llm-paper').style.display = tradingMode === 'paper' ? 'block' : 'none';
    if (tradingMode === 'real') selectX402Provider(x402Provider);
  }
  document.querySelectorAll('.step').forEach(s => s.classList.remove('active'));
  document.getElementById('step' + n).classList.add('active');
  const dots = document.querySelectorAll('.progress .dot');
  dots.forEach((d, i) => {
    d.className = 'dot';
    if (i < n - 1) d.className = 'dot done';
    if (i === n - 1) d.className = 'dot current';
  });
  if (n === 5) buildSummary();
}

function scanNetworks() {
  fetch('/api/scan').then(r => r.json()).then(nets => {
    document.getElementById('scan-spinner').classList.remove('active');
    const container = document.getElementById('networks');
    if (!nets.length) {
      container.innerHTML = '<div class="note">No networks found. Refresh to rescan.</div>';
      return;
    }
    container.innerHTML = nets.map(n =>
      `<div class="net" onclick="selectNet(this,'${n.ssid}')" data-ssid="${n.ssid}">
        <span>${n.ssid}</span>
        <span class="signal">${n.rssi} dBm</span>
      </div>`
    ).join('');
  }).catch(() => {
    document.getElementById('scan-spinner').textContent = 'Scan failed. Retrying…';
    setTimeout(scanNetworks, 3000);
  });
}

function selectNet(el, ssid) {
  document.querySelectorAll('.net').forEach(n => n.classList.remove('selected'));
  el.classList.add('selected');
  selectedSsid = ssid;
}

function generateWallet(force) {
  const btn = document.getElementById('gen-btn');
  btn.innerHTML = '<span class="spin"></span> Deriving key pair — ~10s on this chip…';
  btn.disabled = true;
  const url = '/api/generate-wallet' + (force ? '?force=1' : '');
  fetch(url, {method:'POST'}).then(r => r.json()).then(data => {
    if (data.exists && !data.generated) {
      document.getElementById('gen-exists').style.display = 'block';
      document.getElementById('gen-result').style.display = 'none';
      document.getElementById('exists-addr').textContent = data.address;
      generatedAddress = data.address;
      btn.style.display = 'none';
    } else {
      document.getElementById('gen-result').style.display = 'block';
      document.getElementById('gen-exists').style.display = 'none';
      document.getElementById('gen-addr').textContent = data.address;
      generatedAddress = data.address;
      btn.style.display = 'none';
    }
  }).catch(() => {
    btn.textContent = 'Generation failed — retry';
    btn.disabled = false;
  });
}

function buildSummary() {
  let modelName, provLabel, apiUrl, costNote = '';
  if (tradingMode === 'real') {
    modelName = document.getElementById('oai-model-real').value;
    provLabel = x402Provider === 'engine' ? 'x402engine.app (x402)' : 'tx402.ai (x402)';
    apiUrl = x402Provider === 'engine' ? 'https://x402-gateway-production.up.railway.app' : 'https://tx402.ai/v1';
    const price = x402Prices[modelName] || x402Prices['default'];
    costNote = `<b>Est. cost:</b> ~$${price.toFixed(6)}/request (paid via x402)<br>`;
  } else {
    modelName = document.getElementById('oai-model').value || '(not set)';
    provLabel = 'API Key';
    apiUrl = document.getElementById('oai-url').value || '(not set)';
    costNote = '<b>Cost tracking:</b> simulated (~$0.0005/request)<br>';
  }
  const pk = document.getElementById('wallet-pk').value;
  let walletLine = '';
  if (tradingMode === 'real') {
    if (generatedAddress) walletLine = `<b>Wallet:</b> ${generatedAddress} (on-device)<br>`;
    else if (pk.length === 64) walletLine = `<b>Wallet:</b> imported key<br>`;
  }
  const modeLabel = tradingMode === 'real' ? 'REAL' : 'PAPER';
  const modeColor = tradingMode === 'real' ? '#0d9e50' : '#e6a700';
  document.getElementById('summary').innerHTML =
    `<b>Wi-Fi:</b> ${selectedSsid || '(not selected)'}<br>` +
    `<b>Mode:</b> <span style="color:${modeColor}">${modeLabel}</span><br>` +
    `<b>Provider:</b> ${provLabel}<br>` +
    `<b>API:</b> ${apiUrl}<br>` +
    `<b>Model:</b> ${modelName}<br>` +
    walletLine +
    costNote +
    `<b>Bankroll:</b> $${parseFloat(document.getElementById('bankroll').value).toFixed(2)} USDC`;
}

function saveConfig() {
  if (!selectedSsid) {
    document.getElementById('save-err').textContent = 'Please select a Wi-Fi network.';
    document.getElementById('save-err').style.display = 'block';
    return;
  }
  document.getElementById('save-spinner').classList.add('active');
  document.getElementById('save-btn').style.display = 'none';

  const bankroll = parseFloat(document.getElementById('bankroll').value) || 25;
  const pk = document.getElementById('wallet-pk').value;
  const isReal = tradingMode === 'real';
  const payload = {
    wifi_ssid: selectedSsid,
    wifi_pass: document.getElementById('wifi-pass').value,
    oai_url: isReal ? (x402Provider === 'engine' ? 'https://x402-gateway-production.up.railway.app' : 'https://tx402.ai/v1') : document.getElementById('oai-url').value,
    oai_model: isReal ? document.getElementById('oai-model-real').value : document.getElementById('oai-model').value,
    api_key: isReal ? '' : document.getElementById('api-key').value,
    llm_provider: isReal ? 'x402' : 'apikey',
    wallet_pk: pk.length === 64 ? pk : '',
    bankroll_cents: Math.round(bankroll * 100),
    paper_only: isReal ? 0 : 1,
    agent_name: document.getElementById('agent-name').value.trim() || 'survaiv'
  };

  fetch('/api/save', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(payload)
  }).then(r => {
    if (!r.ok) throw new Error('Save failed');
    document.getElementById('save-spinner').textContent = 'Saved! Rebooting into station mode…';
  }).catch(err => {
    document.getElementById('save-err').textContent = 'Save failed: ' + err.message;
    document.getElementById('save-err').style.display = 'block';
    document.getElementById('save-spinner').classList.remove('active');
    document.getElementById('save-btn').style.display = '';
  });
}

scanNetworks();
</script>
</body>
</html>)rawhtml";

// ─── Captive portal redirect ─────────────────────────────────────
static const char kCaptiveRedirectHtml[] = R"rawhtml(<!DOCTYPE html>
<html><head><meta http-equiv="refresh" content="0;url=http://192.168.4.1/"></head>
<body>Redirecting to <a href="http://192.168.4.1/">setup</a>…</body></html>)rawhtml";

}  // namespace survaiv
