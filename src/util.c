#include "app.h"

wchar_t *u8w(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)(n > 0 ? n : 1) * sizeof(wchar_t));
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n); else w[0] = 0;
    return w;
}
char *wu8(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = (char *)malloc((size_t)(n > 0 ? n : 1));
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL); else s[0] = 0;
    return s;
}
char *dupstr(const char *s) { size_t n = strlen(s) + 1; char *p = (char *)malloc(n); memcpy(p, s, n); return p; }

void data_dir(wchar_t *out, size_t n)
{
    wchar_t base[MAX_PATH] = L".";
    GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    _snwprintf(out, n, L"%ls\\NorthC", base);
    out[n - 1] = 0;
    CreateDirectoryW(out, NULL);
}

void jcopy(char *dst, size_t n, const cJSON *obj, const char *key)
{
    const cJSON *v = obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    dst[0] = 0;
    if (cJSON_IsString(v) && v->valuestring) { strncpy(dst, v->valuestring, n - 1); dst[n - 1] = 0; }
}
int jint(const cJSON *obj, const char *key)
{
    const cJSON *v = obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    return cJSON_IsNumber(v) ? (int)v->valuedouble : 0;
}
int jbool(const cJSON *obj, const char *key) { return obj && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, key)); }

static int iso_to_st(const char *iso, SYSTEMTIME *st)
{
    int Y, M, D, h = 0, m = 0, s = 0;
    memset(st, 0, sizeof *st);
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) < 3) return 0;
    st->wYear = (WORD)Y; st->wMonth = (WORD)M; st->wDay = (WORD)D;
    st->wHour = (WORD)h; st->wMinute = (WORD)m; st->wSecond = (WORD)s;
    return 1;
}
static ULONGLONG st_to_u64(const SYSTEMTIME *st)
{
    FILETIME ft; ULARGE_INTEGER u;
    SystemTimeToFileTime(st, &ft);
    u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

void fmt_full_date(const char *iso, wchar_t *out, int n, int with_day)
{
    SYSTEMTIME st, lt;
    out[0] = 0;
    if (!iso_to_st(iso, &st)) return;
    if (!SystemTimeToTzSpecificLocalTime(NULL, &st, &lt)) lt = st;
    if (with_day) _snwprintf(out, (size_t)n, L"%d\u5E74%d\u6708%d\u65E5", lt.wYear, lt.wMonth, lt.wDay);
    else _snwprintf(out, (size_t)n, L"%d\u5E74%d\u6708", lt.wYear, lt.wMonth);
    out[n - 1] = 0;
}

void fmt_rel_time(const char *iso, wchar_t *out, int n)
{
    SYSTEMTIME st, now, lt, lnow;
    FILETIME fn;
    out[0] = 0;
    if (!iso_to_st(iso, &st)) return;
    GetSystemTimeAsFileTime(&fn);
    FileTimeToSystemTime(&fn, &now);
    long long d = ((long long)st_to_u64(&now) - (long long)st_to_u64(&st)) / 10000000LL;
    if (d < 0) d = 0;
    if (d < 60) _snwprintf(out, (size_t)n, L"%d\u79D2", (int)d);
    else if (d < 3600) _snwprintf(out, (size_t)n, L"%d\u5206", (int)(d / 60));
    else if (d < 86400) _snwprintf(out, (size_t)n, L"%d\u6642\u9593", (int)(d / 3600));
    else {
        if (!SystemTimeToTzSpecificLocalTime(NULL, &st, &lt)) lt = st;
        if (!SystemTimeToTzSpecificLocalTime(NULL, &now, &lnow)) lnow = now;
        if (lt.wYear == lnow.wYear) _snwprintf(out, (size_t)n, L"%d\u6708%d\u65E5", lt.wMonth, lt.wDay);
        else _snwprintf(out, (size_t)n, L"%d\u5E74%d\u6708%d\u65E5", lt.wYear, lt.wMonth, lt.wDay);
    }
    out[n - 1] = 0;
}

void fmt_count(int v, wchar_t *out, int n)
{
    if (v >= 10000) {
        int t = v / 1000;                  /* 12.3万 */
        _snwprintf(out, (size_t)n, t % 10 ? L"%d.%d\u4E07" : L"%d\u4E07", t / 10, t % 10);
    } else if (v >= 1000) {
        int t = v / 100;                   /* 1.2千 */
        _snwprintf(out, (size_t)n, t % 10 ? L"%d.%d\u5343" : L"%d\u5343", t / 10, t % 10);
    } else _snwprintf(out, (size_t)n, L"%d", v);
    out[n - 1] = 0;
}

void fmt_remaining(const char *iso, wchar_t *out, int n)
{
    SYSTEMTIME st, now;
    FILETIME fn;
    out[0] = 0;
    if (!iso_to_st(iso, &st)) return;
    GetSystemTimeAsFileTime(&fn);
    FileTimeToSystemTime(&fn, &now);
    long long d = ((long long)st_to_u64(&st) - (long long)st_to_u64(&now)) / 10000000LL;
    if (d <= 0) { _snwprintf(out, (size_t)n, L"終了"); out[n - 1] = 0; return; }
    if (d >= 86400) _snwprintf(out, (size_t)n, L"残り%d日", (int)(d / 86400));
    else if (d >= 3600) _snwprintf(out, (size_t)n, L"残り%d時間", (int)(d / 3600));
    else _snwprintf(out, (size_t)n, L"残り%d分", (int)(d / 60) + 1);
    out[n - 1] = 0;
}
