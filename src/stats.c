/* stats.c -- 利用者数の集計 (任意・同意制)
 *
 * 送るもの : ユーザーIDと端末ID(MachineGuid)の SHA-256 ハッシュ、アプリのバージョンだけ。1日1回。
 * 送らないもの: パスワード / Cookie / トークン / 投稿やDMの内容 / 生のID。
 * サーバー側では受け取ったハッシュにさらに秘密鍵(pepper)で HMAC をかけて保存する (backend/ 参照)。
 * 同意しない・URL未設定の場合は何も送らない。 */
#include "app.h"
#include <wincrypt.h>
#include "config.h"

#ifndef RRF_SUBKEY_WOW64_64KEY
#define RRF_SUBKEY_WOW64_64KEY 0x00010000
#endif

static char g_user_id[80];
static char g_uid_h[65], g_did_h[65];

static void ini_path(wchar_t *out, size_t n)
{
    wchar_t d[MAX_PATH];
    data_dir(d, MAX_PATH);
    _snwprintf(out, n, L"%ls\\settings.ini", d);
    out[n - 1] = 0;
}
static void sha256_hex(const char *s, char out[65])
{
    HCRYPTPROV p = 0;
    HCRYPTHASH h = 0;
    BYTE d[32];
    DWORD dl = sizeof d;
    out[0] = 0;
    if (!CryptAcquireContextW(&p, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return;
    if (CryptCreateHash(p, CALG_SHA_256, 0, 0, &h)) {
        if (CryptHashData(h, (const BYTE *)s, (DWORD)strlen(s), 0) && CryptGetHashParam(h, HP_HASHVAL, d, &dl, 0))
            for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", d[i]);
        CryptDestroyHash(h);
    }
    CryptReleaseContext(p, 0);
}

/* Windows の固有ID。取れなければ初回に作った乱数を settings.ini に保存して代用 */
static void device_id(char *out, size_t n)
{
    wchar_t w[128];
    DWORD sz = sizeof w;
    out[0] = 0;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW64_64KEY, NULL, w, &sz) == ERROR_SUCCESS && w[0]) {
        char *u = wu8(w);
        strncpy(out, u, n - 1); out[n - 1] = 0;
        free(u);
        return;
    }
    wchar_t p[MAX_PATH], v[80];
    ini_path(p, MAX_PATH);
    GetPrivateProfileStringW(L"telemetry", L"fallback_id", L"", v, 80, p);
    if (!v[0]) {
        HCRYPTPROV pr = 0;
        BYTE r[16];
        if (CryptAcquireContextW(&pr, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) { CryptGenRandom(pr, 16, r); CryptReleaseContext(pr, 0); }
        else memset(r, 0x5a, sizeof r);
        for (int i = 0; i < 16; i++) swprintf(v + i * 2, 3, L"%02x", r[i]);
        WritePrivateProfileStringW(L"telemetry", L"fallback_id", v, p);
    }
    char *u = wu8(v);
    strncpy(out, u, n - 1); out[n - 1] = 0;
    free(u);
}
void stats_hash_user(const char *user_id, char out[65])
{
    char b[200];
    _snprintf(b, sizeof b, "north-client/uid/v1:%s", user_id);
    b[sizeof b - 1] = 0;
    sha256_hex(b, out);
}
void stats_hash_device(char out[65])
{
    char g[160], b[260];
    device_id(g, sizeof g);
    _snprintf(b, sizeof b, "north-client/did/v1:%s", g);
    b[sizeof b - 1] = 0;
    sha256_hex(b, out);
}

/* ---- 設定 (URL は https のみ。検証用に localhost だけ http を許可) ---- */
static void cfg(char *url, size_t un, char *key, size_t kn)
{
    wchar_t p[MAX_PATH], w[300];
    strncpy(url, STATS_URL, un - 1); url[un - 1] = 0;
    strncpy(key, STATS_KEY, kn - 1); key[kn - 1] = 0;
    ini_path(p, MAX_PATH);
    GetPrivateProfileStringW(L"telemetry", L"url", L"", w, 300, p);
    if (w[0]) { char *u = wu8(w); strncpy(url, u, un - 1); url[un - 1] = 0; free(u); }
    GetPrivateProfileStringW(L"telemetry", L"key", L"", w, 300, p);
    if (w[0]) { char *u = wu8(w); strncpy(key, u, kn - 1); key[kn - 1] = 0; free(u); }
    size_t l = strlen(url);
    while (l && url[l - 1] == '/') url[--l] = 0;
}
static int url_ok(const char *u)
{
    if (!strncmp(u, "https://", 8) && u[8]) return 1;
    return !strncmp(u, "http://localhost", 16) || !strncmp(u, "http://127.0.0.1", 16);
}
int stats_configured(void)
{
    char u[300], k[200];
    cfg(u, sizeof u, k, sizeof k);
    return url_ok(u);
}
int stats_consent(void)
{
    wchar_t p[MAX_PATH];
    ini_path(p, MAX_PATH);
    return (int)GetPrivateProfileIntW(L"telemetry", L"consent", (UINT)-1, p);
}
static void set_ini(const wchar_t *key, const wchar_t *val)
{
    wchar_t p[MAX_PATH];
    ini_path(p, MAX_PATH);
    WritePrivateProfileStringW(L"telemetry", key, val, p);
}
static void today(wchar_t *out)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    swprintf(out, 16, L"%04d%02d%02d", t.wYear, t.wMonth, t.wDay);
}

/* op: 1=ping 2=forget */
static void send_op(const char *path, int op)
{
    char url[300], key[200], full[400], hdr[260] = "";
    cfg(url, sizeof url, key, sizeof key);
    if (!url_ok(url) || !g_uid_h[0] || !g_did_h[0]) return;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "v", 1);
    cJSON_AddStringToObject(o, "app", APP_VERSION);
    cJSON_AddStringToObject(o, "uid", g_uid_h);
    cJSON_AddStringToObject(o, "did", g_did_h);
    char *js = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    _snprintf(full, sizeof full, "%s%s", url, path);
    full[sizeof full - 1] = 0;
    if (key[0]) _snprintf(hdr, sizeof hdr, "X-Client-Key: %s\r\n", key);
    net_api_hdr(T_STATS, "POST", full, js, NULL, op, key[0] ? hdr : NULL);
    cJSON_free(js);
}

void stats_on_login(const char *user_id)
{
    if (g_is_demo || !user_id || !user_id[0] || !stats_configured()) return;
    strncpy(g_user_id, user_id, sizeof g_user_id - 1);
    stats_hash_user(user_id, g_uid_h);
    stats_hash_device(g_did_h);
    int c = stats_consent();
    if (c < 0) {
        int r = MessageBoxW(g_main,
            L"利用者数の集計にご協力いただけませんか？\n\n"
            L"【送る情報】\n"
            L"・ユーザーIDと端末IDを一方向ハッシュ化した値(元のIDには戻せません)\n"
            L"・アプリのバージョン\n\n"
            L"【送らない情報】\n"
            L"パスワード、Cookie、投稿やDMの内容、IPアドレスの保存はしません。\n\n"
            L"1日1回、起動時に送信します。左下のアカウントメニュー(スマホ幅では左上のメニュー)からいつでもオフにでき、"
            L"オフにすると保存済みのデータの削除を依頼します。\n\n協力しますか？",
            L"利用者数の集計について", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        c = r == IDYES;
        set_ini(L"consent", c ? L"1" : L"0");
    }
    if (c != 1) return;
    wchar_t td[16], last[16], p[MAX_PATH];
    today(td);
    ini_path(p, MAX_PATH);
    GetPrivateProfileStringW(L"telemetry", L"last_ping", L"", last, 16, p);
    if (!wcscmp(td, last)) return;
    send_op("/v1/ping", 1);
}

void stats_on_result(int status, int arg)
{
    if (arg == 1 && status >= 200 && status < 300) {
        wchar_t td[16];
        today(td);
        set_ini(L"last_ping", td);
    }
}

void stats_toggle(void)
{
    if (!stats_configured()) return;
    if (!g_uid_h[0]) { ui_toast(L"ログイン後に設定できます"); return; }
    if (stats_consent() == 1) {
        set_ini(L"consent", L"0");
        set_ini(L"last_ping", L"");
        send_op("/v1/forget", 2);
        ui_toast(L"集計への協力をオフにしました(保存済みデータの削除を依頼しました)");
    } else {
        set_ini(L"consent", L"1");
        set_ini(L"last_ping", L"");
        send_op("/v1/ping", 1);
        ui_toast(L"ご協力ありがとうございます");
    }
}
