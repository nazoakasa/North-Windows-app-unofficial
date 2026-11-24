'use strict';
const $ = (s) => document.querySelector(s);
const NS = 'http://www.w3.org/2000/svg';
const fmt = (n) => Number(n).toLocaleString('ja-JP');
let days = 30;
let timer = null;

async function api(path) {
  const r = await fetch(path, { credentials: 'same-origin', cache: 'no-store' });
  if (r.status === 401) throw new Error('auth');
  if (!r.ok) throw new Error('http ' + r.status);
  return r.json();
}

/* ------------------------------------------------------------ チャート */
function niceMax(v) {
  if (v <= 4) return 4;
  const p = 10 ** Math.floor(Math.log10(v));
  for (const m of [1, 1.2, 1.6, 2, 2.4, 3, 4, 4.8, 6, 8, 10]) {
    const c = m * p;
    if (c >= v && Number.isInteger(c / 4)) return c;      // 目盛りが整数になる値
  }
  return 10 * p;
}
function el(name, attrs = {}, parent) {
  const e = document.createElementNS(NS, name);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (parent) parent.appendChild(e);
  return e;
}
/* series: [{name, color, values, area?}] / kind: 'line' | 'bar' */
function chart(box, dates, series, kind) {
  box.textContent = '';
  const W = Math.max(300, Math.round(box.clientWidth || 800));
  const H = Math.round(Math.min(260, Math.max(190, W * 0.3)));
  const L = 46, R = 10, T = 10, B = 26;
  const n = dates.length;
  const max = niceMax(Math.max(1, ...series.flatMap((s) => s.values)));
  const step = n > 1 ? (W - L - R) / (n - 1) : 0;
  const x = (i) => (n > 1 ? L + i * step : (L + W - R) / 2);
  const y = (v) => T + (H - T - B) * (1 - v / max);
  const svg = el('svg', { viewBox: `0 0 ${W} ${H}`, role: 'img' });
  for (let g = 0; g <= 4; g++) {
    const gv = (max * g) / 4, gy = y(gv);
    el('line', { x1: L, x2: W - R, y1: gy, y2: gy, class: 'grid' }, svg);
    const t = el('text', { x: L - 8, y: gy + 4, 'text-anchor': 'end', class: 'axis' }, svg);
    t.textContent = fmt(Math.round(gv));
  }
  const ticks = Math.min(W < 520 ? 4 : 6, n);
  for (let k = 0; k < ticks; k++) {
    const i = ticks === 1 ? 0 : Math.round((k * (n - 1)) / (ticks - 1));
    const t = el('text', { x: x(i), y: H - 8, 'text-anchor': k === 0 ? 'start' : k === ticks - 1 ? 'end' : 'middle', class: 'axis' }, svg);
    t.textContent = dates[i].slice(5).replace('-', '/');
  }
  const defs = el('defs', {}, svg);
  series.forEach((s, si) => {
    if (kind === 'bar') {
      const bw = Math.max(1.5, (n > 1 ? step : 40) * 0.68);
      s.values.forEach((v, i) => {
        if (v <= 0) return;
        el('rect', { x: x(i) - bw / 2, y: y(v), width: bw, height: y(0) - y(v), rx: 1.5, fill: s.color }, svg);
      });
      return;
    }
    const pts = s.values.map((v, i) => `${x(i).toFixed(1)},${y(v).toFixed(1)}`);
    if (s.area && n > 1) {
      const id = 'g' + Math.random().toString(36).slice(2, 8) + si;
      const lg = el('linearGradient', { id, x1: 0, y1: 0, x2: 0, y2: 1 }, defs);
      el('stop', { offset: '0%', 'stop-color': s.color, 'stop-opacity': 0.35 }, lg);
      el('stop', { offset: '100%', 'stop-color': s.color, 'stop-opacity': 0 }, lg);
      el('path', { d: `M${x(0)},${y(0)} L${pts.join(' L')} L${x(n - 1)},${y(0)} Z`, fill: `url(#${id})` }, svg);
    }
    el('polyline', { points: pts.join(' '), fill: 'none', stroke: s.color, 'stroke-width': 2.4, 'stroke-linejoin': 'round', 'stroke-linecap': 'round' }, svg);
    if (n === 1) el('circle', { cx: x(0), cy: y(s.values[0]), r: 3.5, fill: s.color }, svg);
  });
  const cursor = el('line', { y1: T, y2: H - B, class: 'cursor', visibility: 'hidden' }, svg);
  const hit = el('rect', { x: L, y: T, width: W - L - R, height: H - T - B, fill: 'transparent' }, svg);
  box.appendChild(svg);
  const tip = document.createElement('div');
  tip.className = 'tip';
  tip.hidden = true;
  box.appendChild(tip);

  function show(ev) {
    const r = svg.getBoundingClientRect();
    const px = ((ev.clientX - r.left) / r.width) * W;
    const i = Math.max(0, Math.min(n - 1, n > 1 ? Math.round((px - L) / step) : 0));
    cursor.setAttribute('x1', x(i)); cursor.setAttribute('x2', x(i)); cursor.setAttribute('visibility', 'visible');
    tip.textContent = '';
    const d = document.createElement('div'); d.textContent = dates[i]; tip.appendChild(d);
    series.forEach((s) => {
      const row = document.createElement('div');
      const nm = document.createElement('span'); nm.textContent = s.name + ' '; nm.style.color = s.color;
      const b = document.createElement('b'); b.textContent = fmt(s.values[i]);
      row.append(nm, b); tip.appendChild(row);
    });
    tip.hidden = false;
    const bx = box.getBoundingClientRect();
    const tx = Math.max(60, Math.min(bx.width - 60, (x(i) / W) * r.width));
    tip.style.left = tx + 'px';
    tip.style.top = ((y(Math.max(...series.map((s) => s.values[i]))) / H) * r.height) - 8 + 'px';
  }
  const hide = () => { cursor.setAttribute('visibility', 'hidden'); tip.hidden = true; };
  hit.addEventListener('pointermove', show);
  hit.addEventListener('pointerdown', show);
  hit.addEventListener('pointerleave', hide);
}

/* ------------------------------------------------------------ 画面 */
const KPIS = [
  ['users', '総ユーザー数', true], ['devices', '端末数'], ['dau', '今日のアクティブ(DAU)'],
  ['wau', '7日間(WAU)'], ['mau', '30日間(MAU)'], ['new_users_1d', '今日の新規'],
  ['new_users_7d', '7日間の新規'], ['users_multi_device', '複数端末のユーザー'],
];
function renderOverview(o) {
  const box = $('#kpis');
  box.textContent = '';
  for (const [key, label, hot] of KPIS) {
    const c = document.createElement('div'); c.className = 'kpi' + (hot ? ' hot' : '');
    const l = document.createElement('div'); l.className = 'l'; l.textContent = label;
    const v = document.createElement('div'); v.className = 'v'; v.textContent = fmt(o[key] ?? 0);
    c.append(l, v); box.appendChild(c);
  }
  const vb = $('#versions');
  vb.textContent = '';
  const ents = Object.entries(o.versions || {});
  const mx = Math.max(1, ...ents.map((e) => e[1]));
  if (!ents.length) { vb.textContent = 'まだデータがありません'; vb.className = 'bars muted'; }
  else vb.className = 'bars';
  for (const [name, c] of ents) {
    const row = document.createElement('div'); row.className = 'bar';
    const nm = document.createElement('div'); nm.className = 'name'; nm.textContent = 'v' + name;
    const tr = document.createElement('div'); tr.className = 'track';
    const f = document.createElement('div'); f.className = 'fill'; f.style.width = (c / mx) * 100 + '%'; tr.appendChild(f);
    const nu = document.createElement('div'); nu.className = 'num'; nu.textContent = fmt(c) + ' 人';
    row.append(nm, tr, nu); vb.appendChild(row);
  }
  $('#updated').textContent = '更新: ' + new Date(o.generated_at).toLocaleString('ja-JP');
}
let lastSeries = null;
function renderSeries(s) {
  lastSeries = s;
  chart($('#chartDau'), s.dates, [{ name: 'アクティブ', color: '#e83848', values: s.dau, area: true }], 'line');
  chart($('#chartNew'), s.dates, [{ name: '新規', color: '#4aa3ff', values: s.new_users }], 'bar');
  chart($('#chartTotal'), s.dates, [{ name: '累計', color: '#00ba7c', values: s.total_users, area: true }], 'line');
}
function show(which) { $('#login').hidden = which !== 'login'; $('#dash').hidden = which !== 'dash'; }

async function load() {
  try {
    const [o, s] = await Promise.all([api('/api/overview'), api('/api/series?days=' + days)]);
    show('dash');
    renderOverview(o);
    renderSeries(s);
    clearInterval(timer);
    timer = setInterval(() => { if (!document.hidden) load(); }, 60000);
  } catch (e) {
    clearInterval(timer);
    if (e.message === 'auth') show('login');
    else { show('dash'); $('#updated').textContent = '取得に失敗しました (' + e.message + ')'; }
  }
}

$('#loginForm').addEventListener('submit', async (ev) => {
  ev.preventDefault();
  $('#loginErr').textContent = '';
  const r = await fetch('/login', { method: 'POST', credentials: 'same-origin', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ token: $('#token').value }) });
  if (r.status === 204) { $('#token').value = ''; load(); }
  else $('#loginErr').textContent = r.status === 429 ? '試行回数が多すぎます。しばらくしてからやり直してください' : 'トークンが違います';
});
$('#refresh').addEventListener('click', load);
$('#logout').addEventListener('click', async () => { await fetch('/logout', { method: 'POST', credentials: 'same-origin' }); show('login'); clearInterval(timer); });
$('#range').addEventListener('click', (ev) => {
  const b = ev.target.closest('button[data-days]');
  if (!b) return;
  days = parseInt(b.dataset.days, 10);
  document.querySelectorAll('#range button').forEach((x) => x.classList.toggle('on', x === b));
  load();
});
let rz = null;
window.addEventListener('resize', () => { clearTimeout(rz); rz = setTimeout(() => { if (lastSeries && !$('#dash').hidden) renderSeries(lastSeries); }, 150); });
load();
