#include "app.h"
#include <shellapi.h>

/* 起動引数:
 *   --demo <dir>   ローカルのJSON/画像で動かす(開発・確認用。<dir>\api\*.json, <dir>\img\*)
 *   --size WxH     初期ウィンドウサイズ
 */
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cmd, int show)
{
    (void)hp; (void)cmd;
    OleInitialize(NULL);

    int W = 1280, H = 860, argc = 0, dbg_drawer = 0, dbg_scroll = 0, dbg_poll = 0, dbg_lb = 0;
    char dbg_prof[80] = "", dbg_view[20] = "", dbg_q[100] = "";
    wchar_t dbg_att[MAX_PATH] = L"";
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--demo") == 0 && i + 1 < argc) { wcsncpy(g_demo, argv[++i], MAX_PATH - 1); g_is_demo = 1; }
        else if (wcscmp(argv[i], L"--size") == 0 && i + 1 < argc) swscanf(argv[++i], L"%dx%d", &W, &H);
        else if (wcscmp(argv[i], L"--open") == 0 && i + 1 < argc) WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, dbg_prof, 79, NULL, NULL);
        else if (wcscmp(argv[i], L"--view") == 0 && i + 1 < argc) WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, dbg_view, 19, NULL, NULL);
        else if (wcscmp(argv[i], L"--q") == 0 && i + 1 < argc) WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, dbg_q, 99, NULL, NULL);
        else if (wcscmp(argv[i], L"--attach") == 0 && i + 1 < argc) wcsncpy(dbg_att, argv[++i], MAX_PATH - 1);
        else if (wcscmp(argv[i], L"--poll") == 0) dbg_poll = 1;
        else if (wcscmp(argv[i], L"--lightbox") == 0) dbg_lb = 1;
        else if (wcscmp(argv[i], L"--drawer") == 0) dbg_drawer = 1;
        else if (wcscmp(argv[i], L"--scroll") == 0 && i + 1 < argc) dbg_scroll = _wtoi(argv[++i]);
    }
    LocalFree(argv);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = main_proc;
    wc.style = CS_DBLCLKS;
    wc.hInstance = hi;
    wc.lpszClassName = L"NorthMainWnd";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    RegisterClassW(&wc);

    HWND h = CreateWindowExW(0, L"NorthMainWnd", L"north", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             CW_USEDEFAULT, CW_USEDEFAULT, W, H, NULL, NULL, hi, NULL);
    if (!h) return 1;
    ShowWindow(h, show);
    ui_startup();
    if (g_is_demo) { ui_debug(dbg_prof, dbg_drawer, dbg_scroll, dbg_view, dbg_q); if (dbg_att[0]) ui_debug_attach(dbg_att); if (dbg_poll) ui_debug_poll(); if (dbg_lb) ui_debug_lightbox(); }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    OleUninitialize();
    return (int)msg.wParam;
}
