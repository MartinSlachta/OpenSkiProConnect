// OpenSkiPro Connect - Screen: what the board and the speaker page share.
// The server (module Screen of Connect) pushes over the WebSocket:
//   config  - the configuration chosen in Connect (columns, zones of the TV, theme ...)
//   state   - the view model: event, phase, live lists, rankings, start list
//   clock   - time of day of the timing device {dayMs, synced}
//   control - message of the operator {message}
//   hello   - {version, web}: first after connecting; another web = Connect updated, reload
// Plain JavaScript without a framework (also for older browsers of TVs).
'use strict';

var Screen = (function () {
  var page = /speaker/.test(document.body.className) ? 'speaker' : 'board';
  var path = location.pathname.match(/^\/board\/(.+)$/);
  var S = {
    page: page,
    // a preview of the editor: /board/preview-edit, /speaker?board=preview-edit
    board: path ? decodeURIComponent(path[1]) : (new RegExp('[?&]board=([^&]*)').exec(location.search) || [])[1] || '',
    config: null,
    state: null,
    message: '',
    clockMs: null,
    clockAt: 0,
    online: false,
    handlers: { config: [], state: [], control: [] }
  };

  S.on = function (type, fn) { S.handlers[type].push(fn); };

  function emit(type) {
    var l = S.handlers[type];
    for (var i = 0; i < l.length; i++) l[i]();
  }

  function connect() {
    var ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host +
                           '/ws?page=' + S.page + '&board=' + encodeURIComponent(S.board));
    ws.onopen = function () { setOnline(true); };
    ws.onclose = function () { setOnline(false); setTimeout(connect, 2000); };
    ws.onerror = function () { try { ws.close(); } catch (e) {} };
    ws.onmessage = function (ev) {
      var m;
      try { m = JSON.parse(ev.data); } catch (e) { return; }
      if (m.type === 'hello') {
        // Connect updated (its pages changed): load the new pages
        if (S.web && m.data.web && m.data.web !== S.web) { location.reload(); return; }
        S.web = m.data.web;
      } else if (m.type === 'clock') {
        S.clockMs = m.data.dayMs;
        S.clockAt = now();
      } else if (m.type === 'config') {
        S.config = m.data;
        applyTheme(m.data);
        emit('config');
      } else if (m.type === 'state') {
        S.state = m.data;
        if (S.config) emit('state');
      } else if (m.type === 'control') {
        S.message = m.data.message || '';
        showMessage();
        emit('control');
      }
    };
  }

  function now() { return window.performance && performance.now ? performance.now() : Date.now(); }

  function setOnline(on) {
    S.online = on;
    var dot = document.getElementById('offline');
    if (dot) dot.style.display = on ? 'none' : 'block';
  }

  // Time of day of the timing device, running locally between the clock messages.
  S.deviceNow = function () {
    if (S.clockMs === null) return null;
    return S.clockMs + (now() - S.clockAt);
  };

  // Running time truncated to tenths (never rounded): "1:02.3".
  S.runningText = function (startDayMs) {
    var dev = S.deviceNow();
    if (dev === null || startDayMs === undefined || startDayMs === null) return '';
    var ms = dev - startDayMs;
    if (ms < 0) ms += 86400000;
    var t = Math.floor(ms / 100), s = Math.floor(t / 10), m = Math.floor(s / 60);
    return (m > 0 ? m + ':' + pad(s % 60) : String(s)) + '.' + (t % 10);
  };

  S.clockText = function (kind) {
    var ms;
    if (kind === 'none') return '';
    if (kind === 'pc') {
      var d = new Date();
      ms = ((d.getHours() * 60 + d.getMinutes()) * 60 + d.getSeconds()) * 1000;
    } else {
      ms = S.deviceNow();
      if (ms === null) return '';
    }
    var s = Math.floor(ms / 1000) % 86400;
    return pad(Math.floor(s / 3600)) + ':' + pad(Math.floor(s / 60) % 60) + ':' + pad(s % 60);
  };

  function pad(n) { return (n < 10 ? '0' : '') + n; }

  S.esc = function (v) {
    return String(v === undefined || v === null ? '' : v)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  };

  // {title1} {title2} {discipline} {location} {date} {run} of the event.
  S.expand = function (text) {
    var st = S.state, ev = (st && st.event) || {};
    var run = st ? (ev.runs && ev.runs[String(st.run)]) || (st.run ? 'Run ' + st.run : '') : '';
    return String(text || '').replace(/\{(\w+)\}/g, function (all, k) {
      if (k === 'run') return run;
      return ev[k] !== undefined && ev[k] !== null ? ev[k] : '';
    }).replace(/^\s*[-–]\s*|\s*[-–]\s*$/g, '');
  };

  // Is a zone visible in the current phase of the race?
  S.visible = function (zone) {
    var p = S.state ? S.state.phase : 'beforeRace';
    switch (zone.visible) {
      case 'timing': return p === 'live';
      case 'notTiming': return p !== 'live';
      case 'beforeRace': return p === 'beforeRace';
      case 'withResults': return p !== 'beforeRace';
      default: return true;
    }
  };

  // flags not in the folder: asked once, not at every redraw
  var noFlag = {};
  S.noFlag = function (img, code) { noFlag[code] = true; img.style.display = 'none'; };

  var GAPS = { gap: 1, groupGap: 1, inter1Gap: 1, inter2Gap: 1, inter3Gap: 1 };

  // HTML of one cell; the row is an object of the view model.
  S.cell = function (col, row, opt) {
    var f = col.field, v = row[f], fmt = col.format || '';
    opt = opt || {};
    if (f === 'rank' && opt.rank !== undefined) v = opt.rank;
    if (f === 'gap' && opt.gap !== undefined) v = opt.gap;
    if (f === 'name') {
      var last = row.lastName || '', first = row.firstName || '';
      if (fmt === 'first last' && (first || last)) v = (first + ' ' + last).trim();
      else if (fmt === 'last' && last) v = last;
      else v = row.name || (last + ' ' + first).trim();
    } else if (f === 'nation') {
      if (!v) return '';
      var flag = noFlag[v] ? '' : '<img class="flag" src="/flags/' + encodeURIComponent(v) + '" alt="" onerror="Screen.noFlag(this, \'' + S.esc(v) + '\')">';
      if (fmt === 'flag') return flag;
      if (fmt === 'code') return S.esc(v);
      return flag + '<span>' + S.esc(v) + '</span>';
    } else if (opt.live === f && row.startDayMs !== undefined) {
      // on course: the running time
      return '<span class="run" data-start="' + row.startDayMs + '">' + S.runningText(row.startDayMs) + '</span>';
    } else if (GAPS[f]) {
      if (v && /^[0-9]/.test(v)) v = '+' + v; // not the reason (gate) of a DSQ
    }
    if ((f === 'rank' || f === 'groupRank' || f === 'bib') && fmt !== 'plain' && v)
      return '<span class="box ' + f + '">' + S.esc(v) + '</span>';
    return S.esc(v);
  };

  // On course: the column where the time runs - the first field of row.live in the columns.
  S.liveField = function (cols, row) {
    if (!row.live) return null;
    for (var i = 0; i < row.live.length; i++)
      for (var k = 0; k < cols.length; k++) if (cols[k].field === row.live[i]) return row.live[i];
    return null;
  };

  // One row of a list in the columns of the configuration.
  S.rowHtml = function (cols, row, cls, attrs) {
    var grid = S.gridColumns(cols), live = S.liveField(cols, row), h = '';
    h += '<div class="row' + (cls ? ' ' + cls : '') + '"' + (attrs || '') + ' style="grid-template-columns:' + grid + '">';
    cols.forEach(function (c) { h += '<div class="a-' + (c.align || 'left') + '">' + S.cell(c, row, { live: live }) + '</div>'; });
    return h + '</div>';
  };

  S.headHtml = function (cols) {
    var h = '<div class="head" style="grid-template-columns:' + S.gridColumns(cols) + '">';
    cols.forEach(function (c) { h += '<div class="a-' + (c.align || 'left') + '">' + S.esc(c.header) + '</div>'; });
    return h + '</div>';
  };

  S.gridColumns = function (cols) {
    var t = [];
    for (var i = 0; i < cols.length; i++) t.push((cols[i].width || 10) + 'fr');
    return t.join(' ');
  };

  // Rows of the ranking of the configuration, inside an output group when asked.
  S.rankingRows = function (which, group) {
    var st = S.state;
    if (!st || !st.rankings) return [];
    var r = st.rankings[which === 'run' ? 'run' : 'total'];
    var rows = (r && r.rows) || [];
    if (group === undefined || group === null || group === 'all') return rows;
    var g = String(group), out = [];
    for (var i = 0; i < rows.length; i++) {
      var row = rows[i];
      if (row.groups && row.groups.indexOf(Number(g)) >= 0) {
        var copy = {};
        for (var k in row) copy[k] = row[k];
        copy.rank = row.groupRanks ? row.groupRanks[g] : row.rank;
        copy.gap = row.groupGaps ? row.groupGaps[g] : row.gap;
        out.push(copy);
      }
    }
    return out;
  };

  S.unclassifiedRows = function (which) {
    var st = S.state, out = [];
    if (!st || !st.rankings) return out;
    var r = st.rankings[which === 'run' ? 'run' : 'total'];
    var groups = (r && r.unclassified) || [];
    for (var i = 0; i < groups.length; i++) {
      if (groups[i].kind === 'notProcessed') continue;
      for (var j = 0; j < groups[i].rows.length; j++) out.push(groups[i].rows[j]);
    }
    return out;
  };

  S.groupLabel = function (g) {
    var ev = S.state && S.state.event;
    return (ev && ev.groups && ev.groups[String(g)]) || '';
  };

  S.groupIndices = function () {
    var ev = S.state && S.state.event, l = [];
    if (ev && ev.groups) for (var k in ev.groups) l.push(Number(k));
    l.sort(function (a, b) { return a - b; });
    return l;
  };

  // Key of the newest finisher (a new one starts the highlight / the jump).
  S.newestFinish = function () {
    var f = S.state && S.state.finished;
    if (!f || !f.length) return '';
    return f[0].bib + '@' + (f[0].finishDayMs || 0) + '@' + (f[0].running || '');
  };

  function applyTheme(c) {
    var t = c.theme || {}, r = document.documentElement.style;
    r.setProperty('--font', '"' + (c.font || 'Arial') + '", Arial, sans-serif');
    if (S.page === 'speaker') { // a fixed light page (screen.css): only the font of the configuration
      if (c.name) document.title = c.name + ' - Speaker - OpenSkiPro Connect';
      var own = document.getElementById('own-css');
      if (own) own.textContent = c.css || '';
      return;
    }
    var map = { background: '--bg', text: '--text', titleBackground: '--title-bg', titleText: '--title-text',
                headerText: '--header-text', row1: '--row1', row2: '--row2', highlight: '--hl',
                highlightText: '--hl-text', accent: '--accent' };
    for (var k in map) if (t[k]) r.setProperty(map[k], t[k]);
    r.setProperty('--font-size', (c.fontSize || 30) + 'px');
    r.setProperty('--line', (c.lineHeight || 44) + 'px');
    document.body.style.backgroundImage = t.backgroundImage ? 'url("/images/' + encodeURIComponent(t.backgroundImage) + '")' : '';
    var css = document.getElementById('own-css');
    if (css) css.textContent = c.css || '';
    if (c.name) document.title = c.name + ' - OpenSkiPro Connect';
  }

  function showMessage() {
    var el = document.getElementById('message');
    if (!el) return;
    el.style.display = S.message ? 'flex' : 'none';
    el.firstChild.textContent = S.message;
  }

  S.start = function () { setOnline(false); connect(); };
  return S;
})();
