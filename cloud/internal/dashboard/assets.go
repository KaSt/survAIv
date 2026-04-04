package dashboard

// IndexHTML is the embedded dashboard HTML.
var IndexHTML = []byte(`<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>⟁ SURVAIV Dashboard</title>
<style>
:root {
  --bg: #0d1117; --bg2: #161b22; --bg3: #21262d; --fg: #c9d1d9; --fg2: #8b949e;
  --accent: #58a6ff; --green: #3fb950; --red: #f85149; --yellow: #d29922;
  --border: #30363d; --card-bg: linear-gradient(135deg, #161b22 0%, #1c2128 100%);
  --radius: 10px; --shadow: 0 2px 8px rgba(0,0,0,0.3);
}
.light {
  --bg: #f6f8fa; --bg2: #ffffff; --bg3: #f0f2f5; --fg: #24292f; --fg2: #656d76;
  --accent: #0969da; --green: #1a7f37; --red: #cf222e; --yellow: #9a6700;
  --border: #d0d7de; --card-bg: linear-gradient(135deg, #ffffff 0%, #f6f8fa 100%);
  --shadow: 0 2px 8px rgba(0,0,0,0.08);
}
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif;
  background: var(--bg); color: var(--fg); line-height: 1.5; min-height: 100vh; }
.container { max-width: 1400px; margin: 0 auto; padding: 16px; }

/* Header */
header { display: flex; align-items: center; justify-content: space-between; padding: 12px 0;
  border-bottom: 1px solid var(--border); margin-bottom: 16px; flex-wrap: wrap; gap: 8px; }
.logo { font-size: 1.4em; font-weight: 700; background: linear-gradient(135deg, var(--accent), var(--green));
  -webkit-background-clip: text; -webkit-text-fill-color: transparent; background-clip: text; }
.status-badge { padding: 4px 12px; border-radius: 20px; font-size: 0.8em; font-weight: 600; }
.status-ok { background: rgba(63,185,80,0.15); color: var(--green); }
.status-err { background: rgba(248,81,73,0.15); color: var(--red); }
.header-meta { font-size: 0.85em; color: var(--fg2); display: flex; gap: 16px; align-items: center; }
.header-actions { display: flex; gap: 8px; }
.btn { padding: 6px 14px; border-radius: 6px; border: 1px solid var(--border); background: var(--bg2);
  color: var(--fg); cursor: pointer; font-size: 0.82em; transition: all 0.2s; }
.btn:hover { border-color: var(--accent); color: var(--accent); }

/* Cards */
.cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
  gap: 12px; margin-bottom: 16px; }
.card { background: var(--card-bg); border: 1px solid var(--border); border-radius: var(--radius);
  padding: 14px 16px; box-shadow: var(--shadow); }
.card-label { font-size: 0.72em; text-transform: uppercase; letter-spacing: 0.05em;
  color: var(--fg2); margin-bottom: 4px; }
.card-value { font-size: 1.5em; font-weight: 700; }
.card-sub { font-size: 0.75em; color: var(--fg2); margin-top: 2px; }
.v-green { color: var(--green); }
.v-red { color: var(--red); }
.v-yellow { color: var(--yellow); }
.v-accent { color: var(--accent); }

/* Sections */
.section { background: var(--bg2); border: 1px solid var(--border); border-radius: var(--radius);
  padding: 16px; margin-bottom: 16px; box-shadow: var(--shadow); }
.section-title { font-size: 0.9em; font-weight: 600; color: var(--accent); margin-bottom: 12px;
  display: flex; align-items: center; gap: 6px; }

/* Tables */
table { width: 100%; border-collapse: collapse; font-size: 0.85em; }
th { text-align: left; padding: 8px 10px; color: var(--fg2); font-weight: 500;
  border-bottom: 1px solid var(--border); font-size: 0.78em; text-transform: uppercase; }
td { padding: 8px 10px; border-bottom: 1px solid var(--border); }
tr:hover td { background: var(--bg3); }
.side-yes { color: var(--green); font-weight: 600; }
.side-no { color: var(--red); font-weight: 600; }

/* Scouted Markets */
.scouted-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(320px, 1fr)); gap: 8px; }
.scouted-item { display: flex; align-items: center; gap: 8px; padding: 8px 10px;
  border-radius: 6px; background: var(--bg3); font-size: 0.84em; }
.signal-dot { width: 8px; height: 8px; border-radius: 50%; flex-shrink: 0; }
.sig-bullish { background: var(--green); box-shadow: 0 0 6px var(--green); }
.sig-bearish { background: var(--red); box-shadow: 0 0 6px var(--red); }
.sig-skip { background: var(--fg2); }
.sig-neutral { background: var(--yellow); }
.scouted-q { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.scouted-meta { color: var(--fg2); font-size: 0.82em; white-space: nowrap; }

/* Decision Log */
.log-list { max-height: 320px; overflow-y: auto; }
.log-item { padding: 6px 0; border-bottom: 1px solid var(--border); font-size: 0.84em; }
.log-type { font-weight: 600; display: inline-block; min-width: 90px; }
.log-type-hold { color: var(--yellow); }
.log-type-buy { color: var(--green); }
.log-type-close { color: var(--red); }
.log-type-tool { color: var(--accent); }
.log-rationale { color: var(--fg2); font-size: 0.9em; margin-top: 2px; }
.log-meta { color: var(--fg2); font-size: 0.82em; }

/* Equity Chart */
.chart-container { position: relative; height: 200px; }
canvas { width: 100% !important; height: 100% !important; }

/* Wisdom */
.wisdom-text { color: var(--fg2); font-size: 0.88em; white-space: pre-wrap;
  background: var(--bg3); padding: 10px; border-radius: 6px; max-height: 140px; overflow-y: auto; }

/* Grid layout */
.grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
@media (max-width: 900px) { .grid-2 { grid-template-columns: 1fr; } }

/* Modal */
.modal-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.6);
  z-index: 100; justify-content: center; align-items: center; }
.modal-overlay.active { display: flex; }
.modal { background: var(--bg2); border: 1px solid var(--border); border-radius: var(--radius);
  padding: 24px; max-width: 460px; width: 90%; box-shadow: 0 8px 30px rgba(0,0,0,0.4); }
.modal h3 { margin-bottom: 16px; color: var(--accent); }
.modal label { display: block; font-size: 0.82em; color: var(--fg2); margin-bottom: 4px; margin-top: 12px; }
.modal input { width: 100%; padding: 8px 10px; border: 1px solid var(--border); border-radius: 6px;
  background: var(--bg3); color: var(--fg); font-size: 0.9em; }
.modal input:focus { outline: none; border-color: var(--accent); }
.modal-actions { display: flex; gap: 8px; margin-top: 20px; justify-content: flex-end; }
.btn-primary { background: var(--accent); color: #fff; border: none; }
.btn-primary:hover { opacity: 0.85; }

/* Scrollbar */
::-webkit-scrollbar { width: 6px; }
::-webkit-scrollbar-track { background: transparent; }
::-webkit-scrollbar-thumb { background: var(--border); border-radius: 3px; }
.no-data { color: var(--fg2); font-style: italic; font-size: 0.88em; padding: 12px 0; }
</style>
</head>
<body>
<div id="auth-overlay" style="display:none;position:fixed;inset:0;background:var(--bg);z-index:200;align-items:center;justify-content:center">
  <div style="max-width:380px;padding:24px;text-align:center">
    <h1 style="font-size:24px;margin-bottom:8px">⟁ SURVAIV</h1>
    <div id="auth-claim" style="display:none">
      <p style="font-size:13px;color:var(--fg2);margin-bottom:16px">This is your agent&#39;s secret PIN. Write it down!</p>
      <div id="auth-pin-display" style="font-size:28px;font-weight:700;letter-spacing:2px;padding:16px;background:var(--bg2);border:2px solid var(--green);border-radius:8px;margin-bottom:16px;font-family:monospace"></div>
      <input id="auth-confirm-input" type="text" style="width:100%;padding:10px;font-size:16px;text-align:center;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--fg)" placeholder="Type the PIN above">
      <button onclick="confirmClaim()" style="width:100%;margin-top:12px;padding:10px;background:var(--green);color:#000;border:none;border-radius:6px;font-size:14px;font-weight:600;cursor:pointer">Claim Agent</button>
      <div id="auth-claim-err" style="color:var(--red);font-size:12px;margin-top:8px"></div>
    </div>
    <div id="auth-login" style="display:none">
      <p style="font-size:13px;color:var(--fg2);margin-bottom:16px">Enter your PIN to access the dashboard.</p>
      <input id="auth-login-input" type="text" style="width:100%;padding:10px;font-size:16px;text-align:center;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--fg)" placeholder="Enter PIN">
      <button onclick="loginWithPin()" style="width:100%;margin-top:12px;padding:10px;background:var(--accent);color:#fff;border:none;border-radius:6px;font-size:14px;font-weight:600;cursor:pointer">Unlock</button>
      <div id="auth-login-err" style="color:var(--red);font-size:12px;margin-top:8px"></div>
    </div>
  </div>
</div>
<div class="container">
  <header>
    <div style="display:flex;align-items:center;gap:12px">
      <span class="logo" id="agent-title">⟁ SURVAIV</span>
      <span id="statusBadge" class="status-badge status-ok">running</span>
    </div>
    <div class="header-meta">
      <span>Model: <strong id="modelName">—</strong></span>
      <span>Uptime: <strong id="uptime">0h 0m</strong></span>
      <span>Cycles: <strong id="cycles">0</strong></span>
    </div>
    <div class="header-actions">
      <button class="btn" onclick="toggleTheme()">🌓 Theme</button>
      <button class="btn" onclick="openSettings()">⚙ Settings</button>
    </div>
  </header>

  <div class="cards" id="budgetCards">
    <div class="card"><div class="card-label">Cash</div><div class="card-value" id="cardCash">$0.00</div><div class="card-sub">USDC available</div></div>
    <div class="card"><div class="card-label">Equity</div><div class="card-value v-accent" id="cardEquity">$0.00</div><div class="card-sub">Cash + positions</div></div>
    <div class="card"><div class="card-label">P&L</div><div class="card-value" id="cardPnl">$0.00</div><div class="card-sub">Realized</div></div>
    <div class="card"><div class="card-label">LLM Spend</div><div class="card-value v-yellow" id="cardLlm">$0.0000</div><div class="card-sub">Inference cost</div></div>
    <div class="card"><div class="card-label">Daily Loss</div><div class="card-value v-red" id="cardLoss">$0.0000</div><div class="card-sub">Today&#39;s drawdown</div></div>
  </div>

  <div class="section">
    <div class="section-title">📈 Equity History</div>
    <div class="chart-container"><canvas id="equityChart"></canvas></div>
  </div>

  <div class="grid-2">
    <div class="section">
      <div class="section-title">📊 Open Positions</div>
      <div id="positionsTable"><div class="no-data">No open positions</div></div>
    </div>
    <div class="section">
      <div class="section-title">🔍 Scouted Markets</div>
      <div id="scoutedList"><div class="no-data">Waiting for first scan...</div></div>
    </div>
  </div>

  <div class="grid-2">
    <div class="section">
      <div class="section-title">📋 Decision Log</div>
      <div class="log-list" id="decisionLog"><div class="no-data">No decisions yet</div></div>
    </div>
    <div class="section">
      <div class="section-title">🧠 Wisdom</div>
      <div id="wisdomPanel"><div class="no-data">Collecting data...</div></div>
    </div>
  </div>
</div>

<div class="modal-overlay" id="settingsModal">
  <div class="modal">
    <h3>⚙ LLM Settings</h3>
    <label>API Endpoint URL</label>
    <input id="settingsUrl" placeholder="https://...">
    <label>Model</label>
    <input id="settingsModel" placeholder="model-name">
    <label>API Key</label>
    <input id="settingsKey" type="password" placeholder="sk-...">
    <div style="margin-top:16px;padding-top:12px;border-top:1px solid var(--border)">
      <div style="font-size:12px;font-weight:600;margin-bottom:6px">Trading Mode</div>
      <div style="display:flex;gap:10px;align-items:center">
        <button id="mode-paper-btn" onclick="setTradingMode(true)" style="padding:6px 14px;cursor:pointer;border-radius:6px;border:1px solid var(--border);background:var(--bg3);color:var(--fg);font-size:0.82em">&#x1F4DD; Paper</button>
        <button id="mode-real-btn" onclick="setTradingMode(false)" style="padding:6px 14px;cursor:pointer;border-radius:6px;border:1px solid var(--border);background:var(--bg3);color:var(--fg);font-size:0.82em">&#x27C1; Real</button>
        <span id="mode-msg" style="font-size:11px;color:var(--fg2)"></span>
      </div>
      <div style="font-size:10px;color:var(--fg2);margin-top:4px">Open positions will continue to completion in their original mode.</div>
    </div>
    <div style="margin-top:16px;padding-top:12px;border-top:1px solid var(--border)">
      <div style="font-size:12px;font-weight:600;margin-bottom:6px">News Search</div>
      <div style="display:flex;gap:6px;flex-wrap:wrap;align-items:end">
        <label style="font-size:10px;color:var(--fg2);flex:1">Provider<br>
          <select id="cfg-news-prov" style="width:100%;font-size:11px;padding:4px 6px;border:1px solid var(--border);border-radius:4px;background:var(--bg3);color:var(--fg)">
            <option value="tavily">Tavily</option>
            <option value="brave">Brave Search</option>
          </select>
        </label>
        <label style="font-size:10px;color:var(--fg2);flex:2">API Key<br>
          <input id="cfg-news-key" type="password" style="width:100%;font-size:11px;padding:4px 6px;border:1px solid var(--border);border-radius:4px;background:var(--bg3);color:var(--fg)" placeholder="tvly-xxx or BSA-xxx">
        </label>
        <button onclick="saveNewsConfig()" style="background:var(--accent);color:#fff;border:none;padding:5px 12px;border-radius:4px;cursor:pointer;font-size:11px">Save</button>
      </div>
      <div id="news-cfg-msg" style="margin-top:4px;font-size:10px;color:var(--fg2)"></div>
    </div>
    <div class="modal-actions">
      <button class="btn" onclick="closeSettings()">Cancel</button>
      <button class="btn btn-primary" onclick="saveSettings()">Save</button>
    </div>
  </div>
</div>

<script>
var $ = function(s) { return document.querySelector(s); };
var $$ = function(s) { return document.querySelectorAll(s); };
var equityData = [];
var darkMode = localStorage.getItem('theme') !== 'light';
if (!darkMode) document.body.classList.add('light');
var authToken = sessionStorage.getItem('survaiv_token') || '';

function authHeaders() {
  var h = {'Content-Type': 'application/json'};
  if (authToken) h['Authorization'] = 'Bearer ' + authToken;
  return h;
}

function toggleTheme() {
  darkMode = !darkMode;
  document.body.classList.toggle('light', !darkMode);
  localStorage.setItem('theme', darkMode ? 'dark' : 'light');
  drawChart();
}

function openSettings() { $('#settingsModal').classList.add('active'); }
function closeSettings() { $('#settingsModal').classList.remove('active'); }
function saveSettings() {
  var body = {};
  var u = $('#settingsUrl').value.trim();
  var m = $('#settingsModel').value.trim();
  var k = $('#settingsKey').value.trim();
  if (u) body.url = u;
  if (m) body.model = m;
  if (k) body.key = k;
  fetch('/api/llm-config', { method: 'POST', headers: authHeaders(), body: JSON.stringify(body) })
    .then(function(r) { return r.json(); }).then(function() { closeSettings(); }).catch(console.error);
}

function setTradingMode(paper) {
  fetch('/api/config', {method:'POST', body:JSON.stringify({paper_only: paper ? 1 : 0}),
    headers: authHeaders()})
    .then(function(r) { return r.json(); }).then(function(d) {
      var msg = document.getElementById('mode-msg');
      msg.textContent = paper ? '\u2713 Paper mode' : '\u2713 Real mode';
      msg.style.color = 'var(--green)';
      setTimeout(function() { msg.textContent = ''; }, 3000);
      loadAll();
    });
}

function fmtUsd(n) { return (n < 0 ? '-' : '') + '$' + Math.abs(n).toFixed(2); }
function fmtUsd4(n) { return (n < 0 ? '-' : '') + '$' + Math.abs(n).toFixed(4); }
function fmtDuration(sec) {
  var h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60);
  return h + 'h ' + m + 'm';
}

function updateState(d) {
  var b = d.budget || {};
  $('#cardCash').textContent = fmtUsd(b.cash || 0);
  $('#cardEquity').textContent = fmtUsd(b.equity || 0);
  var pnl = b.realized_pnl || 0;
  var pe = $('#cardPnl');
  pe.textContent = (pnl >= 0 ? '+' : '') + fmtUsd4(pnl);
  pe.className = 'card-value ' + (pnl > 0 ? 'v-green' : pnl < 0 ? 'v-red' : '');
  $('#cardLlm').textContent = fmtUsd4(b.llm_spend || 0);
  $('#cardLoss').textContent = fmtUsd4(b.daily_loss || 0);
  var st = d.status || 'unknown';
  var sb = $('#statusBadge');
  sb.textContent = st;
  sb.className = 'status-badge ' + (st === 'error' || st === 'llm_error' ? 'status-err' : 'status-ok');
  $('#modelName').textContent = d.active_model || '\u2014';
  $('#uptime').textContent = fmtDuration(d.uptime_seconds || 0);
  $('#cycles').textContent = d.cycle_count || 0;
  if (d.agent_name) document.getElementById('agent-title').textContent = '\u27C1 ' + d.agent_name;
  var pb = document.getElementById('mode-paper-btn');
  var rb = document.getElementById('mode-real-btn');
  if (pb && rb) {
    if (d.paper_only) {
      pb.style.background = 'var(--green)'; pb.style.color = '#000';
      rb.style.background = 'var(--bg3)'; rb.style.color = 'var(--fg)';
    } else {
      pb.style.background = 'var(--bg3)'; pb.style.color = 'var(--fg)';
      rb.style.background = 'var(--accent)'; rb.style.color = '#fff';
    }
  }
  var newsProv = document.getElementById('cfg-news-prov');
  if (newsProv && d.news_provider) newsProv.value = d.news_provider;
  var newsMsg = document.getElementById('news-cfg-msg');
  if (newsMsg) newsMsg.textContent = d.has_news_key ? 'Key configured' : '';
}

function renderPositions(arr) {
  const el = $('#positionsTable');
  if (!arr || arr.length === 0) { el.innerHTML = '<div class="no-data">No open positions</div>'; return; }
  let html = '<table><thead><tr><th>Market</th><th>Side</th><th>Entry</th><th>Current</th><th>P&L</th><th>Stake</th><th>Type</th></tr></thead><tbody>';
  arr.forEach(p => {
    const q = (p.question || '').length > 50 ? p.question.slice(0, 47) + '...' : (p.question || '');
    const sideClass = p.side === 'yes' ? 'side-yes' : 'side-no';
    const pnlVal = p.unrealized_pnl || 0;
    const pnlClass = pnlVal > 0 ? 'v-green' : pnlVal < 0 ? 'v-red' : '';
    html += '<tr><td>' + q + '</td><td class="' + sideClass + '">' + (p.side || '').toUpperCase() +
      '</td><td>' + (p.entry_price || 0).toFixed(2) + '</td><td>' + (p.current_price || 0).toFixed(2) +
      '</td><td class="' + pnlClass + '">' + fmtUsd4(pnlVal) + '</td><td>' + fmtUsd(p.stake_usdc || 0) +
      '</td><td>' + (p.is_live ? '🔴 LIVE' : 'PAPER') + '</td></tr>';
  });
  html += '</tbody></table>';
  el.innerHTML = html;
}

function renderScouted(arr) {
  const el = $('#scoutedList');
  if (!arr || arr.length === 0) { el.innerHTML = '<div class="no-data">Waiting for first scan...</div>'; return; }
  let html = '<div class="scouted-grid">';
  arr.slice(0, 12).forEach(s => {
    const sig = s.signal || 'neutral';
    const cls = sig === 'bullish' ? 'sig-bullish' : sig === 'bearish' ? 'sig-bearish' : sig === 'skip' ? 'sig-skip' : 'sig-neutral';
    const q = (s.question || '').length > 55 ? s.question.slice(0, 52) + '...' : (s.question || '');
    html += '<div class="scouted-item"><div class="signal-dot ' + cls + '"></div>' +
      '<span class="scouted-q">' + q + '</span>' +
      '<span class="scouted-meta">e:' + (s.edge_bps || 0).toFixed(0) + ' c:' + (s.confidence || 0).toFixed(2) + '</span></div>';
  });
  html += '</div>';
  el.innerHTML = html;
}

function renderDecisions(arr) {
  const el = $('#decisionLog');
  if (!arr || arr.length === 0) { el.innerHTML = '<div class="no-data">No decisions yet</div>'; return; }
  let html = '';
  arr.slice(0, 30).forEach(d => {
    const t = d.type || 'unknown';
    let cls = 'log-type-hold';
    if (t.includes('buy')) cls = 'log-type-buy';
    else if (t.includes('close')) cls = 'log-type-close';
    else if (t === 'tool_call') cls = 'log-type-tool';
    const q = (d.question || '').length > 45 ? d.question.slice(0, 42) + '...' : (d.question || '');
    const ts = d.epoch ? new Date(d.epoch * 1000).toLocaleTimeString() : '';
    html += '<div class="log-item"><span class="log-type ' + cls + '">' + t + '</span> ' +
      '<span>' + q + '</span> <span class="log-meta">conf:' + (d.confidence || 0).toFixed(2) +
      ' edge:' + (d.edge_bps || 0).toFixed(0) + ' ' + ts + '</span>';
    if (d.rationale) html += '<div class="log-rationale">' + d.rationale.slice(0, 120) + '</div>';
    html += '</div>';
  });
  el.innerHTML = html;
}

function renderWisdom(d) {
  const el = $('#wisdomPanel');
  if (!d) { el.innerHTML = '<div class="no-data">Collecting data...</div>'; return; }
  let html = '<div style="display:flex;gap:16px;flex-wrap:wrap;margin-bottom:8px;font-size:0.85em">';
  html += '<span>Tracked: <strong>' + (d.total_tracked || 0) + '</strong></span>';
  html += '<span>Resolved: <strong>' + (d.total_resolved || 0) + '</strong></span>';
  html += '<span>Correct: <strong>' + (d.total_correct || 0) + '</strong></span>';
  const acc = d.total_resolved > 0 ? ((d.total_correct / d.total_resolved) * 100).toFixed(1) + '%' : '—';
  html += '<span>Accuracy: <strong>' + acc + '</strong></span>';
  if (d.frozen) html += '<span style="color:var(--yellow)">🧊 Frozen</span>';
  html += '</div>';
  html += '<div class="wisdom-text">' + (d.wisdom_text || 'Collecting data...') + '</div>';
  el.innerHTML = html;
}

// Equity chart
function drawChart() {
  const canvas = $('#equityChart');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const rect = canvas.parentElement.getBoundingClientRect();
  canvas.width = rect.width * (window.devicePixelRatio || 1);
  canvas.height = rect.height * (window.devicePixelRatio || 1);
  ctx.scale(window.devicePixelRatio || 1, window.devicePixelRatio || 1);
  const W = rect.width, H = rect.height;
  ctx.clearRect(0, 0, W, H);

  if (equityData.length < 2) {
    ctx.fillStyle = getComputedStyle(document.body).getPropertyValue('--fg2');
    ctx.font = '13px sans-serif';
    ctx.fillText('Waiting for equity data...', W / 2 - 80, H / 2);
    return;
  }

  const vals = equityData.map(d => d.equity || d[1] || 0);
  const mn = Math.min(...vals) * 0.995, mx = Math.max(...vals) * 1.005;
  const range = mx - mn || 1;
  const pad = { t: 10, b: 20, l: 50, r: 10 };
  const cw = W - pad.l - pad.r, ch = H - pad.t - pad.b;

  const toX = i => pad.l + (i / (vals.length - 1)) * cw;
  const toY = v => pad.t + (1 - (v - mn) / range) * ch;

  // Gradient fill
  const grad = ctx.createLinearGradient(0, pad.t, 0, H - pad.b);
  const accent = getComputedStyle(document.body).getPropertyValue('--accent').trim();
  grad.addColorStop(0, accent + '33');
  grad.addColorStop(1, accent + '00');
  ctx.beginPath();
  ctx.moveTo(toX(0), toY(vals[0]));
  for (let i = 1; i < vals.length; i++) ctx.lineTo(toX(i), toY(vals[i]));
  ctx.lineTo(toX(vals.length - 1), H - pad.b);
  ctx.lineTo(toX(0), H - pad.b);
  ctx.closePath();
  ctx.fillStyle = grad;
  ctx.fill();

  // Line
  ctx.beginPath();
  ctx.moveTo(toX(0), toY(vals[0]));
  for (let i = 1; i < vals.length; i++) ctx.lineTo(toX(i), toY(vals[i]));
  ctx.strokeStyle = accent;
  ctx.lineWidth = 2;
  ctx.stroke();

  // Y axis labels
  ctx.fillStyle = getComputedStyle(document.body).getPropertyValue('--fg2');
  ctx.font = '11px sans-serif';
  ctx.textAlign = 'right';
  for (let i = 0; i <= 4; i++) {
    const v = mn + (range * i / 4);
    ctx.fillText('$' + v.toFixed(2), pad.l - 4, toY(v) + 3);
  }
}

// SSE
var evtSource;
function connectSSE() {
  evtSource = new EventSource('/api/events');
  evtSource.addEventListener('state', function(e) {
    try { updateState(JSON.parse(e.data)); } catch(ex) { console.error(ex); }
  });
  evtSource.addEventListener('scouted', function(e) {
    try { renderScouted(JSON.parse(e.data)); } catch(ex) { console.error(ex); }
  });
  evtSource.onerror = function() { evtSource.close(); setTimeout(connectSSE, 3000); };
}

async function loadAll() {
  try {
    var results = await Promise.all([
      fetch('/api/state').then(function(r) { return r.json(); }),
      fetch('/api/positions').then(function(r) { return r.json(); }),
      fetch('/api/decisions').then(function(r) { return r.json(); }),
      fetch('/api/equity-history').then(function(r) { return r.json(); }),
      fetch('/api/scouted').then(function(r) { return r.json(); }),
      fetch('/api/wisdom').then(function(r) { return r.json(); }),
    ]);
    updateState(results[0]);
    renderPositions(results[1]);
    renderDecisions(results[2]);
    equityData = results[3] || [];
    drawChart();
    renderScouted(results[4]);
    renderWisdom(results[5]);
  } catch(e) { console.error('Load error:', e); }
}

function startDashboard() {
  loadAll();
  connectSSE();
  setInterval(loadAll, 10000);
}

// ── Auth ──
async function checkAuth() {
  try {
    var headers = {};
    if (authToken) headers['Authorization'] = 'Bearer ' + authToken;
    var r = await fetch('/api/auth', {headers: headers});
    var d = await r.json();
    if (!d.claimed) {
      document.getElementById('auth-pin-display').textContent = d.display_pin;
      document.getElementById('auth-claim').style.display = 'block';
      document.getElementById('auth-login').style.display = 'none';
      document.getElementById('auth-overlay').style.display = 'flex';
    } else if (d.needs_pin) {
      document.getElementById('auth-claim').style.display = 'none';
      document.getElementById('auth-login').style.display = 'block';
      document.getElementById('auth-overlay').style.display = 'flex';
    } else {
      document.getElementById('auth-overlay').style.display = 'none';
      startDashboard();
    }
  } catch(e) { console.error('Auth check failed:', e); startDashboard(); }
}

function confirmClaim() {
  var pin = document.getElementById('auth-confirm-input').value.trim();
  fetch('/api/auth', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({action: 'claim', pin: pin})
  })
  .then(function(r) { return r.json(); })
  .then(function(d) {
    if (d.ok) {
      authToken = d.token;
      sessionStorage.setItem('survaiv_token', authToken);
      document.getElementById('auth-overlay').style.display = 'none';
      startDashboard();
    } else {
      document.getElementById('auth-claim-err').textContent = d.error || 'Wrong PIN';
    }
  });
}

function loginWithPin() {
  var pin = document.getElementById('auth-login-input').value.trim();
  fetch('/api/auth', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({action: 'login', pin: pin})
  })
  .then(function(r) { return r.json(); })
  .then(function(d) {
    if (d.ok) {
      authToken = d.token;
      sessionStorage.setItem('survaiv_token', authToken);
      document.getElementById('auth-overlay').style.display = 'none';
      startDashboard();
    } else {
      document.getElementById('auth-login-err').textContent = d.error || 'Wrong PIN';
    }
  });
}

function saveNewsConfig() {
  var body = {news_provider: document.getElementById('cfg-news-prov').value,
    news_api_key: document.getElementById('cfg-news-key').value};
  fetch('/api/config', {method:'POST', body:JSON.stringify(body), headers:authHeaders()})
    .then(function(r) { return r.json(); }).then(function() {
      var msg = document.getElementById('news-cfg-msg');
      msg.textContent = '\u2713 Saved';
      msg.style.color = 'var(--green)';
      setTimeout(function() { msg.textContent = ''; }, 3000);
    }).catch(function() {
      var msg = document.getElementById('news-cfg-msg');
      msg.textContent = '\u2717 Failed';
      msg.style.color = 'var(--red)';
    });
}

window.addEventListener('resize', drawChart);
checkAuth();
</script>
</body>
</html>`)

