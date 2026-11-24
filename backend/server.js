// north-client 利用者数の集計サーバー + Webダッシュボード
//
// 保存するもの : HMAC-SHA256(pepper, クライアントが送った uid/did のハッシュ)、アプリのバージョン、日単位の日付、回数
// 保存しないもの: 生のユーザーID・端末ID、IPアドレス、User-Agent、Cookie、投稿内容
//   - IP はレート制限のためメモリ上でだけ数え、DB にもログにも書かない
//   - DB が漏れても、pepper (環境変数) が無ければハッシュから元のIDを逆引きできない
//
// 環境変数は README.md を参照。 Node.js 22.5 以上 (node:sqlite を使用)。
import http from 'node:http';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { DatabaseSync } from 'node:sqlite';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const HEX64 = /^[0-9a-f]{64}$/;
const VER = /^[0-9A-Za-z.\-]{1,16}$/;
const MAX_BODY = 2048;

/* .env ファイル (KEY=VALUE / # コメント / 引用符つき可)。実際の環境変数は上書きしない */
export function loadDotEnv(file) {
  const out = {};
  let txt = '';
  try { txt = fs.readFileSync(file, 'utf8'); } catch { return out; }
  for (const line of txt.split(/\r?\n/)) {
    const m = line.match(/^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$/);
    if (!m || line.trim().startsWith('#')) continue;
    let v = m[2];
    if ((v.startsWith('"') && v.endsWith('"')) || (v.startsWith("'") && v.endsWith("'"))) v = v.slice(1, -1);
    else v = v.replace(/\s+#.*$/, '');
    out[m[1]] = v;
  }
  return out;
}

export function isPrivateAddr(a) {
  a = String(a || '').replace(/^::ffff:/i, '').toLowerCase();
  if (a === '::1' || a.startsWith('fe80:') || a.startsWith('fc') || a.startsWith('fd')) return true;
  const m = a.match(/^(\d+)\.(\d+)\.\d+\.\d+$/);
  if (!m) return false;
  const [x, y] = [Number(m[1]), Number(m[2])];
  return x === 127 || x === 10 || (x === 192 && y === 168) || (x === 172 && y >= 16 && y <= 31) || (x === 169 && y === 254);
}

export function configFromEnv(env = process.env) {
  const trust = String(env.STATS_TRUST_PROXY || '').toLowerCase();
  const container = Boolean(env.SERVER_PORT);          // Pelican / Pterodactyl などのコンテナ環境
  return {
    pepper: env.STATS_PEPPER || '',
    clientKey: env.STATS_CLIENT_KEY || '',
    adminToken: env.STATS_ADMIN_TOKEN || '',
    dbPath: env.STATS_DB || path.join(HERE, 'stats.db'),
    retentionDays: parseInt(env.STATS_RETENTION_DAYS || '400', 10),
    // false | 'private'(プライベート/ループバックのプロキシだけ信用) | 'any'
    trustProxy: trust === 'any' ? 'any' : ['1', 'true', 'private'].includes(trust) ? 'private' : false,
    // true | false | 'auto'(信用するプロキシが X-Forwarded-Proto: https を付けたときだけ Secure)
    secureCookie: env.STATS_COOKIE_SECURE === '1' ? true : env.STATS_COOKIE_SECURE === '0' ? false : 'auto',
    sessionHours: parseInt(env.STATS_SESSION_HOURS || '12', 10),
    bind: env.STATS_BIND || (container ? '0.0.0.0' : '127.0.0.1'),
    port: parseInt(env.SERVER_PORT || env.PORT || '8787', 10),     // コンテナでは割り当てポート(SERVER_PORT)だけが外に届く
    pingLimit: 30,       // /v1/* : 1 IP あたり 1 分間の上限
    loginLimit: 5,       // /login: 1 IP あたり 1 分間の上限
  };
}

/* 保存先ディレクトリを用意し、作れない/書けないときは原因の分かるメッセージで止める */
function ensureWritableDir(dir) {
  try {
    fs.mkdirSync(dir, { recursive: true });
    fs.accessSync(dir, fs.constants.W_OK);
  } catch (e) {
    throw new Error(`データの保存先 ${dir} を作成/書き込みできません (${e.code || e.message})。` +
      'STATS_DB の指定を確認してください。Pelican などのコンテナ運用では STATS_DB を設定せず、既定の ./stats.db を使うのが確実です');
  }
}

/* 秘密情報の補完: 環境変数/.env に無ければ secrets.json (DB と同じ場所) を使い、無ければ初回に生成して保存する。
 * 既存の DB があるのに pepper が見つからない場合は、黙って作り直さずに止める (照合が切れるため)。 */
export function resolveSecrets(cfg, env = process.env, log = console.log) {
  ensureWritableDir(path.dirname(cfg.dbPath));
  const file = path.join(path.dirname(cfg.dbPath), 'secrets.json');
  let saved = {};
  try { saved = JSON.parse(fs.readFileSync(file, 'utf8')); } catch { /* 無ければ空 */ }
  const gen = () => crypto.randomBytes(36).toString('base64url');
  const created = [];
  if (!cfg.pepper) {
    cfg.pepper = saved.pepper || '';
    if (!cfg.pepper) {
      let hasDb = false;
      try { hasDb = fs.statSync(cfg.dbPath).size > 0; } catch { /* なし */ }
      if (hasDb && env.STATS_ALLOW_NEW_PEPPER !== '1') {
        throw new Error(`既存の DB (${cfg.dbPath}) がありますが STATS_PEPPER が見つかりません。以前の値を .env か環境変数に設定してください。` +
          '(新しく作り直してよい場合だけ STATS_ALLOW_NEW_PEPPER=1。同じ人が新規として数え直されます)');
      }
      cfg.pepper = saved.pepper = gen();
      created.push('pepper');
    }
  }
  if (!cfg.adminToken && env.STATS_ADMIN_TOKEN === undefined) {
    cfg.adminToken = saved.adminToken || (saved.adminToken = gen(), created.push('adminToken'), saved.adminToken);
  }
  if (!cfg.clientKey && env.STATS_CLIENT_KEY === undefined) {
    cfg.clientKey = saved.clientKey || (saved.clientKey = gen(), created.push('clientKey'), saved.clientKey);
  }
  if (created.length) {
    ensureWritableDir(path.dirname(file));
    fs.writeFileSync(file, JSON.stringify(saved, null, 2), { mode: 0o600 });
    log('==================== 初回起動: 秘密情報を生成しました ====================');
    log(`保存先: ${file}  (削除・公開しないこと。pepper は別の場所にも控えを)`);
    if (created.includes('adminToken')) log(`管理用トークン (ダッシュボードのログイン) : ${cfg.adminToken}`);
    if (created.includes('clientKey')) log(`アプリ用キー (config.h の STATS_KEY)       : ${cfg.clientKey}`);
    log('※ トークン/キーの表示はこの1回だけ。あとから見るときは secrets.json を開く。');
    log('==========================================================================');
  }
  return cfg;
}

const today = () => Math.floor(Date.now() / 86400000);          // UTC の日付(エポックからの日数)
const dayStr = (d) => new Date(d * 86400000).toISOString().slice(0, 10);
const safeEq = (a, b) => {
  const x = crypto.createHash('sha256').update(String(a)).digest();
  const y = crypto.createHash('sha256').update(String(b)).digest();
  return crypto.timingSafeEqual(x, y);
};

class RateLimiter {
  constructor(limit) { this.limit = limit; this.hits = new Map(); }
  hit(key) {
    const now = Date.now();
    let q = this.hits.get(key);
    if (!q) { q = []; this.hits.set(key, q); }
    while (q.length && now - q[0] > 60000) q.shift();
    if (q.length >= this.limit) return false;
    q.push(now);
    if (this.hits.size > 5000) for (const [k, v] of this.hits) if (!v.length || now - v[v.length - 1] > 60000) this.hits.delete(k);
    return true;
  }
}

export function createApp(cfg) {
  if (!cfg.pepper || cfg.pepper.length < 32) {
    throw new Error('STATS_PEPPER が未設定、または短すぎます (32文字以上)。例: node -e "console.log(require(\'crypto\').randomBytes(48).toString(\'base64url\'))"');
  }
  ensureWritableDir(path.dirname(cfg.dbPath));
  const isNew = !fs.existsSync(cfg.dbPath);
  let db;
  try { db = new DatabaseSync(cfg.dbPath); } catch (e) {
    throw new Error(`データベースを開けません: ${cfg.dbPath} (${e.message})。ファイルの権限と STATS_DB の指定を確認してください`);
  }
  db.exec('PRAGMA journal_mode=WAL');
  db.exec(`CREATE TABLE IF NOT EXISTS installs (
      uid_h TEXT NOT NULL, did_h TEXT NOT NULL, app TEXT NOT NULL,
      first_seen INTEGER NOT NULL, last_seen INTEGER NOT NULL, pings INTEGER NOT NULL DEFAULT 1,
      PRIMARY KEY (uid_h, did_h));
    CREATE INDEX IF NOT EXISTS idx_installs_last ON installs(last_seen);
    CREATE TABLE IF NOT EXISTS activity (day INTEGER NOT NULL, uid_h TEXT NOT NULL, PRIMARY KEY (day, uid_h));`);
  if (isNew) { try { fs.chmodSync(cfg.dbPath, 0o600); } catch { /* ignore */ } }

  const mac = (kind, value) => crypto.createHmac('sha256', cfg.pepper).update(`${kind}:${value}`).digest('hex');
  const sessionKey = crypto.createHmac('sha256', cfg.pepper).update('dashboard-session-v1').digest();
  const signSession = (exp) => crypto.createHmac('sha256', sessionKey).update(String(exp)).digest('hex');

  const q = {
    upInstall: db.prepare(`INSERT INTO installs (uid_h, did_h, app, first_seen, last_seen, pings) VALUES (?,?,?,?,?,1)
      ON CONFLICT(uid_h, did_h) DO UPDATE SET last_seen=excluded.last_seen, app=excluded.app, pings=pings+1`),
    upActivity: db.prepare('INSERT OR IGNORE INTO activity (day, uid_h) VALUES (?, ?)'),
    delInstall: db.prepare('DELETE FROM installs WHERE uid_h=? AND did_h=?'),
    userLeft: db.prepare('SELECT COUNT(*) c FROM installs WHERE uid_h=?'),
    delActivity: db.prepare('DELETE FROM activity WHERE uid_h=?'),
    purgeInstalls: db.prepare('DELETE FROM installs WHERE last_seen < ?'),
    purgeActivity: db.prepare('DELETE FROM activity WHERE day < ?'),
  };
  const one = (sql, ...a) => db.prepare(sql).get(...a);
  const all = (sql, ...a) => db.prepare(sql).all(...a);

  function purge() {
    const limit = today() - cfg.retentionDays;
    q.purgeInstalls.run(limit);
    q.purgeActivity.run(limit);
  }
  purge();
  const purgeTimer = setInterval(() => { try { purge(); } catch { /* ignore */ } }, 86400000);
  purgeTimer.unref();

  // ---------------- 集計 ----------------
  function overview() {
    const d = today();
    const act = (n) => one('SELECT COUNT(DISTINCT uid_h) c FROM activity WHERE day >= ?', d - n + 1).c;
    const newer = (n) => one('SELECT COUNT(*) c FROM (SELECT MIN(first_seen) f FROM installs GROUP BY uid_h) WHERE f >= ?', d - n + 1).c;
    return {
      generated_at: new Date().toISOString(),
      users: one('SELECT COUNT(DISTINCT uid_h) c FROM installs').c,
      devices: one('SELECT COUNT(DISTINCT did_h) c FROM installs').c,
      users_multi_device: one('SELECT COUNT(*) c FROM (SELECT uid_h FROM installs GROUP BY uid_h HAVING COUNT(DISTINCT did_h) > 1)').c,
      dau: act(1), wau: act(7), mau: act(30),
      new_users_1d: newer(1), new_users_7d: newer(7), new_users_30d: newer(30),
      versions: Object.fromEntries(all('SELECT app, COUNT(DISTINCT uid_h) c FROM installs GROUP BY app ORDER BY c DESC').map((r) => [r.app, r.c])),
    };
  }
  function series(days) {
    const end = today(), start = end - days + 1;
    const dau = new Map(all('SELECT day, COUNT(*) c FROM activity WHERE day >= ? GROUP BY day', start).map((r) => [r.day, r.c]));
    const first = new Map(all('SELECT f, COUNT(*) c FROM (SELECT MIN(first_seen) f FROM installs GROUP BY uid_h) GROUP BY f').map((r) => [r.f, r.c]));
    let total = 0;
    for (const [dd, c] of first) if (dd < start) total += c;
    const out = { dates: [], dau: [], new_users: [], total_users: [] };
    for (let dd = start; dd <= end; dd++) {
      const n = first.get(dd) || 0;
      total += n;
      out.dates.push(dayStr(dd));
      out.dau.push(dau.get(dd) || 0);
      out.new_users.push(n);
      out.total_users.push(total);
    }
    return out;
  }

  // ---------------- HTTP 補助 ----------------
  const pingLimiter = new RateLimiter(cfg.pingLimit);
  const loginLimiter = new RateLimiter(cfg.loginLimit);
  const peerTrusted = (req) => cfg.trustProxy === 'any' || (cfg.trustProxy === 'private' && isPrivateAddr(req.socket.remoteAddress));
  const clientIp = (req) => {
    const peer = req.socket.remoteAddress || '';
    if (peerTrusted(req)) {
      const xr = req.headers['x-real-ip'];
      if (xr) return String(xr).trim();
      const xf = req.headers['x-forwarded-for'];
      if (xf) { const parts = String(xf).split(','); return parts[parts.length - 1].trim(); }
    }
    return peer;
  };

  const baseHeaders = {
    'X-Content-Type-Options': 'nosniff',
    'Referrer-Policy': 'no-referrer',
    'X-Frame-Options': 'DENY',
    'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Resource-Policy': 'same-origin',
  };
  const csp = "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'";

  function send(res, status, body, headers = {}) {
    res.writeHead(status, { ...baseHeaders, ...headers });
    res.end(body);
  }
  const sendJson = (res, status, obj) => send(res, status, JSON.stringify(obj), { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  const empty = (res, status, headers = {}) => send(res, status, '', { 'Cache-Control': 'no-store', ...headers });

  function readJson(req) {
    return new Promise((resolve) => {
      const len = parseInt(req.headers['content-length'] || '0', 10);
      if (len > MAX_BODY) { resolve({ tooLarge: true }); req.resume(); return; }
      const chunks = []; let size = 0;
      req.on('data', (c) => {
        size += c.length;
        if (size > MAX_BODY) { resolve({ tooLarge: true }); req.destroy(); return; }
        chunks.push(c);
      });
      req.on('end', () => { try { resolve({ json: JSON.parse(Buffer.concat(chunks).toString('utf8')) }); } catch { resolve({ json: null }); } });
      req.on('error', () => resolve({ json: null }));
    });
  }
  const parseCookies = (h) => Object.fromEntries((h || '').split(';').map((s) => s.trim().split(/=(.*)/s)).filter((a) => a[0]).map((a) => [a[0], a[1] || '']));

  function isAdmin(req) {
    if (!cfg.adminToken) return false;
    const auth = req.headers.authorization || '';
    if (auth.startsWith('Bearer ') && safeEq(auth.slice(7), cfg.adminToken)) return true;
    const sid = parseCookies(req.headers.cookie).sid || '';
    const [exp, sig] = sid.split('.');
    if (!/^\d{10,16}$/.test(exp || '') || !sig || Date.now() > Number(exp)) return false;
    return safeEq(sig, signSession(exp));
  }
  const cookie = (req, value, maxAge) => {
    const secure = cfg.secureCookie === 'auto'
      ? peerTrusted(req) && String(req.headers['x-forwarded-proto'] || '').split(',')[0].trim() === 'https'
      : cfg.secureCookie;
    return `sid=${value}; Path=/; HttpOnly; SameSite=Strict; Max-Age=${maxAge}${secure ? '; Secure' : ''}`;
  };

  function clientBody(b) {
    if (!b || b.v !== 1) return null;
    if (typeof b.uid !== 'string' || !HEX64.test(b.uid) || typeof b.did !== 'string' || !HEX64.test(b.did)) return null;
    const ver = b.app === undefined ? '' : b.app;
    if (typeof ver !== 'string' || !VER.test(ver)) return null;
    return { uid: b.uid, did: b.did, ver };
  }
  const clientKeyOk = (req) => !cfg.clientKey || safeEq(req.headers['x-client-key'] || '', cfg.clientKey);

  // ---------------- 静的ファイル (ダッシュボード) ----------------
  const files = {
    '/dashboard': ['index.html', 'text/html; charset=utf-8'],
    '/app.js': ['app.js', 'text/javascript; charset=utf-8'],
    '/style.css': ['style.css', 'text/css; charset=utf-8'],
  };
  const staticCache = {};
  for (const [route, [name]] of Object.entries(files)) staticCache[route] = fs.readFileSync(path.join(HERE, 'public', name));

  async function handle(req, res) {
    const url = new URL(req.url, 'http://x');
    const p = url.pathname, m = req.method;

    if (m === 'GET' && files[p]) {
      return send(res, 200, staticCache[p], { 'Content-Type': files[p][1], 'Content-Security-Policy': csp, 'Cache-Control': 'no-cache' });
    }
    if (m === 'GET' && p === '/') return empty(res, 302, { Location: '/dashboard' });

    // ---- クライアント用 ----
    if (m === 'POST' && (p === '/v1/ping' || p === '/v1/forget')) {
      if (parseInt(req.headers['content-length'] || '0', 10) > MAX_BODY) { req.resume(); return empty(res, 413); }
      if (!pingLimiter.hit(clientIp(req))) { req.resume(); return empty(res, 429); }
      if (!clientKeyOk(req)) { req.resume(); return empty(res, 401); }
      const { json, tooLarge } = await readJson(req);
      if (tooLarge) return empty(res, 413);
      const b = clientBody(json);
      if (!b) return empty(res, 400);
      const uh = mac('uid', b.uid), dh = mac('did', b.did);
      if (p === '/v1/ping') {
        const d = today();
        db.exec('BEGIN');
        try { q.upInstall.run(uh, dh, b.ver, d, d); q.upActivity.run(d, uh); db.exec('COMMIT'); } catch (e) { db.exec('ROLLBACK'); throw e; }
      } else {                                     // オプトアウト: この ユーザー×端末 を削除
        db.exec('BEGIN');
        try {
          q.delInstall.run(uh, dh);
          if (q.userLeft.get(uh).c === 0) q.delActivity.run(uh);
          db.exec('COMMIT');
        } catch (e) { db.exec('ROLLBACK'); throw e; }
      }
      return empty(res, 204);
    }

    // ---- ダッシュボード認証 ----
    if (m === 'POST' && p === '/login') {
      if (!loginLimiter.hit(clientIp(req))) { req.resume(); return empty(res, 429); }
      const { json } = await readJson(req);
      await new Promise((r) => setTimeout(r, 250));                 // 総当たりを遅くする
      if (cfg.adminToken && json && typeof json.token === 'string' && safeEq(json.token, cfg.adminToken)) {
        const exp = Date.now() + cfg.sessionHours * 3600000;
        return empty(res, 204, { 'Set-Cookie': cookie(req, `${exp}.${signSession(exp)}`, cfg.sessionHours * 3600) });
      }
      return empty(res, 401);
    }
    if (m === 'POST' && p === '/logout') return empty(res, 204, { 'Set-Cookie': cookie(req, '', 0) });

    // ---- 管理用 API ----
    if (m === 'GET' && (p === '/v1/stats' || p === '/api/overview')) {
      if (!cfg.adminToken) return empty(res, 404);
      if (!isAdmin(req)) return empty(res, 401);
      return sendJson(res, 200, overview());
    }
    if (m === 'GET' && p === '/api/series') {
      if (!cfg.adminToken) return empty(res, 404);
      if (!isAdmin(req)) return empty(res, 401);
      const days = Math.min(730, Math.max(1, parseInt(url.searchParams.get('days') || '30', 10) || 30));
      return sendJson(res, 200, series(days));
    }
    return empty(res, 404);
  }

  const server = http.createServer((req, res) => {
    handle(req, res).catch(() => { if (!res.headersSent) empty(res, 500); else res.end(); });    // エラー内容は返さない・記録しない
  });
  server.requestTimeout = 15000;
  server.headersTimeout = 10000;
  return { server, db, close: () => new Promise((r) => { clearInterval(purgeTimer); server.close(() => { db.close(); r(); }); }) };
}

// 直接実行されたときだけ起動
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  // 設定の優先順位: 実際の環境変数 > .env > secrets.json(自動生成) 。Pelican などパネル運用では .env をファイルマネージャーで編集する
  const env = { ...loadDotEnv(process.env.STATS_ENV_FILE || path.join(HERE, '.env')), ...process.env };
  try {
    const cfg = resolveSecrets(configFromEnv(env), env);
    if (env.SERVER_PORT) {
      if (env.PORT && env.PORT !== env.SERVER_PORT) console.warn(`警告: PORT=${env.PORT} は無視し、割り当てポート SERVER_PORT=${env.SERVER_PORT} を使います (.env の PORT は不要です)`);
      if (['127.0.0.1', 'localhost', '::1'].includes(cfg.bind) && env.STATS_BIND_FORCE !== '1') {
        console.warn(`警告: STATS_BIND=${cfg.bind} だとコンテナの外から届かないため 0.0.0.0 で待ち受けます (.env の STATS_BIND は不要です。そのままにしたい場合は STATS_BIND_FORCE=1)`);
        cfg.bind = '0.0.0.0';
      }
    }
    console.log(`設定: DB=${cfg.dbPath} / 待ち受け=${cfg.bind}:${cfg.port} / プロキシ信用=${cfg.trustProxy || 'なし'} / 管理トークン=${cfg.adminToken ? '設定あり' : 'なし(ダッシュボード無効)'} / アプリ用キー=${cfg.clientKey ? '設定あり' : 'なし(検証しない)'}`);
    const app = createApp(cfg);
    app.server.listen(cfg.port, cfg.bind, () => console.log(`north-stats listening on http://${cfg.bind}:${cfg.port}  (dashboard: /dashboard)`));
  } catch (e) { console.error(e.message); process.exit(1); }
}
