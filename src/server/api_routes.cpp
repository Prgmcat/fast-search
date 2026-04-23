#include "server/api_routes.h"
#include "platform/platform.h"
#include "httplib.h"
#include "json.hpp"
#include <thread>

namespace fastsearch {

using json = nlohmann::json;

static json file_to_json(const FileRecord& f) {
    return {
        {"id", f.id}, {"name", f.name}, {"path", f.path},
        {"parent_dir", f.parent_dir}, {"size", f.size},
        {"modified", f.modified}, {"is_dir", f.is_dir}, {"ext", f.ext}
    };
}

// ── embedded web UI ──────────────────────────────────────

static const char* INDEX_HTML = R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>FastSearch</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:#0f1117;--card:#1a1d27;--border:#2a2d3a;--text:#e0e0e8;
--text2:#8888a0;--accent:#5b7ff5;--accent2:#4a6de0;--hover:#22253a;--green:#4ade80;--red:#f87171}
html,body{height:100%}
body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);overflow:hidden}
.container{max-width:1200px;height:100vh;margin:0 auto;padding:10px 16px;display:flex;flex-direction:column;gap:8px}
.topbar{display:flex;align-items:center;gap:12px;flex-shrink:0}
.topbar h1{font-size:1.1rem;font-weight:700;letter-spacing:.5px;background:linear-gradient(135deg,#5b7ff5,#8b5cf6);-webkit-background-clip:text;-webkit-text-fill-color:transparent;white-space:nowrap}
.topbar .stats{margin-left:auto;color:var(--text2);font-size:.78rem;white-space:nowrap}
.search-box{position:relative;flex-shrink:0}
.search-box input{width:100%;padding:10px 14px 10px 38px;font-size:.95rem;border:1.5px solid var(--border);border-radius:8px;background:var(--card);color:var(--text);outline:none;transition:border .2s}
.search-box input:focus{border-color:var(--accent)}
.search-box .icon{position:absolute;left:12px;top:50%;transform:translateY(-50%);color:var(--text2);font-size:1rem}
.toolbar{display:flex;align-items:center;gap:6px;flex-wrap:wrap;flex-shrink:0}
.tab{padding:5px 12px;border-radius:6px;border:1px solid var(--border);background:var(--card);color:var(--text2);cursor:pointer;font-size:.82rem;transition:.15s}
.tab.active{background:var(--accent);color:#fff;border-color:var(--accent)}
.toolbar .sep{width:1px;height:20px;background:var(--border);margin:0 4px}
.toolbar input[type=text],.toolbar select{padding:5px 9px;border:1px solid var(--border);border-radius:6px;background:var(--card);color:var(--text);font-size:.8rem;outline:none;min-width:0}
.toolbar input[type=text]{width:140px}
.toolbar input:focus,.toolbar select:focus{border-color:var(--accent)}
.toolbar label{display:flex;align-items:center;gap:4px;color:var(--text2);font-size:.8rem;cursor:pointer}
.results{flex:1;min-height:0;overflow-y:auto;border:1px solid var(--border);border-radius:8px;background:var(--card)}
.result-item{display:flex;align-items:center;padding:8px 14px;border-bottom:1px solid var(--border);cursor:pointer;transition:background .15s;gap:10px}
.result-item:last-child{border-bottom:none}
.result-item:hover{background:var(--hover)}
.r-icon{font-size:1.1rem;width:24px;text-align:center;flex-shrink:0}
.r-info{flex:1;min-width:0}
.r-name{font-weight:600;font-size:.9rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.r-path{font-size:.75rem;color:var(--text2);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-top:1px}
.r-meta{text-align:right;font-size:.72rem;color:var(--text2);flex-shrink:0;white-space:nowrap;line-height:1.3}
.content-match{font-family:'Cascadia Code','Fira Code',monospace;font-size:.78rem;color:var(--green);background:#1a2e1a;padding:3px 7px;border-radius:4px;margin-top:3px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.empty{text-align:center;padding:40px;color:var(--text2);font-size:.9rem}
.pagination{display:flex;justify-content:center;align-items:center;gap:8px;flex-shrink:0;padding-bottom:2px}
.pagination button{padding:5px 14px;border:1px solid var(--border);border-radius:6px;background:var(--card);color:var(--text);cursor:pointer;transition:.2s;font-size:.82rem}
.pagination button:hover:not(:disabled){border-color:var(--accent)}
.pagination button:disabled{opacity:.4;cursor:default}
.pagination #pageInfo{padding:0 6px;color:var(--text2);font-size:.8rem}
.loading{text-align:center;padding:40px;color:var(--text2)}
.badge{display:inline-block;padding:1px 6px;border-radius:3px;font-size:.68rem;font-weight:600;margin-left:5px;vertical-align:1px}
.badge-dir{background:#3b3050;color:#c084fc}
.badge-file{background:#1e3040;color:#60a5fa}
.result-item.editing{cursor:default;background:var(--hover)}
.rename-input{width:100%;font:inherit;font-weight:600;color:var(--text);background:#0f1220;border:1.5px solid var(--accent);border-radius:4px;padding:2px 6px;outline:none;box-sizing:border-box}
.rename-input:disabled{opacity:.6}
.toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:#3a1a1a;color:#fca5a5;border:1px solid #7f1d1d;padding:8px 16px;border-radius:6px;font-size:.85rem;z-index:1000;opacity:0;transition:opacity .2s}
.toast.show{opacity:1}
/* tidy dark scrollbar inside results */
.results::-webkit-scrollbar{width:10px}
.results::-webkit-scrollbar-track{background:transparent}
.results::-webkit-scrollbar-thumb{background:#2a2d3a;border-radius:5px;border:2px solid var(--card)}
.results::-webkit-scrollbar-thumb:hover{background:#3a3d4a}
</style>
</head>
<body>
<div class="container">
  <div class="topbar">
    <h1>FastSearch</h1>
    <span class="stats" id="statsLine">Loading...</span>
  </div>
  <div class="search-box">
    <span class="icon">&#128269;</span>
    <input type="text" id="searchInput" placeholder="Type to search files..." autofocus>
  </div>
  <div class="toolbar">
    <div class="tab active" data-mode="name">Filename</div>
    <div class="tab" data-mode="content">Content</div>
    <div class="sep"></div>
    <input type="text" id="extFilter" placeholder="Extension (cpp)">
    <input type="text" id="pathFilter" placeholder="Path scope (D:/src)">
    <select id="typeFilter"><option value="-1">All</option><option value="0">Files</option><option value="1">Dirs</option></select>
    <select id="sortBy"><option value="">Default</option><option value="name">Name</option><option value="size">Size</option><option value="modified">Date</option></select>
    <select id="sortOrder"><option value="asc">Asc</option><option value="desc">Desc</option></select>
    <label><input type="checkbox" id="regexToggle">Regex</label>
  </div>
  <div class="results" id="results"><div class="empty">Type something to start searching</div></div>
  <div class="pagination" id="pagination" style="display:none">
    <button id="prevBtn" disabled>Previous</button>
    <span id="pageInfo"></span>
    <button id="nextBtn">Next</button>
  </div>
</div>
)HTML" R"HTML(<script>
const API = '';
let mode = 'name', page = 0, pageSize = 50, debounceTimer = null;

function $(id) { return document.getElementById(id); }
function formatSize(b) {
  if (b < 1024) return b + ' B';
  if (b < 1048576) return (b/1024).toFixed(1) + ' KB';
  if (b < 1073741824) return (b/1048576).toFixed(1) + ' MB';
  return (b/1073741824).toFixed(2) + ' GB';
}
function formatTime(ts) {
  if (!ts) return '';
  return new Date(ts * 1000).toLocaleString('zh-CN');
}
function escHtml(s) {
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

async function loadStats() {
  try {
    const r = await fetch(API + '/api/stats');
    const d = await r.json();
    $('statsLine').textContent = `${d.total_files.toLocaleString()} files | ${d.total_dirs.toLocaleString()} dirs | DB ${formatSize(d.db_size_bytes)}`;
  } catch(e) { $('statsLine').textContent = 'Cannot connect to server'; }
}

async function doSearch() {
  const q = $('searchInput').value.trim();
  if (!q) { $('results').innerHTML = '<div class="empty">Type something to start searching</div>'; $('pagination').style.display='none'; return; }
  const ext = $('extFilter').value.trim();
  const pathF = $('pathFilter').value.trim();
  $('results').innerHTML = '<div class="loading">Searching...</div>';
  try {
    const typeF = $('typeFilter').value;
    const sortBy = $('sortBy').value;
    const sortOrd = $('sortOrder').value;
    const useRegex = $('regexToggle').checked;
    let url;
    if (mode === 'name') {
      url = `${API}/api/search?q=${encodeURIComponent(q)}&limit=${pageSize}&offset=${page*pageSize}`;
      if (ext) url += `&ext=${encodeURIComponent(ext)}`;
      if (pathF) url += `&path=${encodeURIComponent(pathF)}`;
      if (typeF !== '-1') url += `&type=${typeF}`;
      if (sortBy) url += `&sort=${sortBy}&order=${sortOrd}`;
      if (useRegex) url += `&regex=1`;
    } else {
      url = `${API}/api/search/content?q=${encodeURIComponent(q)}&limit=${pageSize}`;
      if (ext) url += `&ext=${encodeURIComponent(ext)}`;
      if (pathF) url += `&path=${encodeURIComponent(pathF)}`;
    }
    const r = await fetch(url);
    const d = await r.json();
    renderResults(d);
  } catch(e) { $('results').innerHTML = '<div class="empty">Search error: ' + e.message + '</div>'; }
}

function renderResults(data) {
  const items = data.results || [];
  if (!items.length) { $('results').innerHTML = '<div class="empty">No results found</div>'; $('pagination').style.display='none'; return; }
  let html = '';
  for (const f of items) {
    const icon = f.is_dir ? '&#128193;' : '&#128196;';
    const badge = f.is_dir ? '<span class="badge badge-dir">DIR</span>' : '';
    const sizeStr = f.is_dir ? '' : formatSize(f.size || 0);
    const timeStr = formatTime(f.modified);
    let extra = '';
    if (f.line_number) {
      extra = `<div class="content-match">Line ${f.line_number}: ${escHtml(f.line_text || '')}</div>`;
    }
    html += `<div class="result-item" data-path="${escHtml(f.path)}" data-is-dir="${f.is_dir ? 1 : 0}">
      <div class="r-icon">${icon}</div>
      <div class="r-info"><div class="r-name">${escHtml(f.name)}${badge}</div><div class="r-path">${escHtml(f.path)}</div>${extra}</div>
      <div class="r-meta">${sizeStr}<br>${timeStr}</div></div>`;
  }
  $('results').innerHTML = html;
  const hasMore = items.length >= pageSize;
  $('pagination').style.display = (page > 0 || hasMore) ? 'flex' : 'none';
  $('prevBtn').disabled = page === 0;
  $('nextBtn').disabled = !hasMore;
  $('pageInfo').textContent = `Page ${page + 1}`;
}

async function openFile(path) {
  try { await fetch(`${API}/api/open?path=${encodeURIComponent(path)}`); } catch(e){}
}

// Delegated click → openFile for the row, unless the row is in inline
// rename/edit mode. Putting this on the container avoids having to rewrite
// an inline onclick attribute when the row's path changes after a rename.
document.addEventListener('click', function(e) {
  const row = e.target.closest && e.target.closest('.result-item');
  if (!row) return;
  if (row.classList.contains('editing')) { e.stopPropagation(); return; }
  if (e.target.closest('.rename-input')) return;
  openFile(row.dataset.path);
});

// Right-click on a result item → ask the native GUI host to show the real
// Windows shell context menu (same as right-clicking in File Explorer).
// Only active when running inside WebView2; in a plain browser we fall back
// to the default browser menu.
document.addEventListener('contextmenu', function(e) {
  if (!(window.chrome && window.chrome.webview && window.chrome.webview.postMessage)) return;
  let el = e.target;
  while (el && !el.classList.contains('result-item')) el = el.parentElement;
  if (!el) return;
  e.preventDefault();
  const path = el.dataset.path || '';
  const shift = e.shiftKey ? 1 : 0;
  window.chrome.webview.postMessage(
    `ctxmenu|${Math.round(e.clientX)}|${Math.round(e.clientY)}|${shift}|${path}`);
});

// ── Inline rename ────────────────────────────────────────────
// Triggered when the native host posts `beginRename|<path>` after the user
// picks "重命名" from the shell context menu. We replace the filename in the
// matching row with an <input>, Enter commits back via postMessage, Esc /
// blur cancels. Server answers with renameResult|<ok>|<old>|<new>.

function showToast(text) {
  let t = document.getElementById('toast');
  if (!t) { t = document.createElement('div'); t.id = 'toast'; t.className = 'toast'; document.body.appendChild(t); }
  t.textContent = text;
  t.classList.add('show');
  clearTimeout(t._hide);
  t._hide = setTimeout(() => t.classList.remove('show'), 2200);
}

// Windows paths compare case-insensitively and with / vs \ equivalent.
function normPath(p) { return (p || '').replace(/\//g, '\\').toLowerCase(); }

function findRowByPath(path) {
  const target = normPath(path);
  const rows = document.querySelectorAll('.result-item');
  for (const r of rows) if (normPath(r.dataset.path) === target) return r;
  return null;
}

function beginInlineRename(path) {
  const row = findRowByPath(path);
  if (!row || row.classList.contains('editing')) return;
  const nameEl = row.querySelector('.r-name');
  if (!nameEl) return;

  const base = path.split(/[\\/]/).pop();
  const savedHtml = nameEl.innerHTML;
  row.classList.add('editing');

  const input = document.createElement('input');
  input.type = 'text';
  input.className = 'rename-input';
  input.value = base;
  input.spellcheck = false;
  input.autocomplete = 'off';

  nameEl.innerHTML = '';
  nameEl.appendChild(input);

  // Focus + select the stem (everything before the last '.'), matching
  // Explorer's rename UX. Directories select the whole name.
  setTimeout(() => {
    input.focus();
    const isDir = row.dataset.isDir === '1';
    const dot = base.lastIndexOf('.');
    if (!isDir && dot > 0) input.setSelectionRange(0, dot);
    else input.select();
  }, 0);

  let finished = false;
  const cancel = () => {
    if (finished) return;
    finished = true;
    nameEl.innerHTML = savedHtml;
    row.classList.remove('editing');
  };
  const commit = () => {
    if (finished) return;
    const newName = input.value.trim();
    if (!newName || newName === base) { cancel(); return; }
    finished = true;
    input.disabled = true;
    row.dataset.renaming = '1';
    window.chrome.webview.postMessage('doRename|' + path + '|' + newName);
  };

  input.addEventListener('keydown', ev => {
    ev.stopPropagation();
    if (ev.key === 'Enter') { ev.preventDefault(); commit(); }
    else if (ev.key === 'Escape') { ev.preventDefault(); cancel(); }
  });
  input.addEventListener('blur', cancel);
  input.addEventListener('click', ev => ev.stopPropagation());
  input.addEventListener('dblclick', ev => ev.stopPropagation());
}

function finishInlineRename(oldPath, newPath, ok) {
  const row = findRowByPath(oldPath);
  if (!row) return;
  delete row.dataset.renaming;
  row.classList.remove('editing');

  const nameEl = row.querySelector('.r-name');
  const pathEl = row.querySelector('.r-path');
  if (!ok) {
    if (nameEl) {
      const origBase = oldPath.split(/[\\/]/).pop();
      const badge = row.dataset.isDir === '1'
        ? '<span class="badge badge-dir">DIR</span>' : '';
      nameEl.innerHTML = escHtml(origBase) + badge;
    }
    showToast('重命名失败');
    return;
  }

  const newBase = newPath.split(/[\\/]/).pop();
  row.dataset.path = newPath;
  const badge = row.dataset.isDir === '1'
    ? '<span class="badge badge-dir">DIR</span>' : '';
  if (nameEl) nameEl.innerHTML = escHtml(newBase) + badge;
  if (pathEl) pathEl.textContent = newPath;
}

if (window.chrome && window.chrome.webview && window.chrome.webview.addEventListener) {
  window.chrome.webview.addEventListener('message', function(e) {
    const raw = typeof e.data === 'string' ? e.data : JSON.stringify(e.data);
    if (raw.startsWith('beginRename|')) {
      beginInlineRename(raw.substring('beginRename|'.length));
      return;
    }
    if (raw.startsWith('renameResult|')) {
      const rest = raw.substring('renameResult|'.length);
      const p1 = rest.indexOf('|'); if (p1 < 0) return;
      const ok = rest.substring(0, p1) === '1';
      const body = rest.substring(p1 + 1);
      const p2 = body.indexOf('|'); if (p2 < 0) return;
      const oldPath = body.substring(0, p2);
      const newPath = body.substring(p2 + 1);
      finishInlineRename(oldPath, newPath, ok);
    }
  });
}

$('searchInput').addEventListener('input', () => {
  clearTimeout(debounceTimer);
  page = 0;
  debounceTimer = setTimeout(doSearch, 200);
});
$('extFilter').addEventListener('input', () => { clearTimeout(debounceTimer); page=0; debounceTimer=setTimeout(doSearch,300); });
$('pathFilter').addEventListener('input', () => { clearTimeout(debounceTimer); page=0; debounceTimer=setTimeout(doSearch,300); });
$('typeFilter').addEventListener('change', () => { page=0; doSearch(); });
$('sortBy').addEventListener('change', () => { page=0; doSearch(); });
$('sortOrder').addEventListener('change', () => { page=0; doSearch(); });
$('regexToggle').addEventListener('change', () => { page=0; doSearch(); });

document.querySelectorAll('.tab').forEach(t => {
  t.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach(x => x.classList.remove('active'));
    t.classList.add('active');
    mode = t.dataset.mode;
    page = 0;
    doSearch();
  });
});

$('prevBtn').addEventListener('click', () => { if(page>0){page--;doSearch();} });
$('nextBtn').addEventListener('click', () => { page++;doSearch(); });

$('searchInput').addEventListener('keydown', e => {
  if(e.key==='Enter'){clearTimeout(debounceTimer);doSearch();}
});

loadStats();
</script>
</body>
</html>)HTML";

void register_api_routes(httplib::Server& svr,
                         Database& db,
                         Indexer& indexer,
                         Searcher& searcher,
                         Watcher& watcher,
                         Config& config) {

    // ── Web UI ──
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(INDEX_HTML, "text/html; charset=utf-8");
    });

    // ── filename search ──
    svr.Get("/api/search", [&](const httplib::Request& req, httplib::Response& res) {
        SearchOptions opts;
        if (req.has_param("q"))        opts.query = req.get_param_value("q");
        if (req.has_param("ext"))      opts.ext_filter = req.get_param_value("ext");
        if (req.has_param("path"))     opts.path_filter = req.get_param_value("path");
        if (req.has_param("limit"))    opts.limit = std::stoi(req.get_param_value("limit"));
        if (req.has_param("offset"))   opts.offset = std::stoi(req.get_param_value("offset"));
        if (req.has_param("regex"))    opts.regex = req.get_param_value("regex") == "1";
        if (req.has_param("sort"))     opts.sort_by = req.get_param_value("sort");
        if (req.has_param("order"))    opts.sort_order = req.get_param_value("order");
        if (req.has_param("type"))     opts.type_filter = std::stoi(req.get_param_value("type"));
        if (req.has_param("min_size")) opts.min_size = std::stoll(req.get_param_value("min_size"));
        if (req.has_param("max_size")) opts.max_size = std::stoll(req.get_param_value("max_size"));

        auto results = searcher.search_filename(opts);

        json j;
        j["results"] = json::array();
        for (auto& f : results) j["results"].push_back(file_to_json(f));
        j["count"] = results.size();
        j["query"] = opts.query;

        res.set_content(j.dump(), "application/json");
    });

    // ── content search ──
    svr.Get("/api/search/content", [&](const httplib::Request& req, httplib::Response& res) {
        std::string q, path_scope, ext;
        int limit = 100;
        if (req.has_param("q"))     q = req.get_param_value("q");
        if (req.has_param("path"))  path_scope = req.get_param_value("path");
        if (req.has_param("ext"))   ext = req.get_param_value("ext");
        if (req.has_param("limit")) limit = std::stoi(req.get_param_value("limit"));

        auto matches = searcher.search_content(q, path_scope, ext, limit);

        json j;
        j["results"] = json::array();
        for (auto& m : matches) {
            std::string fname;
            auto dot = m.path.rfind('/');
            fname = (dot != std::string::npos) ? m.path.substr(dot + 1) : m.path;
            j["results"].push_back({
                {"name", fname},
                {"path", m.path},
                {"line_number", m.line_number},
                {"line_text", m.line_text},
                {"is_dir", false},
                {"size", 0},
                {"modified", 0}
            });
        }
        j["count"] = matches.size();
        j["query"] = q;

        res.set_content(j.dump(), "application/json");
    });

    // ── stats ──
    svr.Get("/api/stats", [&](const httplib::Request&, httplib::Response& res) {
        auto stats = db.get_stats();
        json j = {
            {"total_files", stats.total_files},
            {"total_dirs", stats.total_dirs},
            {"db_size_bytes", stats.db_size_bytes},
            {"indexing", indexer.is_running()},
            {"watching", watcher.is_running()}
        };
        res.set_content(j.dump(), "application/json");
    });

    // ── reindex ──
    svr.Post("/api/index", [&](const httplib::Request&, httplib::Response& res) {
        if (indexer.is_running()) {
            res.set_content(R"({"status":"already_running"})", "application/json");
            return;
        }
        std::thread([&]() { indexer.rebuild_index(); }).detach();
        res.set_content(R"({"status":"started"})", "application/json");
    });

    // ── config ──
    svr.Get("/api/config", [&](const httplib::Request&, httplib::Response& res) {
        json j = {
            {"server_host", config.server_host},
            {"server_port", config.server_port},
            {"index_paths", config.index_paths},
            {"exclude_patterns", config.exclude_patterns},
            {"db_path", config.db_path}
        };
        res.set_content(j.dump(2), "application/json");
    });

    svr.Put("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            if (j.contains("index_paths")) config.index_paths = j["index_paths"].get<std::vector<std::string>>();
            if (j.contains("exclude_patterns")) config.exclude_patterns = j["exclude_patterns"].get<std::vector<std::string>>();
            config.save(Config::default_config_path());
            res.set_content(R"({"status":"ok"})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ── open file in system explorer ──
    svr.Get("/api/open", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("path")) {
            res.status = 400;
            res.set_content(R"({"error":"missing path"})", "application/json");
            return;
        }
        std::string path = req.get_param_value("path");
        platform::open_in_explorer(path);
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // ── CORS headers for all responses ──
    svr.set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        return httplib::Server::HandlerResponse::Unhandled;
    });
}

} // namespace fastsearch
