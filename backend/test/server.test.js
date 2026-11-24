import test from 'node:test';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { createApp, configFromEnv } from '../server.js';

const H = (s) => crypto.createHash('sha256').update(s).digest('hex');
const uid1 = H('north-client/uid/v1:12345'), did1 = H('north-client/did/v1:guid-A'), did2 = H('north-client/did/v1:guid-B');
const uid2 = H('north-client/uid/v1:999');

async function start(over = {}) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'north-stats-'));
  const cfg = { ...configFromEnv({}), pepper: 'p'.repeat(48), clientKey: 'k-test', adminToken: 'adm-token', dbPath: path.join(dir, 't.db'), secureCookie: false, ...over };
  const app = createApp(cfg);
  await new Promise((r) => app.server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${app.server.address().port}`;
  return { app, cfg, base, close: () => app.close() };
}
const post = (base, p, body, headers = {}) =>
  fetch(base + p, { method: 'POST', headers: { 'content-type': 'application/json', ...headers }, body: typeof body === 'string' ? body : JSON.stringify(body) });
const ok = { 'X-Client-Key': 'k-test' };
const body = { v: 1, app: '0.6', uid: uid1, did: did1 };

test('ping: 検証・重複・サイズ上限', async (t) => {
  const s = await start();
  t.after(s.close);
  assert.equal((await post(s.base, '/v1/ping', body, ok)).status, 204);
  assert.equal((await post(s.base, '/v1/ping', body, ok)).status, 204);               // 同じ組は1行
  assert.equal((await post(s.base, '/v1/ping', body)).status, 401);                    // キー無し
  assert.equal((await post(s.base, '/v1/ping', { ...body, uid: 'zz' }, ok)).status, 400);
  assert.equal((await post(s.base, '/v1/ping', { ...body, app: '<script>' }, ok)).status, 400);
  assert.equal((await post(s.base, '/v1/ping', { ...body, v: 2 }, ok)).status, 400);
  assert.equal((await post(s.base, '/v1/ping', 'not json', ok)).status, 400);
  assert.equal((await post(s.base, '/v1/ping', 'x'.repeat(5000), ok)).status, 413);
  const db = new DatabaseSync(s.cfg.dbPath);
  assert.equal(db.prepare('SELECT COUNT(*) c FROM installs').get().c, 1);
  assert.equal(db.prepare('SELECT pings FROM installs').get().pings, 2);
  db.close();
});

test('保存値は HMAC 済みで、クライアントのハッシュは残らない / DB 権限 0600', async (t) => {
  const s = await start();
  t.after(s.close);
  await post(s.base, '/v1/ping', body, ok);
  const db = new DatabaseSync(s.cfg.dbPath);
  const dump = JSON.stringify([...db.prepare('SELECT * FROM installs').all(), ...db.prepare('SELECT * FROM activity').all()]);
  db.close();
  assert.ok(!dump.includes(uid1) && !dump.includes(did1), 'クライアントのハッシュがそのまま保存されている');
  const expect = crypto.createHmac('sha256', s.cfg.pepper).update(`uid:${uid1}`).digest('hex');
  assert.ok(dump.includes(expect));
  assert.equal(fs.statSync(s.cfg.dbPath).mode & 0o777, 0o600);
});

test('集計: overview / series / forget', async (t) => {
  const s = await start();
  t.after(s.close);
  await post(s.base, '/v1/ping', body, ok);
  await post(s.base, '/v1/ping', { ...body, did: did2 }, ok);                          // 同一ユーザー・別端末
  await post(s.base, '/v1/ping', { ...body, uid: uid2, app: '0.5' }, ok);
  const A = { Authorization: 'Bearer adm-token' };
  const o = await (await fetch(s.base + '/api/overview', { headers: A })).json();
  assert.equal(o.users, 2); assert.equal(o.devices, 2); assert.equal(o.users_multi_device, 1);
  assert.equal(o.dau, 2); assert.equal(o.wau, 2); assert.equal(o.mau, 2);
  assert.equal(o.new_users_1d, 2); assert.deepEqual(o.versions, { '0.6': 1, '0.5': 1 });
  const sr = await (await fetch(s.base + '/api/series?days=7', { headers: A })).json();
  assert.equal(sr.dates.length, 7);
  assert.equal(sr.dau.at(-1), 2); assert.equal(sr.new_users.at(-1), 2); assert.equal(sr.total_users.at(-1), 2);
  assert.equal(sr.total_users[0], 0);
  const sr2 = await (await fetch(s.base + '/api/series?days=99999', { headers: A })).json();
  assert.equal(sr2.dates.length, 730);                                                 // 上限

  assert.equal((await post(s.base, '/v1/forget', { ...body, uid: uid2 }, ok)).status, 204);   // uid2 は端末が1台なので全消去
  const o2 = await (await fetch(s.base + '/api/overview', { headers: A })).json();
  assert.equal(o2.users, 1); assert.equal(o2.dau, 1);
  assert.equal((await post(s.base, '/v1/forget', body, ok)).status, 204);              // uid1 は did2 が残る
  const o3 = await (await fetch(s.base + '/api/overview', { headers: A })).json();
  assert.equal(o3.users, 1); assert.equal(o3.devices, 1); assert.equal(o3.dau, 1);
});

test('認証: Bearer / Cookie セッション / 失敗', async (t) => {
  const s = await start();
  t.after(s.close);
  assert.equal((await fetch(s.base + '/api/overview')).status, 401);
  assert.equal((await fetch(s.base + '/v1/stats')).status, 401);
  assert.equal((await fetch(s.base + '/api/overview', { headers: { Authorization: 'Bearer wrong' } })).status, 401);
  assert.equal((await fetch(s.base + '/v1/stats', { headers: { Authorization: 'Bearer adm-token' } })).status, 200);
  assert.equal((await post(s.base, '/login', { token: 'wrong' })).status, 401);
  const r = await post(s.base, '/login', { token: 'adm-token' });
  assert.equal(r.status, 204);
  const set = r.headers.get('set-cookie');
  assert.match(set, /HttpOnly/); assert.match(set, /SameSite=Strict/);
  const sid = set.split(';')[0];
  assert.equal((await fetch(s.base + '/api/overview', { headers: { Cookie: sid } })).status, 200);
  const [exp, sig] = sid.slice(4).split('.');
  assert.equal((await fetch(s.base + '/api/overview', { headers: { Cookie: `sid=${exp}.${'0'.repeat(sig.length)}` } })).status, 401);   // 改ざん
  assert.equal((await fetch(s.base + '/api/overview', { headers: { Cookie: `sid=${Date.now() - 1000}.${sig}` } })).status, 401);          // 期限/署名不一致
});

test('ログインのレート制限 (5回/分)', async (t) => {
  const s = await start();
  t.after(s.close);
  const codes = [];
  for (let i = 0; i < 7; i++) codes.push((await post(s.base, '/login', { token: 'wrong' })).status);
  assert.deepEqual(codes.slice(0, 5), [401, 401, 401, 401, 401]);
  assert.equal(codes[5], 429);
  assert.equal((await post(s.base, '/login', { token: 'adm-token' })).status, 429);    // 正しくても制限中は拒否
});

test('ping のレート制限・管理トークン未設定なら管理APIは404', async (t) => {
  const s = await start({ adminToken: '' });
  t.after(s.close);
  const codes = [];
  for (let i = 0; i < 35; i++) codes.push((await post(s.base, '/v1/ping', body, ok)).status);
  assert.ok(codes.includes(429));
  assert.equal((await fetch(s.base + '/api/overview')).status, 404);
  assert.equal((await post(s.base, '/login', { token: '' })).status, 401);
});

test('静的ファイルとセキュリティヘッダー', async (t) => {
  const s = await start();
  t.after(s.close);
  const r = await fetch(s.base + '/dashboard');
  assert.equal(r.status, 200);
  assert.match(r.headers.get('content-security-policy'), /script-src 'self'/);
  assert.match(r.headers.get('content-security-policy'), /frame-ancestors 'none'/);
  assert.equal(r.headers.get('x-content-type-options'), 'nosniff');
  const html = await r.text();
  assert.ok(!/<script(?![^>]*\bsrc=)[^>]*>/.test(html), 'インラインスクリプトがある');
  assert.match((await fetch(s.base + '/app.js')).headers.get('content-type'), /javascript/);
  assert.equal((await fetch(s.base + '/', { redirect: 'manual' })).status, 302);
  assert.equal((await fetch(s.base + '/../server.js')).status, 404);
  assert.equal((await fetch(s.base + '/stats.db')).status, 404);
});

test('PEPPER が短いと起動しない', () => {
  assert.throws(() => createApp({ ...configFromEnv({}), pepper: 'short', dbPath: path.join(os.tmpdir(), 'x.db') }), /STATS_PEPPER/);
});
