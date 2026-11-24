#ifndef APP_H
#define APP_H
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "cJSON.h"

/* ---------------- GDI+ flat API (必要な分だけ手書き宣言) ---------------- */
typedef int GpStatus;
typedef struct GpGraphics GpGraphics;
typedef struct GpImage GpImage;
typedef GpImage GpBitmap;
typedef struct GpPen GpPen;
typedef struct GpBrush GpBrush;
typedef struct GpPath GpPath;
typedef struct { float X, Y; } GpPointF;
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread; BOOL SuppressExternalCodecs; } GdiplusStartupInput;
GpStatus WINAPI GdiplusStartup(ULONG_PTR *token, const GdiplusStartupInput *in, void *out);
void WINAPI GdiplusShutdown(ULONG_PTR token);
GpStatus WINAPI GdipCreateFromHDC(HDC, GpGraphics **);
GpStatus WINAPI GdipDeleteGraphics(GpGraphics *);
GpStatus WINAPI GdipFlush(GpGraphics *, int);
GpStatus WINAPI GdipSetSmoothingMode(GpGraphics *, int);
GpStatus WINAPI GdipSetInterpolationMode(GpGraphics *, int);
GpStatus WINAPI GdipSetPixelOffsetMode(GpGraphics *, int);
GpStatus WINAPI GdipSaveGraphics(GpGraphics *, UINT *);
GpStatus WINAPI GdipRestoreGraphics(GpGraphics *, UINT);
GpStatus WINAPI GdipSetClipPath(GpGraphics *, GpPath *, int);
GpStatus WINAPI GdipResetClip(GpGraphics *);
GpStatus WINAPI GdipCreateSolidFill(UINT32, GpBrush **);
GpStatus WINAPI GdipDeleteBrush(GpBrush *);
GpStatus WINAPI GdipCreatePen1(UINT32, float, int, GpPen **);
GpStatus WINAPI GdipDeletePen(GpPen *);
GpStatus WINAPI GdipSetPenStartCap(GpPen *, int);
GpStatus WINAPI GdipSetPenEndCap(GpPen *, int);
GpStatus WINAPI GdipSetPenLineJoin(GpPen *, int);
GpStatus WINAPI GdipFillEllipse(GpGraphics *, GpBrush *, float, float, float, float);
GpStatus WINAPI GdipDrawEllipse(GpGraphics *, GpPen *, float, float, float, float);
GpStatus WINAPI GdipDrawLine(GpGraphics *, GpPen *, float, float, float, float);
GpStatus WINAPI GdipDrawArc(GpGraphics *, GpPen *, float, float, float, float, float, float);
GpStatus WINAPI GdipDrawLines(GpGraphics *, GpPen *, const GpPointF *, int);
GpStatus WINAPI GdipFillPolygon(GpGraphics *, GpBrush *, const GpPointF *, int, int);
GpStatus WINAPI GdipFillRectangle(GpGraphics *, GpBrush *, float, float, float, float);
GpStatus WINAPI GdipCreatePath(int, GpPath **);
GpStatus WINAPI GdipDeletePath(GpPath *);
GpStatus WINAPI GdipAddPathArc(GpPath *, float, float, float, float, float, float);
GpStatus WINAPI GdipAddPathBezier(GpPath *, float, float, float, float, float, float, float, float);
GpStatus WINAPI GdipClosePathFigure(GpPath *);
GpStatus WINAPI GdipFillPath(GpGraphics *, GpBrush *, GpPath *);
GpStatus WINAPI GdipDrawPath(GpGraphics *, GpPen *, GpPath *);
GpStatus WINAPI GdipCreateBitmapFromStream(IStream *, GpBitmap **);
GpStatus WINAPI GdipCreateBitmapFromScan0(int, int, int, int, BYTE *, GpBitmap **);
GpStatus WINAPI GdipGetImageWidth(GpImage *, UINT *);
GpStatus WINAPI GdipGetImageHeight(GpImage *, UINT *);
GpStatus WINAPI GdipGetImageGraphicsContext(GpImage *, GpGraphics **);
GpStatus WINAPI GdipDrawImageRectRectI(GpGraphics *, GpImage *, int, int, int, int, int, int, int, int, int, void *, void *, void *);
GpStatus WINAPI GdipDisposeImage(GpImage *);

/* ---------------- メッセージ / タグ ---------------- */
#define WM_APP_LOGIN_DONE (WM_APP + 1)
#define WM_APP_API (WM_APP + 10)
#define WM_APP_IMG (WM_APP + 11)

enum {
    T_ME = 1, T_HOME, T_HOME_MORE, T_TRENDS, T_SUGG, T_UNREAD_N, T_UNREAD_D, T_NEWCOUNT,
    T_PROFILE, T_PROFTL, T_LIKE, T_RT, T_BM, T_POST, T_LOGOUT,
    T_NOTIFS, T_NOTIF_READ, T_SHIST, T_SHIST_ADD, T_SHIST_DEL, T_SUGGEST, T_SEARCH,
    T_CONVS, T_CHAT, T_CHAT_SEND, T_CHAT_READ, T_TYPING_GET, T_TYPING_SET, T_FOLLOW, T_DM_CREATE,
    T_THREAD, T_THREAD_REPLY, T_BOOKMARKS, T_MEDIA,
    T_VOTE, T_DELETE, T_PIN, T_POLICY, T_STATS
};

typedef struct { int tag, status; char *body; char *ctx; int arg; } ApiRes;

/* ---------------- データモデル ---------------- */
typedef struct { char handle[80], name[120], avatar[200]; int verified; } UserLite;
typedef struct {
    char id[40], handle[80], name[120], bio[1200], location[100], website[200];
    char avatar[200], header[200], created[40];
    int followers, following, posts, verified;
    int is_following, follow_req, prot;
} User;
typedef struct { char url[200], thumb[200]; int w, h, kind; } Media;

typedef struct { char id[40], label[104]; int votes, pct; } PollOpt;

typedef struct Post {
    char id[40], created[40], edited[40];
    char *text; wchar_t *wtext; BYTE *attr; int wlen;
    BYTE *sty; DWORD *clr;                 /* 書式タグ由来のスタイル(太字/斜体/色) */
    int policy;                            /* 返信制限 0=全員 1=フォロー中 2=メンション */
    int has_poll, npopt, p_total, p_ended; PollOpt popt[4];
    char poll_id[40], p_viewer[40], p_ends[40];
    UserLite au;
    char rt_name[120], rt_handle[80];
    int pinned;
    int likes, rts, replies, liked, rted, bm;
    Media media[4]; int nmedia;
    char lp_url[400], lp_title[300], lp_domain[100], lp_image[400];
    char ctx_text[160]; int ctx_icon;      /* 文脈行(通知用) ctx_icon: IC_* + 1 */
    int hasq; UserLite qau; char q_id[40]; wchar_t *q_wtext; BYTE *q_attr; int q_wlen; BYTE *q_sty; DWORD *q_clr;
    /* layout cache */
    int lay_w, y, h;
    int l_poll, l_ctx, l_head, l_text, l_texth, l_edit, l_media, l_mediah, l_card, l_cardh, l_card_imgh, l_quote, l_quoteh, l_qtexth, l_act;
} Post;

/* ---------------- net.c ---------------- */
typedef struct Img {
    char *url; int maxdim;
    volatile LONG state;     /* 0=pending 1=ready 2=failed */
    GpBitmap *bmp; int w, h;
    struct Img *next;
} Img;

extern char g_ua[512], g_cookie[4096];
extern wchar_t g_demo[MAX_PATH];
extern int g_is_demo;
extern HWND g_main;

void net_init(void);
void net_shutdown(void);
void net_reopen(void);
void net_api(int tag, const char *method, const char *path, const char *body, const char *ctx, int arg);
void net_api_hdr(int tag, const char *method, const char *path, const char *body, const char *ctx, int arg, const char *hdr);
void net_upload(int tag, const wchar_t *file, const char *ctx, int arg);
Img *img_get(const char *url, int maxdim);
void img_release(const char *url, int maxdim);
int session_save(void);
int session_load(void);
void session_clear(void);

/* ---------------- util.c ---------------- */
wchar_t *u8w(const char *s);
char *wu8(const wchar_t *w);
char *dupstr(const char *s);
void data_dir(wchar_t *out, size_t n);
void jcopy(char *dst, size_t n, const cJSON *obj, const char *key);
int jint(const cJSON *obj, const char *key);
int jbool(const cJSON *obj, const char *key);
void fmt_rel_time(const char *iso, wchar_t *out, int n);
void fmt_full_date(const char *iso, wchar_t *out, int n, int with_day);
void fmt_count(int v, wchar_t *out, int n);
void fmt_remaining(const char *iso, wchar_t *out, int n);

/* ---------------- stats.c (利用者数の集計。同意制) ---------------- */
int stats_configured(void);
int stats_consent(void);                 /* -1=未選択 0=拒否 1=同意 */
void stats_on_login(const char *user_id);
void stats_on_result(int status, int arg);
void stats_toggle(void);                 /* 同意のオン/オフ切り替え (オフ時は削除依頼を送る) */
void stats_hash_user(const char *user_id, char out[65]);
void stats_hash_device(char out[65]);

/* ---------------- login.c ---------------- */
void open_login(void);

/* ---------------- ui.c ---------------- */
LRESULT CALLBACK main_proc(HWND, UINT, WPARAM, LPARAM);
void ui_startup(void);
void ui_toast(const wchar_t *s);
void ui_debug_attach(const wchar_t *path);
void ui_debug_poll(void);
void ui_debug_lightbox(void);
void ui_debug(const char *profile, int drawer, int scroll, const char *view, const char *q);

#endif
