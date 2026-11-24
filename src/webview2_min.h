/* webview2_min.h -- WebView2 の COM インターフェースのうち本アプリで使う分だけの最小C宣言。
 * vtable の並びは WebView2 SDK 1.0.x の公開IDLに準拠。使わないスロットは void* で埋めてある。 */
#ifndef WEBVIEW2_MIN_H
#define WEBVIEW2_MIN_H
#include <windows.h>

typedef struct ICoreWebView2Environment ICoreWebView2Environment;
typedef struct ICoreWebView2Controller ICoreWebView2Controller;
typedef struct ICoreWebView2 ICoreWebView2;
typedef struct ICoreWebView2Settings ICoreWebView2Settings;
typedef struct ICoreWebView2Settings2 ICoreWebView2Settings2;
typedef struct ICoreWebView2_2 ICoreWebView2_2;
typedef struct ICoreWebView2CookieManager ICoreWebView2CookieManager;
typedef struct ICoreWebView2CookieList ICoreWebView2CookieList;
typedef struct ICoreWebView2Cookie ICoreWebView2Cookie;
typedef struct ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler;
typedef struct ICoreWebView2CreateCoreWebView2ControllerCompletedHandler ICoreWebView2CreateCoreWebView2ControllerCompletedHandler;
typedef struct ICoreWebView2SourceChangedEventHandler ICoreWebView2SourceChangedEventHandler;
typedef struct ICoreWebView2GetCookiesCompletedHandler ICoreWebView2GetCookiesCompletedHandler;

typedef struct ICoreWebView2EnvironmentVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2Environment *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2Environment *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2Environment *This);
    HRESULT (STDMETHODCALLTYPE *CreateCoreWebView2Controller)(ICoreWebView2Environment *This, HWND parentWindow, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *handler);
    void *CreateWebResourceResponse_;
    void *BrowserVersionString_;
    void *add_NewBrowserVersionAvailable_;
    void *remove_NewBrowserVersionAvailable_;
} ICoreWebView2EnvironmentVtbl;
struct ICoreWebView2Environment { const ICoreWebView2EnvironmentVtbl *lpVtbl; };

typedef struct ICoreWebView2ControllerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2Controller *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2Controller *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2Controller *This);
    void *IsVisible_;
    void *SetIsVisible_;
    void *Bounds_;
    HRESULT (STDMETHODCALLTYPE *SetBounds)(ICoreWebView2Controller *This, RECT bounds);
    void *ZoomFactor_;
    void *SetZoomFactor_;
    void *add_ZoomFactorChanged_;
    void *remove_ZoomFactorChanged_;
    void *SetBoundsAndZoomFactor_;
    void *MoveFocus_;
    void *add_MoveFocusRequested_;
    void *remove_MoveFocusRequested_;
    void *add_GotFocus_;
    void *remove_GotFocus_;
    void *add_LostFocus_;
    void *remove_LostFocus_;
    void *add_AcceleratorKeyPressed_;
    void *remove_AcceleratorKeyPressed_;
    void *ParentWindow_;
    void *SetParentWindow_;
    void *NotifyParentWindowPositionChanged_;
    HRESULT (STDMETHODCALLTYPE *Close)(ICoreWebView2Controller *This);
    HRESULT (STDMETHODCALLTYPE *CoreWebView2)(ICoreWebView2Controller *This, ICoreWebView2 **webview2);
} ICoreWebView2ControllerVtbl;
struct ICoreWebView2Controller { const ICoreWebView2ControllerVtbl *lpVtbl; };

typedef struct ICoreWebView2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2 *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2 *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2 *This);
    HRESULT (STDMETHODCALLTYPE *Settings)(ICoreWebView2 *This, ICoreWebView2Settings **settings);
    HRESULT (STDMETHODCALLTYPE *Source)(ICoreWebView2 *This, LPWSTR *uri);
    HRESULT (STDMETHODCALLTYPE *Navigate)(ICoreWebView2 *This, LPCWSTR uri);
    void *NavigateToString_;
    void *add_NavigationStarting_;
    void *remove_NavigationStarting_;
    void *add_ContentLoading_;
    void *remove_ContentLoading_;
    HRESULT (STDMETHODCALLTYPE *add_SourceChanged)(ICoreWebView2 *This, ICoreWebView2SourceChangedEventHandler *handler, long long *token);
    void *remove_SourceChanged_;
    void *add_HistoryChanged_;
    void *remove_HistoryChanged_;
    void *add_NavigationCompleted_;
    void *remove_NavigationCompleted_;
    void *add_FrameNavigationStarting_;
    void *remove_FrameNavigationStarting_;
    void *add_FrameNavigationCompleted_;
    void *remove_FrameNavigationCompleted_;
    void *add_ScriptDialogOpening_;
    void *remove_ScriptDialogOpening_;
    void *add_PermissionRequested_;
    void *remove_PermissionRequested_;
    void *add_ProcessFailed_;
    void *remove_ProcessFailed_;
    void *AddScriptToExecuteOnDocumentCreated_;
    void *RemoveScriptToExecuteOnDocumentCreated_;
    void *ExecuteScript_;
    void *CapturePreview_;
    void *Reload_;
    void *PostWebMessageAsJson_;
    void *PostWebMessageAsString_;
    void *add_WebMessageReceived_;
    void *remove_WebMessageReceived_;
    void *CallDevToolsProtocolMethod_;
    void *BrowserProcessId_;
    void *CanGoBack_;
    void *CanGoForward_;
    void *GoBack_;
    void *GoForward_;
    void *GetDevToolsProtocolEventReceiver_;
    void *Stop_;
    void *add_NewWindowRequested_;
    void *remove_NewWindowRequested_;
    void *add_DocumentTitleChanged_;
    void *remove_DocumentTitleChanged_;
    void *DocumentTitle_;
    void *AddHostObjectToScript_;
    void *RemoveHostObjectFromScript_;
    void *OpenDevToolsWindow_;
    void *add_ContainsFullScreenElementChanged_;
    void *remove_ContainsFullScreenElementChanged_;
    void *ContainsFullScreenElement_;
    void *add_WebResourceRequested_;
    void *remove_WebResourceRequested_;
    void *AddWebResourceRequestedFilter_;
    void *RemoveWebResourceRequestedFilter_;
    void *add_WindowCloseRequested_;
    void *remove_WindowCloseRequested_;
} ICoreWebView2Vtbl;
struct ICoreWebView2 { const ICoreWebView2Vtbl *lpVtbl; };

typedef struct ICoreWebView2SettingsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2Settings *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2Settings *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2Settings *This);
    void *IsScriptEnabled_;
    void *SetIsScriptEnabled_;
    void *IsWebMessageEnabled_;
    void *SetIsWebMessageEnabled_;
    void *AreDefaultScriptDialogsEnabled_;
    void *SetAreDefaultScriptDialogsEnabled_;
    void *IsStatusBarEnabled_;
    void *SetIsStatusBarEnabled_;
    void *AreDevToolsEnabled_;
    void *SetAreDevToolsEnabled_;
    void *AreDefaultContextMenusEnabled_;
    void *SetAreDefaultContextMenusEnabled_;
    void *AreHostObjectsAllowed_;
    void *SetAreHostObjectsAllowed_;
    void *IsZoomControlEnabled_;
    void *SetIsZoomControlEnabled_;
    void *IsBuiltInErrorPageEnabled_;
    void *SetIsBuiltInErrorPageEnabled_;
} ICoreWebView2SettingsVtbl;
struct ICoreWebView2Settings { const ICoreWebView2SettingsVtbl *lpVtbl; };

typedef struct ICoreWebView2Settings2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2Settings2 *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2Settings2 *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2Settings2 *This);
    void *base_slot_0_;
    void *base_slot_1_;
    void *base_slot_2_;
    void *base_slot_3_;
    void *base_slot_4_;
    void *base_slot_5_;
    void *base_slot_6_;
    void *base_slot_7_;
    void *base_slot_8_;
    void *base_slot_9_;
    void *base_slot_10_;
    void *base_slot_11_;
    void *base_slot_12_;
    void *base_slot_13_;
    void *base_slot_14_;
    void *base_slot_15_;
    void *base_slot_16_;
    void *base_slot_17_;
    HRESULT (STDMETHODCALLTYPE *UserAgent)(ICoreWebView2Settings2 *This, LPWSTR *userAgent);
    void *SetUserAgent_;
} ICoreWebView2Settings2Vtbl;
struct ICoreWebView2Settings2 { const ICoreWebView2Settings2Vtbl *lpVtbl; };

typedef struct ICoreWebView2_2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2_2 *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2_2 *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2_2 *This);
    void *base_slot_0_;
    void *base_slot_1_;
    void *base_slot_2_;
    void *base_slot_3_;
    void *base_slot_4_;
    void *base_slot_5_;
    void *base_slot_6_;
    void *base_slot_7_;
    void *base_slot_8_;
    void *base_slot_9_;
    void *base_slot_10_;
    void *base_slot_11_;
    void *base_slot_12_;
    void *base_slot_13_;
    void *base_slot_14_;
    void *base_slot_15_;
    void *base_slot_16_;
    void *base_slot_17_;
    void *base_slot_18_;
    void *base_slot_19_;
    void *base_slot_20_;
    void *base_slot_21_;
    void *base_slot_22_;
    void *base_slot_23_;
    void *base_slot_24_;
    void *base_slot_25_;
    void *base_slot_26_;
    void *base_slot_27_;
    void *base_slot_28_;
    void *base_slot_29_;
    void *base_slot_30_;
    void *base_slot_31_;
    void *base_slot_32_;
    void *base_slot_33_;
    void *base_slot_34_;
    void *base_slot_35_;
    void *base_slot_36_;
    void *base_slot_37_;
    void *base_slot_38_;
    void *base_slot_39_;
    void *base_slot_40_;
    void *base_slot_41_;
    void *base_slot_42_;
    void *base_slot_43_;
    void *base_slot_44_;
    void *base_slot_45_;
    void *base_slot_46_;
    void *base_slot_47_;
    void *base_slot_48_;
    void *base_slot_49_;
    void *base_slot_50_;
    void *base_slot_51_;
    void *base_slot_52_;
    void *base_slot_53_;
    void *base_slot_54_;
    void *base_slot_55_;
    void *base_slot_56_;
    void *base_slot_57_;
    void *add_WebResourceResponseReceived_;
    void *remove_WebResourceResponseReceived_;
    void *NavigateWithWebResourceRequest_;
    void *add_DOMContentLoaded_;
    void *remove_DOMContentLoaded_;
    HRESULT (STDMETHODCALLTYPE *CookieManager)(ICoreWebView2_2 *This, ICoreWebView2CookieManager **cookieManager);
    void *Environment_;
} ICoreWebView2_2Vtbl;
struct ICoreWebView2_2 { const ICoreWebView2_2Vtbl *lpVtbl; };

typedef struct ICoreWebView2CookieManagerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2CookieManager *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2CookieManager *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2CookieManager *This);
    void *CreateCookie_;
    void *CopyCookie_;
    HRESULT (STDMETHODCALLTYPE *GetCookies)(ICoreWebView2CookieManager *This, LPCWSTR uri, ICoreWebView2GetCookiesCompletedHandler *handler);
    void *AddOrUpdateCookie_;
    void *DeleteCookie_;
    void *DeleteCookies_;
    void *DeleteCookiesWithDomainAndPath_;
    HRESULT (STDMETHODCALLTYPE *DeleteAllCookies)(ICoreWebView2CookieManager *This);
} ICoreWebView2CookieManagerVtbl;
struct ICoreWebView2CookieManager { const ICoreWebView2CookieManagerVtbl *lpVtbl; };

typedef struct ICoreWebView2CookieListVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2CookieList *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2CookieList *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2CookieList *This);
    HRESULT (STDMETHODCALLTYPE *Count)(ICoreWebView2CookieList *This, UINT *count);
    HRESULT (STDMETHODCALLTYPE *GetValueAtIndex)(ICoreWebView2CookieList *This, UINT index, ICoreWebView2Cookie **cookie);
} ICoreWebView2CookieListVtbl;
struct ICoreWebView2CookieList { const ICoreWebView2CookieListVtbl *lpVtbl; };

typedef struct ICoreWebView2CookieVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2Cookie *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2Cookie *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2Cookie *This);
    HRESULT (STDMETHODCALLTYPE *Name)(ICoreWebView2Cookie *This, LPWSTR *name);
    HRESULT (STDMETHODCALLTYPE *Value)(ICoreWebView2Cookie *This, LPWSTR *value);
    void *SetValue_;
    HRESULT (STDMETHODCALLTYPE *Domain)(ICoreWebView2Cookie *This, LPWSTR *domain);
    HRESULT (STDMETHODCALLTYPE *Path)(ICoreWebView2Cookie *This, LPWSTR *path);
    void *Expires_;
    void *SetExpires_;
    void *IsHttpOnly_;
    void *SetIsHttpOnly_;
    void *SameSite_;
    void *SetSameSite_;
    void *IsSecure_;
    void *SetIsSecure_;
    void *IsSession_;
} ICoreWebView2CookieVtbl;
struct ICoreWebView2Cookie { const ICoreWebView2CookieVtbl *lpVtbl; };

typedef struct ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *This, HRESULT errorCode, ICoreWebView2Environment *env);
} ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl;
struct ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler { const ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl *lpVtbl; };

typedef struct ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *This, HRESULT errorCode, ICoreWebView2Controller *controller);
} ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl;
struct ICoreWebView2CreateCoreWebView2ControllerCompletedHandler { const ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl *lpVtbl; };

typedef struct ICoreWebView2SourceChangedEventHandlerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2SourceChangedEventHandler *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2SourceChangedEventHandler *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2SourceChangedEventHandler *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(ICoreWebView2SourceChangedEventHandler *This, ICoreWebView2 *sender, void *args);
} ICoreWebView2SourceChangedEventHandlerVtbl;
struct ICoreWebView2SourceChangedEventHandler { const ICoreWebView2SourceChangedEventHandlerVtbl *lpVtbl; };

typedef struct ICoreWebView2GetCookiesCompletedHandlerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ICoreWebView2GetCookiesCompletedHandler *This, REFIID riid, void **ppv);
    ULONG (STDMETHODCALLTYPE *AddRef)(ICoreWebView2GetCookiesCompletedHandler *This);
    ULONG (STDMETHODCALLTYPE *Release)(ICoreWebView2GetCookiesCompletedHandler *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(ICoreWebView2GetCookiesCompletedHandler *This, HRESULT errorCode, ICoreWebView2CookieList *list);
} ICoreWebView2GetCookiesCompletedHandlerVtbl;
struct ICoreWebView2GetCookiesCompletedHandler { const ICoreWebView2GetCookiesCompletedHandlerVtbl *lpVtbl; };

static const GUID IID_ICoreWebView2_2 = {0x9e8f0cf8,0xe670,0x4b5e,{0xb2,0xbc,0x73,0xe0,0x61,0xe3,0x18,0x4c}};
static const GUID IID_ICoreWebView2Settings2 = {0xee9a0f68,0xf46c,0x4e32,{0xac,0x23,0xef,0x8c,0xac,0x22,0x4d,0x2a}};

typedef HRESULT (STDAPICALLTYPE *PFN_CreateCoreWebView2EnvironmentWithOptions)(PCWSTR browserExecutableFolder, PCWSTR userDataFolder, void *environmentOptions, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *handler);
#endif
