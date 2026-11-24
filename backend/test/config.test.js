import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createApp, configFromEnv, loadDotEnv, resolveSecrets, isPrivateAddr } from '../server.js';

const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), 'north-cfg-'));
const quiet = () => {};

test('.env の読み込み (コメント・引用符・export)', () => {
  const d = tmp(), f = path.join(d, '.env');
  fs.writeFileSync(f, '# comment\nSTATS_PEPPER="abc def"\nexport PORT=9000 # inline\nSTATS_CLIENT_KEY=\n\n  BAD LINE\nQ=\'x y\'\n');
  assert.deepEqual(loadDotEnv(f), { STATS_PEPPER: 'abc def', PORT: '9000', STATS_CLIENT_KEY: '', Q: 'x y' });
  assert.deepEqual(loadDotEnv(path.join(d, 'none')), {});
});

test('コンテナ環境(SERVER_PORT)では 0.0.0.0 とそのポートで待ち受ける', () => {
  const c = configFromEnv({ SERVER_PORT: '25565' });
  assert.equal(c.bind, '0.0.0.0'); assert.equal(c.port, 25565);
  const l = configFromEnv({});
  assert.equal(l.bind, '127.0.0.1'); assert.equal(l.port, 8787);
  assert.equal(configFromEnv({ SERVER_PORT: '1', PORT: '2', STATS_BIND: '10.0.0.5' }).port, 1);   // 割り当てポートが優先
});

test('秘密情報: 初回生成(0600)・再利用・環境変数優先・明示的な空は無効', () => {
  const d = tmp();
  const env = { STATS_DB: path.join(d, 'stats.db') };
  const lines = [];
  const c1 = resolveSecrets(configFromEnv(env), env, (l) => lines.push(l));
  assert.ok(c1.pepper.length >= 32 && c1.adminToken && c1.clientKey);
  const f = path.join(d, 'secrets.json');
  assert.equal(fs.statSync(f).mode & 0o777, 0o600);
  assert.ok(lines.join('\n').includes(c1.adminToken) && lines.join('\n').includes(c1.clientKey));
  assert.ok(!lines.join('\n').includes(c1.pepper), 'pepper をログに出してはいけない');
  const lines2 = [];
  const c2 = resolveSecrets(configFromEnv(env), env, (l) => lines2.push(l));
  assert.deepEqual([c2.pepper, c2.adminToken, c2.clientKey], [c1.pepper, c1.adminToken, c1.clientKey]);
  assert.equal(lines2.length, 0, '2回目は表示しない');
  const env3 = { ...env, STATS_PEPPER: 'e'.repeat(40), STATS_ADMIN_TOKEN: 'mine' };
  const c3 = resolveSecrets(configFromEnv(env3), env3, quiet);
  assert.equal(c3.pepper, 'e'.repeat(40)); assert.equal(c3.adminToken, 'mine');
  const env4 = { ...env, STATS_ADMIN_TOKEN: '', STATS_CLIENT_KEY: '' };
  const c4 = resolveSecrets(configFromEnv(env4), env4, quiet);
  assert.equal(c4.adminToken, ''); assert.equal(c4.clientKey, '');
});

test('既存の DB があるのに pepper が無ければ起動を止める', async () => {
  const d = tmp();
  const env = { STATS_DB: path.join(d, 'stats.db') };
  const c = resolveSecrets(configFromEnv(env), env, quiet);
  const app = createApp(c);
  await app.close();
  fs.unlinkSync(path.join(d, 'secrets.json'));
  assert.throws(() => resolveSecrets(configFromEnv(env), env, quiet), /STATS_PEPPER/);
  const env2 = { ...env, STATS_ALLOW_NEW_PEPPER: '1' };
  assert.ok(resolveSecrets(configFromEnv(env2), env2, quiet).pepper);
});

test('isPrivateAddr', () => {
  for (const a of ['127.0.0.1', '10.1.2.3', '172.18.0.1', '192.168.0.9', '::1', '::ffff:172.17.0.1', 'fd12::1']) assert.ok(isPrivateAddr(a), a);
  for (const a of ['8.8.8.8', '172.32.0.1', '203.0.113.5', '2001:db8::1', '', undefined]) assert.ok(!isPrivateAddr(a), String(a));
});

async function start(over) {
  const d = tmp();
  const cfg = { ...configFromEnv({}), pepper: 'p'.repeat(48), clientKey: 'k', adminToken: 'adm', dbPath: path.join(d, 't.db'), ...over };
  const app = createApp(cfg);
  await new Promise((r) => app.server.listen(0, '127.0.0.1', r));
  return { base: `http://127.0.0.1:${app.server.address().port}`, close: () => app.close() };
}
const hex = (c) => c.repeat(64);
const ping = (base, headers = {}) => fetch(base + '/v1/ping', { method: 'POST', headers: { 'content-type': 'application/json', 'x-client-key': 'k', ...headers }, body: JSON.stringify({ v: 1, app: '0.6', uid: hex('a'), did: hex('b') }) });

test('Cookie の Secure: auto はプロキシが https を伝えたときだけ', async (t) => {
  const s = await start({ trustProxy: 'private', secureCookie: 'auto' });
  t.after(s.close);
  const login = (h) => fetch(s.base + '/login', { method: 'POST', headers: { 'content-type': 'application/json', ...h }, body: JSON.stringify({ token: 'adm' }) });
  assert.doesNotMatch((await login({})).headers.get('set-cookie'), /Secure/);
  assert.match((await login({ 'x-forwarded-proto': 'https' })).headers.get('set-cookie'), /Secure/);
  const s2 = await start({ trustProxy: false, secureCookie: 'auto' });
  t.after(s2.close);
  const r = await fetch(s2.base + '/login', { method: 'POST', headers: { 'content-type': 'application/json', 'x-forwarded-proto': 'https' }, body: JSON.stringify({ token: 'adm' }) });
  assert.doesNotMatch(r.headers.get('set-cookie'), /Secure/, '信用しないプロキシの X-Forwarded-Proto は無視');
});

test('レート制限: 信用するプロキシ経由なら X-Real-IP ごと、信用しなければ偽装できない', async (t) => {
  const s = await start({ trustProxy: 'private' });
  t.after(s.close);
  const A = { 'x-real-ip': '203.0.113.1' }, B = { 'x-real-ip': '203.0.113.2' };
  const codes = [];
  for (let i = 0; i < 32; i++) codes.push((await ping(s.base, A)).status);
  assert.ok(codes.includes(429));
  assert.equal((await ping(s.base, B)).status, 204);                     // 別の利用者は影響を受けない
  const s2 = await start({ trustProxy: false });
  t.after(s2.close);
  const c2 = [];
  for (let i = 0; i < 32; i++) c2.push((await ping(s2.base, { 'x-real-ip': `203.0.113.${i}` })).status);
  assert.ok(c2.includes(429), 'X-Real-IP を変えてもすり抜けられてはいけない');
});

test('SERVER_PORT は PORT より優先 (コンテナの割り当てポートだけが外に届く)', () => {
  assert.equal(configFromEnv({ SERVER_PORT: '25565', PORT: '8787' }).port, 25565);
  assert.equal(configFromEnv({ PORT: '9000' }).port, 9000);
});

test('DB の保存先ディレクトリは自動で作る / 作れないときは原因の分かるエラー', async () => {
  const d = tmp();
  const deep = path.join(d, 'a', 'b', 'stats.db');
  const app = createApp({ ...configFromEnv({}), pepper: 'p'.repeat(48), dbPath: deep });
  assert.ok(fs.existsSync(deep));
  await app.close();
  const f = path.join(d, 'afile');
  fs.writeFileSync(f, 'x');
  assert.throws(() => createApp({ ...configFromEnv({}), pepper: 'p'.repeat(48), dbPath: path.join(f, 'sub', 'stats.db') }), /保存先.*作成\/書き込みできません/);
});
