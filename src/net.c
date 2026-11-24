#include "app.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <shlwapi.h>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#endif
#ifndef WINHTTP_DECOMPRESSION_FLAG_ALL
#define WINHTTP_DECOMPRESSION_FLAG_ALL 0x00000003
#endif
#ifndef WINHTTP_OPTION_DISABLE_FEATURE
#define WINHTTP_OPTION_DISABLE_FEATURE 63
#endif
#ifndef WINHTTP_DISABLE_COOKIES
#define WINHTTP_DISABLE_COOKIES 0x00000001
#endif

char g_ua[512];
char g_cookie[4096];
wchar_t g_demo[MAX_PATH];
int g_is_demo;

/* ------------------------------------------------- session (DPAPI 保存) */
static void session_path(wchar_t *out, size_t n)
{
    wchar_t d[MAX_PATH];
    data_dir(d, MAX_PATH);
    _snwprintf(out, n, L"%ls\\session.bin", d);
    out[n - 1] = 0;
}
int session_save(void)
{
    char buf[5200];
    int len = _snprintf(buf, sizeof buf, "%s\n%s", g_ua, g_cookie);
    if (len <= 0) return 0;
    DATA_BLOB in = { (DWORD)len, (BYTE *)buf }, out = { 0, NULL };
    if (!CryptProtectData(&in, L"NorthC", NULL, NULL, NULL, 0, &out)) return 0;
    wchar_t p[MAX_PATH];
    session_path(p, MAX_PATH);
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { LocalFree(out.pbData); return 0; }
    DWORD w;
    WriteFile(h, out.pbData, out.cbData, &w, NULL);
    CloseHandle(h);
    LocalFree(out.pbData);
    return 1;
}
int session_load(void)
{
    wchar_t p[MAX_PATH];
    session_path(p, MAX_PATH);
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD sz = GetFileSize(h, NULL), r;
    BYTE *raw = (BYTE *)malloc(sz);
    ReadFile(h, raw, sz, &r, NULL);
    CloseHandle(h);
    DATA_BLOB in = { r, raw }, out = { 0, NULL };
    int ok = 0;
    if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out)) {
        char *s = (char *)malloc(out.cbData + 1);
        memcpy(s, out.pbData, out.cbData);
        s[out.cbData] = 0;
        char *nl = strchr(s, '\n');
        if (nl) {
            *nl = 0;
            strncpy(g_ua, s, sizeof g_ua - 1);
            strncpy(g_cookie, nl + 1, sizeof g_cookie - 1);
            ok = g_cookie[0] != 0;
        }
        free(s);
        LocalFree(out.pbData);
    }
    free(raw);
    return ok;
}
void session_clear(void)
{
    wchar_t p[MAX_PATH];
    session_path(p, MAX_PATH);
    DeleteFileW(p);
    g_cookie[0] = 0;
}

/* ------------------------------------------------------------- WinHTTP */
static HINTERNET g_ses;
static CRITICAL_SECTION g_ses_cs;

void net_reopen(void)
{
    EnterCriticalSection(&g_ses_cs);
    if (g_ses) WinHttpCloseHandle(g_ses);
    wchar_t *ua = u8w(g_ua[0] ? g_ua : "Mozilla/5.0 (Windows NT 10.0; Win64; x64) NorthC/0.2");
    g_ses = WinHttpOpen(ua, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    free(ua);
    LeaveCriticalSection(&g_ses_cs);
}

/* url は "/api/..." または "https://host/..." 。戻り値: malloc(長さ+1, NUL終端) */
static BYTE *http_fetch(const char *url, const wchar_t *method, const char *body, int bodylen, const wchar_t *ctype, const wchar_t *extra, int *status, int *outlen)
{
    *status = 0; *outlen = 0;
    wchar_t host[256] = L"north.rip", path[2200];
    int secure = 1;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    wchar_t *wu = u8w(url);
    if (url[0] == '/') {
        wcsncpy(path, wu, 2199); path[2199] = 0;
    } else {
        URL_COMPONENTS uc;
        wchar_t extra[1500];
        memset(&uc, 0, sizeof uc);
        uc.dwStructSize = sizeof uc;
        uc.lpszHostName = host; uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path; uc.dwUrlPathLength = 700;
        uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1500;
        if (!WinHttpCrackUrl(wu, 0, 0, &uc)) { free(wu); return NULL; }
        secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
        port = uc.nPort;
        wcsncat(path, extra, 2199 - wcslen(path));
    }
    free(wu);

    EnterCriticalSection(&g_ses_cs);
    HINTERNET ses = g_ses;
    LeaveCriticalSection(&g_ses_cs);
    if (!ses) return NULL;
    HINTERNET con = WinHttpConnect(ses, host, port, 0);
    if (!con) return NULL;
    HINTERNET rq = WinHttpOpenRequest(con, method, path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      secure ? WINHTTP_FLAG_SECURE : 0);
    if (!rq) { WinHttpCloseHandle(con); return NULL; }

    DWORD dec = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(rq, WINHTTP_OPTION_DECOMPRESSION, &dec, sizeof dec);
    DWORD nocookie = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(rq, WINHTTP_OPTION_DISABLE_FEATURE, &nocookie, sizeof nocookie);

    int isnorth = _wcsicmp(host, L"north.rip") == 0;
    wchar_t ctline[300] = L"";
    if (body) _snwprintf(ctline, 300, L"Content-Type: %ls\r\n", ctype ? ctype : L"application/json");
    ctline[299] = 0;
    wchar_t *ck = u8w(isnorth ? g_cookie : "");
    size_t cap = wcslen(ck) + 800 + (extra ? wcslen(extra) : 0);
    wchar_t *hdr = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (isnorth)
        _snwprintf(hdr, cap,
                   L"Cookie: %ls\r\nAccept: */*\r\nAccept-Language: ja,ja-JP;q=0.9,en;q=0.8\r\n"
                   L"Referer: https://north.rip/home\r\nOrigin: https://north.rip\r\n"
                   L"Sec-Fetch-Site: same-origin\r\nSec-Fetch-Mode: cors\r\nSec-Fetch-Dest: empty\r\n%ls",
                   ck, ctline);
    else if (body)
        _snwprintf(hdr, cap, L"Accept: application/json\r\n%ls%ls", ctline, extra ? extra : L"");
    else
        _snwprintf(hdr, cap, L"Accept: image/*,*/*;q=0.8\r\n");
    hdr[cap - 1] = 0;
    free(ck);

    DWORD blen = body ? (DWORD)bodylen : 0;
    BOOL ok = WinHttpSendRequest(rq, hdr, (DWORD)-1L, (LPVOID)body, blen, blen, 0) && WinHttpReceiveResponse(rq, NULL);
    free(hdr);
    BYTE *buf = NULL;
    if (ok) {
        DWORD code = 0, sz = sizeof code;
        WinHttpQueryHeaders(rq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
        *status = (int)code;
        size_t len = 0, alloc = 65536;
        buf = (BYTE *)malloc(alloc);
        for (;;) {
            DWORD avail = 0, got = 0;
            if (!WinHttpQueryDataAvailable(rq, &avail) || avail == 0) break;
            if (len + avail + 1 > alloc) { alloc = (len + avail + 1) * 2; buf = (BYTE *)realloc(buf, alloc); }
            if (!WinHttpReadData(rq, buf + len, avail, &got) || got == 0) break;
            len += got;
            if (len > 40u * 1024 * 1024) break;
        }
        buf[len] = 0;
        *outlen = (int)len;
    }
    WinHttpCloseHandle(rq);
    WinHttpCloseHandle(con);
    return buf;
}

/* ---------------------------------------------------------- デモモード */
static BYTE *read_file(const wchar_t *p, int *len)
{
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(h, NULL), r = 0;
    BYTE *b = (BYTE *)malloc(sz + 1);
    ReadFile(h, b, sz, &r, NULL);
    CloseHandle(h);
    b[r] = 0;
    *len = (int)r;
    return b;
}
static BYTE *demo_api(const char *method, const char *path, int *status, int *len)
{
    *status = 200;
    if (_stricmp(method, "GET") != 0) { BYTE *b = (BYTE *)dupstr("{}"); *len = 2; return b; }
    char name[300];
    size_t k = 0;
    for (const char *s = path[0] == '/' ? path + 1 : path; *s && k < 250; s++)
        name[k++] = isalnum((unsigned char)*s) ? *s : '_';
    name[k] = 0;
    wchar_t *wn = u8w(name), fp[MAX_PATH * 2];
    _snwprintf(fp, MAX_PATH * 2, L"%ls\\api\\%ls.json", g_demo, wn);
    fp[MAX_PATH * 2 - 1] = 0;
    free(wn);
    BYTE *b = read_file(fp, len);
    if (!b) { *status = 404; b = (BYTE *)dupstr("{}"); *len = 2; }
    return b;
}
static BYTE *demo_img(const char *url, int *len)
{
    const char *base = strrchr(url, '/');
    base = base ? base + 1 : url;
    char *q = strchr((char *)base, '?'); if (q) *q = 0;
    wchar_t *wn = u8w(base), fp[MAX_PATH * 2];
    _snwprintf(fp, MAX_PATH * 2, L"%ls\\img\\%ls", g_demo, wn);
    fp[MAX_PATH * 2 - 1] = 0;
    free(wn);
    return read_file(fp, len);
}

/* ------------------------------------------------------- 画像デコード */
static GpBitmap *decode_scaled(const BYTE *data, int n, int maxdim, int *ow, int *oh)
{
    IStream *st = SHCreateMemStream(data, (UINT)n);
    if (!st) return NULL;
    GpBitmap *src = NULL;
    if (GdipCreateBitmapFromStream(st, &src) != 0 || !src) { st->lpVtbl->Release(st); return NULL; }
    UINT w = 0, h = 0;
    GdipGetImageWidth(src, &w);
    GdipGetImageHeight(src, &h);
    if (!w || !h) { GdipDisposeImage(src); st->lpVtbl->Release(st); return NULL; }
    int nw = (int)w, nh = (int)h;
    int m = nw > nh ? nw : nh;
    if (m > maxdim) { nw = (int)((long long)nw * maxdim / m); nh = (int)((long long)nh * maxdim / m); if (nw < 1) nw = 1; if (nh < 1) nh = 1; }
    GpBitmap *dst = NULL;
    if (GdipCreateBitmapFromScan0(nw, nh, 0, 0xE200B /* 32bppPARGB */, NULL, &dst) != 0 || !dst) {
        GdipDisposeImage(src); st->lpVtbl->Release(st); return NULL;
    }
    GpGraphics *g = NULL;
    GdipGetImageGraphicsContext(dst, &g);
    GdipSetInterpolationMode(g, 7);
    GdipSetPixelOffsetMode(g, 4);
    GdipDrawImageRectRectI(g, src, 0, 0, nw, nh, 0, 0, (int)w, (int)h, 2, NULL, NULL, NULL);
    GdipDeleteGraphics(g);
    GdipDisposeImage(src);
    st->lpVtbl->Release(st);
    *ow = nw; *oh = nh;
    return dst;
}

/* ------------------------------------------------------ ワーカースレッド */
typedef struct Job {
    int kind;                 /* 0=API 1=IMG */
    int tag, arg;
    char method[8];
    char *path, *body, *ctx, *hdr;
    wchar_t *file;
    Img *img;
    struct Job *next;
} Job;

static Job *g_q;
static CRITICAL_SECTION g_qcs;
static HANDLE g_sem;
static volatile LONG g_quit;
#define NWORKERS 4
static HANDLE g_thr[NWORKERS];
static ULONG_PTR g_gdip;

static void push(Job *j)
{
    EnterCriticalSection(&g_qcs);
    Job **pp = &g_q;
    while (*pp) pp = &(*pp)->next;
    *pp = j;
    LeaveCriticalSection(&g_qcs);
    ReleaseSemaphore(g_sem, 1, NULL);
}
static Job *pop(void)
{
    Job *r = NULL;
    EnterCriticalSection(&g_qcs);
    Job **pp = &g_q, **first = NULL;
    for (; *pp; pp = &(*pp)->next) {
        if ((*pp)->kind == 0 || (*pp)->kind == 2) { first = pp; break; }   /* API / アップロード優先 */
        if (!first) first = pp;
    }
    if (first) { r = *first; *first = r->next; }
    LeaveCriticalSection(&g_qcs);
    return r;
}

static BYTE *build_multipart(const wchar_t *file, wchar_t *ctype, size_t ctn, int *outlen, int *toolarge)
{
    int len = 0;
    *toolarge = 0;
    BYTE *data = read_file(file, &len);
    if (!data) return NULL;
    if (len > 50 * 1024 * 1024) { free(data); *toolarge = 1; return NULL; }
    const wchar_t *base = file, *p;
    for (p = file; *p; p++) if (*p == L'\\' || *p == L'/') base = p + 1;
    char *name8 = wu8(base);
    for (char *q = name8; *q; q++) if (*q == '"' || *q == '\r' || *q == '\n') *q = '_';
    const wchar_t *ext = wcsrchr(base, L'.');
    const char *mime = "application/octet-stream";
    if (ext) {
        if (!_wcsicmp(ext, L".png")) mime = "image/png";
        else if (!_wcsicmp(ext, L".jpg") || !_wcsicmp(ext, L".jpeg")) mime = "image/jpeg";
        else if (!_wcsicmp(ext, L".gif")) mime = "image/gif";
        else if (!_wcsicmp(ext, L".webp")) mime = "image/webp";
    }
    char bnd[64];
    _snprintf(bnd, sizeof bnd, "----NorthClient%08lX%08X", (unsigned long)GetTickCount(), (unsigned)rand());
    char head[1200], foot[100];
    int hl = _snprintf(head, sizeof head, "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\nContent-Type: %s\r\n\r\n", bnd, name8, mime);
    int fl = _snprintf(foot, sizeof foot, "\r\n--%s--\r\n", bnd);
    free(name8);
    if (hl <= 0 || hl >= (int)sizeof head) { free(data); return NULL; }
    BYTE *out = (BYTE *)malloc((size_t)hl + (size_t)len + (size_t)fl + 1);
    memcpy(out, head, (size_t)hl);
    memcpy(out + hl, data, (size_t)len);
    memcpy(out + hl + len, foot, (size_t)fl);
    out[hl + len + fl] = 0;
    free(data);
    *outlen = hl + len + fl;
    _snwprintf(ctype, ctn, L"multipart/form-data; boundary=%hs", bnd);
    ctype[ctn - 1] = 0;
    return out;
}

static void run_job(Job *j)
{
    if (j->kind == 2) {
        int st = 0, len = 0, blen = 0, big = 0;
        BYTE *b = NULL;
        if (g_is_demo) {
            b = (BYTE *)dupstr("{\"id\":\"demo\",\"kind\":\"PHOTO\",\"status\":\"READY\",\"url\":\"/media/demo.jpg\",\"thumbnailUrl\":null,\"width\":800,\"height\":600}");
            st = 201;
            Sleep(600);
        } else {
            wchar_t ct[200];
            BYTE *mp = build_multipart(j->file, ct, 200, &blen, &big);
            if (mp) { b = http_fetch("/api/media", L"POST", (const char *)mp, blen, ct, NULL, &st, &len); free(mp); }
            else st = big ? 413 : 0;
        }
        ApiRes *r = (ApiRes *)calloc(1, sizeof *r);
        r->tag = j->tag; r->status = st; r->body = (char *)b; r->ctx = j->ctx; r->arg = j->arg;
        j->ctx = NULL;
        PostMessageW(g_main, WM_APP_API, 0, (LPARAM)r);
        free(j->path); free(j->body); free(j->ctx); free(j->file); free(j->hdr);
        free(j);
        return;
    }
    if (j->kind == 0) {
        int st = 0, len = 0;
        BYTE *b;
        wchar_t wm[8];
        MultiByteToWideChar(CP_UTF8, 0, j->method, -1, wm, 8);
        if (g_is_demo) b = demo_api(j->method, j->path, &st, &len);
        else {
            wchar_t *wh = j->hdr ? u8w(j->hdr) : NULL;
            b = http_fetch(j->path, wm, j->body, j->body ? (int)strlen(j->body) : 0, NULL, wh, &st, &len);
            free(wh);
        }
        ApiRes *r = (ApiRes *)calloc(1, sizeof *r);
        r->tag = j->tag; r->status = st; r->body = (char *)b; r->ctx = j->ctx; r->arg = j->arg;
        j->ctx = NULL;
        PostMessageW(g_main, WM_APP_API, 0, (LPARAM)r);
    } else {
        Img *e = j->img;
        int st = 0, len = 0;
        BYTE *b;
        if (strncmp(e->url, "file:", 5) == 0) {
            wchar_t *wp = u8w(e->url + 5);
            b = read_file(wp, &len);
            free(wp);
            st = b ? 200 : 404;
        } else if (g_is_demo) { b = demo_img(e->url, &len); st = b ? 200 : 404; }
        else b = http_fetch(e->url, L"GET", NULL, 0, NULL, NULL, &st, &len);
        int w = 0, h = 0;
        GpBitmap *bm = (b && st == 200 && len > 0) ? decode_scaled(b, len, e->maxdim, &w, &h) : NULL;
        free(b);
        if (bm) { e->bmp = bm; e->w = w; e->h = h; InterlockedExchange(&e->state, 1); }
        else InterlockedExchange(&e->state, 2);
        PostMessageW(g_main, WM_APP_IMG, 0, 0);
    }
    free(j->path); free(j->body); free(j->ctx); free(j->file); free(j->hdr);
    free(j);
}

static DWORD WINAPI worker(LPVOID p)
{
    (void)p;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    for (;;) {
        WaitForSingleObject(g_sem, INFINITE);
        if (g_quit) break;
        Job *j = pop();
        if (j) run_job(j);
    }
    CoUninitialize();
    return 0;
}

void net_init(void)
{
    GdiplusStartupInput in = { 1, NULL, FALSE, FALSE };
    GdiplusStartup(&g_gdip, &in, NULL);
    InitializeCriticalSection(&g_ses_cs);
    InitializeCriticalSection(&g_qcs);
    g_sem = CreateSemaphoreW(NULL, 0, 100000, NULL);
    net_reopen();
    for (int i = 0; i < NWORKERS; i++) g_thr[i] = CreateThread(NULL, 0, worker, NULL, 0, NULL);
}

void net_shutdown(void)
{
    InterlockedExchange(&g_quit, 1);
    ReleaseSemaphore(g_sem, NWORKERS, NULL);
    WaitForMultipleObjects(NWORKERS, g_thr, TRUE, 1500);
}

void net_upload(int tag, const wchar_t *file, const char *ctx, int arg)
{
    Job *j = (Job *)calloc(1, sizeof *j);
    j->kind = 2; j->tag = tag; j->arg = arg;
    j->file = (wchar_t *)malloc((wcslen(file) + 1) * sizeof(wchar_t));
    wcscpy(j->file, file);
    j->ctx = ctx ? dupstr(ctx) : NULL;
    push(j);
}

void net_api_hdr(int tag, const char *method, const char *path, const char *body, const char *ctx, int arg, const char *hdr)
{
    Job *j = (Job *)calloc(1, sizeof *j);
    j->kind = 0; j->tag = tag; j->arg = arg;
    strncpy(j->method, method, 7);
    j->path = dupstr(path);
    j->body = body ? dupstr(body) : NULL;
    j->ctx = ctx ? dupstr(ctx) : NULL;
    j->hdr = hdr ? dupstr(hdr) : NULL;
    push(j);
}

void net_api(int tag, const char *method, const char *path, const char *body, const char *ctx, int arg)
{
    Job *j = (Job *)calloc(1, sizeof *j);
    j->kind = 0; j->tag = tag; j->arg = arg;
    strncpy(j->method, method, 7);
    j->path = dupstr(path);
    j->body = body ? dupstr(body) : NULL;
    j->ctx = ctx ? dupstr(ctx) : NULL;
    push(j);
}

/* ------------------------------------------------------- 画像キャッシュ */
static Img *g_imgtab[2048];
static unsigned hash_str(const char *s) { unsigned h = 2166136261u; while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; } return h; }

Img *img_get(const char *url, int maxdim)
{
    if (!url || !url[0]) return NULL;
    unsigned k = hash_str(url) & 2047;
    for (Img *e = g_imgtab[k]; e; e = e->next)
        if (e->maxdim == maxdim && strcmp(e->url, url) == 0) return e;      /* 同じURLでも解像度が違えば別エントリ */
    Img *e = (Img *)calloc(1, sizeof *e);
    e->url = dupstr(url);
    e->maxdim = maxdim;
    e->next = g_imgtab[k];
    g_imgtab[k] = e;
    Job *j = (Job *)calloc(1, sizeof *j);
    j->kind = 1; j->img = e;
    push(j);
    return e;
}

/* 大きく読み込んだ画像(ビューア用)を閉じたときにメモリを返す。読み込み中のものは残す */
void img_release(const char *url, int maxdim)
{
    if (!url || !url[0]) return;
    unsigned k = hash_str(url) & 2047;
    for (Img **pp = &g_imgtab[k]; *pp; pp = &(*pp)->next) {
        Img *e = *pp;
        if (e->maxdim != maxdim || strcmp(e->url, url) != 0) continue;
        if (e->state == 0) return;
        *pp = e->next;
        if (e->bmp) GdipDisposeImage(e->bmp);
        free(e->url);
        free(e);
        return;
    }
}
