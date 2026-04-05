#!/usr/bin/env python3
"""Transform ESP32 dashboard HTML into Go assets.go for the cloud version."""

# Read the ESP32 source
with open('/Users/ka/code/survaiv/main/web_assets.h', 'r') as f:
    content = f.read()

# Extract only the dashboard HTML (first R"rawhtml( ... )rawhtml")
start = content.index('R"rawhtml(') + len('R"rawhtml(')
end = content.index(')rawhtml"', start)
html = content[start:end]

# ─── 1. Auth: X-Auth-Token → Authorization: Bearer ──────────────

html = html.replace(
    "if (authToken) h['X-Auth-Token'] = authToken;",
    "if (authToken) h['Authorization'] = 'Bearer ' + authToken;"
)
html = html.replace(
    "fetch('/api/auth', {headers:{'X-Auth-Token': authToken}})",
    "fetch('/api/auth', {headers:{'Authorization': 'Bearer ' + authToken}})"
)
html = html.replace(
    "headers: authToken ? {'X-Auth-Token': authToken} : {}",
    "headers: authToken ? {'Authorization': 'Bearer ' + authToken} : {}"
)
html = html.replace(
    "headers:{'Content-Type':'application/json','X-Auth-Token':authToken}",
    "headers:authHeaders()"
)
html = html.replace(
    "xhr.setRequestHeader('X-Auth-Token', authToken);",
    "xhr.setRequestHeader('Authorization', 'Bearer ' + authToken);"
)

# ─── 2. API endpoints ───────────────────────────────────────────

html = html.replace("authFetch('/api/history')", "authFetch('/api/decisions')")
html = html.replace("authFetch('/api/equity')", "authFetch('/api/equity-history')")

# ─── 3. News config field names ─────────────────────────────────

html = html.replace(
    "news_prov: $('cfg-news-prov').value, news_key: $('cfg-news-key').value",
    "news_provider: $('cfg-news-prov').value, news_api_key: $('cfg-news-key').value"
)

# ─── 4. LLM config field names ──────────────────────────────────

html = html.replace("if (url) body.oai_url = url;", "if (url) body.url = url;")
html = html.replace("if (model) body.oai_model = model;", "if (model) body.model = model;")
html = html.replace("if (key) body.api_key = key;", "if (key) body.key = key;")

# ─── 5. Firmware → Version ──────────────────────────────────────

html = html.replace("Version:", "Version:")  # no-op if already done
html = html.replace("Firmware:", "Version:")
html = html.replace(
    "$('v-fw').textContent = s.firmware;",
    "$('v-fw').textContent = s.version || s.firmware || '\u2014';"
)

# ─── 6. OTA: hide/remove ────────────────────────────────────────

# Remove OTA button from settings modal
ota_btn_html = """        <label id="ota-btn" class="badge" style="padding:6px 14px;background:#fff8e1;color:var(--yellow);border:1px solid var(--yellow);cursor:pointer;display:none">
          \u26a1 OTA Update
          <input type="file" id="ota-file" accept=".bin" style="display:none"
            onchange="otaUpdate(this.files[0])">
        </label>"""
html = html.replace(ota_btn_html, "")

# Remove OTA badge from footer
html = html.replace(
    ' <span id="v-ota-badge" style="font-size:9px;padding:1px 5px;border-radius:3px;display:none"></span>',
    ''
)

# Remove OTA button show/hide JS
html = html.replace(
    "  var otaBtn = $('ota-btn');\n   if (otaBtn) otaBtn.style.display = s.ota_enabled ? '' : 'none';",
    ""
)

# Remove OTA badge JS block
ota_badge_js = """    var badge = $('v-ota-badge');
    if (badge) {
      badge.style.display = 'inline';
      if (s.ota_enabled) {
        badge.textContent = 'OTA';
        badge.style.background = '#e3f2fd'; badge.style.color = '#1976d2';
      } else {
        badge.textContent = 'NO-OTA';
        badge.style.background = '#fff3e0'; badge.style.color = '#e65100';
      }
    }"""
html = html.replace(ota_badge_js, "")

# ─── 7. System stats: adapt for Go's sys format ─────────────────

old_sys_mem = """    var ramPct = sy.total_heap > 0 ? Math.round(100*(sy.total_heap-sy.free_heap)/sy.total_heap) : 0;
    h += '<span>RAM: '+bar(ramPct)+' '+ramPct+'% ('+fmtKB(sy.free_heap)+' free)</span>';
    h += '<span>Min free: '+fmtKB(sy.min_free_heap)+'</span>';"""

new_sys_mem = """    var memPct = sy.sys > 0 ? Math.round(sy.alloc * 100 / sy.sys) : 0;
    h += '<span>Mem: '+bar(memPct)+' '+memPct+'% ('+fmtKB(sy.alloc)+' / '+fmtKB(sy.sys)+')</span>';
    h += '<span>Goroutines: '+sy.goroutines+'</span>';
    h += '<span>GC: '+sy.gc_cycles+'</span>';"""

html = html.replace(old_sys_mem, new_sys_mem)

# ─── 8. Equity data format: d[1] → d.equity ─────────────────────

html = html.replace(
    "const vals = equityData.map(d => d[1]);",
    "const vals = equityData.map(function(d) { return d.equity || d[1] || 0; });"
)
html = html.replace(
    "const baseline = equityData[0][1];",
    "const baseline = equityData[0].equity || equityData[0][1] || 0;"
)
html = html.replace(
    "const pnl = equityData.map(d => d[1] - baseline);",
    "const pnl = equityData.map(function(d) { return (d.equity || d[1] || 0) - baseline; });"
)

# ─── 9. Template literals → string concatenation ────────────────
# Use the EXACT characters from the file (not escape sequences)

ELLIPSIS = '\u2026'  # …
MAGNIFY = '\U0001f50d'  # 🔍
MDASH = '\u2014'  # —
CENT = '\u00a2'  # ¢
MIDDOT = '\u00b7'  # ·

# 9a. Uptime string
html = html.replace(
    "$('v-uptime').textContent = `Uptime: ${hrs}h ${mins}m`;",
    "$('v-uptime').textContent = 'Uptime: ' + hrs + 'h ' + mins + 'm';"
)

# 9b. Error retry string
html = html.replace(
    f"const retry = s.next_retry_sec > 0 ? ` {MIDDOT} retrying in ${{s.next_retry_sec}}s` : '';",
    f"const retry = s.next_retry_sec > 0 ? ' {MIDDOT} retrying in ' + s.next_retry_sec + 's' : '';"
)

# 9c. updatePositions template literal
old_positions = f"""  body.innerHTML = positions.map(p => {{
    const pnlCls = p.unrealized_pnl >= 0 ? 'pos' : 'neg';
    const sign = p.unrealized_pnl >= 0 ? '+' : '';
    const q = p.question.length > 50 ? p.question.substring(0, 47) + '{ELLIPSIS}' : p.question;
    return `<tr>
      <td title="${{p.question}}">${{q}}</td>
      <td>${{p.side.toUpperCase()}}</td>
      <td>${{p.entry_price.toFixed(2)}}</td>
      <td>${{p.current_price.toFixed(2)}}</td>
      <td class="${{pnlCls}}">${{sign}}${{fmtUsd(p.unrealized_pnl)}}</td>
      <td>${{fmtUsd(p.stake_usdc)}}</td>
      <td>${{p.is_live ? '<span class="badge live">LIVE</span>' : '<span class="badge paper">PAPER</span>'}}</td>
    </tr>`;
  }}).join('');"""

new_positions = """  body.innerHTML = positions.map(function(p) {
    var pnlCls = p.unrealized_pnl >= 0 ? 'pos' : 'neg';
    var sign = p.unrealized_pnl >= 0 ? '+' : '';
    var q = p.question.length > 50 ? p.question.substring(0, 47) + '\\u2026' : p.question;
    return '<tr>' +
      '<td title="' + p.question + '">' + q + '</td>' +
      '<td>' + p.side.toUpperCase() + '</td>' +
      '<td>' + p.entry_price.toFixed(2) + '</td>' +
      '<td>' + p.current_price.toFixed(2) + '</td>' +
      '<td class="' + pnlCls + '">' + sign + fmtUsd(p.unrealized_pnl) + '</td>' +
      '<td>' + fmtUsd(p.stake_usdc) + '</td>' +
      '<td>' + (p.is_live ? '<span class="badge live">LIVE</span>' : '<span class="badge paper">PAPER</span>') + '</td>' +
      '</tr>';
  }).join('');"""

assert old_positions in html, "Could not find positions template literal!"
html = html.replace(old_positions, new_positions)

# 9d. updateDecisions template literal
# Build the exact search string using actual characters
old_decisions = (
    "  log.innerHTML = decisions.slice(0, 30).map(d => {\n"
    "    const dt = new Date(d.epoch * 1000);\n"
    "    const time = dt.toLocaleTimeString();\n"
    "    let typeCls = 'hold';\n"
    "    if (d.type.includes('buy')) typeCls = 'buy';\n"
    "    else if (d.type.includes('close')) typeCls = 'close';\n"
    "    else if (d.type === 'tool_call') typeCls = 'tool';\n"
    f"    const q = d.question ? (d.question.length > 40 ? d.question.substring(0, 37) + '{ELLIPSIS}' : d.question) : '';\n"
    "    return `<div class=\"log-entry\">\n"
    "      <span class=\"log-time\">${time}</span>\n"
    "      <span class=\"log-type ${typeCls}\">${d.type}</span>\n"
    f"      ${{d.tools_used && d.tools_used.length ? d.tools_used.map(t => '<span style=\"font-size:9px;background:var(--bg2);border:1px solid var(--border);border-radius:3px;padding:0 3px;margin-left:2px;color:var(--dim)\">{MAGNIFY} ' + t + '</span>').join('') : ''}}\n"
    "      ${q ? '<span>' + q + '</span>' : ''}\n"
    "      ${d.confidence ? ' <span style=\"color:var(--blue)\">' + (d.confidence*100).toFixed(0) + '%</span>' : ''}\n"
    "      ${d.rationale ? '<div style=\"color:var(--dim);margin-top:2px;font-size:10px\">' + d.rationale.substring(0, 120) + '</div>' : ''}\n"
    "    </div>`;\n"
    "  }).join('');"
)

new_decisions = (
    "  log.innerHTML = decisions.slice(0, 30).map(function(d) {\n"
    "    var dt = new Date(d.epoch * 1000);\n"
    "    var time = dt.toLocaleTimeString();\n"
    "    var typeCls = 'hold';\n"
    "    if (d.type.indexOf('buy') >= 0) typeCls = 'buy';\n"
    "    else if (d.type.indexOf('close') >= 0) typeCls = 'close';\n"
    "    else if (d.type === 'tool_call') typeCls = 'tool';\n"
    "    var q = d.question ? (d.question.length > 40 ? d.question.substring(0, 37) + '\\u2026' : d.question) : '';\n"
    "    var h = '<div class=\"log-entry\">' +\n"
    "      '<span class=\"log-time\">' + time + '</span>' +\n"
    "      '<span class=\"log-type ' + typeCls + '\">' + d.type + '</span>';\n"
    "    if (d.tools_used && d.tools_used.length) {\n"
    "      h += d.tools_used.map(function(t) { return '<span style=\"font-size:9px;background:var(--bg2);border:1px solid var(--border);border-radius:3px;padding:0 3px;margin-left:2px;color:var(--dim)\">" + MAGNIFY + " ' + t + '</span>'; }).join('');\n"
    "    }\n"
    "    if (q) h += '<span>' + q + '</span>';\n"
    "    if (d.confidence) h += ' <span style=\"color:var(--blue)\">' + (d.confidence*100).toFixed(0) + '%</span>';\n"
    "    if (d.rationale) h += '<div style=\"color:var(--dim);margin-top:2px;font-size:10px\">' + d.rationale.substring(0, 120) + '</div>';\n"
    "    h += '</div>';\n"
    "    return h;\n"
    "  }).join('');"
)

if old_decisions not in html:
    # Debug: find closest match
    idx = html.find("log.innerHTML = decisions.slice(0, 30)")
    if idx >= 0:
        chunk = html[idx:idx+50]
        print(f"Found at idx {idx}: {repr(chunk)}")
    raise ValueError("Could not find decisions template literal!")
html = html.replace(old_decisions, new_decisions)

# 9e. updateScouted template literal
old_scouted = (
    "  el.innerHTML = list.map(s => {\n"
    f"    const q = s.question ? (s.question.length > 60 ? s.question.substring(0, 57) + '{ELLIPSIS}' : s.question) : s.market_id;\n"
    "    const sig = s.signal || 'neutral';\n"
    f"    const edge = s.edge_bps ? s.edge_bps.toFixed(0) + 'bp' : '{MDASH}';\n"
    f"    const conf = s.confidence ? (s.confidence * 100).toFixed(0) + '%' : '{MDASH}';\n"
    "    const vol = s.volume > 1000 ? (s.volume / 1000).toFixed(0) + 'k' : s.volume.toFixed(0);\n"
    "    const liq = s.liquidity > 1000 ? (s.liquidity / 1000).toFixed(0) + 'k' : s.liquidity.toFixed(0);\n"
    "    const note = s.note ? s.note.substring(0, 80) : '';\n"
    "    return `<div class=\"scout-card\">\n"
    "      <span class=\"scout-signal ${sig}\" title=\"${sig}\"></span>\n"
    "      <div>\n"
    "        <div class=\"scout-q\" title=\"${s.question || ''}\">${q}</div>\n"
    "        <div class=\"scout-meta\">\n"
    "          <span>Edge: ${edge}</span>\n"
    "          <span>Vol: $${vol}</span>\n"
    "          <span>Liq: $${liq}</span>\n"
    f"          ${{note ? '<span>{MDASH} ' + note + '</span>' : ''}}\n"
    "        </div>\n"
    "      </div>\n"
    "      <div class=\"scout-right\">\n"
    f"        <div class=\"scout-price\">${{s.yes_price ? (s.yes_price * 100).toFixed(0) + '{CENT}' : '{MDASH}'}}</div>\n"
    "        <div class=\"scout-conf\">${conf}</div>\n"
    "      </div>\n"
    "    </div>`;\n"
    "  }).join('');"
)

new_scouted = (
    "  el.innerHTML = list.map(function(s) {\n"
    "    var q = s.question ? (s.question.length > 60 ? s.question.substring(0, 57) + '\\u2026' : s.question) : s.market_id;\n"
    "    var sig = s.signal || 'neutral';\n"
    "    var edge = s.edge_bps ? s.edge_bps.toFixed(0) + 'bp' : '\\u2014';\n"
    "    var conf = s.confidence ? (s.confidence * 100).toFixed(0) + '%' : '\\u2014';\n"
    "    var vol = s.volume > 1000 ? (s.volume / 1000).toFixed(0) + 'k' : s.volume.toFixed(0);\n"
    "    var liq = s.liquidity > 1000 ? (s.liquidity / 1000).toFixed(0) + 'k' : s.liquidity.toFixed(0);\n"
    "    var note = s.note ? s.note.substring(0, 80) : '';\n"
    "    return '<div class=\"scout-card\">' +\n"
    "      '<span class=\"scout-signal ' + sig + '\" title=\"' + sig + '\"></span>' +\n"
    "      '<div>' +\n"
    "        '<div class=\"scout-q\" title=\"' + (s.question || '') + '\">' + q + '</div>' +\n"
    "        '<div class=\"scout-meta\">' +\n"
    "          '<span>Edge: ' + edge + '</span>' +\n"
    "          '<span>Vol: $' + vol + '</span>' +\n"
    "          '<span>Liq: $' + liq + '</span>' +\n"
    "          (note ? '<span>\\u2014 ' + note + '</span>' : '') +\n"
    "        '</div>' +\n"
    "      '</div>' +\n"
    "      '<div class=\"scout-right\">' +\n"
    "        '<div class=\"scout-price\">' + (s.yes_price ? (s.yes_price * 100).toFixed(0) + '\\u00a2' : '\\u2014') + '</div>' +\n"
    "        '<div class=\"scout-conf\">' + conf + '</div>' +\n"
    "      '</div>' +\n"
    "    '</div>';\n"
    "  }).join('');"
)

assert old_scouted in html, "Could not find scouted template literal!"
html = html.replace(old_scouted, new_scouted)

# 9f. SSE decision handler template literal
old_sse = (
    "      entry.innerHTML = `<span class=\"log-time\">${dt.toLocaleTimeString()}</span>\n"
    "        <span class=\"log-type ${typeCls}\">${d.type}</span>\n"
    f"        ${{d.tools_used && d.tools_used.length ? d.tools_used.map(t => '<span style=\"font-size:9px;background:var(--bg2);border:1px solid var(--border);border-radius:3px;padding:0 3px;margin-left:2px;color:var(--dim)\">{MAGNIFY} ' + t + '</span>').join('') : ''}}\n"
    "        ${d.rationale ? '<div style=\"color:var(--dim);margin-top:2px;font-size:10px\">' + d.rationale.substring(0, 120) + '</div>' : ''}`;"
)

new_sse = (
    "      entry.innerHTML = '<span class=\"log-time\">' + dt.toLocaleTimeString() + '</span>' +\n"
    "        '<span class=\"log-type ' + typeCls + '\">' + d.type + '</span>' +\n"
    "        (d.tools_used && d.tools_used.length ? d.tools_used.map(function(t) { return '<span style=\"font-size:9px;background:var(--bg2);border:1px solid var(--border);border-radius:3px;padding:0 3px;margin-left:2px;color:var(--dim)\">" + MAGNIFY + " ' + t + '</span>'; }).join('') : '') +\n"
    "        (d.rationale ? '<div style=\"color:var(--dim);margin-top:2px;font-size:10px\">' + d.rationale.substring(0, 120) + '</div>' : '');"
)

if old_sse not in html:
    idx = html.find("entry.innerHTML = `")
    if idx >= 0:
        chunk = html[idx:idx+200]
        print(f"SSE found at idx {idx}: {repr(chunk[:200])}")
    raise ValueError("Could not find SSE decision template literal!")
html = html.replace(old_sse, new_sse)

# ─── Remove the otaUpdate function ──────────────────────────────
# Find it by its function signature
import re
html = re.sub(
    r'function otaUpdate\(file\)\s*\{[^}]*(?:\{[^}]*\}[^}]*)*\}',
    '// OTA not available in cloud mode',
    html,
    count=1
)

# ─── Final check: no backticks remaining ─────────────────────────

if '`' in html:
    lines = html.split('\n')
    for i, line in enumerate(lines):
        if '`' in line:
            print(f"WARNING: Backtick remaining at line {i+1}: {repr(line.strip()[:100])}")
    raise ValueError("Backtick characters still present in HTML!")

# ─── Write the Go file ──────────────────────────────────────────

go_file = 'package dashboard\n\n// IndexHTML is the embedded dashboard HTML.\nvar IndexHTML = []byte(`' + html + '`)\n'

with open('/Users/ka/code/survaiv/cloud/internal/dashboard/assets.go', 'w') as f:
    f.write(go_file)

print("SUCCESS: wrote assets.go")
print(f"HTML size: {len(html)} bytes")
