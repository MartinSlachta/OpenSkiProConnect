// OpenSkiPro Connect - Screen: a board for a TV (docs/screen-design.md 4.1).
// Fixed design size scaled to the window; zones top to bottom; the lists page by
// themselves (ranking, start list), after a finish the page of the finisher.
'use strict';

(function () {
  var S = Screen;
  var canvas = document.getElementById('canvas');
  var pagers = {};       // zone index -> {page, gi, until, jump}
  var lastNewest = null; // key of the newest finisher seen
  var finishUntil = 0;   // the newest finisher highlighted until
  var logoIndex = 0, logoUntil = 0;

  function now() { return Date.now(); }

  function fit() {
    var c = S.config;
    if (!c) return;
    var k = Math.min(window.innerWidth / c.width, window.innerHeight / c.height);
    canvas.style.width = c.width + 'px';
    canvas.style.height = c.height + 'px';
    canvas.style.transform = 'translate(' + Math.max(0, (window.innerWidth - c.width * k) / 2) + 'px,' +
                             Math.max(0, (window.innerHeight - c.height * k) / 2) + 'px) scale(' + k + ')';
  }

  function lines(zone) { return zone.lines > 0 ? zone.lines : 10; }

  // The groups a paged zone goes through and the rows of one of them.
  function groupsOf(zone) {
    var r = S.config.ranking || {};
    if (zone.type !== 'ranking') return ['all'];
    if (r.group === 'rotate') {
      var g = S.groupIndices();
      return g.length ? g : ['all'];
    }
    return [r.group === undefined || r.group === '' ? 'all' : r.group];
  }

  function rowsOf(zone, group) {
    var st = S.state, r = S.config.ranking || {};
    if (!st) return [];
    switch (zone.type) {
      case 'ranking': return S.rankingRows(r.which, group);
      case 'startList': return (st.startList && st.startList.rows) || [];
      case 'unclassified': return S.unclassifiedRows(r.which);
      case 'finish': return (st.finished || []).slice(0, lines(zone));
      // the nearest to the finish (started first) at the bottom, just above the finish
      case 'onCourse': return (st.onCourse || []).slice(0, lines(zone)).reverse();
      case 'nextStart': return (st.onStart || []).slice(0, lines(zone)).reverse(); // the next one at the bottom, above on course
    }
    return [];
  }

  function paged(zone) { return zone.type === 'ranking' || zone.type === 'startList' || zone.type === 'unclassified'; }

  function pageCount(zone, rows) {
    var n = Math.max(1, Math.ceil(rows.length / lines(zone)));
    var max = (S.config.ranking || {}).maxPages;
    return max > 0 ? Math.min(n, max) : n;
  }

  function pager(i) {
    if (!pagers[i]) pagers[i] = { page: 0, gi: 0, until: now() + seconds(0) * 1000, jump: null };
    return pagers[i];
  }

  function seconds(page) {
    var r = S.config.ranking || {};
    return page === 0 ? (r.page1Seconds || 8) : (r.pageSeconds || 5);
  }

  // Next page (or group) when the time of the page is over.
  function advance() {
    if (!S.config || !S.state) return;
    var changed = false, t = now(), r = S.config.ranking || {};
    S.config.zones.forEach(function (zone, i) {
      if (!paged(zone) || !S.visible(zone)) return;
      var p = pager(i);
      if (p.jump && t >= p.jump.until) { p.jump = null; p.until = t + seconds(p.page) * 1000; changed = true; }
      if (p.jump || t < p.until) return;
      var groups = groupsOf(zone);
      if (p.gi >= groups.length) p.gi = 0;
      var count = pageCount(zone, rowsOf(zone, groups[p.gi]));
      if (r.freeze && zone.type === 'ranking') p.page = count; // only the 1st page (of every group)
      else p.page++;
      if (p.page >= count) { p.page = 0; p.gi = (p.gi + 1) % groups.length; }
      p.until = t + seconds(p.page) * 1000;
      changed = true;
    });
    if (changed) render();
  }

  // A new finisher: the finish zone highlights it, the ranking shows its page.
  function onNewFinisher() {
    var key = S.newestFinish();
    if (lastNewest === null) { lastNewest = key; return; }
    if (!key || key === lastNewest) return;
    lastNewest = key;
    var r = S.config.ranking || {}, bib = S.state.finished[0].bib, t = now();
    finishUntil = t + (r.finishSeconds || 0) * 1000;
    if (!(r.jumpSeconds > 0)) return;
    S.config.zones.forEach(function (zone, i) {
      if (zone.type !== 'ranking') return;
      var groups = groupsOf(zone);
      for (var gi = 0; gi < groups.length; gi++) {
        var rows = rowsOf(zone, groups[gi]);
        for (var k = 0; k < rows.length; k++) {
          if (rows[k].bib === bib) {
            var page = Math.floor(k / lines(zone));
            if (page >= pageCount(zone, rows)) return;
            pager(i).jump = { bib: bib, gi: gi, page: page, until: t + r.jumpSeconds * 1000 };
            return;
          }
        }
      }
    });
  }

  // bottomHeight: the rows at the bottom of a box of that height (on course: just above the finish)
  // Every list in the same columns of the configuration: they line up by themselves.
  function listHtml(rows, hlBib, hlFirst, bottomHeight) {
    var cols = S.config.columns || [], h = cols.length ? S.headHtml(cols) : '';
    if (bottomHeight) h += '<div class="bottom" style="height:' + bottomHeight + 'px">';
    for (var k = 0; k < rows.length; k++) {
      var row = rows[k], hl = (hlBib && row.bib === hlBib) || (hlFirst && k === 0);
      h += S.rowHtml(cols, row, hl ? 'hl' : '');
    }
    if (bottomHeight) h += '</div>';
    return h;
  }

  function countersHtml() {
    var r = S.config.ranking || {}, st = S.state;
    var rk = st && st.rankings && st.rankings[r.which === 'run' ? 'run' : 'total'];
    var c = (rk && rk.counters) || {};
    return '<span class="counters"><span>Classified : ' + (c.classified || 0) + '</span><span>List : ' + (c.list || 0) +
           '</span><span>DNS : ' + (c.dns || 0) + '</span><span>DNF : ' + (c.dnf || 0) + '</span><span>DSQ : ' + (c.dsq || 0) + '</span></span>';
  }

  function zoneHtml(zone, i) {
    var c = S.config, L = c.lineHeight || 44;
    if (zone.type === 'title') {
      var t1 = S.expand(c.title1), t2 = S.expand(c.title2);
      if (zone.lines >= 2)
        return '<div class="title"><div class="t">' + S.esc(t1) + '</div><div class="clock" data-clock="1"></div></div>' +
               '<div class="title" style="height:' + L + 'px;font-size:.9em"><div class="t">' + S.esc(t2) + '</div></div>';
      return '<div class="title"><div class="t">' + S.esc(t1 + (t1 && t2 ? ' - ' : '') + t2) + '</div><div class="clock" data-clock="1"></div></div>';
    }
    if (zone.type === 'foot') {
      var logos = c.logos || [], logo = '';
      if (logos.length) logo = '<img src="/images/' + encodeURIComponent(logos[logoIndex % logos.length]) + '" alt="">';
      return '<div class="foot" style="height:' + (lines(zone) * L) + 'px"><div class="text">' + S.esc(S.expand(c.footText)) + '</div>' + logo + '</div>';
    }
    var title = zone.title || '', hlBib = null, rows;
    if (paged(zone)) {
      var p = pager(i), groups = groupsOf(zone), gi = p.jump ? p.jump.gi : p.gi;
      if (gi >= groups.length) gi = 0;
      var all = rowsOf(zone, groups[gi]), page = p.jump ? p.jump.page : p.page;
      if (page >= pageCount(zone, all)) page = 0;
      rows = all.slice(page * lines(zone), (page + 1) * lines(zone));
      if (p.jump) hlBib = p.jump.bib;
      if (groups[gi] !== 'all' && groups[gi] !== undefined) title += (title ? ' - ' : '') + S.groupLabel(groups[gi]);
    } else {
      rows = rowsOf(zone);
    }
    var h = '';
    if (zone.title || (zone.type === 'ranking' && zone.counters))
      h += '<div class="zone-title"><span>' + S.esc(title) + '</span>' + (zone.type === 'ranking' && zone.counters ? countersHtml() : '') + '</div>';
    h += listHtml(rows, hlBib, zone.type === 'finish' && now() < finishUntil, zone.type === 'onCourse' || zone.type === 'nextStart' ? lines(zone) * L : 0);
    var height = (zone.title || (zone.type === 'ranking' && zone.counters) ? L : 0) + ((S.config.columns || []).length ? L * 0.8 : 0) + lines(zone) * L;
    return '<div style="height:' + height + 'px;overflow:hidden">' + h + '</div>';
  }

  function render() {
    var c = S.config;
    if (!c) return;
    document.getElementById('wait').style.display = 'none';
    var h = '';
    c.zones.forEach(function (zone, i) {
      if (S.visible(zone)) h += '<div class="zone z-' + zone.type + '">' + zoneHtml(zone, i) + '</div>';
    });
    canvas.innerHTML = h;
    tick();
  }

  // Running times and the clock, 10 times a second, without drawing again.
  function tick() {
    var runs = canvas.querySelectorAll('.run[data-start]');
    for (var i = 0; i < runs.length; i++) runs[i].textContent = S.runningText(Number(runs[i].getAttribute('data-start')));
    var clocks = canvas.querySelectorAll('[data-clock]');
    for (var k = 0; k < clocks.length; k++) clocks[k].textContent = S.clockText(S.config ? S.config.clock : 'device');
  }

  setInterval(tick, 100);
  setInterval(function () {
    advance();
    var c = S.config, t = now();
    if (c && c.logos && c.logos.length > 1 && t >= logoUntil) {
      logoUntil = t + (c.logoSeconds || 10) * 1000;
      logoIndex++;
      render();
    }
    if (finishUntil && t >= finishUntil) { finishUntil = 0; render(); }
  }, 250);

  window.addEventListener('resize', fit);
  S.on('config', function () { pagers = {}; fit(); render(); });
  S.on('state', function () { onNewFinisher(); render(); });
  S.start();
})();
