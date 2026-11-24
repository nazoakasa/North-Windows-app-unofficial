#include "app.h"
#include "webview2_min.h"

/* ログインだけ WebView2 で行い、north_session を取り出す */
typedef struct {
    HWND hwnd;
    ICoreWebView2Environment *env;
    ICoreWebView2Controller *ctrl;
    ICoreWebView2 *view;
    ICoreWebView2_2 *view2;
    char ua[512];
    int busy;
    int finished;
} LoginCtx;
static LoginCtx g_lg;
static HMODULE g_loader;
static PFN_CreateCoreWebView2EnvironmentWithOptions g_createEnv;

/* 使い捨てハンドラ共通の IUnknown 実装 (静的オブジェクトなので解放しない) */
static HRESULT STDMETHODCALLTYPE h_qi(void *This, REFIID riid, void **ppv) { (void)riid; *ppv = This; return S_OK; }
static ULONG STDMETHODCALLTYPE h_addref(void *This) { (void)This; return 2; }
static ULONG STDMETHODCALLTYPE h_release(void *This) { (void)This; return 1; }

static void login_resize(void)
{
    if (!g_lg.ctrl) return;
    RECT rc;
    GetClientRect(g_lg.hwnd, &rc);
    g_lg.ctrl->lpVtbl->SetBounds(g_lg.ctrl, rc);
}

/* --- GetCookies 完了 --- */
static HRESULT STDMETHODCALLTYPE cookies_invoke(ICoreWebView2GetCookiesCompletedHandler *This, HRESULT hr, ICoreWebView2CookieList *list)
{
    (void)This;
    g_lg.busy = 0;
    if (FAILED(hr) || !list || g_lg.finished) return S_OK;
    UINT count = 0;
    list->lpVtbl->Count(list, &count);
    char hdr[4096];
    size_t pos = 0;
    int have = 0;
    hdr[0] = 0;
    for (UINT i = 0; i < count; i++) {
        ICoreWebView2Cookie *c = NULL;
        if (FAILED(list->lpVtbl->GetValueAtIndex(list, i, &c)) || !c) continue;
        LPWSTR nm = NULL, vl = NULL;
        c->lpVtbl->Name(c, &nm);
        c->lpVtbl->Value(c, &vl);
        if (nm && vl) {
            char *n8 = wu8(nm), *v8 = wu8(vl);
            if (strcmp(n8, "north_session") == 0 && v8[0]) have = 1;
            int w = _snprintf(hdr + pos, sizeof hdr - pos, "%s%s=%s", pos ? "; " : "", n8, v8);
            if (w > 0) pos += (size_t)w;
            free(n8); free(v8);
        }
        if (nm) CoTaskMemFree(nm);
        if (vl) CoTaskMemFree(vl);
        c->lpVtbl->Release(c);
    }
    if (have) {
        g_lg.finished = 1;
        strncpy(g_cookie, hdr, sizeof g_cookie - 1);
        strncpy(g_ua, g_lg.ua, sizeof g_ua - 1);
        session_save();
        PostMessageW(g_main, WM_APP_LOGIN_DONE, 0, 0);
        PostMessageW(g_lg.hwnd, WM_CLOSE, 0, 0);
    }
    return S_OK;
}
static ICoreWebView2GetCookiesCompletedHandlerVtbl g_cookiesVtbl = {
    (void *)h_qi, (void *)h_addref, (void *)h_release, cookies_invoke
};
static ICoreWebView2GetCookiesCompletedHandler g_cookiesHandler = { &g_cookiesVtbl };

/* --- SourceChanged (SPA の pushState でも発火) --- */
static HRESULT STDMETHODCALLTYPE source_invoke(ICoreWebView2SourceChangedEventHandler *This, ICoreWebView2 *sender, void *args)
{
    (void)This; (void)args;
    if (g_lg.busy || g_lg.finished || !g_lg.view2) return S_OK;
    LPWSTR src = NULL;
    sender->lpVtbl->Source(sender, &src);
    int onHome = src && wcsncmp(src, L"https://north.rip/home", 22) == 0;
    if (src) CoTaskMemFree(src);
    if (!onHome) return S_OK;

    ICoreWebView2CookieManager *cm = NULL;
    if (SUCCEEDED(g_lg.view2->lpVtbl->CookieManager(g_lg.view2, &cm)) && cm) {
        g_lg.busy = 1;
        cm->lpVtbl->GetCookies(cm, L"https://north.rip", &g_cookiesHandler);
        cm->lpVtbl->Release(cm);
    }
    return S_OK;
}
static ICoreWebView2SourceChangedEventHandlerVtbl g_sourceVtbl = {
    (void *)h_qi, (void *)h_addref, (void *)h_release, source_invoke
};
static ICoreWebView2SourceChangedEventHandler g_sourceHandler = { &g_sourceVtbl };

/* --- Controller 作成完了 --- */
static HRESULT STDMETHODCALLTYPE ctrl_invoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *This, HRESULT hr, ICoreWebView2Controller *c)
{
    (void)This;
    if (FAILED(hr) || !c) {
        MessageBoxW(g_lg.hwnd, L"WebView2 の初期化に失敗しました。", L"North", MB_ICONERROR);
        return S_OK;
    }
    c->lpVtbl->AddRef(c);
    g_lg.ctrl = c;
    c->lpVtbl->CoreWebView2(c, &g_lg.view);
    login_resize();

    ICoreWebView2 *v = g_lg.view;
    long long tok = 0;
    v->lpVtbl->add_SourceChanged(v, &g_sourceHandler, &tok);

    /* UA を控える (以降の WinHTTP でも同じ UA を使う) */
    ICoreWebView2Settings *st = NULL;
    strcpy(g_lg.ua, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    if (SUCCEEDED(v->lpVtbl->Settings(v, &st)) && st) {
        ICoreWebView2Settings2 *s2 = NULL;
        if (SUCCEEDED(st->lpVtbl->QueryInterface(st, &IID_ICoreWebView2Settings2, (void **)&s2)) && s2) {
            LPWSTR ua = NULL;
            if (SUCCEEDED(s2->lpVtbl->UserAgent(s2, &ua)) && ua) {
                char *u8 = wu8(ua);
                strncpy(g_lg.ua, u8, sizeof g_lg.ua - 1);
                free(u8);
                CoTaskMemFree(ua);
            }
            s2->lpVtbl->Release(s2);
        }
        st->lpVtbl->Release(st);
    }

    if (SUCCEEDED(v->lpVtbl->QueryInterface(v, &IID_ICoreWebView2_2, (void **)&g_lg.view2)) && g_lg.view2) {
        ICoreWebView2CookieManager *cm = NULL;
        if (SUCCEEDED(g_lg.view2->lpVtbl->CookieManager(g_lg.view2, &cm)) && cm) {
            cm->lpVtbl->DeleteAllCookies(cm); /* 古いセッションを引きずらない */
            cm->lpVtbl->Release(cm);
        }
    }
    v->lpVtbl->Navigate(v, L"https://north.rip/login");
    return S_OK;
}
static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl g_ctrlVtbl = {
    (void *)h_qi, (void *)h_addref, (void *)h_release, ctrl_invoke
};
static ICoreWebView2CreateCoreWebView2ControllerCompletedHandler g_ctrlHandler = { &g_ctrlVtbl };

/* --- Environment 作成完了 --- */
static HRESULT STDMETHODCALLTYPE env_invoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *This, HRESULT hr, ICoreWebView2Environment *env)
{
    (void)This;
    if (FAILED(hr) || !env) {
        MessageBoxW(g_lg.hwnd, L"WebView2 Runtime が見つかりません。\nMicrosoft Edge WebView2 Runtime をインストールしてください。", L"North", MB_ICONERROR);
        return S_OK;
    }
    env->lpVtbl->AddRef(env);
    g_lg.env = env;
    env->lpVtbl->CreateCoreWebView2Controller(env, g_lg.hwnd, &g_ctrlHandler);
    return S_OK;
}
static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl g_envVtbl = {
    (void *)h_qi, (void *)h_addref, (void *)h_release, env_invoke
};
static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler g_envHandler = { &g_envVtbl };

static LRESULT CALLBACK login_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_SIZE: login_resize(); return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (g_lg.view2) { g_lg.view2->lpVtbl->Release(g_lg.view2); g_lg.view2 = NULL; }
        if (g_lg.view) { g_lg.view->lpVtbl->Release(g_lg.view); g_lg.view = NULL; }
        if (g_lg.ctrl) { g_lg.ctrl->lpVtbl->Close(g_lg.ctrl); g_lg.ctrl->lpVtbl->Release(g_lg.ctrl); g_lg.ctrl = NULL; }
        if (g_lg.env) { g_lg.env->lpVtbl->Release(g_lg.env); g_lg.env = NULL; }
        g_lg.hwnd = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void open_login(void)
{
    if (g_lg.hwnd) { SetForegroundWindow(g_lg.hwnd); return; }
    if (!g_loader) {
        g_loader = LoadLibraryW(L"WebView2Loader.dll");
        if (g_loader) g_createEnv = (PFN_CreateCoreWebView2EnvironmentWithOptions)GetProcAddress(g_loader, "CreateCoreWebView2EnvironmentWithOptions");
    }
    if (!g_createEnv) {
        MessageBoxW(g_main, L"WebView2Loader.dll が見つかりません。north.exe と同じフォルダに置いてください。", L"North", MB_ICONERROR);
        return;
    }
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = login_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"NorthLoginWnd";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wc); /* 2回目以降は失敗するが無害 */

    memset(&g_lg, 0, sizeof g_lg);
    g_lg.hwnd = CreateWindowExW(0, L"NorthLoginWnd", L"North - ログイン (ログインできたら自動で閉じます)",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 520, 760,
                                g_main, NULL, wc.hInstance, NULL);

    wchar_t udf[MAX_PATH], d[MAX_PATH];
    data_dir(d, MAX_PATH);
    _snwprintf(udf, MAX_PATH, L"%ls\\WebView2", d);
    udf[MAX_PATH - 1] = 0;
    HRESULT hr = g_createEnv(NULL, udf, NULL, &g_envHandler);
    if (FAILED(hr)) {
        MessageBoxW(g_main, L"WebView2 の環境作成に失敗しました。", L"North", MB_ICONERROR);
        DestroyWindow(g_lg.hwnd);
    }
}

