// OpenSkiPro Connect - Screen: the page of the speaker (docs/screen-design.md 4.2, 9).
// Nothing switches by itself: next to start, on course and the last finishers on the
// left, the whole results on the right, scrolled by the commentator - a new finisher
// is highlighted, the scroll position stays. A click on a row: card of the competitor.
// The same columns as the TV (the columns of the configuration); its own light look.
'use strict';

(function () {
  var S = Screen;
  var left = document.getElementById('left'), right = document.getElementById('right');
  var view = load('osc-speaker', { which: 'total', group: 'all', scale: 1 });
  var lastNewest = null, newestBib = null, finishUntil = 0;

  function load(key, def) {
    try { var v = JSON.parse(localStorage.getItem(key)); if (v) for (var k in def) if (v[k] === undefined) v[k] = def[k]; return v || def; }
    catch (e) { return def; }
  }
  function store() { try { localStorage.setItem('osc-speaker', JSON.stringify(view)); } catch (e) {} }

  // the lists of the page (fixed): left the live ones, right the results
  var LEFT = [{ type: 'nextStart', title: 'Next to start', lines: 5 }, { type: 'onCourse', title: 'On course' },
              { type: 'finish', title: 'Last finishers', lines: 3 }];
  var RESULTS = [{ type: 'ranking', title: 'Results', counters: true }, { type: 'unclassified', title: 'DNS / DNF / DSQ' }];
  var BEFORE = [{ type: 'startList', title: 'Start list' }];

  function listHtml(zone, rows, hl) {
    var cols = S.config.columns || [], h = '<div class="zone-title">' + S.esc(zone.title) + counters(zone) + '</div>';
    if (cols.length) h += S.headHtml(cols);
    if (!rows.length) h += '<div class="sp-empty">-</div>';
    rows.forEach(function (row) { h += S.rowHtml(cols, row, hl && row.bib === hl ? 'hl' : '', ' data-bib="' + S.esc(row.bib) + '"'); });
    return '<div class="sp-zone z-' + zone.type + '">' + h + '</div>';
  }

  function counters(zone) {
    if (zone.type !== 'ranking' || !zone.counters || !S.state) return '';
    var rk = S.state.rankings[view.which === 'run' ? 'run' : 'total'], c = (rk && rk.counters) || {};
    return '<span class="counters"><span>Classified : ' + (c.classified || 0) + '</span><span>DNS : ' + (c.dns || 0) +
           '</span><span>DNF : ' + (c.dnf || 0) + '</span><span>DSQ : ' + (c.dsq || 0) + '</span></span>';
  }

  function rowsOf(zone) {
    var st = S.state, n = zone.lines > 0 ? zone.lines : 1000;
    switch (zone.type) {
      case 'onCourse': return (st.onCourse || []).slice(0, n).reverse(); // the nearest to the finish at the bottom
      case 'nextStart': return (st.onStart || []).slice(0, n).reverse(); // the next one at the bottom, above on course
      case 'finish': return (st.finished || []).slice(0, n);
      case 'ranking': return S.rankingRows(view.which, view.group);
      case 'startList': return ((st.startList && st.startList.rows) || []).slice(0, n);
      case 'unclassified': return S.unclassifiedRows(view.which);
    }
    return [];
  }

  // Bib of the first row visible at the top of the results (kept after a redraw).
  function anchor() {
    var rows = right.querySelectorAll('.row'), top = right.getBoundingClientRect().top;
    for (var i = 0; i < rows.length; i++) {
      var r = rows[i].getBoundingClientRect();
      if (r.bottom > top + 1) return { bib: rows[i].getAttribute('data-bib'), offset: r.top - top };
    }
    return null;
  }

  function render() {
    var c = S.config, st = S.state;
    if (!c || !st) return;
    var t1 = S.expand(c.title1), t2 = S.expand(c.title2);
    document.getElementById('title').textContent = t1 + (t1 && t2 ? ' - ' : '') + t2;
    document.getElementById('which').textContent = view.which === 'run' ? 'Run ' + (st.run || '') : 'Total';
    fillGroups();
    var hl = Date.now() < finishUntil ? newestBib : null, l = '', r = '';
    LEFT.forEach(function (zone) { l += listHtml(zone, rowsOf(zone), hl); });
    (st.phase === 'beforeRace' ? BEFORE : RESULTS).forEach(function (zone) { r += listHtml(zone, rowsOf(zone), hl); });
    left.innerHTML = l;
    var a = anchor(), keep = right.scrollTop;
    right.innerHTML = r;
    if (a && keep > 0) {
      var row = right.querySelector('.row[data-bib="' + a.bib + '"]');
      if (row) right.scrollTop += row.getBoundingClientRect().top - right.getBoundingClientRect().top - a.offset;
      else right.scrollTop = keep;
    }
    tick();
  }

  function fillGroups() {
    var sel = document.getElementById('group'), g = S.groupIndices(), want = ['all'].concat(g).join(',');
    if (sel.getAttribute('data-groups') === want) return;
    sel.setAttribute('data-groups', want);
    var h = '<option value="all">All</option>';
    g.forEach(function (i) { h += '<option value="' + i + '">' + S.esc(S.groupLabel(i)) + '</option>'; });
    sel.innerHTML = h;
    sel.value = String(view.group);
    if (sel.value !== String(view.group)) { view.group = 'all'; sel.value = 'all'; }
    sel.style.display = g.length ? '' : 'none';
  }

  function onNewFinisher() {
    var key = S.newestFinish();
    if (lastNewest !== null && key && key !== lastNewest) {
      newestBib = S.state.finished[0].bib;
      finishUntil = Date.now() + ((S.config.ranking || {}).finishSeconds || 15) * 1000;
    }
    lastNewest = key;
  }

  // Card of a competitor: everything known in this race.
  function card(bib) {
    var st = S.state, rows = [], i;
    var lists = [st.rankings.total && st.rankings.total.rows, st.rankings.run && st.rankings.run.rows, st.onCourse, st.finished, st.onStart,
                 st.startList && st.startList.rows, S.unclassifiedRows('total'), S.unclassifiedRows('run')];
    lists.forEach(function (l) { (l || []).forEach(function (r) { if (r.bib === bib) rows.push(r); }); });
    if (!rows.length) return;
    var m = {}; // the rankings first: the live lists have the ranks of the moment of the finish
    for (i = 0; i < rows.length; i++) for (var k in rows[i]) if (m[k] === undefined && rows[i][k] !== null) m[k] = rows[i][k];
    var items = [['Bib', m.bib], ['Nation', m.nation], ['Club', m.club], ['Team', m.team], ['Year', m.year], ['Category', m.category],
                 ['Code', m.code], ['Rank', m.rank], ['Rank in ' + (m.group || 'group'), m.groupRank], ['Time', m.time], ['Gap', m.gap ? '+' + m.gap : '']];
    var runs = (st.event && st.event.runCount) || 1, inters = (st.event && st.event.interCount) || 0;
    for (i = 1; i <= runs; i++) if (m['run' + i]) items.push(['Run ' + i, m['run' + i] + (m['run' + i + 'Rank'] ? '  (' + m['run' + i + 'Rank'] + ')' : '')]);
    for (i = 1; i <= inters; i++) if (m['inter' + i]) items.push(['Intermediate ' + i, m['inter' + i] + (m['inter' + i + 'Gap'] ? '  +' + m['inter' + i + 'Gap'] : '') + (m['inter' + i + 'Rank'] ? '  (' + m['inter' + i + 'Rank'] + ')' : '')]);
    if (m.status) items.push(['Status', m.status + (m.reason ? ' - ' + m.reason : '')]);
    var h = '<h2>' + S.esc(m.name) + '</h2><table>';
    items.forEach(function (it) { if (it[1]) h += '<tr><td>' + S.esc(it[0]) + '</td><td><b>' + S.esc(it[1]) + '</b></td></tr>'; });
    var el = document.getElementById('card');
    el.firstChild.innerHTML = h + '</table>';
    el.style.display = 'flex';
  }

  function tick() {
    var runs = document.querySelectorAll('.run[data-start]');
    for (var i = 0; i < runs.length; i++) runs[i].textContent = S.runningText(Number(runs[i].getAttribute('data-start')));
    document.getElementById('clock').textContent = S.clockText(S.config ? S.config.clock : 'device');
  }

  function applyScale() { document.body.style.fontSize = 'calc(var(--font-size) * ' + view.scale + ')'; }

  document.getElementById('which').onclick = function () { view.which = view.which === 'run' ? 'total' : 'run'; store(); render(); };
  document.getElementById('group').onchange = function () { view.group = this.value; store(); render(); };
  document.getElementById('smaller').onclick = function () { view.scale = Math.max(0.6, view.scale - 0.1); store(); applyScale(); };
  document.getElementById('bigger').onclick = function () { view.scale = Math.min(2.5, view.scale + 0.1); store(); applyScale(); };
  // pointerup, not click: a new state may redraw the rows between pressing and releasing
  var cardShown = 0;
  document.body.addEventListener(window.PointerEvent ? 'pointerup' : 'mouseup', function (e) {
    var el = document.getElementById('card');
    if (el.style.display === 'flex') {
      if (Date.now() - cardShown > 300) el.style.display = 'none';
      return;
    }
    var row = e.target.closest ? e.target.closest('.row[data-bib]') : null;
    if (row) { card(row.getAttribute('data-bib')); cardShown = Date.now(); }
  });

  setInterval(tick, 100);
  setInterval(function () { if (finishUntil && Date.now() >= finishUntil) { finishUntil = 0; render(); } }, 500);
  applyScale();
  S.on('config', render);
  S.on('state', function () { onNewFinisher(); render(); });
  S.start();
})();
