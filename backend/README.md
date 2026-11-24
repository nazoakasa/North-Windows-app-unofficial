# north-stats — 利用者数の集計サーバー + Web ダッシュボード

north-client の「利用者数の集計」(任意・同意制)を受け取って保管し、ダッシュボードで見られるようにするサーバー。
Node.js 22.5 以上 / 依存パッケージなし (標準の `node:sqlite` を使用)。

## 何を保存するか
| 保存する | 保存しない |
|---|---|
| ユーザーIDと端末IDの **HMAC-SHA256** (※下記) | 生のユーザーID・端末ID |
| アプリのバージョン、初回/最終アクセスの**日付**(日単位)、回数 | IPアドレス、User-Agent、Cookie、投稿・DMの内容 |

- アプリは `SHA-256(固定ラベル + ID)` だけを送る (元のIDは送らない)。
- サーバーはそれにさらに **秘密鍵 `STATS_PEPPER` で HMAC** をかけて保存する。DB ファイルだけが漏れても、pepper が無ければ元のIDを逆引き・照合できない。
- IP はレート制限のためメモリ上だけで数え、DB にもログにも書かない (nginx 側も `access_log off` にする)。
- DB は `0600`。`STATS_RETENTION_DAYS` (既定400日) を過ぎた行は自動で削除。
- アプリでオプトアウトすると `/v1/forget` が呼ばれ、そのユーザー×端末の行を削除する。

## セットアップ (Ubuntu 24.04 の例)
```bash
# 1) Node.js 22 (Ubuntu 標準の nodejs は古いので NodeSource などを使う)
curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -
sudo apt install -y nodejs

# 2) 配置
sudo mkdir -p /opt/north-stats && sudo cp -r server.js package.json public /opt/north-stats/

# 3) 環境変数 (.env.example を参考に値を埋める。空のままでも初回起動で自動生成される)
sudo cp .env.example /etc/north-stats.env && sudo chmod 600 /etc/north-stats.env
sudo nano /etc/north-stats.env

# 4) 起動
sudo cp north-stats.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now north-stats
curl -s http://127.0.0.1:8787/v1/stats -H "Authorization: Bearer <STATS_ADMIN_TOKEN>"

# 5) nginx + TLS (nginx.conf.example を参考に。ドメインを自分のものに)
```
ダッシュボードは `https://<ドメイン>/dashboard`。管理用トークン(`STATS_ADMIN_TOKEN`)でログインする。

## Pelican Panel / Pterodactyl でホストする場合
汎用の Node.js egg (Node 22.5 以上。例: `nodejs_25`) でそのまま動く。環境変数を足せなくても起動できるようにしてある。

1. サーバーの `/home/container` に `server.js` `package.json` `public/` をアップロードする。Startup の `MAIN_FILE` は `server.js`。
2. 起動する。**初回だけ、コンソールに管理用トークンとアプリ用キーが表示される** (`secrets.json` に 0600 で保存される。あとから見るときはファイルマネージャーで開く)。
   - 管理用トークン → ダッシュボード(`/dashboard`)のログイン
   - アプリ用キー → アプリの `config.h` の `STATS_KEY`
3. 待ち受けは自動で `0.0.0.0:<割り当てポート>` (`SERVER_PORT`)。ポートを直接インターネットに公開せず、**TLS を終端するリバースプロキシ(nginx / Cloudflare Tunnel など)から割り当てポートへ転送**する。
   ポートはファイアウォール等でプロキシからだけ届くようにしておく。
4. リバースプロキシ配下では、`.env` (ファイルマネージャーで `/home/container/.env` を作る) に次を書く。書かないと全利用者が同じIP扱いになり、レート制限を共有してしまう。
   ```
   STATS_TRUST_PROXY=1
   ```
   `1` はプライベートアドレス(Docker のゲートウェイ等)から来た接続の `X-Real-IP` だけを信用する。プロキシは `X-Real-IP` と `X-Forwarded-Proto` を付けること(`nginx.conf.example` 参照)。
5. **`.env.example` をそのままコピーしない**。`STATS_DB` `PORT` `STATS_BIND` は Pelican では設定しない (雛形では無効になっている)。
   設定するのは `STATS_TRUST_PROXY=1` くらいで、他は空でよい。
6. 設定の優先順位は **実際の環境変数 > `.env` > `secrets.json`(自動生成)**。egg の変数を使える場合は `.env.example` の名前でそのまま設定してもよい。

保存場所とバックアップ:
- `stats.db` (集計データ) と `secrets.json` (pepper など) は `/home/container` 直下。Pelican のバックアップ機能で両方残す。
- 起動ログの `設定:` の行に、実際に使われる DB のパス・待ち受け・トークンの有無が出る。うまく動かないときはまずここを見る。
- 自動生成した pepper は DB と同じディレクトリにある。**DB ファイルだけ**が漏れても守られるが、サーバーのディレクトリごと漏れると両方漏れる。
  分けたい場合は `STATS_PEPPER` を環境変数(別の場所)で渡し、`secrets.json` から `pepper` を消す。
- DB があるのに pepper が見つからないときは、データの突き合わせが切れるのを防ぐため起動を中止する (`STATS_ALLOW_NEW_PEPPER=1` で作り直し可)。

## アプリ側の設定
`north-c/src/config.h`
```c
#define STATS_URL "https://stats.example.com"   // 末尾の / なし。空ならアプリは集計を一切行わない
#define STATS_KEY "(STATS_CLIENT_KEY と同じ文字列)"
```
ビルド後に変えたい場合は、利用者側の `%LOCALAPPDATA%\NorthC\settings.ini` の `[telemetry]` に `url=` / `key=` を書けば上書きできる。
URL は `https://` のみ有効 (検証用に `http://localhost` / `127.0.0.1` だけ可)。

## API
| メソッド/パス | 認証 | 内容 |
|---|---|---|
| `POST /v1/ping` | `X-Client-Key` | `{"v":1,"app":"0.6","uid":"<hex64>","did":"<hex64>"}` → 204 |
| `POST /v1/forget` | `X-Client-Key` | 同じ形式。その ユーザー×端末 を削除 → 204 |
| `POST /login` / `POST /logout` | 管理トークン | ダッシュボードのセッション (HttpOnly / SameSite=Strict の Cookie) |
| `GET /api/overview` (`/v1/stats`) | Bearer か Cookie | 総ユーザー数・端末数・DAU/WAU/MAU・新規・バージョン別 |
| `GET /api/series?days=N` | Bearer か Cookie | 日次の DAU / 新規 / 累計 (N は 1〜730) |
| `GET /dashboard` | - | ダッシュボード(画面)。データは上の API を認証して取得 |

## 安全面の工夫と限界
- 入力は厳密に検証 (hex64 / バージョン文字列 / 2KB 上限)。管理トークンの比較は定数時間。ログインは 1IP あたり 5回/分 + 遅延。
- ダッシュボードは CSP (`script-src 'self'`、インラインスクリプトなし)・`X-Frame-Options: DENY` などのヘッダーつき。
- `STATS_PEPPER` は**バックアップを別の場所に**。失うと同じ人が「新規」として数え直される (データが壊れるわけではない)。
- `STATS_CLIENT_KEY` はアプリに埋め込まれるため、取り出されれば偽の ping を送れる。完全な不正防止ではなく、人数は「おおよそ」として扱う。
- 日付は UTC。DAU/WAU/MAU はユーザー単位 (同じユーザーが複数端末でも1人)。

## テスト
```bash
npm test      # 15 件: 検証 / HMAC保存 / 集計 / forget / 認証 / レート制限 / ヘッダー / .env / 秘密情報の自動生成 / プロキシ信用
```
