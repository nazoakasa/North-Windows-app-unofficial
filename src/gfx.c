#include "gfx.h"

int g_dpi = 96;
TF F_BODY, F_BOLD, F_SMALL, F_SMALLB, F_TITLE, F_BIG, F_NAV, F_NAVB, F_TAG, F_BODYI, F_BOLDI;
HDC g_mdc;
HDC P_dc;
GpGraphics *P_g;
LinkCb g_link_cb;

static wchar_t g_face[LF_FACESIZE] = L"Segoe UI";

/* --------------------------------------------------------------- フォント */
static void tf_make(TF *t, int px, int weight, int italic)
{
    if (t->f) DeleteObject(t->f);
    LOGFONTW lf;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = -PX(px);
    lf.lfWeight = weight;
    lf.lfItalic = (BYTE)italic;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy(lf.lfFaceName, g_face);
    t->f = CreateFontIndirectW(&lf);
    HGDIOBJ old = SelectObject(g_mdc, t->f);
    TEXTMETRICW tm;
    GetTextMetricsW(g_mdc, &tm);
    SelectObject(g_mdc, old);
    t->h = tm.tmHeight;
    t->lh = PX(px + 5);
    if (!t->w) t->w = (short *)malloc(65536 * sizeof(short));
    memset(t->w, 0xFF, 65536 * sizeof(short));
}

void gfx_fonts(void)
{
    static const wchar_t *cand[] = { L"Yu Gothic UI", L"Meiryo UI", L"Segoe UI", L"MS UI Gothic" };
    for (int i = 0; i < 4; i++) {
        LOGFONTW lf;
        memset(&lf, 0, sizeof lf);
        lf.lfHeight = -16;
        wcscpy(lf.lfFaceName, cand[i]);
        HFONT f = CreateFontIndirectW(&lf);
        HGDIOBJ old = SelectObject(g_mdc, f);
        wchar_t got[LF_FACESIZE] = L"";
        GetTextFaceW(g_mdc, LF_FACESIZE, got);
        SelectObject(g_mdc, old);
        DeleteObject(f);
        if (_wcsicmp(got, cand[i]) == 0) { wcscpy(g_face, cand[i]); break; }
    }
    tf_make(&F_BODY, 15, FW_NORMAL, 0);
    tf_make(&F_BOLD, 15, FW_BOLD, 0);
    tf_make(&F_SMALL, 13, FW_NORMAL, 0);
    tf_make(&F_SMALLB, 13, FW_BOLD, 0);
    tf_make(&F_TITLE, 20, FW_BOLD, 0);
    tf_make(&F_BIG, 20, FW_BOLD, 0);
    tf_make(&F_NAV, 19, FW_NORMAL, 0);
    tf_make(&F_NAVB, 19, FW_BOLD, 0);
    tf_make(&F_TAG, 15, FW_BOLD, 0);
    tf_make(&F_BODYI, 15, FW_NORMAL, 1);
    tf_make(&F_BOLDI, 15, FW_BOLD, 1);
    F_BODY.v_b = &F_BOLD; F_BODY.v_i = &F_BODYI; F_BODY.v_bi = &F_BOLDI;
    F_TITLE.lh = PX(26); F_BIG.lh = PX(26); F_NAV.lh = PX(26); F_NAVB.lh = PX(26);
}

void gfx_init(void)
{
    HDC s = GetDC(NULL);
    g_mdc = CreateCompatibleDC(s);
    ReleaseDC(NULL, s);
    SetBkMode(g_mdc, TRANSPARENT);
    gfx_fonts();
}

void gfx_shutdown(void)
{
    TF *all[] = { &F_BODY, &F_BOLD, &F_SMALL, &F_SMALLB, &F_TITLE, &F_BIG, &F_NAV, &F_NAVB, &F_TAG, &F_BODYI, &F_BOLDI };
    for (int i = 0; i < 11; i++) { if (all[i]->f) DeleteObject(all[i]->f); free(all[i]->w); }
    DeleteDC(g_mdc);
}

void gfx_begin(HDC dc)
{
    P_dc = dc;
    GdipCreateFromHDC(dc, &P_g);
    GdipSetSmoothingMode(P_g, 4);
    GdipSetPixelOffsetMode(P_g, 4);
    GdipSetInterpolationMode(P_g, 7);
    SetBkMode(dc, TRANSPARENT);
}
void gfx_end(void)
{
    GdipFlush(P_g, 1);
    GdipDeleteGraphics(P_g);
    P_g = NULL;
    P_dc = NULL;
}

/* ------------------------------------------------------------ 図形 */
#define ARGB(a, c) ((UINT32)(((a) << 24) | (GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c)))

static GpBrush *brush(COLORREF c, int a)
{
    static struct { UINT32 k; GpBrush *b; } cache[32];
    static int n;
    UINT32 k = ARGB(a, c);
    for (int i = 0; i < 32 && i < n; i++) if (cache[i].k == k) return cache[i].b;
    int slot = n % 32;
    if (n >= 32) GdipDeleteBrush(cache[slot].b);
    GpBrush *b = NULL;
    GdipCreateSolidFill(k, &b);
    cache[slot].k = k; cache[slot].b = b;
    n++;
    return b;
}
static GpPen *pen(COLORREF c, float w)
{
    static struct { UINT32 k; int w; GpPen *p; } cache[32];
    static int n;
    UINT32 k = ARGB(255, c);
    int wk = (int)(w * 8);
    for (int i = 0; i < 32 && i < n; i++) if (cache[i].k == k && cache[i].w == wk) return cache[i].p;
    int slot = n % 32;
    if (n >= 32) GdipDeletePen(cache[slot].p);
    GpPen *p = NULL;
    GdipCreatePen1(k, w, 2, &p);
    GdipSetPenStartCap(p, 2);
    GdipSetPenEndCap(p, 2);
    GdipSetPenLineJoin(p, 2);
    cache[slot].k = k; cache[slot].w = wk; cache[slot].p = p;
    n++;
    return p;
}

static GpPath *rrect_path(float x, float y, float w, float h, float r)
{
    GpPath *p = NULL;
    GdipCreatePath(0, &p);
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0.5f) {
        GpPointF q[4] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } };
        (void)q;
        r = 0.5f;
    }
    float d = r * 2;
    GdipAddPathArc(p, x, y, d, d, 180, 90);
    GdipAddPathArc(p, x + w - d, y, d, d, 270, 90);
    GdipAddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
    GdipAddPathArc(p, x, y + h - d, d, d, 90, 90);
    GdipClosePathFigure(p);
    return p;
}

void g_fill_circle(float cx, float cy, float r, COLORREF c, int a)
{
    GdipFillEllipse(P_g, brush(c, a), cx - r, cy - r, r * 2, r * 2);
}
void g_circle(float cx, float cy, float r, COLORREF c, float lw)
{
    GdipDrawEllipse(P_g, pen(c, lw), cx - r, cy - r, r * 2, r * 2);
}
void g_fill_rrect(float x, float y, float w, float h, float r, COLORREF c, int a)
{
    GpPath *p = rrect_path(x, y, w, h, r);
    GdipFillPath(P_g, brush(c, a), p);
    GdipDeletePath(p);
}
void g_stroke_rrect(float x, float y, float w, float h, float r, COLORREF c, float lw)
{
    GpPath *p = rrect_path(x, y, w, h, r);
    GdipDrawPath(P_g, pen(c, lw), p);
    GdipDeletePath(p);
}
void g_line(float x1, float y1, float x2, float y2, COLORREF c, float lw)
{
    GdipDrawLine(P_g, pen(c, lw), x1, y1, x2, y2);
}
void g_fill_poly(const GpPointF *p, int n, COLORREF c) { GdipFillPolygon(P_g, brush(c, 255), p, n, 0); }
void g_arc(float cx, float cy, float r, float start, float sweep, COLORREF c, float lw)
{
    GdipDrawArc(P_g, pen(c, lw), cx - r, cy - r, r * 2, r * 2, start, sweep);
}
void g_rect(int l, int t, int r, int b, COLORREF c)
{
    HBRUSH hb = CreateSolidBrush(c);
    RECT rc = { l, t, r, b };
    FillRect(P_dc, &rc, hb);
    DeleteObject(hb);
}
void g_hline(int x1, int x2, int y, COLORREF c) { g_rect(x1, y, x2, y + 1, c); }

void g_draw_img(Img *im, float x, float y, float w, float h, float r)
{
    GpPath *p = rrect_path(x, y, w, h, r);
    if (!im || im->state != 1) {
        GdipFillPath(P_g, brush(RGB(26, 34, 58), 255), p);
        GdipDeletePath(p);
        return;
    }
    UINT st = 0;
    GdipSaveGraphics(P_g, &st);
    GdipSetClipPath(P_g, p, 1);
    float sx = 0, sy = 0, sw = (float)im->w, sh = (float)im->h;
    float ar = w / h, iar = (float)im->w / (float)im->h;
    if (iar > ar) { sw = im->h * ar; sx = (im->w - sw) / 2; }
    else          { sh = im->w / ar; sy = (im->h - sh) / 2; }
    GdipDrawImageRectRectI(P_g, im->bmp, (int)(x + 0.5f), (int)(y + 0.5f), (int)(w + 0.5f), (int)(h + 0.5f),
                           (int)sx, (int)sy, (int)sw, (int)sh, 2, NULL, NULL, NULL);
    GdipRestoreGraphics(P_g, st);
    GdipDeletePath(p);
}

void g_draw_img_whole(Img *im, int x, int y, int w, int h)
{
    if (!im || im->state != 1 || w < 1 || h < 1) return;
    GdipDrawImageRectRectI(P_g, im->bmp, x, y, w, h, 0, 0, im->w, im->h, 2, NULL, NULL, NULL);
}

/* ------------------------------------------------------------ アイコン */
static void polyline(const GpPointF *p, int n, COLORREF c, float lw) { GdipDrawLines(P_g, pen(c, lw), p, n); }

void g_icon(int ic, float x, float y, float s, COLORREF c, int filled, float lw)
{
    GpPointF p[12];
#define PT(i, fx, fy) do { p[i].X = x + (fx) * s; p[i].Y = y + (fy) * s; } while (0)
#define FX(fx) (x + (fx) * s)
#define FY(fy) (y + (fy) * s)
    switch (ic) {
    case IC_HOME:
        PT(0, 0.5f, 0.08f); PT(1, 0.08f, 0.46f); PT(2, 0.2f, 0.46f); PT(3, 0.2f, 0.9f);
        PT(4, 0.42f, 0.9f); PT(5, 0.42f, 0.62f); PT(6, 0.58f, 0.62f); PT(7, 0.58f, 0.9f);
        PT(8, 0.8f, 0.9f); PT(9, 0.8f, 0.46f); PT(10, 0.92f, 0.46f); PT(11, 0.5f, 0.08f);
        if (filled) GdipFillPolygon(P_g, brush(c, 255), p, 11, 0);
        polyline(p, 12, c, lw);
        break;
    case IC_SEARCH:
        g_circle(FX(0.42f), FY(0.42f), 0.3f * s, c, lw);
        g_line(FX(0.64f), FY(0.64f), FX(0.9f), FY(0.9f), c, lw);
        break;
    case IC_BELL:
        GdipDrawArc(P_g, pen(c, lw), FX(0.22f), FY(0.1f), 0.56f * s, 0.56f * s, 180, 180);
        PT(0, 0.22f, 0.38f); PT(1, 0.2f, 0.7f); PT(2, 0.1f, 0.8f); PT(3, 0.9f, 0.8f); PT(4, 0.8f, 0.7f); PT(5, 0.78f, 0.38f);
        polyline(p, 6, c, lw);
        GdipDrawArc(P_g, pen(c, lw), FX(0.4f), FY(0.76f), 0.2f * s, 0.16f * s, 0, 180);
        if (filled) g_fill_circle(FX(0.5f), FY(0.5f), 0.22f * s, c, 255);
        break;
    case IC_MAIL:
        g_stroke_rrect(FX(0.08f), FY(0.2f), 0.84f * s, 0.62f * s, 0.08f * s, c, lw);
        PT(0, 0.1f, 0.27f); PT(1, 0.5f, 0.58f); PT(2, 0.9f, 0.27f);
        polyline(p, 3, c, lw);
        break;
    case IC_BOOKMARK:
        PT(0, 0.24f, 0.1f); PT(1, 0.76f, 0.1f); PT(2, 0.76f, 0.9f); PT(3, 0.5f, 0.68f); PT(4, 0.24f, 0.9f); PT(5, 0.24f, 0.1f);
        if (filled) GdipFillPolygon(P_g, brush(c, 255), p, 5, 0);
        polyline(p, 6, c, lw);
        break;
    case IC_LIST:
        g_line(FX(0.14f), FY(0.26f), FX(0.86f), FY(0.26f), c, lw);
        g_line(FX(0.14f), FY(0.5f), FX(0.86f), FY(0.5f), c, lw);
        g_line(FX(0.14f), FY(0.74f), FX(0.62f), FY(0.74f), c, lw);
        break;
    case IC_PERSON:
        g_circle(FX(0.5f), FY(0.3f), 0.2f * s, c, lw);
        GdipDrawArc(P_g, pen(c, lw), FX(0.14f), FY(0.56f), 0.72f * s, 0.8f * s, 180, 180);
        g_line(FX(0.14f), FY(0.96f), FX(0.86f), FY(0.96f), c, lw);
        if (filled) { g_fill_circle(FX(0.5f), FY(0.3f), 0.2f * s, c, 255); }
        break;
    case IC_MORE:
        g_fill_circle(FX(0.2f), FY(0.5f), 0.07f * s + lw * 0.3f, c, 255);
        g_fill_circle(FX(0.5f), FY(0.5f), 0.07f * s + lw * 0.3f, c, 255);
        g_fill_circle(FX(0.8f), FY(0.5f), 0.07f * s + lw * 0.3f, c, 255);
        break;
    case IC_REPLY:
        g_circle(FX(0.5f), FY(0.46f), 0.38f * s, c, lw);
        PT(0, 0.3f, 0.74f); PT(1, 0.2f, 0.94f); PT(2, 0.5f, 0.82f);
        polyline(p, 3, c, lw);
        break;
    case IC_RT:
        PT(0, 0.2f, 0.62f); PT(1, 0.2f, 0.34f); PT(2, 0.74f, 0.34f);
        polyline(p, 3, c, lw);
        PT(0, 0.6f, 0.18f); PT(1, 0.76f, 0.34f); PT(2, 0.6f, 0.5f);
        polyline(p, 3, c, lw);
        PT(0, 0.8f, 0.38f); PT(1, 0.8f, 0.66f); PT(2, 0.26f, 0.66f);
        polyline(p, 3, c, lw);
        PT(0, 0.4f, 0.5f); PT(1, 0.24f, 0.66f); PT(2, 0.4f, 0.82f);
        polyline(p, 3, c, lw);
        break;
    case IC_HEART: {
        GpPath *hp = NULL;
        GdipCreatePath(0, &hp);
        GdipAddPathBezier(hp, FX(0.5f), FY(0.88f), FX(0.1f), FY(0.62f), FX(0.02f), FY(0.34f), FX(0.2f), FY(0.2f));
        GdipAddPathBezier(hp, FX(0.2f), FY(0.2f), FX(0.36f), FY(0.08f), FX(0.5f), FY(0.2f), FX(0.5f), FY(0.3f));
        GdipAddPathBezier(hp, FX(0.5f), FY(0.3f), FX(0.5f), FY(0.2f), FX(0.64f), FY(0.08f), FX(0.8f), FY(0.2f));
        GdipAddPathBezier(hp, FX(0.8f), FY(0.2f), FX(0.98f), FY(0.34f), FX(0.9f), FY(0.62f), FX(0.5f), FY(0.88f));
        GdipClosePathFigure(hp);
        if (filled) GdipFillPath(P_g, brush(c, 255), hp);
        GdipDrawPath(P_g, pen(c, lw), hp);
        GdipDeletePath(hp);
        break; }
    case IC_SHARE:
        PT(0, 0.3f, 0.42f); PT(1, 0.18f, 0.42f); PT(2, 0.18f, 0.9f); PT(3, 0.82f, 0.9f); PT(4, 0.82f, 0.42f); PT(5, 0.7f, 0.42f);
        polyline(p, 6, c, lw);
        g_line(FX(0.5f), FY(0.68f), FX(0.5f), FY(0.1f), c, lw);
        PT(0, 0.3f, 0.28f); PT(1, 0.5f, 0.08f); PT(2, 0.7f, 0.28f);
        polyline(p, 3, c, lw);
        break;
    case IC_BACK:
        g_line(FX(0.9f), FY(0.5f), FX(0.14f), FY(0.5f), c, lw);
        PT(0, 0.42f, 0.22f); PT(1, 0.14f, 0.5f); PT(2, 0.42f, 0.78f);
        polyline(p, 3, c, lw);
        break;
    case IC_PEN:
        PT(0, 0.14f, 0.88f); PT(1, 0.26f, 0.56f); PT(2, 0.66f, 0.16f); PT(3, 0.84f, 0.34f); PT(4, 0.44f, 0.74f); PT(5, 0.14f, 0.88f);
        polyline(p, 6, c, lw);
        g_line(FX(0.56f), FY(0.26f), FX(0.74f), FY(0.44f), c, lw);
        break;
    case IC_CAL:
        g_stroke_rrect(FX(0.12f), FY(0.2f), 0.76f * s, 0.7f * s, 0.08f * s, c, lw);
        g_line(FX(0.12f), FY(0.42f), FX(0.88f), FY(0.42f), c, lw);
        g_line(FX(0.32f), FY(0.1f), FX(0.32f), FY(0.28f), c, lw);
        g_line(FX(0.68f), FY(0.1f), FX(0.68f), FY(0.28f), c, lw);
        break;
    case IC_PIN:
        g_circle(FX(0.5f), FY(0.36f), 0.22f * s, c, lw);
        PT(0, 0.3f, 0.5f); PT(1, 0.5f, 0.92f); PT(2, 0.7f, 0.5f);
        polyline(p, 3, c, lw);
        break;
    case IC_LINK:
        g_stroke_rrect(FX(0.06f), FY(0.34f), 0.52f * s, 0.32f * s, 0.16f * s, c, lw);
        g_stroke_rrect(FX(0.42f), FY(0.34f), 0.52f * s, 0.32f * s, 0.16f * s, c, lw);
        break;
    case IC_SPARK:
        PT(0, 0.5f, 0.04f); PT(1, 0.6f, 0.4f); PT(2, 0.96f, 0.5f); PT(3, 0.6f, 0.6f);
        PT(4, 0.5f, 0.96f); PT(5, 0.4f, 0.6f); PT(6, 0.04f, 0.5f); PT(7, 0.4f, 0.4f);
        GdipFillPolygon(P_g, brush(c, 255), p, 8, 0);
        break;
    case IC_CLOSE:
        g_line(FX(0.2f), FY(0.2f), FX(0.8f), FY(0.8f), c, lw);
        g_line(FX(0.8f), FY(0.2f), FX(0.2f), FY(0.8f), c, lw);
        break;
    case IC_PLAY:
        PT(0, 0.34f, 0.2f); PT(1, 0.8f, 0.5f); PT(2, 0.34f, 0.8f);
        GdipFillPolygon(P_g, brush(c, 255), p, 3, 0);
        break;
    case IC_IMAGE:
        g_stroke_rrect(FX(0.1f), FY(0.16f), 0.8f * s, 0.68f * s, 0.1f * s, c, lw);
        g_circle(FX(0.34f), FY(0.38f), 0.08f * s, c, lw);
        PT(0, 0.12f, 0.78f); PT(1, 0.38f, 0.54f); PT(2, 0.55f, 0.7f); PT(3, 0.7f, 0.5f); PT(4, 0.88f, 0.72f);
        polyline(p, 5, c, lw);
        break;
    case IC_EMOJI:
        g_circle(FX(0.5f), FY(0.5f), 0.4f * s, c, lw);
        g_fill_circle(FX(0.37f), FY(0.42f), 0.05f * s + lw * 0.2f, c, 255);
        g_fill_circle(FX(0.63f), FY(0.42f), 0.05f * s + lw * 0.2f, c, 255);
        GdipDrawArc(P_g, pen(c, lw), FX(0.3f), FY(0.4f), 0.4f * s, 0.32f * s, 20, 140);
        break;
    case IC_GLOBE:
        g_circle(FX(0.5f), FY(0.5f), 0.4f * s, c, lw);
        GdipDrawEllipse(P_g, pen(c, lw), FX(0.32f), FY(0.1f), 0.36f * s, 0.8f * s);
        g_line(FX(0.1f), FY(0.5f), FX(0.9f), FY(0.5f), c, lw);
        break;
    case IC_SEND:
        PT(0, 0.08f, 0.12f); PT(1, 0.94f, 0.5f); PT(2, 0.08f, 0.88f); PT(3, 0.26f, 0.5f);
        GdipFillPolygon(P_g, brush(c, 255), p, 4, 0);
        break;
    case IC_BADGE:
        g_fill_circle(FX(0.5f), FY(0.5f), 0.5f * s, c, 255);
        PT(0, 0.27f, 0.52f); PT(1, 0.43f, 0.68f); PT(2, 0.75f, 0.34f);
        polyline(p, 3, RGB(255, 255, 255), lw);
        break;
    }
}

/* -------------------------------------------------------------- テキスト */
static int is_tagch(wchar_t c)
{
    return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'_' || c > 0x7F;
}
static int is_hdl(wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'_'; }

/* attr: 0=通常 1=URL 2=ハッシュタグ 3=メンション */
void compute_attr(const wchar_t *s, int n, BYTE *attr)
{
    memset(attr, 0, (size_t)n);
    for (int i = 0; i < n;) {
        if ((n - i > 7 && wcsncmp(s + i, L"http://", 7) == 0) || (n - i > 8 && wcsncmp(s + i, L"https://", 8) == 0)) {
            int j = i;
            while (j < n && s[j] > L' ' && s[j] < 0x7F) j++;
            memset(attr + i, 1, (size_t)(j - i));
            i = j;
        } else if (s[i] == L'#' && (i == 0 || !is_tagch(s[i - 1])) && i + 1 < n && is_tagch(s[i + 1])) {
            int j = i + 1;
            while (j < n && is_tagch(s[j])) j++;
            memset(attr + i, 2, (size_t)(j - i));
            i = j;
        } else if (s[i] == L'@' && (i == 0 || !is_hdl(s[i - 1])) && i + 1 < n && is_hdl(s[i + 1])) {
            int j = i + 1;
            while (j < n && is_hdl(s[j])) j++;
            memset(attr + i, 3, (size_t)(j - i));
            i = j;
        } else i++;
    }
}

static int cwidth(HDC dc, TF *tf, wchar_t c)
{
    short w = tf->w[c];
    if (w >= 0) return w;
    SIZE sz;
    GetTextExtentPoint32W(dc, &c, 1, &sz);
    tf->w[c] = (short)sz.cx;
    return sz.cx;
}

int text_w(TF *tf, const wchar_t *s, int n)
{
    SIZE sz;
    HGDIOBJ old = SelectObject(g_mdc, tf->f);
    GetTextExtentPoint32W(g_mdc, s, n, &sz);
    SelectObject(g_mdc, old);
    return sz.cx;
}
int text_u8_w(TF *tf, const char *s)
{
    wchar_t *w = u8w(s);
    int r = text_w(tf, w, (int)wcslen(w));
    free(w);
    return r;
}

static int ascii_word(wchar_t c) { return c > L' ' && c < 0x7F; }

static TF *variant_of(TF *tf, int st)
{
    if (!tf->v_b) return tf;
    if ((st & 3) == 3) return tf->v_bi;
    if (st & 1) return tf->v_b;
    if (st & 2) return tf->v_i;
    return tf;
}

int text_flow_ex(TF *tf, const wchar_t *s, int len, const BYTE *attr, const BYTE *sty, const DWORD *clr,
                 int x, int y, int w, int maxlines, COLORREF col, int draw, int post_idx, int src_kind)
{
    HDC dc = draw ? P_dc : g_mdc;
    HGDIOBJ old = SelectObject(dc, tf->f);
    TF *cur = tf;
    int i = 0, lines = 0;
    int yoff = (tf->lh - tf->h) / 2;
    while (i < len && lines < maxlines) {
        int j = i, lw = 0, lb = -1, lbw = 0;
        while (j < len) {
            wchar_t c = s[j];
            if (c == L'\n') break;
            TF *v = variant_of(tf, sty ? sty[j] : 0);
            if (v != cur) { SelectObject(dc, v->f); cur = v; }
            int adv = 1, cw;
            if (c >= 0xD800 && c < 0xDC00 && j + 1 < len) {
                SIZE sz;
                GetTextExtentPoint32W(dc, s + j, 2, &sz);
                cw = sz.cx; adv = 2;
            } else cw = cwidth(dc, v, c);
            if (lw + cw > w && j > i) {
                if (lb > i && ascii_word(c) && ascii_word(s[j - 1])) { j = lb; lw = lbw; }
                break;
            }
            lw += cw; j += adv;
            if (c == L' ') { lb = j; lbw = lw; }
        }
        if (draw) {
            int cx = x, k = i;
            while (k < j) {
                int a = attr ? attr[k] : 0, st = sty ? sty[k] : 0;
                DWORD cl = clr ? clr[k] : 0;
                int e = k + 1;
                while (e < j && (attr ? attr[e] : 0) == a && (sty ? sty[e] : 0) == st && (clr ? clr[e] : 0) == cl) e++;
                TF *v = variant_of(tf, st);
                if (v != cur) { SelectObject(dc, v->f); cur = v; }
                SIZE sz;
                SetTextColor(dc, a ? C_ACCENT : ((cl & 0x01000000) ? (COLORREF)(cl & 0xFFFFFF) : col));
                TextOutW(dc, cx, y + lines * tf->lh + yoff, s + k, e - k);
                GetTextExtentPoint32W(dc, s + k, e - k, &sz);
                if (a && g_link_cb) {
                    int ts = k, te = e;
                    while (ts > 0 && attr[ts - 1] == a) ts--;
                    while (te < len && attr[te] == a) te++;
                    g_link_cb(cx, y + lines * tf->lh, sz.cx, tf->lh, post_idx, src_kind, ts, te - ts, a);
                }
                cx += sz.cx;
                k = e;
            }
        }
        i = j;
        if (i < len && s[i] == L'\n') i++;
        lines++;
    }
    SelectObject(dc, old);
    return lines * tf->lh;
}

int text_flow(TF *tf, const wchar_t *s, int len, const BYTE *attr, int x, int y, int w, int maxlines,
              COLORREF col, int draw, int post_idx, int src_kind)
{
    return text_flow_ex(tf, s, len, attr, NULL, NULL, x, y, w, maxlines, col, draw, post_idx, src_kind);
}

void text_at(TF *tf, int x, int y, const wchar_t *s, int n, COLORREF c)
{
    HGDIOBJ old = SelectObject(P_dc, tf->f);
    SetTextColor(P_dc, c);
    TextOutW(P_dc, x, y + (tf->lh - tf->h) / 2, s, n);
    SelectObject(P_dc, old);
}
void text_u8(TF *tf, int x, int y, const char *s, COLORREF c)
{
    wchar_t *w = u8w(s);
    text_at(tf, x, y, w, (int)wcslen(w), c);
    free(w);
}
void text_clipw(TF *tf, int x, int y, int maxw, const wchar_t *s, COLORREF c)
{
    HGDIOBJ old = SelectObject(P_dc, tf->f);
    SetTextColor(P_dc, c);
    RECT rc = { x, y, x + maxw, y + tf->lh };
    DrawTextW(P_dc, s, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(P_dc, old);
}
void text_clip(TF *tf, int x, int y, int maxw, const char *s, COLORREF c)
{
    wchar_t *w = u8w(s);
    text_clipw(tf, x, y, maxw, w, c);
    free(w);
}
