#ifndef GFX_H
#define GFX_H
#include "app.h"

extern int g_dpi;
#define PX(n) MulDiv((n), g_dpi, 96)

#define C_BG      RGB(10, 15, 32)
#define C_LINE    RGB(36, 44, 70)
#define C_TEXT    RGB(231, 233, 234)
#define C_GRAY    RGB(139, 152, 168)
#define C_ACCENT  RGB(232, 56, 72)
#define C_GREEN   RGB(0, 186, 124)
#define C_PANEL   RGB(18, 25, 48)

typedef struct TF { HFONT f; int h, lh; short *w; struct TF *v_b, *v_i, *v_bi; } TF;
extern TF F_BODY, F_BOLD, F_SMALL, F_SMALLB, F_TITLE, F_BIG, F_NAV, F_NAVB, F_TAG, F_BODYI, F_BOLDI;
extern HDC g_mdc;                  /* 計測用 */
extern HDC P_dc;
extern GpGraphics *P_g;

void gfx_init(void);
void gfx_fonts(void);              /* g_dpi 変更時にも呼ぶ */
void gfx_shutdown(void);
void gfx_begin(HDC dc);
void gfx_end(void);

void g_fill_circle(float cx, float cy, float r, COLORREF c, int a);
void g_fill_rrect(float x, float y, float w, float h, float r, COLORREF c, int a);
void g_stroke_rrect(float x, float y, float w, float h, float r, COLORREF c, float lw);
void g_line(float x1, float y1, float x2, float y2, COLORREF c, float lw);
void g_fill_poly(const GpPointF *p, int n, COLORREF c);
void g_arc(float cx, float cy, float r, float start, float sweep, COLORREF c, float lw);
void g_circle(float cx, float cy, float r, COLORREF c, float lw);
void g_draw_img(Img *im, float x, float y, float w, float h, float r);
void g_draw_img_whole(Img *im, int x, int y, int w, int h);   /* 画像全体を (x,y,w,h) に拡縮して描く(切り抜きなし) */
void g_rect(int l, int t, int r, int b, COLORREF c);
void g_hline(int x1, int x2, int y, COLORREF c);

enum { IC_HOME, IC_SEARCH, IC_BELL, IC_MAIL, IC_BOOKMARK, IC_LIST, IC_PERSON, IC_MORE, IC_REPLY, IC_RT,
       IC_HEART, IC_SHARE, IC_BACK, IC_PEN, IC_CAL, IC_PIN, IC_LINK, IC_SPARK, IC_CLOSE, IC_PLAY, IC_BADGE, IC_SEND, IC_IMAGE, IC_EMOJI, IC_GLOBE };
void g_icon(int ic, float x, float y, float s, COLORREF c, int filled, float lw);

typedef void (*LinkCb)(int x, int y, int w, int h, int post_idx, int src_kind, int tok_start, int tok_len, int attr);
extern LinkCb g_link_cb;

void compute_attr(const wchar_t *s, int n, BYTE *attr);
int text_w(TF *tf, const wchar_t *s, int n);
int text_flow(TF *tf, const wchar_t *s, int len, const BYTE *attr, int x, int y, int w, int maxlines,
              COLORREF col, int draw, int post_idx, int src_kind);
/* sty: bit0=太字 bit1=斜体 / clr: 0x01000000|COLORREF なら色指定あり */
int text_flow_ex(TF *tf, const wchar_t *s, int len, const BYTE *attr, const BYTE *sty, const DWORD *clr,
                 int x, int y, int w, int maxlines, COLORREF col, int draw, int post_idx, int src_kind);
void text_at(TF *tf, int x, int y, const wchar_t *s, int n, COLORREF c);
void text_u8(TF *tf, int x, int y, const char *s, COLORREF c);
int text_u8_w(TF *tf, const char *s);
void text_clip(TF *tf, int x, int y, int maxw, const char *s, COLORREF c);
void text_clipw(TF *tf, int x, int y, int maxw, const wchar_t *s, COLORREF c);
#endif
