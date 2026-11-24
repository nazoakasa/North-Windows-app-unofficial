/* ui.c -- メイン画面 (Twitter 風 / レスポンシブ)。GDI + GDI+ の自前描画。 */
#include "gfx.h"
#include <shellapi.h>
#include <wingdi.h>
#include <commdlg.h>

#define APP_VER L"client v0.6"

HWND g_main;
enum { ED_POST, ED_SEARCH, ED_DM, ED_OPT0, ED_OPT1, ED_OPT2, ED_OPT3, NED };
static HWND g_ed[NED];
static WNDPROC g_ed_old[NED];
#define g_edit g_ed[ED_POST]

/* ------------------------------------------------------------ 状態 */
typedef struct {
    Post **p; int n, cap;
    char cursor[64];
    int loading, loaded;
} Feed;
static Feed g_home, g_nfeed, g_sfeed, g_bfeed, g_tfeed;
static struct {
    User u; int have; char handle[80]; int tab;
    Feed feed;
    wchar_t *bio_w; BYTE *bio_a; int bio_len;
} g_prof;
static User g_me;
static int g_have_me;

enum { V_HOME, V_PROFILE, V_NOTIF, V_SEARCH, V_DM, V_CHAT, V_THREAD, V_BM };
static int g_view = V_HOME, g_back_to = V_HOME;
static int g_scroll[8];
enum { AT_POST, AT_THR, AT_DM, NAT };
static int att_h(int c);
static int g_policy;                                   /* 0=全員 1=フォロー中 2=メンション */
static int g_poll_on, g_poll_n = 2, g_poll_d = 1, g_poll_h, g_poll_m;
static void poll_reset(void);
static void ed_text_u8(int idx, char *out, size_t n);
static int g_thr_main = -1, g_thr_scroll_main;
static char g_thr_id[40];
static int g_drawer;
static wchar_t g_toast[160];
static int g_toast_on;
static int g_unread_n, g_unread_d, g_newcount;
static char g_newest[48];
static struct { char tag[120]; int count, is_hash; } g_trends[10];
static int g_ntrends;
static UserLite g_sugg[5];
static int g_nsugg;
static char g_reply_id[40], g_reply_handle[80];
static int g_img_dirty;
static int g_login_prompted;

enum { NAV_HOME, NAV_SEARCH, NAV_NOTIF, NAV_MSG, NAV_BM, NAV_LIST, NAV_PROFILE, NAV_MORE, NAV_SETTINGS, NAV_LOGOUT, NAV_STATS };
static const struct { int ic; const wchar_t *label; int act; } NAVS[] = {
    { IC_HOME, L"\u30DB\u30FC\u30E0", NAV_HOME },
    { IC_SEARCH, L"\u8A71\u984C\u3092\u691C\u7D22", NAV_SEARCH },
    { IC_BELL, L"\u901A\u77E5", NAV_NOTIF },
    { IC_MAIL, L"\u30E1\u30C3\u30BB\u30FC\u30B8", NAV_MSG },
    { IC_BOOKMARK, L"\u30D6\u30C3\u30AF\u30DE\u30FC\u30AF", NAV_BM },
    { IC_LIST, L"\u30EA\u30B9\u30C8", NAV_LIST },
    { IC_PERSON, L"\u30D7\u30ED\u30D5\u30A3\u30FC\u30EB", NAV_PROFILE },
    { IC_MORE, L"\u3082\u3063\u3068\u898B\u308B", NAV_MORE },
};
#define NNAV 8
static const wchar_t *TAB_LABEL[4] = { L"\u30DD\u30B9\u30C8", L"\u30DD\u30B9\u30C8\u3068\u8FD4\u4FE1", L"\u30E1\u30C7\u30A3\u30A2", L"\u3044\u3044\u306D" };
static const char *TAB_KEY[4] = { "tweets", "replies", "media", "likes" };

/* ------------------------------------------------------------ ヒット領域 */
enum { H_NAV = 1, H_POSTBTN, H_COMPOSE_BTN, H_AVATAR, H_LIKE, H_RT, H_REPLY, H_BM, H_SHARE, H_MORE, H_LINK, H_MEDIA,
       H_CARD, H_QUOTE, H_TAB, H_BACK, H_NEWBANNER, H_FAB, H_ME_AVATAR, H_ACCOUNT, H_DRAWER_ITEM, H_DRAWER_BG,
       H_TREND, H_SUGG, H_SEARCH, H_PROF_BTN, H_REPLY_CLR, H_PROFILE_HANDLE, H_DRAWER_PANEL,
       H_ACTOR, H_NOTIF, H_NTAB, H_STAB, H_PERSON, H_SUGU, H_SUGT, H_SUGQ, H_HIST, H_HIST_DEL,
       H_DM_REQ, H_CONV, H_CHAT_OLDER, H_DMMEDIA, H_DM_SEND, H_FOLLOW, H_DM_BTN, H_AVATAR_CHAT, H_POSTBODY, H_THR_SEND, H_ATTACH, H_ATT_DEL, H_VOTE,
       H_POLICY, H_POLL_TOG, H_POLL_OFF, H_OPT_ADD, H_OPT_DEL, H_POLL_D, H_POLL_H, H_POLL_M, H_BOLD, H_ITALIC, H_EMOJI,
       H_LB_BG, H_LB_IMG, H_LB_CLOSE, H_LB_PREV, H_LB_NEXT, H_LB_WEB, H_PROF_AVATAR, H_PROF_HEADER, H_ATT_VIEW };
typedef struct { RECT r; int type, a, b, c, d, e; } Hit;
static Hit g_hits[4000];
static int g_nhit;
static RECT g_hclip;

static void hit(int l, int t, int r, int b, int type, int a, int bb)
{
    if (g_nhit >= 4000) return;
    RECT x = { l, t, r, b };
    if (x.left < g_hclip.left) x.left = g_hclip.left;
    if (x.right > g_hclip.right) x.right = g_hclip.right;
    if (x.top < g_hclip.top) x.top = g_hclip.top;
    if (x.bottom > g_hclip.bottom) x.bottom = g_hclip.bottom;
    if (x.right <= x.left || x.bottom <= x.top) return;
    Hit *h = &g_hits[g_nhit++];
    h->r = x; h->type = type; h->a = a; h->b = bb; h->c = h->d = h->e = 0;
}
static void link_cb(int x, int y, int w, int h, int pi, int sk, int ts, int tl, int attr)
{
    if (g_nhit >= 4000) return;
    int before = g_nhit;
    hit(x, y, x + w, y + h, H_LINK, pi, sk);
    if (g_nhit > before) { g_hits[before].c = ts; g_hits[before].d = tl; g_hits[before].e = attr; }
}

/* ------------------------------------------------------------ ジオメトリ */
typedef struct { int W, H, mode, navx, navw, cx, cw, rx, rw, top_h, bot_h, vy0, vy1; } Geo;
static Geo G;

static void calc_geo(int W, int H)
{
    G.W = W; G.H = H;
    if (W >= PX(1060)) {
        G.mode = 2; G.navw = PX(250); G.rw = PX(330);
        G.cw = W - G.navw - G.rw; if (G.cw > PX(600)) G.cw = PX(600);
        int total = G.navw + G.cw + G.rw, ox = (W - total) / 2;
        G.navx = ox; G.cx = ox + G.navw; G.rx = G.cx + G.cw;
    } else if (W >= PX(700)) {
        G.mode = 1; G.navw = PX(84); G.rw = 0;
        G.cw = W - G.navw; if (G.cw > PX(600)) G.cw = PX(600);
        int total = G.navw + G.cw, ox = (W - total) / 2;
        G.navx = ox; G.cx = ox + G.navw; G.rx = G.cx + G.cw;
    } else {
        G.mode = 0; G.navw = 0; G.rw = 0; G.navx = 0; G.cx = 0; G.cw = W; G.rx = W;
    }
    G.top_h = PX(53);
    int inbar = g_view == V_CHAT || g_view == V_THREAD;
    G.bot_h = (G.mode == 0 && !inbar) ? PX(56) : 0;
    G.vy0 = G.top_h; G.vy1 = H - G.bot_h - (inbar ? PX(68) + att_h(g_view == V_THREAD ? AT_THR : AT_DM) : 0);
}

/* ------------------------------------------------------------ モデル */
static void ulite(UserLite *u, const cJSON *a)
{
    jcopy(u->handle, sizeof u->handle, a, "handle");
    jcopy(u->name, sizeof u->name, a, "name");
    jcopy(u->avatar, sizeof u->avatar, a, "avatarOriginalUrl");
    if (!u->avatar[0]) jcopy(u->avatar, sizeof u->avatar, a, "avatarUrl");
    u->verified = jbool(a, "verified");
}
static void parse_user(User *u, const cJSON *a)
{
    memset(u, 0, sizeof *u);
    jcopy(u->id, sizeof u->id, a, "id");
    jcopy(u->handle, sizeof u->handle, a, "handle");
    jcopy(u->name, sizeof u->name, a, "name");
    jcopy(u->bio, sizeof u->bio, a, "bio");
    jcopy(u->location, sizeof u->location, a, "location");
    jcopy(u->website, sizeof u->website, a, "website");
    jcopy(u->avatar, sizeof u->avatar, a, "avatarOriginalUrl");
    if (!u->avatar[0]) jcopy(u->avatar, sizeof u->avatar, a, "avatarUrl");
    jcopy(u->header, sizeof u->header, a, "headerUrl");
    jcopy(u->created, sizeof u->created, a, "createdAt");
    u->followers = jint(a, "followerCount");
    u->following = jint(a, "followingCount");
    u->posts = jint(a, "tweetCount");
    if (!u->posts) u->posts = jint(a, "postCount");
    u->verified = jbool(a, "verified");
    u->is_following = jbool(a, "following");
    u->follow_req = jbool(a, "followRequested");
    u->prot = jbool(a, "protected");
}
static wchar_t *wide_nocr(const char *s, int *len)
{
    wchar_t *w = u8w(s);
    int j = 0;
    for (int i = 0; w[i]; i++) if (w[i] != L'\r') w[j++] = w[i];
    w[j] = 0;
    *len = j;
    return w;
}
static void set_text(const char *s, wchar_t **w, BYTE **a, int *n)
{
    *w = wide_nocr(s, n);
    *a = (BYTE *)malloc((size_t)(*n + 1));
    compute_attr(*w, *n, *a);
}

/* ---- 書式タグ: <太字> <斜体> <色=赤> (bold/italic/b/i/color も可) ---- */
static int cf_char(wchar_t c) { return c == 0x200B || (c >= 0x200C && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x2064) || c == 0xFEFF || c == 0x00AD; }
static int color_lookup(const wchar_t *v, int n, COLORREF *out)
{
    static const struct { const wchar_t *k; unsigned rgb; } T[] = {
        { L"赤", 0xf4212e }, { L"青", 0x1d9bf0 }, { L"緑", 0x00ba7c }, { L"黄", 0xffd400 }, { L"橙", 0xff7a00 }, { L"紫", 0x7856ff },
        { L"桃", 0xf91880 }, { L"水", 0x5ac8fa }, { L"白", 0xffffff }, { L"黒", 0x000000 }, { L"灰", 0x8b98a5 }, { L"金", 0xd4af37 }, { L"銀", 0xc0c0c0 },
        { L"red", 0xf4212e }, { L"blue", 0x1d9bf0 }, { L"green", 0x00ba7c }, { L"yellow", 0xffd400 }, { L"orange", 0xff7a00 },
        { L"purple", 0x7856ff }, { L"pink", 0xf91880 }, { L"skyblue", 0x5ac8fa }, { L"white", 0xffffff }, { L"black", 0x000000 },
        { L"gray", 0x8b98a5 }, { L"grey", 0x8b98a5 }, { L"gold", 0xd4af37 }, { L"silver", 0xc0c0c0 } };
    while (n > 0 && v[0] == L' ') { v++; n--; }
    while (n > 0 && v[n - 1] == L' ') n--;
    if (n <= 0 || n > 24) return 0;
    int off = v[0] == L'#' ? 1 : 0;
    if (n - off == 6) {
        unsigned x = 0; int ok = 1;
        for (int i = off; i < n; i++) {
            wchar_t c = v[i]; int d = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
            if (d < 0) { ok = 0; break; }
            x = x * 16 + (unsigned)d;
        }
        if (ok) { *out = RGB((x >> 16) & 255, (x >> 8) & 255, x & 255); return 1; }
    }
    wchar_t low[32];
    for (int i = 0; i < n; i++) low[i] = (wchar_t)towlower(v[i]);
    low[n] = 0;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (!wcscmp(low, T[i].k)) { *out = RGB((T[i].rgb >> 16) & 255, (T[i].rgb >> 8) & 255, T[i].rgb & 255); return 1; }
    return 0;
}
/* 戻り値: タグの長さ(0なら通常の文字)。kind: 1=太字 2=斜体 3=色 */
static int parse_tag(const wchar_t *w, int n, int i, int *kind, int *closing, COLORREF *col)
{
    int j = i + 1;
    if (j >= n) return 0;
    *closing = w[j] == L'/';
    if (*closing) j++;
    while (j < n && cf_char(w[j])) j++;
    int ns = j;
    while (j < n && w[j] != L'<' && w[j] != L'>' && w[j] != L'=' && w[j] != L'/' && !iswspace(w[j]) && !cf_char(w[j])) j++;
    int nl = j - ns;
    if (nl < 1 || nl > 10) return 0;
    wchar_t name[12];
    for (int k = 0; k < nl; k++) name[k] = (wchar_t)towlower(w[ns + k]);
    name[nl] = 0;
    int kd;
    if (!wcscmp(name, L"色") || !wcscmp(name, L"color")) kd = 3;
    else if (!wcscmp(name, L"太字") || !wcscmp(name, L"bold") || !wcscmp(name, L"b")) kd = 1;
    else if (!wcscmp(name, L"斜体") || !wcscmp(name, L"italic") || !wcscmp(name, L"i")) kd = 2;
    else return 0;
    while (j < n && cf_char(w[j])) j++;
    while (j < n && iswspace(w[j])) j++;
    int hasval = 0, vs = 0, ve = 0;
    if (j < n && w[j] == L'=') {
        hasval = 1; j++; vs = j;
        while (j < n && w[j] != L'<' && w[j] != L'>') j++;
        ve = j;
    }
    if (j >= n || w[j] != L'>') return 0;
    if (*closing && hasval) return 0;
    if (kd == 3 && !*closing) { if (!hasval || !color_lookup(w + vs, ve - vs, col)) return 0; }
    else if (hasval) return 0;
    *kind = kd;
    return j - i + 1;
}
static wchar_t *parse_markup(const char *src, BYTE **sty, DWORD **clr, int *n)
{
    int rn;
    wchar_t *w = wide_nocr(src, &rn);
    wchar_t *out = (wchar_t *)malloc((size_t)(rn + 1) * sizeof(wchar_t));
    BYTE *os = (BYTE *)calloc((size_t)rn + 1, 1);
    DWORD *oc = (DWORD *)calloc((size_t)rn + 1, sizeof(DWORD));
    struct { int kind; COLORREF col; } st[32];
    int sp = 0, m = 0;
    for (int i = 0; i < rn;) {
        if (w[i] == L'<') {
            int kd = 0, cl = 0, l;
            COLORREF col = 0;
            if ((l = parse_tag(w, rn, i, &kd, &cl, &col)) > 0) {
                if (!cl) { if (sp < 32) { st[sp].kind = kd; st[sp].col = col; sp++; } }
                else for (int k = sp - 1; k >= 0; k--) if (st[k].kind == kd) { memmove(&st[k], &st[k + 1], (size_t)(sp - k - 1) * sizeof st[0]); sp--; break; }
                i += l;
                continue;
            }
        }
        BYTE b = 0; DWORD c = 0;
        for (int k = 0; k < sp; k++) { if (st[k].kind == 1) b |= 1; else if (st[k].kind == 2) b |= 2; else c = 0x01000000u | (DWORD)st[k].col; }
        out[m] = w[i]; os[m] = b; oc[m] = c; m++; i++;
    }
    out[m] = 0;
    free(w);
    *sty = os; *clr = oc; *n = m;
    return out;
}
static void set_text_md(const char *s, wchar_t **w, BYTE **a, BYTE **sty, DWORD **clr, int *n)
{
    *w = parse_markup(s, sty, clr, n);
    *a = (BYTE *)malloc((size_t)(*n + 1));
    compute_attr(*w, *n, *a);
}
static void parse_poll(Post *p, const cJSON *po)
{
    const cJSON *o;
    p->has_poll = 1; p->npopt = 0;
    jcopy(p->poll_id, sizeof p->poll_id, po, "id");
    jcopy(p->p_viewer, sizeof p->p_viewer, po, "viewerOptionId");
    jcopy(p->p_ends, sizeof p->p_ends, po, "endsAt");
    p->p_total = jint(po, "totalVotes");
    p->p_ended = jbool(po, "ended");
    cJSON_ArrayForEach(o, cJSON_GetObjectItemCaseSensitive(po, "options")) {
        if (p->npopt >= 4) break;
        PollOpt *q = &p->popt[p->npopt++];
        jcopy(q->id, sizeof q->id, o, "id");
        jcopy(q->label, sizeof q->label, o, "label");
        q->votes = jint(o, "voteCount");
        q->pct = jint(o, "percent");
    }
}

static void free_post(Post *p)
{
    if (!p) return;
    free(p->text); free(p->wtext); free(p->attr); free(p->q_wtext); free(p->q_attr); free(p->sty); free(p->clr); free(p->q_sty); free(p->q_clr);
    free(p);
}
static void feed_clear(Feed *f)
{
    for (int i = 0; i < f->n; i++) free_post(f->p[i]);
    f->n = 0; f->cursor[0] = 0;
}
static void feed_add(Feed *f, Post *p)
{
    if (f->n == f->cap) { f->cap = f->cap ? f->cap * 2 : 64; f->p = (Post **)realloc(f->p, (size_t)f->cap * sizeof(Post *)); }
    f->p[f->n++] = p;
}
static int feed_has(Feed *f, const char *id)
{
    for (int i = 0; i < f->n; i++) if (strcmp(f->p[i]->id, id) == 0 && !f->p[i]->rt_name[0]) return 1;
    return 0;
}

static Post *parse_post(const cJSON *it)
{
    const cJSON *src = it, *rt = cJSON_GetObjectItemCaseSensitive(it, "retweetOf");
    Post *p = (Post *)calloc(1, sizeof *p);
    if (cJSON_IsObject(rt) && cJSON_GetObjectItemCaseSensitive(rt, "id")) {
        src = rt;
        const cJSON *oa = cJSON_GetObjectItemCaseSensitive(it, "author");
        jcopy(p->rt_name, sizeof p->rt_name, oa, "name");
        jcopy(p->rt_handle, sizeof p->rt_handle, oa, "handle");
        if (!p->rt_name[0]) strcpy(p->rt_name, p->rt_handle);
    }
    jcopy(p->id, sizeof p->id, src, "id");
    if (!p->id[0]) { free(p); return NULL; }
    jcopy(p->created, sizeof p->created, src, "createdAt");
    jcopy(p->edited, sizeof p->edited, src, "editedAt");
    const cJSON *tx = cJSON_GetObjectItemCaseSensitive(src, "text");
    p->text = dupstr(cJSON_IsString(tx) && tx->valuestring ? tx->valuestring : "");
    set_text_md(p->text, &p->wtext, &p->attr, &p->sty, &p->clr, &p->wlen);
    {
        char rp[24];
        jcopy(rp, sizeof rp, src, "replyPolicy");
        p->policy = !strcmp(rp, "FOLLOWING") || !strcmp(rp, "FOLLOWERS") ? 1 : !strcmp(rp, "MENTIONED") ? 2 : 0;
        const cJSON *po = cJSON_GetObjectItemCaseSensitive(src, "poll");
        if (cJSON_IsObject(po)) parse_poll(p, po);
    }
    ulite(&p->au, cJSON_GetObjectItemCaseSensitive(src, "author"));
    p->pinned = jbool(it, "pinned");
    p->likes = jint(src, "likeCount"); p->rts = jint(src, "retweetCount"); p->replies = jint(src, "replyCount");
    p->liked = jbool(src, "liked"); p->rted = jbool(src, "retweeted"); p->bm = jbool(src, "bookmarked");

    const cJSON *m, *ma = cJSON_GetObjectItemCaseSensitive(src, "media");
    cJSON_ArrayForEach(m, ma) {
        if (p->nmedia >= 4) break;
        Media *d = &p->media[p->nmedia++];
        char kind[24];
        jcopy(d->url, sizeof d->url, m, "url");
        jcopy(d->thumb, sizeof d->thumb, m, "thumbnailUrl");
        if (!d->thumb[0]) strcpy(d->thumb, d->url);
        d->w = jint(m, "width"); d->h = jint(m, "height");
        jcopy(kind, sizeof kind, m, "kind");
        d->kind = strcmp(kind, "PHOTO") == 0 ? 0 : 1;
    }
    const cJSON *lp = cJSON_GetObjectItemCaseSensitive(src, "linkPreview");
    if (cJSON_IsObject(lp)) {
        jcopy(p->lp_url, sizeof p->lp_url, lp, "url");
        jcopy(p->lp_title, sizeof p->lp_title, lp, "title");
        jcopy(p->lp_domain, sizeof p->lp_domain, lp, "domain");
        jcopy(p->lp_image, sizeof p->lp_image, lp, "imageUrl");
    }
    const cJSON *q = cJSON_GetObjectItemCaseSensitive(src, "quoted");
    if (cJSON_IsObject(q)) {
        const cJSON *qt = cJSON_GetObjectItemCaseSensitive(q, "text");
        p->hasq = 1;
        jcopy(p->q_id, sizeof p->q_id, q, "id");
        ulite(&p->qau, cJSON_GetObjectItemCaseSensitive(q, "author"));
        set_text_md(cJSON_IsString(qt) && qt->valuestring ? qt->valuestring : "", &p->q_wtext, &p->q_attr, &p->q_sty, &p->q_clr, &p->q_wlen);
    }
    return p;
}

static void feed_parse(Feed *f, const char *json, int append)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    const cJSON *it, *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!append) feed_clear(f);
    cJSON_ArrayForEach(it, items) {
        Post *p = parse_post(it);
        if (!p) continue;
        if (append && !p->rt_name[0] && feed_has(f, p->id)) { free_post(p); continue; }
        feed_add(f, p);
    }
    const cJSON *nc = cJSON_GetObjectItemCaseSensitive(root, "nextCursor");
    f->cursor[0] = 0;
    if (cJSON_IsString(nc) && nc->valuestring) strncpy(f->cursor, nc->valuestring, sizeof f->cursor - 1);
    f->loaded = 1;
    cJSON_Delete(root);
}

static Feed *cur_feed(void)
{
    switch (g_view) {
    case V_PROFILE: return &g_prof.feed;
    case V_NOTIF: return &g_nfeed;
    case V_SEARCH: return &g_sfeed;
    case V_THREAD: return &g_tfeed;
    case V_BM: return &g_bfeed;
    default: return &g_home;
    }
}

/* ------------------------------------------------------------ レイアウト */
static void layout_post(Post *p, int cw)
{
    int pad = PX(16), av = PX(48), gap = PX(12);
    int x0 = pad + av + gap, w = cw - x0 - pad;
    int y = PX(12);
    p->l_ctx = -1; p->l_edit = -1; p->l_media = -1; p->l_card = -1; p->l_quote = -1; p->l_poll = -1;
    if (p->rt_name[0] || p->pinned || p->ctx_text[0]) { p->l_ctx = y; y += PX(22); }
    p->l_head = y; y += PX(22);
    p->l_text = y;
    p->l_texth = p->wlen ? text_flow_ex(&F_BODY, p->wtext, p->wlen, NULL, p->sty, p->clr, 0, 0, w, 1000, 0, 0, 0, 0) : 0;
    y += p->l_texth + PX(2);
    if (p->edited[0]) { p->l_edit = y; y += PX(20); }
    if (p->has_poll && p->npopt) { y += PX(8); p->l_poll = y; y += p->npopt * PX(40) + PX(24); }
    if (p->nmedia) {
        y += PX(8); p->l_media = y;
        if (p->nmedia == 1) {
            int mh = PX(300);
            if (p->media[0].w > 0 && p->media[0].h > 0) mh = (int)((long long)w * p->media[0].h / p->media[0].w);
            int lo = w * 40 / 100, hi = w * 120 / 100;
            if (hi > PX(520)) hi = PX(520);
            if (mh < lo) mh = lo;
            if (mh > hi) mh = hi;
            p->l_mediah = mh;
        } else p->l_mediah = w * 56 / 100;
        y += p->l_mediah;
    } else if (p->lp_url[0]) {
        y += PX(8); p->l_card = y;
        p->l_card_imgh = p->lp_image[0] ? w * 52 / 100 : 0;
        wchar_t *t = u8w(p->lp_title);
        int th = text_flow(&F_BOLD, t, (int)wcslen(t), NULL, 0, 0, w - PX(24), 2, 0, 0, 0, 0);
        free(t);
        p->l_cardh = p->l_card_imgh + PX(12) + PX(18) + th + PX(12);
        y += p->l_cardh;
    }
    if (p->hasq) {
        y += PX(8); p->l_quote = y;
        p->l_qtexth = p->q_wlen ? text_flow_ex(&F_BODY, p->q_wtext, p->q_wlen, NULL, p->q_sty, p->q_clr, 0, 0, w - PX(24), 6, 0, 0, 0, 1) : 0;
        p->l_quoteh = PX(12) + PX(24) + p->l_qtexth + PX(12);
        y += p->l_quoteh;
    }
    y += PX(4); p->l_act = y; y += PX(36) + PX(2);
    int minh = PX(12) + av + PX(12);
    p->h = y > minh ? y : minh;
    p->lay_w = cw;
}

static int feed_layout(Feed *f, int cw)
{
    int y = 0;
    for (int i = 0; i < f->n; i++) {
        Post *p = f->p[i];
        if (p->lay_w != cw) layout_post(p, cw);
        p->y = y; y += p->h;
    }
    return y;
}

/* ================================================================ 描画 */
static COLORREF C_BTN_OFF = RGB(58, 68, 94);

/* 入力欄は描画中に位置だけ記録し、描画後にまとめて反映する(点滅防止) */
static struct { int vis, x, y, w, h; } g_ew[NED];
static int g_focus_search;
static void place_edit(int idx, int vis, int x, int y, int w, int h)
{
    g_ew[idx].vis = vis; g_ew[idx].x = x; g_ew[idx].y = y; g_ew[idx].w = w; g_ew[idx].h = h;
}
static void apply_edits(void)
{
    static int lvis[NED] = { -1, -1, -1, -1, -1, -1, -1 }, lx[NED], ly[NED], lw[NED], lh[NED];
    for (int i = 0; i < NED; i++) {
        if (g_ew[i].vis != lvis[i]) { ShowWindow(g_ed[i], g_ew[i].vis ? SW_SHOWNA : SW_HIDE); lvis[i] = g_ew[i].vis; lx[i] = ly[i] = lw[i] = lh[i] = -1; }
        if (g_ew[i].vis && (g_ew[i].x != lx[i] || g_ew[i].y != ly[i] || g_ew[i].w != lw[i] || g_ew[i].h != lh[i])) {
            SetWindowPos(g_ed[i], NULL, g_ew[i].x, g_ew[i].y, g_ew[i].w, g_ew[i].h, SWP_NOZORDER | SWP_NOACTIVATE);
            lx[i] = g_ew[i].x; ly[i] = g_ew[i].y; lw[i] = g_ew[i].w; lh[i] = g_ew[i].h;
        }
        if (i == ED_SEARCH && g_ew[i].vis && g_focus_search) { SetFocus(g_ed[i]); g_focus_search = 0; }
    }
}

static void draw_avatar(const char *url, int x, int y, int sz)
{
    g_draw_img(img_get(url, 128), (float)x, (float)y, (float)sz, (float)sz, sz / 2.0f);
}
static void draw_avatar_ph(const char *url, const char *name, int x, int y, int sz)
{
    Img *im = url && url[0] ? img_get(url, 128) : NULL;
    if (im && im->state == 1) { g_draw_img(im, (float)x, (float)y, (float)sz, (float)sz, sz / 2.0f); return; }
    g_fill_circle(x + sz / 2.0f, y + sz / 2.0f, sz / 2.0f, RGB(40, 52, 88), 255);
    if (name && name[0]) {
        wchar_t *w = u8w(name), c[3] = { w[0], 0, 0 };
        if (w[0] >= 0xD800 && w[0] < 0xDC00 && w[1]) c[1] = w[1];
        SIZE s; HGDIOBJ old = SelectObject(P_dc, F_BOLD.f);
        GetTextExtentPoint32W(P_dc, c, (int)wcslen(c), &s);
        SelectObject(P_dc, old);
        text_at(&F_BOLD, x + (sz - s.cx) / 2, y + (sz - F_BOLD.lh) / 2, c, (int)wcslen(c), C_TEXT);
        free(w);
    }
}

static void badge_icon(int x, int y, int sz) { g_icon(IC_BADGE, (float)x, (float)y, (float)sz, C_ACCENT, 1, PX(2) * 0.8f); }

static void count_text(TF *tf, int x, int y, int n, COLORREF c)
{
    if (n <= 0) return;
    wchar_t b[24];
    fmt_count(n, b, 24);
    text_at(tf, x, y, b, (int)wcslen(b), c);
}

static void draw_poll(Post *p, int idx, int x, int y, int w)
{
    int voted = p->p_viewer[0] || p->p_ended;
    int rh = PX(34);
    for (int i = 0; i < p->npopt; i++) {
        PollOpt *o = &p->popt[i];
        int ry = y + i * PX(40);
        int mine = p->p_viewer[0] && strcmp(p->p_viewer, o->id) == 0;
        if (voted) {
            g_fill_rrect((float)x, (float)ry, (float)w, (float)rh, PX(8), RGB(24, 33, 60), 255);
            int fw = w * o->pct / 100;
            if (fw > PX(8)) { g_fill_rrect((float)x, (float)ry, (float)fw, (float)rh, PX(8), mine ? C_ACCENT : RGB(54, 72, 112), mine ? 200 : 255); }
            else if (o->pct > 0) g_fill_rrect((float)x, (float)ry, (float)PX(8), (float)rh, PX(4), mine ? C_ACCENT : RGB(54, 72, 112), 255);
            text_clip(mine ? &F_BOLD : &F_BODY, x + PX(12) + (mine ? PX(22) : 0), ry + (rh - F_BODY.lh) / 2, w - PX(80), o->label, C_TEXT);
            if (mine) {
                float cx0 = (float)(x + PX(12)), cy0 = (float)(ry + rh / 2);
                g_line(cx0, cy0, cx0 + PX(5), cy0 + PX(5), RGB(255, 255, 255), 2.0f);
                g_line(cx0 + PX(5), cy0 + PX(5), cx0 + PX(14), cy0 - PX(5), RGB(255, 255, 255), 2.0f);
            }
            wchar_t b[16];
            _snwprintf(b, 16, L"%d%%", o->pct); b[15] = 0;
            int tw = text_w(&F_BOLD, b, (int)wcslen(b));
            text_at(&F_BOLD, x + w - PX(12) - tw, ry + (rh - F_BOLD.lh) / 2, b, (int)wcslen(b), C_TEXT);
        } else {
            g_stroke_rrect((float)x, (float)ry, (float)w, (float)rh, rh / 2.0f, C_ACCENT, 1.4f);
            text_clip(&F_BOLD, x + PX(16), ry + (rh - F_BOLD.lh) / 2, w - PX(32), o->label, C_ACCENT);
            hit(x, ry, x + w, ry + rh, H_VOTE, idx, i);
        }
    }
    wchar_t rem[40], n1[24], b[100];
    fmt_remaining(p->p_ends, rem, 40);
    fmt_count(p->p_total, n1, 24);
    _snwprintf(b, 100, L"%ls票 · %ls", n1, p->p_ended ? L"終了" : rem); b[99] = 0;
    text_at(&F_SMALL, x, y + p->npopt * PX(40) + PX(2), b, (int)wcslen(b), C_GRAY);
}

static void draw_post(Post *p, int idx, int x, int y, int cw)
{
    int pad = PX(16), av = PX(48), gap = PX(12);
    int x0 = x + pad + av + gap, w = cw - (x0 - x) - pad;
    hit(x, y, x + cw, y + p->h, H_POSTBODY, idx, 0);

    /* 文脈行: リポスト / 固定 */
    if (p->l_ctx >= 0) {
        int cy = y + p->l_ctx;
        if (p->ctx_text[0]) {
            g_icon(p->ctx_icon - 1, (float)(x + pad + av - PX(16)), (float)(cy + PX(3)), (float)PX(16),
                   p->ctx_icon - 1 == IC_HEART ? C_ACCENT : C_GRAY, p->ctx_icon - 1 == IC_HEART, PX(2) * 0.75f);
            text_clip(&F_SMALLB, x0, cy, w, p->ctx_text, C_GRAY);
        } else if (p->rt_name[0]) {
            g_icon(IC_RT, (float)(x + pad + av - PX(16)), (float)(cy + PX(3)), (float)PX(16), C_GRAY, 0, PX(2) * 0.75f);
            wchar_t *n = u8w(p->rt_name);
            wchar_t buf[200];
            _snwprintf(buf, 200, L"%ls\u3055\u3093\u304C\u30EA\u30DD\u30B9\u30C8\u3057\u307E\u3057\u305F", n);
            buf[199] = 0;
            free(n);
            text_clipw(&F_SMALLB, x0, cy, w, buf, C_GRAY);
        } else {
            g_icon(IC_PIN, (float)(x + pad + av - PX(16)), (float)(cy + PX(3)), (float)PX(16), C_GRAY, 0, PX(2) * 0.75f);
            text_clipw(&F_SMALLB, x0, cy, w, L"\u56FA\u5B9A\u3055\u308C\u305F\u30DD\u30B9\u30C8", C_GRAY);
        }
    }

    /* アバター */
    draw_avatar_ph(p->au.avatar, p->au.name, x + pad, y + PX(12), av);
    hit(x + pad, y + PX(12), x + pad + av, y + PX(12) + av, H_AVATAR, idx, 0);

    /* ヘッダー行 */
    int hy = y + p->l_head, cx = x0;
    int maxname = w * 55 / 100;
    int nw = text_u8_w(&F_BOLD, p->au.name);
    if (nw > maxname) nw = maxname;
    text_clip(&F_BOLD, cx, hy, nw, p->au.name, C_TEXT);
    hit(cx, hy, cx + nw, hy + PX(22), H_AVATAR, idx, 0);
    cx += nw + PX(4);
    if (p->au.verified) { badge_icon(cx, hy + PX(3), PX(16)); cx += PX(20); }
    wchar_t hnd[120], tm[40], dot[8] = L"\u00B7";
    wchar_t *hh = u8w(p->au.handle);
    _snwprintf(hnd, 120, L"@%ls", hh); hnd[119] = 0; free(hh);
    int room = x + cw - pad - PX(30) - cx;
    int hw = text_w(&F_BODY, hnd, (int)wcslen(hnd));
    if (hw > room * 70 / 100) hw = room * 70 / 100;
    if (hw > PX(20)) text_clipw(&F_BODY, cx, hy, hw, hnd, C_GRAY);
    cx += hw + PX(6);
    fmt_rel_time(p->created, tm, 40);
    if (cx < x + cw - pad - PX(50)) {
        text_at(&F_BODY, cx, hy, dot, 1, C_GRAY);
        text_at(&F_BODY, cx + PX(10), hy, tm, (int)wcslen(tm), C_GRAY);
    }
    g_icon(IC_MORE, (float)(x + cw - pad - PX(20)), (float)(hy + PX(2)), (float)PX(20), C_GRAY, 0, PX(2) * 0.8f);
    hit(x + cw - pad - PX(30), hy - PX(4), x + cw - PX(4), hy + PX(26), H_MORE, idx, 0);

    /* 本文 */
    if (p->wlen) text_flow_ex(&F_BODY, p->wtext, p->wlen, p->attr, p->sty, p->clr, x0, y + p->l_text, w, 1000, C_TEXT, 1, idx, 0);
    if (p->l_poll >= 0) draw_poll(p, idx, x0, y + p->l_poll, w);
    if (p->l_edit >= 0) {
        wchar_t t[40], b[80];
        fmt_rel_time(p->edited, t, 40);
        _snwprintf(b, 80, L"\u6700\u7D42\u66F4\u65B0 %ls", t); b[79] = 0;
        text_at(&F_SMALL, x0, y + p->l_edit, b, (int)wcslen(b), C_GRAY);
    }

    /* メディア */
    if (p->l_media >= 0) {
        int my = y + p->l_media, mh = p->l_mediah, n = p->nmedia, g2 = PX(3);
        RECT tr[4];
        if (n == 1) { SetRect(&tr[0], x0, my, x0 + w, my + mh); }
        else {
            int hw2 = (w - g2) / 2;
            if (n == 2) { SetRect(&tr[0], x0, my, x0 + hw2, my + mh); SetRect(&tr[1], x0 + hw2 + g2, my, x0 + w, my + mh); }
            else if (n == 3) {
                int hh2 = (mh - g2) / 2;
                SetRect(&tr[0], x0, my, x0 + hw2, my + mh);
                SetRect(&tr[1], x0 + hw2 + g2, my, x0 + w, my + hh2);
                SetRect(&tr[2], x0 + hw2 + g2, my + hh2 + g2, x0 + w, my + mh);
            } else {
                int hh2 = (mh - g2) / 2;
                SetRect(&tr[0], x0, my, x0 + hw2, my + hh2);
                SetRect(&tr[1], x0 + hw2 + g2, my, x0 + w, my + hh2);
                SetRect(&tr[2], x0, my + hh2 + g2, x0 + hw2, my + mh);
                SetRect(&tr[3], x0 + hw2 + g2, my + hh2 + g2, x0 + w, my + mh);
            }
        }
        for (int i = 0; i < n; i++) {
            float r = n == 1 ? (float)PX(16) : (float)PX(6);
            g_draw_img(img_get(p->media[i].thumb, 800), (float)tr[i].left, (float)tr[i].top,
                       (float)(tr[i].right - tr[i].left), (float)(tr[i].bottom - tr[i].top), r);
            g_stroke_rrect((float)tr[i].left, (float)tr[i].top, (float)(tr[i].right - tr[i].left),
                           (float)(tr[i].bottom - tr[i].top), r, C_LINE, 1.0f);
            if (p->media[i].kind) {
                float cx2 = (tr[i].left + tr[i].right) / 2.0f, cy2 = (tr[i].top + tr[i].bottom) / 2.0f;
                g_fill_circle(cx2, cy2, (float)PX(24), RGB(0, 0, 0), 150);
                g_icon(IC_PLAY, cx2 - PX(14), cy2 - PX(14), (float)PX(28), RGB(255, 255, 255), 1, 1);
            }
            hit(tr[i].left, tr[i].top, tr[i].right, tr[i].bottom, H_MEDIA, idx, i);
        }
    }

    /* リンクカード */
    if (p->l_card >= 0) {
        int cy = y + p->l_card, ch = p->l_cardh;
        if (p->l_card_imgh) {
            g_draw_img(img_get(p->lp_image, 640), (float)x0, (float)cy, (float)w, (float)(p->l_card_imgh + PX(14)), (float)PX(14));
            g_rect(x0, cy + p->l_card_imgh, x0 + w, cy + p->l_card_imgh + PX(14), C_BG);   /* 下側の角丸を隠す */
        }
        g_stroke_rrect((float)x0, (float)cy, (float)w, (float)ch, (float)PX(14), C_LINE, 1.0f);
        int ty = cy + p->l_card_imgh + PX(12);
        text_clip(&F_SMALL, x0 + PX(12), ty, w - PX(24), p->lp_domain, C_GRAY);
        wchar_t *t = u8w(p->lp_title);
        text_flow(&F_BOLD, t, (int)wcslen(t), NULL, x0 + PX(12), ty + PX(18), w - PX(24), 2, C_TEXT, 1, idx, 9);
        free(t);
        hit(x0, cy, x0 + w, cy + ch, H_CARD, idx, 0);
    }

    /* 引用 */
    if (p->l_quote >= 0) {
        int qy = y + p->l_quote;
        g_stroke_rrect((float)x0, (float)qy, (float)w, (float)p->l_quoteh, (float)PX(14), C_LINE, 1.0f);
        hit(x0, qy, x0 + w, qy + p->l_quoteh, H_QUOTE, idx, 0);
        draw_avatar_ph(p->qau.avatar, p->qau.name, x0 + PX(12), qy + PX(10), PX(20));
        int qx = x0 + PX(38), qn = text_u8_w(&F_BOLD, p->qau.name);
        if (qn > w / 2) qn = w / 2;
        text_clip(&F_BOLD, qx, qy + PX(9), qn, p->qau.name, C_TEXT);
        wchar_t hb[120], *qh = u8w(p->qau.handle);
        _snwprintf(hb, 120, L"@%ls", qh); hb[119] = 0; free(qh);
        int rem = x0 + w - PX(12) - (qx + qn + PX(6));
        if (rem > PX(30)) text_clipw(&F_BODY, qx + qn + PX(6), qy + PX(9), rem, hb, C_GRAY);
        if (p->q_wlen) text_flow_ex(&F_BODY, p->q_wtext, p->q_wlen, p->q_attr, p->q_sty, p->q_clr, x0 + PX(12), qy + PX(12) + PX(24), w - PX(24), 6, C_TEXT, 1, idx, 1);
    }

    /* アクション行 */
    {
        int ay = y + p->l_act, step = (w - PX(36)) / 4, is = PX(18);
        int cyc = ay + PX(18) - is / 2;
        float lw = PX(2) * 0.8f;
        int ax[5];
        for (int i = 0; i < 5; i++) ax[i] = x0 + i * step;
        g_icon(IC_REPLY, (float)ax[0], (float)cyc, (float)is, C_GRAY, 0, lw);
        count_text(&F_SMALL, ax[0] + PX(26), ay + PX(9) - PX(2), p->replies, C_GRAY);
        g_icon(IC_RT, (float)ax[1], (float)cyc, (float)is, p->rted ? C_GREEN : C_GRAY, 0, lw);
        count_text(&F_SMALL, ax[1] + PX(26), ay + PX(9) - PX(2), p->rts, p->rted ? C_GREEN : C_GRAY);
        g_icon(IC_HEART, (float)ax[2], (float)cyc, (float)is, p->liked ? C_ACCENT : C_GRAY, p->liked, lw);
        count_text(&F_SMALL, ax[2] + PX(26), ay + PX(9) - PX(2), p->likes, p->liked ? C_ACCENT : C_GRAY);
        g_icon(IC_BOOKMARK, (float)ax[3], (float)cyc, (float)is, p->bm ? C_ACCENT : C_GRAY, p->bm, lw);
        g_icon(IC_SHARE, (float)ax[4], (float)cyc, (float)is, C_GRAY, 0, lw);
        int types[5] = { H_REPLY, H_RT, H_LIKE, H_BM, H_SHARE };
        for (int i = 0; i < 5; i++) hit(ax[i] - PX(8), ay, ax[i] + (i < 3 ? PX(64) : PX(34)), ay + PX(36), types[i], idx, 0);
    }
    g_hline(x, x + cw, y + p->h - 1, C_LINE);
}

/* ---------------------------------------------------------- コンポーザー */
/* ------------------------------------------------------------ 画像ビューア */
#define LB_MAXDIM 2048
typedef struct { char url[220], thumb[220]; } LbImg;
static struct { int on, n, idx, drag, lx, ly; float zoom, px, py; LbImg im[4]; } g_lb;

static void lb_release_all(void) { for (int i = 0; i < g_lb.n; i++) img_release(g_lb.im[i].url, LB_MAXDIM); }
static void lb_open(const LbImg *l, int n, int idx)
{
    if (n <= 0) return;
    if (n > 4) n = 4;
    if (g_lb.on) lb_release_all();
    memcpy(g_lb.im, l, (size_t)n * sizeof(LbImg));
    g_lb.n = n;
    g_lb.idx = idx < 0 ? 0 : idx >= n ? n - 1 : idx;
    g_lb.zoom = 1.0f; g_lb.px = g_lb.py = 0; g_lb.drag = 0;
    g_lb.on = 1;
    SetFocus(g_main);
    InvalidateRect(g_main, NULL, FALSE);
}
static void lb_close(void)
{
    if (!g_lb.on) return;
    g_lb.on = 0;
    if (g_lb.drag) ReleaseCapture();
    g_lb.drag = 0;
    lb_release_all();
    InvalidateRect(g_main, NULL, FALSE);
}
static void lb_step(int d)
{
    if (g_lb.n < 2) return;
    g_lb.idx = (g_lb.idx + d + g_lb.n) % g_lb.n;
    g_lb.zoom = 1.0f; g_lb.px = g_lb.py = 0;
    InvalidateRect(g_main, NULL, FALSE);
}
/* 表示中の画像(読み込み完了までは小さいサムネイルで代用)と、フィット時の倍率 */
static Img *lb_current(float *fit_out, int *loading)
{
    LbImg *li = &g_lb.im[g_lb.idx];
    Img *big = img_get(li->url, LB_MAXDIM), *show = (big && big->state == 1) ? big : NULL;
    *loading = show == NULL;
    if (!show) { Img *t = img_get(li->thumb, 800); if (t && t->state == 1) show = t; }
    if (show) {
        float fx = (float)(G.W - 2 * PX(24)) / (float)show->w, fy = (float)(G.H - 2 * PX(72)) / (float)show->h;
        float fit = fx < fy ? fx : fy, cap = show == big ? 2.0f : 3.0f;   /* 大きい画像は縮小のみ、小さい画像(アイコン等)は最大2倍まで拡大 */
        *fit_out = fit > cap ? cap : fit;
    }
    return show;
}
static void lb_zoom_at(float factor, int mx, int my)
{
    float fit; int ld;
    if (!lb_current(&fit, &ld)) return;
    float z1 = g_lb.zoom, z2 = z1 * factor;
    if (z2 < 1.0f) z2 = 1.0f;
    if (z2 > 8.0f) z2 = 8.0f;
    float cx = (float)(mx - G.W / 2), cy = (float)(my - G.H / 2), k = z2 / z1;
    g_lb.px = cx - (cx - g_lb.px) * k;
    g_lb.py = cy - (cy - g_lb.py) * k;
    g_lb.zoom = z2;
    if (z2 <= 1.001f) { g_lb.zoom = 1.0f; g_lb.px = g_lb.py = 0; }
    InvalidateRect(g_main, NULL, FALSE);
}

static void draw_lightbox(void)
{
    int W = G.W, H = G.H;
    hit(0, 0, W, H, H_LB_BG, 0, 0);
    g_fill_rrect(0, 0, (float)W, (float)H, 0, RGB(0, 0, 0), 250);
    float fit = 1.0f; int loading = 0;
    Img *show = lb_current(&fit, &loading);
    if (show) {
        float s = fit * g_lb.zoom;
        int w = (int)(show->w * s + 0.5f), h = (int)(show->h * s + 0.5f);
        float mxp = w > W ? (w - W) / 2.0f : 0, myp = h > H ? (h - H) / 2.0f : 0;
        if (g_lb.px > mxp) g_lb.px = mxp;
        if (g_lb.px < -mxp) g_lb.px = -mxp;
        if (g_lb.py > myp) g_lb.py = myp;
        if (g_lb.py < -myp) g_lb.py = -myp;
        int x = (W - w) / 2 + (int)g_lb.px, y = (H - h) / 2 + (int)g_lb.py;
        g_draw_img_whole(show, x, y, w, h);
        hit(x, y, x + w, y + h, H_LB_IMG, 0, 0);
    }
    if (loading) {
        const wchar_t *t = L"読み込み中...";
        int tw = text_w(&F_BODY, t, (int)wcslen(t));
        text_at(&F_BODY, (W - tw) / 2, show ? H - PX(100) : (H - F_BODY.lh) / 2, t, (int)wcslen(t), C_GRAY);
    }
    int r = PX(22), pad = PX(16);
    COLORREF bg = RGB(34, 42, 72);
    g_fill_circle((float)(pad + r), (float)(pad + r), (float)r, bg, 235);
    g_icon(IC_CLOSE, (float)(pad + r - PX(10)), (float)(pad + r - PX(10)), (float)PX(20), RGB(255, 255, 255), 0, PX(2) * 1.0f);
    hit(pad, pad, pad + 2 * r, pad + 2 * r, H_LB_CLOSE, 0, 0);
    if (strncmp(g_lb.im[g_lb.idx].url, "file:", 5) != 0) {
        g_fill_circle((float)(W - pad - r), (float)(pad + r), (float)r, bg, 235);
        g_icon(IC_LINK, (float)(W - pad - r - PX(11)), (float)(pad + r - PX(11)), (float)PX(22), RGB(255, 255, 255), 0, PX(2) * 0.9f);
        hit(W - pad - 2 * r, pad, W - pad, pad + 2 * r, H_LB_WEB, 0, 0);
    }
    if (g_lb.n > 1) {
        wchar_t b[24];
        _snwprintf(b, 24, L"%d / %d", g_lb.idx + 1, g_lb.n); b[23] = 0;
        int tw = text_w(&F_BOLD, b, (int)wcslen(b));
        text_at(&F_BOLD, (W - tw) / 2, pad + r - F_BOLD.lh / 2, b, (int)wcslen(b), RGB(255, 255, 255));
        for (int side = 0; side < 2; side++) {
            int cx = side == 0 ? pad + r : W - pad - r, cy = H / 2;
            g_fill_circle((float)cx, (float)cy, (float)r, bg, 235);
            float d = side == 0 ? -1.0f : 1.0f, a = (float)PX(7);
            g_line(cx - d * a, (float)cy, cx + d * a, (float)cy, RGB(255, 255, 255), 2.2f);
            g_line(cx + d * a, (float)cy, cx + d * a - d * PX(8), (float)(cy - PX(8)), RGB(255, 255, 255), 2.2f);
            g_line(cx + d * a, (float)cy, cx + d * a - d * PX(8), (float)(cy + PX(8)), RGB(255, 255, 255), 2.2f);
            hit(cx - r, cy - r, cx + r, cy + r, side == 0 ? H_LB_PREV : H_LB_NEXT, 0, 0);
        }
    }
    const wchar_t *hint = L"ホイール: 拡大 / ドラッグ: 移動 / ダブルクリック: 拡大切替 / Esc: 閉じる";
    int hw = text_w(&F_SMALL, hint, (int)wcslen(hint));
    if (hw < W - PX(40)) text_at(&F_SMALL, (W - hw) / 2, H - PX(40), hint, (int)wcslen(hint), RGB(150, 160, 180));
}

/* ---------------------------------------------------------- 添付画像 */
typedef struct { char id[40]; char key[620]; int state, serial; } Att;     /* state: 0=アップロード中 1=完了 2=失敗 */
static Att g_att[NAT][4];
static int g_natt[NAT], g_att_serial;

static int att_ctx_view(void)
{
    switch (g_view) { case V_HOME: return AT_POST; case V_THREAD: return AT_THR; case V_CHAT: return AT_DM; }
    return -1;
}
static int att_h(int c) { return g_natt[c] ? PX(84) : 0; }
static int att_ready(int c) { int n = 0; for (int i = 0; i < g_natt[c]; i++) if (g_att[c][i].state == 1) n++; return n; }
static int att_pending(int c) { int n = 0; for (int i = 0; i < g_natt[c]; i++) if (g_att[c][i].state == 0) n++; return n; }
static void att_del(int c, int i)
{
    if (i < 0 || i >= g_natt[c]) return;
    for (int k = i; k + 1 < g_natt[c]; k++) g_att[c][k] = g_att[c][k + 1];
    g_natt[c]--;
}
static cJSON *att_ids(int c)
{
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < g_natt[c]; i++) if (g_att[c][i].state == 1) cJSON_AddItemToArray(a, cJSON_CreateString(g_att[c][i].id));
    return a;
}
static void att_add_file(int c, const wchar_t *path)
{
    if (g_natt[c] >= 4) { ui_toast(L"添付できる画像は4枚までです"); return; }
    const wchar_t *ext = wcsrchr(path, L'.');
    if (!ext || !(!_wcsicmp(ext, L".png") || !_wcsicmp(ext, L".jpg") || !_wcsicmp(ext, L".jpeg") || !_wcsicmp(ext, L".gif") || !_wcsicmp(ext, L".webp"))) {
        ui_toast(L"対応している形式は png / jpg / gif / webp です");
        return;
    }
    Att *a = &g_att[c][g_natt[c]++];
    memset(a, 0, sizeof *a);
    a->serial = ++g_att_serial;
    char *u = wu8(path);
    _snprintf(a->key, sizeof a->key, "file:%s", u);
    a->key[sizeof a->key - 1] = 0;
    free(u);
    char cx[8];
    _snprintf(cx, sizeof cx, "%d", c);
    net_upload(T_MEDIA, path, cx, a->serial);
    InvalidateRect(g_main, NULL, FALSE);
}
static void att_pick(int c)
{
    if (g_natt[c] >= 4) { ui_toast(L"添付できる画像は4枚までです"); return; }
    static wchar_t buf[4096];
    OPENFILENAMEW of;
    memset(&of, 0, sizeof of);
    buf[0] = 0;
    of.lStructSize = sizeof of;
    of.hwndOwner = g_main;
    of.lpstrFilter = L"画像 (png, jpg, gif, webp)\0*.png;*.jpg;*.jpeg;*.gif;*.webp\0";
    of.lpstrFile = buf;
    of.nMaxFile = 4096;
    of.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return;
    const wchar_t *p = buf;
    size_t l = wcslen(p);
    if (p[l + 1] == 0) att_add_file(c, p);              /* 単一選択: フルパス */
    else {                                              /* 複数選択: dir\0file\0file\0\0 */
        wchar_t dir[MAX_PATH];
        wcsncpy(dir, p, MAX_PATH - 1); dir[MAX_PATH - 1] = 0;
        p += l + 1;
        while (*p) {
            wchar_t full[MAX_PATH * 2];
            _snwprintf(full, MAX_PATH * 2, L"%ls\\%ls", dir, p);
            full[MAX_PATH * 2 - 1] = 0;
            att_add_file(c, full);
            p += wcslen(p) + 1;
        }
    }
}
void ui_debug_attach(const wchar_t *path) { att_add_file(AT_POST, path); }
static int g_dbg_lb;
static void dbg_lb_try(void)
{
    if (!g_dbg_lb) return;
    int best = -1;
    for (int i = 0; i < g_home.n; i++) {
        Post *p = g_home.p[i];
        if (p->nmedia >= 1 && p->media[0].kind == 0 && (best < 0 || p->nmedia > g_home.p[best]->nmedia)) best = i;
    }
    if (best < 0) return;
    Post *p = g_home.p[best];
    LbImg l[4]; int n = 0;
    for (int k = 0; k < p->nmedia; k++) if (p->media[k].kind == 0) { strncpy(l[n].url, p->media[k].url, 219); l[n].url[219] = 0; strncpy(l[n].thumb, p->media[k].thumb, 219); l[n].thumb[219] = 0; n++; }
    g_dbg_lb = 0;
    lb_open(l, n, 0);
}
void ui_debug_lightbox(void) { g_dbg_lb = 1; }
void ui_debug_poll(void)
{
    g_poll_on = 1; g_poll_n = 3;
    SetWindowTextW(g_ed[ED_OPT0], L"田所浩二");
    SetWindowTextW(g_ed[ED_OPT1], L"114514");
    SetWindowTextW(g_edit, L"<太字>突然</太字>の<斜体>告白</斜体>(迫真)");
}

static void draw_atts(int c, int x, int y)
{
    int sz = PX(72);
    for (int i = 0; i < g_natt[c]; i++) {
        Att *a = &g_att[c][i];
        int ax = x + i * (sz + PX(8));
        g_draw_img(img_get(a->key, 256), (float)ax, (float)y, (float)sz, (float)sz, (float)PX(12));
        g_stroke_rrect((float)ax, (float)y, (float)sz, (float)sz, (float)PX(12), C_LINE, 1.0f);
        hit(ax, y, ax + sz, y + sz, H_ATT_VIEW, c, i);
        if (a->state != 1) {
            g_fill_rrect((float)ax, (float)y, (float)sz, (float)sz, (float)PX(12), a->state == 2 ? C_ACCENT : RGB(0, 0, 0), a->state == 2 ? 150 : 140);
            const wchar_t *t = a->state == 2 ? L"失敗" : L"送信中";
            int tw = text_w(&F_SMALLB, t, (int)wcslen(t));
            text_at(&F_SMALLB, ax + (sz - tw) / 2, y + (sz - F_SMALLB.lh) / 2, t, (int)wcslen(t), RGB(255, 255, 255));
        }
        g_fill_circle((float)(ax + sz - PX(11)), (float)(y + PX(11)), (float)PX(10), RGB(0, 0, 0), 190);
        g_icon(IC_CLOSE, (float)(ax + sz - PX(17)), (float)(y + PX(5)), (float)PX(12), RGB(255, 255, 255), 0, 1.5f);
        hit(ax + sz - PX(26), y, ax + sz, y + PX(26), H_ATT_DEL, c, i);
    }
}

/* ---------------------------------------------------------- ポスト入力欄 */
static const char *POLICY_KEY[3] = { "EVERYONE", "FOLLOWING", "MENTIONED" };
static const wchar_t *POLICY_LABEL[3] = { L"全員が返信できます", L"フォロー中のアカウントが返信できます", L"@したアカウントのみ返信できます" };

static int poll_h(void) { return g_poll_on ? PX(14) + g_poll_n * PX(66) + (g_poll_n < 4 ? PX(40) : PX(8)) + PX(104) : 0; }
static int composer_h(void)
{
    return PX(138) + PX(32) + (g_reply_id[0] ? PX(30) : 0) + att_h(AT_POST) + (g_poll_on ? poll_h() + PX(8) : 0);
}
static int has_text(void) { return GetWindowTextLengthW(g_edit) > 0; }

static void ed_text_u8(int idx, char *out, size_t n)
{
    int len = GetWindowTextLengthW(g_ed[idx]);
    out[0] = 0;
    if (len <= 0) return;
    wchar_t *w = (wchar_t *)malloc((size_t)(len + 1) * sizeof(wchar_t));
    GetWindowTextW(g_ed[idx], w, len + 1);
    char *u = wu8(w);
    strncpy(out, u, n - 1); out[n - 1] = 0;
    free(u); free(w);
}
static int cp_len(const char *u8)
{
    wchar_t *w = u8w(u8);
    int n = 0;
    for (int i = 0; w[i]; i++) { if (w[i] >= 0xD800 && w[i] < 0xDC00 && w[i + 1]) i++; n++; }
    free(w);
    return n;
}
static void trim_inplace(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') p++;
    memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == '\t')) s[--n] = 0;
}
/* 重み付き文字数 (書式タグは除外・URLは23) */
static int count_units(void)
{
    int len = GetWindowTextLengthW(g_edit);
    if (len <= 0) return 0;
    wchar_t *w = (wchar_t *)malloc((size_t)(len + 1) * sizeof(wchar_t));
    GetWindowTextW(g_edit, w, len + 1);
    char *u = wu8(w);
    free(w);
    BYTE *sty; DWORD *clr; int m;
    wchar_t *pl = parse_markup(u, &sty, &clr, &m);
    BYTE *at = (BYTE *)malloc((size_t)m + 1);
    compute_attr(pl, m, at);
    int units = 0;
    for (int i = 0; i < m; i++) {
        if (at[i] == 1) { if (i == 0 || at[i - 1] != 1) units += 23; continue; }
        wchar_t c = pl[i];
        if (c >= 0xD800 && c < 0xDC00) { units += 2; i++; continue; }
        if ((c <= 4351) || (c >= 8192 && c <= 8205) || (c >= 8208 && c <= 8223) || (c >= 8242 && c <= 8247)) units += 1;
        else units += 2;
    }
    free(pl); free(sty); free(clr); free(at); free(u);
    return units;
}
static int poll_minutes(void) { return g_poll_d * 1440 + g_poll_h * 60 + g_poll_m; }
/* 戻り値: 0=OK, それ以外=エラーメッセージ */
static const wchar_t *poll_error(void)
{
    if (!g_poll_on) return NULL;
    char o[4][120];
    for (int i = 0; i < g_poll_n; i++) {
        ed_text_u8(ED_OPT0 + i, o[i], sizeof o[i]);
        trim_inplace(o[i]);
        if (!o[i][0]) return L"アンケートの選択肢を入力してください";
        if (cp_len(o[i]) > 25) return L"選択肢は25文字までです";
        for (int k = 0; k < i; k++) if (!_stricmp(o[i], o[k])) return L"同じ選択肢は使えません";
    }
    int mn = poll_minutes();
    if (mn < 5 || mn > 10080) return L"アンケート期間は5分〜7日で指定してください";
    return NULL;
}
static void wrap_sel(const wchar_t *o, const wchar_t *c)
{
    DWORD s0 = 0, e0 = 0;
    SendMessageW(g_edit, EM_GETSEL, (WPARAM)&s0, (LPARAM)&e0);
    int len = GetWindowTextLengthW(g_edit);
    wchar_t *t = (wchar_t *)malloc((size_t)(len + 1) * sizeof(wchar_t));
    GetWindowTextW(g_edit, t, len + 1);
    if ((int)e0 > len) e0 = (DWORD)len;
    if (s0 > e0) s0 = e0;
    size_t ol = wcslen(o), cl = wcslen(c);
    wchar_t *n = (wchar_t *)malloc((size_t)(len + (int)ol + (int)cl + 1) * sizeof(wchar_t));
    wmemcpy(n, t, s0);
    wmemcpy(n + s0, o, ol);
    wmemcpy(n + s0 + ol, t + s0, e0 - s0);
    wmemcpy(n + s0 + ol + (e0 - s0), c, cl);
    wcscpy(n + s0 + ol + (e0 - s0) + cl, t + e0);
    SetWindowTextW(g_edit, n);
    if (s0 == e0) SendMessageW(g_edit, EM_SETSEL, s0 + ol, s0 + ol);
    else SendMessageW(g_edit, EM_SETSEL, s0 + ol, e0 + ol);
    SetFocus(g_edit);
    free(t); free(n);
    InvalidateRect(g_main, NULL, FALSE);
}
static void open_emoji_panel(void)
{
    SetFocus(g_edit);
    keybd_event(VK_LWIN, 0, 0, 0);
    keybd_event(VK_OEM_PERIOD, 0, 0, 0);
    keybd_event(VK_OEM_PERIOD, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
}
static void poll_reset(void)
{
    g_poll_on = 0; g_poll_n = 2; g_poll_d = 1; g_poll_h = g_poll_m = 0;
    for (int i = 0; i < 4; i++) SetWindowTextW(g_ed[ED_OPT0 + i], L"");
}

static void draw_selectbox(int x, int y, int w, const wchar_t *label, int val, int htype)
{
    wchar_t b[16];
    text_at(&F_SMALL, x, y, label, (int)wcslen(label), C_GRAY);
    int by = y + PX(22), bh = PX(38);
    g_stroke_rrect((float)x, (float)by, (float)w, (float)bh, PX(6), RGB(90, 100, 130), 1.0f);
    _snwprintf(b, 16, L"%d", val); b[15] = 0;
    text_at(&F_BODY, x + PX(12), by + (bh - F_BODY.lh) / 2, b, (int)wcslen(b), C_TEXT);
    GpPointF tri[3];
    tri[0].X = (float)(x + w - PX(24)); tri[0].Y = (float)(by + bh / 2 - PX(3));
    tri[1].X = (float)(x + w - PX(14)); tri[1].Y = tri[0].Y;
    tri[2].X = (float)(x + w - PX(19)); tri[2].Y = (float)(by + bh / 2 + PX(4));
    g_fill_poly(tri, 3, C_GRAY);
    hit(x, by, x + w, by + bh, htype, 0, 0);
}

static void draw_composer(int x, int y, int cw)
{
    int pad = PX(16), av = PX(48), gap = PX(12), ry = 0;
    if (g_reply_id[0]) {
        wchar_t b[140], *h = u8w(g_reply_handle);
        _snwprintf(b, 140, L"返信先: @%ls", h); b[139] = 0; free(h);
        text_at(&F_SMALL, x + pad + av + gap, y + PX(8), b, (int)wcslen(b), C_GRAY);
        g_icon(IC_CLOSE, (float)(x + cw - pad - PX(20)), (float)(y + PX(6)), (float)PX(18), C_GRAY, 0, PX(2) * 0.8f);
        hit(x + cw - pad - PX(30), y, x + cw, y + PX(30), H_REPLY_CLR, 0, 0);
        ry = PX(30);
    }
    draw_avatar_ph(g_me.avatar, g_me.name, x + pad, y + ry + PX(12), av);
    int ex = x + pad + av + gap, ey = y + ry + PX(14), ew = cw - (ex - x) - pad, eh = PX(64);
    if (ey >= G.vy0 && ey + eh <= G.vy1) place_edit(ED_POST, 1, ex, ey, ew, eh);
    else {
        place_edit(ED_POST, 0, 0, 0, 0, 0);
        if (!has_text() && ey + eh > G.vy0 && ey < G.vy1)
            text_at(&F_BODY, ex, ey, L"いまどうしてる？", (int)wcslen(L"いまどうしてる？"), C_GRAY);
    }
    /* 返信制限 */
    int ypol = ey + eh + PX(4);
    g_icon(g_policy == 0 ? IC_GLOBE : IC_PERSON, (float)ex, (float)(ypol + PX(4)), (float)PX(16), C_ACCENT, 0, PX(2) * 0.8f);
    text_clipw(&F_SMALLB, ex + PX(24), ypol, ew - PX(24), POLICY_LABEL[g_policy], C_ACCENT);
    hit(ex, ypol, ex + ew, ypol + PX(26), H_POLICY, 0, 0);
    int yn = ypol + PX(30);

    /* アンケート */
    if (g_poll_on) {
        int ph = poll_h(), fy = yn + PX(14), fw = ew - PX(60);
        g_stroke_rrect((float)ex, (float)yn, (float)ew, (float)ph, PX(16), RGB(60, 72, 104), 1.0f);
        g_icon(IC_CLOSE, (float)(ex + ew - PX(36)), (float)(yn + PX(14)), (float)PX(20), C_GRAY, 0, PX(2) * 0.9f);
        hit(ex + ew - PX(44), yn + PX(6), ex + ew - PX(4), yn + PX(44), H_POLL_OFF, 0, 0);
        for (int i = 0; i < g_poll_n; i++) {
            int fx = ex + PX(14);
            g_stroke_rrect((float)fx, (float)fy, (float)fw, (float)PX(54), PX(8), RGB(90, 100, 130), 1.0f);
            wchar_t lb[24];
            _snwprintf(lb, 24, L"選択肢 %d", i + 1); lb[23] = 0;
            text_at(&F_SMALL, fx + PX(10), fy + PX(4), lb, (int)wcslen(lb), C_GRAY);
            char ot[120];
            ed_text_u8(ED_OPT0 + i, ot, sizeof ot);
            int vis = fy >= G.vy0 && fy + PX(54) <= G.vy1;
            if (vis) place_edit(ED_OPT0 + i, 1, fx + PX(10), fy + PX(26), fw - PX(60), PX(20));
            else if (fy + PX(54) > G.vy0 && fy < G.vy1) text_clip(&F_BODY, fx + PX(10), fy + PX(24), fw - PX(60), ot, C_TEXT);
            wchar_t rm[12];
            _snwprintf(rm, 12, L"%d", 25 - cp_len(ot)); rm[11] = 0;
            int rw = text_w(&F_SMALL, rm, (int)wcslen(rm));
            text_at(&F_SMALL, fx + fw - PX(10) - rw, fy + PX(30), rm, (int)wcslen(rm), 25 - cp_len(ot) < 0 ? C_ACCENT : C_GRAY);
            if (g_poll_n > 2 && i >= 2) {
                g_icon(IC_CLOSE, (float)(ex + ew - PX(36)), (float)(fy + PX(17)), (float)PX(20), C_GRAY, 0, PX(2) * 0.9f);
                hit(ex + ew - PX(44), fy + PX(8), ex + ew - PX(4), fy + PX(46), H_OPT_DEL, i, 0);
            }
            fy += PX(66);
        }
        if (g_poll_n < 4) {
            text_at(&F_BOLD, ex + PX(22), fy + PX(8), L"＋ 選択肢を追加", (int)wcslen(L"＋ 選択肢を追加"), C_ACCENT);
            hit(ex + PX(14), fy, ex + PX(200), fy + PX(36), H_OPT_ADD, 0, 0);
            fy += PX(40);
        } else fy += PX(8);
        g_hline(ex + 1, ex + ew - 1, fy, RGB(60, 72, 104));
        text_at(&F_SMALL, ex + PX(14), fy + PX(8), L"アンケート期間", (int)wcslen(L"アンケート期間"), C_GRAY);
        int sw = (ew - PX(28) - PX(24)) / 3, sy = fy + PX(26);
        draw_selectbox(ex + PX(14), sy, sw, L"日", g_poll_d, H_POLL_D);
        draw_selectbox(ex + PX(14) + sw + PX(12), sy, sw, L"時間", g_poll_h, H_POLL_H);
        draw_selectbox(ex + PX(14) + 2 * (sw + PX(12)), sy, sw, L"分", g_poll_m, H_POLL_M);
        yn += ph + PX(8);
    }
    if (g_natt[AT_POST]) draw_atts(AT_POST, ex, yn);

    /* ツールバー */
    int bw = PX(96), bh = PX(36), bx = x + cw - pad - bw, by = y + composer_h() - PX(12) - bh;
    {
        int ic = PX(22), st = PX(34), tx = ex, ty = by + (bh - ic) / 2;
        int can_img = g_natt[AT_POST] < 4 && !g_poll_on, can_poll = g_natt[AT_POST] == 0;
        g_icon(IC_IMAGE, (float)tx, (float)ty, (float)ic, can_img ? C_ACCENT : C_BTN_OFF, 0, PX(2) * 0.9f);
        if (can_img) hit(tx - PX(4), by, tx + st - PX(6), by + bh, H_ATTACH, AT_POST, 0);
        tx += st;
        g_icon(IC_LIST, (float)tx, (float)ty, (float)ic, can_poll ? (g_poll_on ? C_TEXT : C_ACCENT) : C_BTN_OFF, 0, PX(2) * 0.9f);
        hit(tx - PX(4), by, tx + st - PX(6), by + bh, H_POLL_TOG, 0, 0);
        tx += st;
        g_icon(IC_EMOJI, (float)tx, (float)ty, (float)ic, C_ACCENT, 0, PX(2) * 0.9f);
        hit(tx - PX(4), by, tx + st - PX(6), by + bh, H_EMOJI, 0, 0);
        tx += st;
        text_at(&F_BOLD, tx + PX(3), by + (bh - F_BOLD.lh) / 2, L"B", 1, C_ACCENT);
        hit(tx - PX(4), by, tx + st - PX(6), by + bh, H_BOLD, 0, 0);
        tx += st;
        text_at(&F_BODYI, tx + PX(5), by + (bh - F_BODY.lh) / 2, L"I", 1, C_ACCENT);
        hit(tx - PX(4), by, tx + st - PX(6), by + bh, H_ITALIC, 0, 0);
    }
    {
        int units = count_units(), left = 280 - units;
        float rcx = (float)(bx - PX(24)), rcy = (float)(by + bh / 2), r = (float)PX(10);
        if (units > 0) {
            g_circle(rcx, rcy, r, C_LINE, 2.0f);
            float sw = 360.0f * (float)units / 280.0f;
            if (sw > 360) sw = 360;
            g_arc(rcx, rcy, r, -90, sw, left < 0 ? C_ACCENT : (left <= 20 ? RGB(255, 212, 0) : C_ACCENT), 2.4f);
            if (left <= 20) {
                wchar_t b[12];
                _snwprintf(b, 12, L"%d", left); b[11] = 0;
                int tw = text_w(&F_SMALL, b, (int)wcslen(b));
                text_at(&F_SMALL, (int)rcx - tw - PX(16), by + (bh - F_SMALL.lh) / 2, b, (int)wcslen(b), left < 0 ? C_ACCENT : C_GRAY);
            }
        }
    }
    g_fill_rrect((float)bx, (float)by, (float)bw, (float)bh, bh / 2.0f, C_BTN_OFF, 0);
    int on = (has_text() || att_ready(AT_POST) > 0) && !att_pending(AT_POST) && !poll_error();
    g_fill_rrect((float)bx, (float)by, (float)bw, (float)bh, bh / 2.0f, on ? C_ACCENT : C_BTN_OFF, 255);
    SIZE sz; HGDIOBJ old = SelectObject(P_dc, F_BOLD.f);
    const wchar_t *lbl = L"ポストする";
    GetTextExtentPoint32W(P_dc, lbl, (int)wcslen(lbl), &sz);
    SelectObject(P_dc, old);
    text_at(&F_BOLD, bx + (bw - sz.cx) / 2, by + (bh - F_BOLD.lh) / 2, lbl, (int)wcslen(lbl), on ? RGB(255, 255, 255) : RGB(150, 160, 180));
    if (on) hit(bx, by, bx + bw, by + bh, H_POSTBTN, 0, 0);
    g_hline(x, x + cw, y + composer_h() - 1, C_LINE);
}

/* ------------------------------------------------------------ プロフィール */
static int draw_profile_header(int x, int y, int cw)
{
    const User *u = &g_prof.u;
    int pad = PX(16), bannerH = cw / 3, A = PX(96);
    int y0 = y;
    if (g_prof.have && u->header[0]) g_draw_img(img_get(u->header, 1000), (float)x, (float)y, (float)cw, (float)bannerH, 0);
    else g_rect(x, y, x + cw, y + bannerH, RGB(30, 44, 80));
    hit(x, y, x + cw, y + bannerH, H_PROF_HEADER, 0, 0);
    int ay = y + bannerH - A / 2;
    g_fill_circle(x + pad + A / 2.0f, ay + A / 2.0f, A / 2.0f + PX(4), C_BG, 255);
    draw_avatar_ph(u->avatar, u->name[0] ? u->name : g_prof.handle, x + pad, ay, A);
    hit(x + pad, ay, x + pad + A, ay + A, H_PROF_AVATAR, 0, 0);

    int own = g_have_me && _stricmp(g_prof.handle, g_me.handle) == 0;
    {
        int bh = PX(36), by = y + bannerH + PX(12);
        if (own) {
            const wchar_t *bl = L"プロフィールを編集";
            int bw = text_w(&F_BOLD, bl, (int)wcslen(bl)) + PX(32), bx = x + cw - pad - bw;
            g_stroke_rrect((float)bx, (float)by, (float)bw, (float)bh, bh / 2.0f, RGB(80, 92, 122), 1.0f);
            text_at(&F_BOLD, bx + PX(16), by + (bh - F_BOLD.lh) / 2, bl, (int)wcslen(bl), C_TEXT);
            hit(bx, by, bx + bw, by + bh, H_PROF_BTN, 0, 0);
        } else if (g_prof.have) {
            int on = 0;
            const wchar_t *bl = u->is_following ? L"フォロー中" : u->follow_req ? L"リクエスト済み" : (on = 1, L"フォロー");
            int bw = text_w(&F_BOLD, bl, (int)wcslen(bl)) + PX(32), bx = x + cw - pad - bw;
            if (on) g_fill_rrect((float)bx, (float)by, (float)bw, (float)bh, bh / 2.0f, RGB(255, 255, 255), 255);
            else g_stroke_rrect((float)bx, (float)by, (float)bw, (float)bh, bh / 2.0f, RGB(80, 92, 122), 1.0f);
            text_at(&F_BOLD, bx + PX(16), by + (bh - F_BOLD.lh) / 2, bl, (int)wcslen(bl), on ? RGB(10, 15, 32) : C_TEXT);
            hit(bx, by, bx + bw, by + bh, H_FOLLOW, 0, 0);
            int dx = bx - PX(8) - bh;
            g_stroke_rrect((float)dx, (float)by, (float)bh, (float)bh, bh / 2.0f, RGB(80, 92, 122), 1.0f);
            g_icon(IC_MAIL, (float)(dx + PX(8)), (float)(by + PX(8)), (float)PX(20), C_TEXT, 0, PX(2) * 0.9f);
            hit(dx, by, dx + bh, by + bh, H_DM_BTN, 0, 0);
        }
    }

    y = ay + A + PX(8);
    const char *nm = u->name[0] ? u->name : g_prof.handle;
    int nw = text_u8_w(&F_BIG, nm);
    if (nw > cw - 2 * pad - PX(28)) nw = cw - 2 * pad - PX(28);
    text_clip(&F_BIG, x + pad, y, nw, nm, C_TEXT);
    if (u->verified) badge_icon(x + pad + nw + PX(4), y + PX(4), PX(18));
    y += F_BIG.lh;
    wchar_t hb[120], *hh = u8w(g_prof.handle);
    _snwprintf(hb, 120, L"@%ls", hh); hb[119] = 0; free(hh);
    text_at(&F_BODY, x + pad, y, hb, (int)wcslen(hb), C_GRAY);
    y += PX(24) + PX(8);

    if (g_prof.bio_len) {
        y += text_flow(&F_BODY, g_prof.bio_w, g_prof.bio_len, g_prof.bio_a, x + pad, y, cw - 2 * pad, 100, C_TEXT, 1, 0, 2);
        y += PX(8);
    }
    /* メタ行 */
    {
        int mx = x + pad, isz = PX(16);
        int row_y = y;
        if (u->location[0]) {
            g_icon(IC_PIN, (float)mx, (float)(row_y + PX(2)), (float)isz, C_GRAY, 0, PX(2) * 0.75f);
            int w2 = text_u8_w(&F_SMALL, u->location);
            text_u8(&F_SMALL, mx + isz + PX(4), row_y, u->location, C_GRAY);
            mx += isz + PX(4) + w2 + PX(14);
        }
        if (u->website[0]) {
            g_icon(IC_LINK, (float)mx, (float)(row_y + PX(2)), (float)isz, C_GRAY, 0, PX(2) * 0.75f);
            const char *ws = u->website;
            if (strncmp(ws, "https://", 8) == 0) ws += 8; else if (strncmp(ws, "http://", 7) == 0) ws += 7;
            int w2 = text_u8_w(&F_SMALL, ws);
            text_u8(&F_SMALL, mx + isz + PX(4), row_y, ws, C_ACCENT);
            mx += isz + PX(4) + w2 + PX(14);
        }
        if (u->created[0]) {
            wchar_t d[40], b[100];
            fmt_full_date(u->created, d, 40, 0);
            _snwprintf(b, 100, L"%ls\u304B\u3089north\u3092\u5229\u7528\u3057\u3066\u3044\u307E\u3059", d); b[99] = 0;
            int need = isz + PX(4) + text_w(&F_SMALL, b, (int)wcslen(b));
            if (mx + need > x + cw - pad) { mx = x + pad; row_y += PX(20); }
            g_icon(IC_CAL, (float)mx, (float)(row_y + PX(2)), (float)isz, C_GRAY, 0, PX(2) * 0.75f);
            text_at(&F_SMALL, mx + isz + PX(4), row_y, b, (int)wcslen(b), C_GRAY);
        }
        y = row_y + PX(20) + PX(6);
    }
    /* フォロー数 */
    {
        wchar_t n1[24], n2[24];
        fmt_count(u->following, n1, 24); fmt_count(u->followers, n2, 24);
        int fx = x + pad;
        text_at(&F_SMALLB, fx, y, n1, (int)wcslen(n1), C_TEXT); fx += text_w(&F_SMALLB, n1, (int)wcslen(n1)) + PX(4);
        text_at(&F_SMALL, fx, y, L"\u30D5\u30A9\u30ED\u30FC\u4E2D", (int)wcslen(L"\u30D5\u30A9\u30ED\u30FC\u4E2D"), C_GRAY); fx += text_w(&F_SMALL, L"\u30D5\u30A9\u30ED\u30FC\u4E2D", (int)wcslen(L"\u30D5\u30A9\u30ED\u30FC\u4E2D")) + PX(16);
        text_at(&F_SMALLB, fx, y, n2, (int)wcslen(n2), C_TEXT); fx += text_w(&F_SMALLB, n2, (int)wcslen(n2)) + PX(4);
        text_at(&F_SMALL, fx, y, L"\u30D5\u30A9\u30ED\u30EF\u30FC", (int)wcslen(L"\u30D5\u30A9\u30ED\u30EF\u30FC"), C_GRAY);
        y += PX(24) + PX(8);
    }
    /* タブ */
    {
        int th = PX(53), tw = cw / 4;
        for (int i = 0; i < 4; i++) {
            int act = i == g_prof.tab;
            TF *tf = act ? &F_BOLD : &F_BODY;
            int lw2 = text_w(tf, TAB_LABEL[i], (int)wcslen(TAB_LABEL[i]));
            text_at(tf, x + i * tw + (tw - lw2) / 2, y + (th - tf->lh) / 2, TAB_LABEL[i], (int)wcslen(TAB_LABEL[i]), act ? C_TEXT : C_GRAY);
            if (act) g_fill_rrect((float)(x + i * tw + (tw - lw2) / 2 - PX(4)), (float)(y + th - PX(4)), (float)(lw2 + PX(8)), (float)PX(4), PX(2), C_ACCENT, 255);
            hit(x + i * tw, y, x + (i + 1) * tw, y + th, H_TAB, i, 0);
        }
        g_hline(x, x + cw, y + th - 1, C_LINE);
        y += th;
    }
    return y - y0;
}

static void request_more(void);

static int draw_feed(Feed *f, int x, int y, int cw)
{
    int y0 = y;
    int total = feed_layout(f, cw);
    for (int i = 0; i < f->n; i++) {
        Post *p = f->p[i];
        int py = y + p->y;
        if (py + p->h > G.vy0 && py < G.vy1) {
            if (f == &g_tfeed && i == g_thr_main) g_rect(x, py, x + cw, py + p->h - 1, RGB(14, 22, 46));
            draw_post(p, i, x, py, cw);
        }
    }
    y += total;
    const wchar_t *msg = NULL;
    if (f->loading) msg = L"\u8AAD\u307F\u8FBC\u307F\u4E2D...";
    else if (f->loaded && f->n == 0) msg = L"\u30DD\u30B9\u30C8\u304C\u3042\u308A\u307E\u305B\u3093";
    else if (f->loaded && !f->cursor[0]) msg = L"\u3053\u3053\u307E\u3067\u3067\u3059";
    if (msg) {
        int w2 = text_w(&F_SMALL, msg, (int)wcslen(msg));
        text_at(&F_SMALL, x + (cw - w2) / 2, y + PX(20), msg, (int)wcslen(msg), C_GRAY);
    }
    y += PX(64);
    return y - y0;
}

static void urlenc(const char *s, char *out, size_t n);
static void draw_badge_num(float cx, float cy, int n);
/* ================================================================
 *  通知 / 検索 / DM  (ここから)
 * ================================================================ */
static void open_profile(const char *handle);
static void show_view(int v);
static void load_bookmarks(int append);

static void ed_get_u8(int idx, char *out, size_t n)
{
    int len = GetWindowTextLengthW(g_ed[idx]);
    out[0] = 0;
    if (len <= 0) return;
    wchar_t *w = (wchar_t *)malloc((size_t)(len + 1) * sizeof(wchar_t));
    GetWindowTextW(g_ed[idx], w, len + 1);
    char *u = wu8(w);
    strncpy(out, u, n - 1); out[n - 1] = 0;
    free(u); free(w);
}

static int draw_tabs(int x, int y, int cw, const wchar_t **lab, int n, int act, int htype)
{
    int th = PX(53), tw = cw / n;
    for (int i = 0; i < n; i++) {
        TF *tf = i == act ? &F_BOLD : &F_BODY;
        int lw = text_w(tf, lab[i], (int)wcslen(lab[i]));
        text_at(tf, x + i * tw + (tw - lw) / 2, y + (th - tf->lh) / 2, lab[i], (int)wcslen(lab[i]), i == act ? C_TEXT : C_GRAY);
        if (i == act) g_fill_rrect((float)(x + i * tw + (tw - lw) / 2 - PX(4)), (float)(y + th - PX(4)), (float)(lw + PX(8)), (float)PX(4), PX(2), C_ACCENT, 255);
        hit(x + i * tw, y, x + (i + 1) * tw, y + th, htype, i, 0);
    }
    g_hline(x, x + cw, y + th - 1, C_LINE);
    return th;
}

static int draw_user_row(int x, int y, int cw, const UserLite *u, const char *sub, int htype, int hidx)
{
    int pad = PX(16), h = sub && sub[0] ? PX(72) : PX(64);
    draw_avatar_ph(u->avatar, u->name, x + pad, y + (h - PX(44)) / 2, PX(44));
    int tx = x + pad + PX(44) + PX(12), w = cw - (tx - x) - pad;
    int nw = text_u8_w(&F_BOLD, u->name);
    if (nw > w - PX(24)) nw = w - PX(24);
    text_clip(&F_BOLD, tx, y + PX(10), nw, u->name, C_TEXT);
    if (u->verified) badge_icon(tx + nw + PX(4), y + PX(13), PX(16));
    wchar_t hb[120], *hh = u8w(u->handle);
    _snwprintf(hb, 120, L"@%ls", hh); hb[119] = 0; free(hh);
    text_clipw(&F_SMALL, tx, y + PX(10) + F_BOLD.lh - PX(2), w, hb, C_GRAY);
    if (sub && sub[0]) text_clip(&F_SMALL, tx, y + PX(10) + F_BOLD.lh + F_SMALL.lh - PX(4), w, sub, C_TEXT);
    hit(x, y, x + cw, y + h, htype, hidx, 0);
    g_hline(x, x + cw, y + h - 1, C_LINE);
    return h;
}

/* ------------------------------------------------------------ 通知 */
typedef struct Notif {
    char id[40], kind[20];
    UserLite act[5]; int nact, actor_count;
    int read; char created[40];
    Post *tweet; int card, fidx;
    wchar_t label[260];
    int lab_h, tw_h, lay_w, y, h;
} Notif;
static Notif **g_notifs;
static int g_nnotif, g_notif_cap;
static char g_notif_cursor[64];
static int g_notif_loading, g_notif_loaded, g_ntab;
static const wchar_t *NTAB_LABEL[2] = { L"すべて", L"メンション" };
static const char *NTAB_KEY[2] = { "all", "mentions" };

static void notif_free_all(void)
{
    for (int i = 0; i < g_nnotif; i++) { free_post(g_notifs[i]->tweet); free(g_notifs[i]); }
    g_nnotif = 0;
    g_nfeed.n = 0;
}
static void notif_label(Notif *n)
{
    wchar_t *nm = u8w(n->nact ? n->act[0].name : "?");
    wchar_t more[40] = L"";
    const wchar_t *tail;
    const char *k = n->kind;
    if (n->actor_count > 1) { _snwprintf(more, 40, L"、他%d人", n->actor_count - 1); more[39] = 0; }
    if (!strcmp(k, "FOLLOW")) tail = L"にフォローされました";
    else if (!strcmp(k, "LIKE")) tail = L"があなたのポストにいいねしました";
    else if (!strcmp(k, "RETWEET")) tail = L"があなたのポストをリポストしました";
    else if (!strcmp(k, "QUOTE")) tail = L"があなたのポストを引用しました";
    else if (!strcmp(k, "REPLY")) tail = L"が返信しました";
    else if (!strcmp(k, "MENTION")) tail = L"があなたをメンションしました";
    else tail = L"から通知があります";
    _snwprintf(n->label, 260, L"%lsさん%ls%ls", nm, more, tail);
    n->label[259] = 0;
    free(nm);
}
static void rebuild_nfeed(void)
{
    g_nfeed.n = 0;
    for (int i = 0; i < g_nnotif; i++) {
        Notif *n = g_notifs[i];
        n->fidx = -1;
        if (n->card) { n->fidx = g_nfeed.n; feed_add(&g_nfeed, n->tweet); }
    }
}
static void parse_notifs(const char *json, int append)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    if (!append) notif_free_all();
    const cJSON *it, *ac;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "items")) {
        Notif *n = (Notif *)calloc(1, sizeof *n);
        jcopy(n->id, sizeof n->id, it, "id");
        jcopy(n->kind, sizeof n->kind, it, "kind");
        jcopy(n->created, sizeof n->created, it, "createdAt");
        n->read = jbool(it, "read");
        cJSON_ArrayForEach(ac, cJSON_GetObjectItemCaseSensitive(it, "actors")) {
            if (n->nact >= 5) break;
            ulite(&n->act[n->nact++], ac);
        }
        n->actor_count = jint(it, "actorCount");
        if (n->actor_count < n->nact) n->actor_count = n->nact;
        const cJSON *tw = cJSON_GetObjectItemCaseSensitive(it, "tweet");
        if (cJSON_IsObject(tw)) n->tweet = parse_post(tw);
        notif_label(n);
        int simple = !strcmp(n->kind, "FOLLOW") || !strcmp(n->kind, "LIKE") || !strcmp(n->kind, "RETWEET");
        n->card = n->tweet && !simple;
        if (n->card) {
            char *u = wu8(n->label);
            strncpy(n->tweet->ctx_text, u, 159);
            free(u);
            n->tweet->ctx_icon = (!strcmp(n->kind, "QUOTE") ? IC_RT : IC_REPLY) + 1;
        }
        if (g_nnotif == g_notif_cap) { g_notif_cap = g_notif_cap ? g_notif_cap * 2 : 64; g_notifs = (Notif **)realloc(g_notifs, (size_t)g_notif_cap * sizeof(Notif *)); }
        g_notifs[g_nnotif++] = n;
    }
    const cJSON *nc = cJSON_GetObjectItemCaseSensitive(root, "nextCursor");
    g_notif_cursor[0] = 0;
    if (cJSON_IsString(nc) && nc->valuestring) strncpy(g_notif_cursor, nc->valuestring, sizeof g_notif_cursor - 1);
    g_notif_loaded = 1;
    cJSON_Delete(root);
    rebuild_nfeed();
}
static void load_notifs(int append)
{
    char path[300], cur[200];
    if (g_notif_loading) return;
    if (append) {
        if (!g_notif_cursor[0]) return;
        urlenc(g_notif_cursor, cur, sizeof cur);
        _snprintf(path, sizeof path, "/api/notifications?tab=%s&cursor=%s", NTAB_KEY[g_ntab], cur);
    } else _snprintf(path, sizeof path, "/api/notifications?tab=%s", NTAB_KEY[g_ntab]);
    path[sizeof path - 1] = 0;
    g_notif_loading = 1;
    net_api(T_NOTIFS, "GET", path, NULL, NULL, g_ntab * 2 + (append ? 1 : 0));
}
static void layout_notif(Notif *n, int cw)
{
    if (n->card) {
        Post *p = n->tweet;
        if (p->lay_w != cw) layout_post(p, cw);
        n->h = p->h;
    } else {
        int pad = PX(16), x0 = pad + PX(44) + PX(8), w = cw - x0 - pad;
        n->lab_h = text_flow(&F_BODY, n->label, (int)wcslen(n->label), NULL, 0, 0, w, 3, 0, 0, 0, 0);
        n->tw_h = (n->tweet && n->tweet->wlen) ? text_flow(&F_BODY, n->tweet->wtext, n->tweet->wlen, NULL, 0, 0, w, 4, 0, 0, 0, 0) : 0;
        n->h = PX(12) + PX(34) + PX(8) + n->lab_h + (n->tw_h ? PX(6) + n->tw_h : 0) + PX(14);
    }
    n->lay_w = cw;
}
static void draw_notif(Notif *n, int idx, int x, int y, int cw)
{
    if (!n->read) g_rect(x, y, x + cw, y + n->h, RGB(15, 23, 48));
    if (n->card) { draw_post(n->tweet, n->fidx, x, y, cw); return; }
    int pad = PX(16), x0 = x + pad + PX(44) + PX(8), w = cw - (x0 - x) - pad;
    int ic = IC_PERSON; COLORREF col = C_ACCENT; int filled = 0;
    if (!strcmp(n->kind, "LIKE")) { ic = IC_HEART; filled = 1; }
    else if (!strcmp(n->kind, "RETWEET")) { ic = IC_RT; col = C_GREEN; }
    g_icon(ic, (float)(x + pad + PX(4)), (float)(y + PX(10)), (float)PX(30), col, filled, PX(2) * 1.1f);
    for (int i = 0; i < n->nact; i++) {
        int ax = x0 + i * (PX(34) + PX(6));
        draw_avatar_ph(n->act[i].avatar, n->act[i].name, ax, y + PX(12), PX(34));
        int before = g_nhit;
        hit(ax, y + PX(12), ax + PX(34), y + PX(46), H_ACTOR, idx, i);
        (void)before;
    }
    int ty = y + PX(12) + PX(34) + PX(8);
    text_flow(&F_BODY, n->label, (int)wcslen(n->label), NULL, x0, ty, w, 3, C_TEXT, 1, 0, 9);
    ty += n->lab_h;
    if (n->tw_h) text_flow(&F_BODY, n->tweet->wtext, n->tweet->wlen, NULL, x0, ty + PX(6), w, 4, C_GRAY, 1, 0, 9);
    hit(x, y, x + cw, y + n->h, H_NOTIF, idx, 0);
    g_hline(x, x + cw, y + n->h - 1, C_LINE);
}
static int draw_notifs(int x, int y, int cw)
{
    int y0 = y;
    y += draw_tabs(x, y, cw, NTAB_LABEL, 2, g_ntab, H_NTAB);
    int yy = y;
    for (int i = 0; i < g_nnotif; i++) {
        Notif *n = g_notifs[i];
        if (n->lay_w != cw || (n->card && n->tweet->lay_w != cw)) layout_notif(n, cw);
        n->y = yy - y; 
        if (yy + n->h > G.vy0 && yy < G.vy1) draw_notif(n, i, x, yy, cw);
        yy += n->h;
    }
    y = yy;
    const wchar_t *msg = NULL;
    if (g_notif_loading) msg = L"読み込み中...";
    else if (g_notif_loaded && !g_nnotif) msg = L"通知はまだありません";
    else if (g_notif_loaded && !g_notif_cursor[0]) msg = L"ここまでです";
    if (msg) { int w2 = text_w(&F_SMALL, msg, (int)wcslen(msg)); text_at(&F_SMALL, x + (cw - w2) / 2, y + PX(20), msg, (int)wcslen(msg), C_GRAY); }
    y += PX(64);
    return y - y0;
}

/* ------------------------------------------------------------ 検索 */
typedef struct { UserLite u; char bio[200]; } Person;
typedef struct { char id[40], query[120]; UserLite user; int has_user; } Hist;
static Person g_ppl[60];
static int g_nppl;
static Hist g_hist[20];
static int g_nhist;
static UserLite g_su[6];
static int g_nsu;
static char g_st[5][100];
static int g_nst;
static char g_sq[200];
static int g_stab, g_sphase, g_search_loaded;
static const wchar_t *STAB_LABEL[4] = { L"トップ", L"最新", L"ユーザー", L"メディア" };
static const char *STAB_KEY[4] = { "top", "latest", "people", "media" };

static void parse_hist(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    g_nhist = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "items")) {
        if (g_nhist >= 20) break;
        Hist *h = &g_hist[g_nhist++];
        memset(h, 0, sizeof *h);
        jcopy(h->id, sizeof h->id, it, "id");
        jcopy(h->query, sizeof h->query, it, "query");
        const cJSON *u = cJSON_GetObjectItemCaseSensitive(it, "user");
        if (cJSON_IsObject(u)) { ulite(&h->user, u); h->has_user = 1; }
    }
    cJSON_Delete(root);
}
static void parse_suggest(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    g_nsu = g_nst = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "users")) { if (g_nsu >= 6) break; ulite(&g_su[g_nsu++], it); }
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "hashtags")) {
        if (g_nst >= 5) break;
        jcopy(g_st[g_nst], sizeof g_st[0], it, "tag");
        if (g_st[g_nst][0]) g_nst++;
    }
    cJSON_Delete(root);
}
static void search_load(int append)
{
    char path[500], enc[400], cur[200];
    if (!g_sq[0] || g_sfeed.loading) return;
    urlenc(g_sq, enc, sizeof enc);
    if (append) {
        if (!g_sfeed.cursor[0]) return;
        urlenc(g_sfeed.cursor, cur, sizeof cur);
        _snprintf(path, sizeof path, "/api/search?q=%s&tab=%s&cursor=%s", enc, STAB_KEY[g_stab], cur);
    } else _snprintf(path, sizeof path, "/api/search?q=%s&tab=%s", enc, STAB_KEY[g_stab]);
    path[sizeof path - 1] = 0;
    g_sfeed.loading = 1;
    net_api(T_SEARCH, "GET", path, NULL, g_sq, g_stab * 2 + (append ? 1 : 0));
}
static void search_reset_results(void)
{
    feed_clear(&g_sfeed);
    g_sfeed.loaded = 0;
    g_nppl = 0;
}
static void do_search(void)
{
    char q[200];
    ed_get_u8(ED_SEARCH, q, sizeof q);
    char *s = q;
    while (*s == ' ') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    if (!n) return;
    strncpy(g_sq, s, sizeof g_sq - 1); g_sq[sizeof g_sq - 1] = 0;
    g_sphase = 1;
    g_scroll[V_SEARCH] = 0;
    search_reset_results();
    search_load(0);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "query", g_sq);
    char *js = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    net_api(T_SHIST_ADD, "POST", "/api/search/history", js, NULL, 0);
    cJSON_free(js);
    SetFocus(g_main);
    InvalidateRect(g_main, NULL, FALSE);
}
static void search_for(const char *q)
{
    show_view(V_SEARCH);
    wchar_t *w = u8w(q);
    SetWindowTextW(g_ed[ED_SEARCH], w);
    free(w);
    do_search();
}
static void parse_search(const char *json, int append)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    if (!append) search_reset_results();
    const cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "items")) {
        if (cJSON_GetObjectItemCaseSensitive(it, "text") || cJSON_GetObjectItemCaseSensitive(it, "author")) {
            Post *p = parse_post(it);
            if (!p) continue;
            if (append && !p->rt_name[0] && feed_has(&g_sfeed, p->id)) { free_post(p); continue; }
            feed_add(&g_sfeed, p);
        } else if (cJSON_GetObjectItemCaseSensitive(it, "handle") && g_nppl < 60) {
            Person *pe = &g_ppl[g_nppl++];
            memset(pe, 0, sizeof *pe);
            ulite(&pe->u, it);
            jcopy(pe->bio, sizeof pe->bio, it, "bio");
        }
    }
    const cJSON *nc = cJSON_GetObjectItemCaseSensitive(root, "nextCursor");
    g_sfeed.cursor[0] = 0;
    if (cJSON_IsString(nc) && nc->valuestring) strncpy(g_sfeed.cursor, nc->valuestring, sizeof g_sfeed.cursor - 1);
    g_sfeed.loaded = 1;
    cJSON_Delete(root);
}

static int draw_search(int x, int y, int cw)
{
    int y0 = y, pad = PX(16);
    char cur[200];
    ed_get_u8(ED_SEARCH, cur, sizeof cur);
    if (g_sphase == 1) {
        y += draw_tabs(x, y, cw, STAB_LABEL, 4, g_stab, H_STAB);
        if (g_stab == 2) {
            for (int i = 0; i < g_nppl; i++) {
                if (y + PX(72) > G.vy0 && y < G.vy1) y += draw_user_row(x, y, cw, &g_ppl[i].u, g_ppl[i].bio, H_PERSON, i);
                else y += g_ppl[i].bio[0] ? PX(72) : PX(64);
            }
            const wchar_t *msg = g_sfeed.loading ? L"読み込み中..." : (g_sfeed.loaded && !g_nppl ? L"ユーザーが見つかりません" : NULL);
            if (msg) text_at(&F_SMALL, x + pad, y + PX(20), msg, (int)wcslen(msg), C_GRAY);
            y += PX(64);
        } else y += draw_feed(&g_sfeed, x, y, cw);
        return y - y0;
    }
    if (cur[0]) {
        /* 入力中: サジェスト */
        wchar_t *q = u8w(cur), b[260];
        _snwprintf(b, 260, L"「%ls」を検索", q); b[259] = 0;
        free(q);
        g_icon(IC_SEARCH, (float)(x + pad), (float)(y + PX(16)), (float)PX(22), C_GRAY, 0, PX(2) * 0.9f);
        text_clipw(&F_BOLD, x + pad + PX(36), y + PX(14), cw - PX(60), b, C_TEXT);
        hit(x, y, x + cw, y + PX(52), H_SUGQ, 0, 0);
        g_hline(x, x + cw, y + PX(52) - 1, C_LINE);
        y += PX(52);
        for (int i = 0; i < g_nst; i++) {
            wchar_t *t = u8w(g_st[i]);
            _snwprintf(b, 260, L"#%ls", t); b[259] = 0;
            free(t);
            text_clipw(&F_BOLD, x + pad, y + PX(14), cw - 2 * pad, b, C_TEXT);
            hit(x, y, x + cw, y + PX(52), H_SUGT, i, 0);
            g_hline(x, x + cw, y + PX(52) - 1, C_LINE);
            y += PX(52);
        }
        for (int i = 0; i < g_nsu; i++) y += draw_user_row(x, y, cw, &g_su[i], NULL, H_SUGU, i);
        return y - y0;
    }
    /* 検索履歴 */
    text_at(&F_TITLE, x + pad, y + PX(12), L"最近の検索", (int)wcslen(L"最近の検索"), C_TEXT);
    y += PX(56);
    if (!g_nhist) {
        text_at(&F_BODY, x + pad, y + PX(4), L"検索履歴はありません", (int)wcslen(L"検索履歴はありません"), C_GRAY);
        y += PX(40);
    }
    for (int i = 0; i < g_nhist; i++) {
        Hist *h = &g_hist[i];
        int rh = PX(56);
        if (h->has_user) {
            draw_avatar_ph(h->user.avatar, h->user.name, x + pad, y + PX(8), PX(40));
            text_clip(&F_BOLD, x + pad + PX(52), y + PX(8), cw - PX(140), h->user.name, C_TEXT);
            wchar_t hb[120], *hh = u8w(h->user.handle);
            _snwprintf(hb, 120, L"@%ls", hh); hb[119] = 0; free(hh);
            text_clipw(&F_SMALL, x + pad + PX(52), y + PX(8) + F_BOLD.lh - PX(2), cw - PX(140), hb, C_GRAY);
        } else {
            g_icon(IC_SEARCH, (float)(x + pad + PX(8)), (float)(y + PX(16)), (float)PX(24), C_GRAY, 0, PX(2) * 0.9f);
            text_clip(&F_BOLD, x + pad + PX(52), y + (rh - F_BOLD.lh) / 2, cw - PX(140), h->query, C_TEXT);
        }
        hit(x, y, x + cw - PX(56), y + rh, H_HIST, i, 0);
        g_icon(IC_CLOSE, (float)(x + cw - pad - PX(24)), (float)(y + PX(16)), (float)PX(22), C_GRAY, 0, PX(2) * 0.9f);
        hit(x + cw - PX(56), y, x + cw, y + rh, H_HIST_DEL, i, 0);
        g_hline(x, x + cw, y + rh - 1, C_LINE);
        y += rh;
    }
    /* トレンド */
    if (g_ntrends) {
        y += PX(12);
        text_at(&F_TITLE, x + pad, y + PX(8), L"いまどうしてる？", (int)wcslen(L"いまどうしてる？"), C_TEXT);
        y += PX(52);
        for (int i = 0; i < g_ntrends && i < 8; i++) {
            wchar_t b[200], *t = u8w(g_trends[i].tag), c[24];
            _snwprintf(b, 200, L"%d · 日本のトレンド", i + 1); b[199] = 0;
            text_at(&F_SMALL, x + pad, y, b, (int)wcslen(b), C_GRAY);
            _snwprintf(b, 200, L"%ls%ls", g_trends[i].is_hash ? L"#" : L"", t); b[199] = 0;
            text_clipw(&F_TAG, x + pad, y + PX(18), cw - 2 * pad, b, C_TEXT);
            fmt_count(g_trends[i].count, c, 24);
            _snwprintf(b, 200, L"%ls件のポスト", c); b[199] = 0;
            text_at(&F_SMALL, x + pad, y + PX(18) + F_TAG.lh - PX(2), b, (int)wcslen(b), C_GRAY);
            free(t);
            hit(x, y - PX(4), x + cw, y + PX(60), H_TREND, i, 0);
            y += PX(66);
        }
    }
    y += PX(40);
    return y - y0;
}

/* ------------------------------------------------------------ DM 一覧 */
typedef struct {
    char id[40], name[160];
    int group;
    UserLite part[4]; int npart;
    char last[220]; char updated[40];
    int unread, last_mine, request;
} Conv;
static Conv g_convs[100];
static int g_nconv, g_req_count, g_dm_req, g_dm_loading, g_dm_loaded;

static int is_me(const char *handle) { return g_have_me && _stricmp(handle, g_me.handle) == 0; }

static void conv_title(const Conv *c, char *out, size_t n)
{
    out[0] = 0;
    if (c->group && c->name[0]) { strncpy(out, c->name, n - 1); out[n - 1] = 0; return; }
    size_t k = 0;
    for (int i = 0; i < c->npart; i++) {
        if (is_me(c->part[i].handle)) continue;
        if (k) k += (size_t)_snprintf(out + k, n - k, "、");
        k += (size_t)_snprintf(out + k, n - k, "%s", c->part[i].name);
        if (k >= n - 8) break;
    }
    if (!out[0] && c->npart) { strncpy(out, c->part[0].name, n - 1); out[n - 1] = 0; }
    out[n - 1] = 0;
}
static const UserLite *conv_peer(const Conv *c)
{
    for (int i = 0; i < c->npart; i++) if (!is_me(c->part[i].handle)) return &c->part[i];
    return c->npart ? &c->part[0] : NULL;
}
static void parse_conv_obj(const cJSON *it, Conv *c)
{
    const cJSON *p;
    memset(c, 0, sizeof *c);
    jcopy(c->id, sizeof c->id, it, "id");
    jcopy(c->name, sizeof c->name, it, "name");
    jcopy(c->updated, sizeof c->updated, it, "updatedAt");
    c->group = jbool(it, "group");
    c->unread = jint(it, "unreadCount");
    c->request = jbool(it, "request");
    cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(it, "participants")) {
        if (c->npart >= 4) break;
        ulite(&c->part[c->npart++], p);
    }
    const cJSON *lm = cJSON_GetObjectItemCaseSensitive(it, "lastMessage");
    if (cJSON_IsObject(lm)) {
        jcopy(c->last, sizeof c->last, lm, "text");
        const cJSON *se = cJSON_GetObjectItemCaseSensitive(lm, "sender");
        char hd[80];
        jcopy(hd, sizeof hd, se, "handle");
        c->last_mine = is_me(hd);
        if (!c->last[0]) {
            const cJSON *md = cJSON_GetObjectItemCaseSensitive(lm, "media");
            if (cJSON_IsArray(md) && cJSON_GetArraySize(md) > 0) strcpy(c->last, "(画像)");
        }
        for (char *q = c->last; *q; q++) if (*q == '\n' || *q == '\r') *q = ' ';
    }
}
static void parse_convs(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    g_nconv = 0;
    g_req_count = jint(root, "requestCount");
    const cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "items")) {
        if (g_nconv >= 100) break;
        parse_conv_obj(it, &g_convs[g_nconv++]);
    }
    g_dm_loaded = 1;
    cJSON_Delete(root);
}
static void load_convs(void)
{
    if (g_dm_loading) return;
    g_dm_loading = 1;
    net_api(T_CONVS, "GET", g_dm_req ? "/api/dm/conversations?requests=true" : "/api/dm/conversations?requests=false", NULL, NULL, g_dm_req);
}
static int draw_dm_list(int x, int y, int cw)
{
    int y0 = y, pad = PX(16);
    if (!g_dm_req && g_req_count > 0) {
        text_at(&F_BOLD, x + pad, y + PX(18), L"メッセージリクエスト", (int)wcslen(L"メッセージリクエスト"), C_TEXT);
        draw_badge_num((float)(x + cw - pad - PX(20)), (float)(y + PX(28)), g_req_count);
        hit(x, y, x + cw, y + PX(56), H_DM_REQ, 0, 0);
        g_hline(x, x + cw, y + PX(56) - 1, C_LINE);
        y += PX(56);
    } else if (g_dm_req) {
        g_icon(IC_BACK, (float)(x + pad), (float)(y + PX(16)), (float)PX(22), C_TEXT, 0, PX(2));
        text_at(&F_BOLD, x + pad + PX(36), y + PX(18), L"受信トレイに戻る", (int)wcslen(L"受信トレイに戻る"), C_TEXT);
        hit(x, y, x + cw, y + PX(56), H_DM_REQ, 0, 0);
        g_hline(x, x + cw, y + PX(56) - 1, C_LINE);
        y += PX(56);
    }
    for (int i = 0; i < g_nconv; i++) {
        Conv *c = &g_convs[i];
        int rh = PX(78);
        if (y + rh > G.vy0 && y < G.vy1) {
            const UserLite *pe = conv_peer(c);
            char title[200];
            conv_title(c, title, sizeof title);
            draw_avatar_ph(pe ? pe->avatar : "", title, x + pad, y + PX(15), PX(48));
            if (c->group) {
                g_fill_circle(x + pad + PX(44), y + PX(15) + PX(44), PX(9), C_ACCENT, 255);
                g_icon(IC_PERSON, (float)(x + pad + PX(38)), (float)(y + PX(15) + PX(38)), (float)PX(12), RGB(255, 255, 255), 0, 1.2f);
            }
            int tx = x + pad + PX(48) + PX(12), w = cw - (tx - x) - pad;
            wchar_t tm[40];
            fmt_rel_time(c->updated, tm, 40);
            int tw = text_w(&F_SMALL, tm, (int)wcslen(tm));
            text_clip(&F_BOLD, tx, y + PX(14), w - tw - PX(10), title, C_TEXT);
            text_at(&F_SMALL, x + cw - pad - tw, y + PX(14), tm, (int)wcslen(tm), C_GRAY);
            char sn[260];
            _snprintf(sn, sizeof sn, "%s%s", c->last_mine ? "あなた: " : "", c->last);
            sn[sizeof sn - 1] = 0;
            text_clip(&F_BODY, tx, y + PX(14) + F_BOLD.lh, w - (c->unread > 0 ? PX(34) : 0), sn, c->unread > 0 ? C_TEXT : C_GRAY);
            if (c->unread > 0) draw_badge_num((float)(x + cw - pad - PX(10)), (float)(y + PX(14) + F_BOLD.lh + PX(10)), c->unread);
            hit(x, y, x + cw, y + rh, H_CONV, i, 0);
            g_hline(x, x + cw, y + rh - 1, C_LINE);
        }
        y += rh;
    }
    const wchar_t *msg = g_dm_loading ? L"読み込み中..." : (g_dm_loaded && !g_nconv ? L"メッセージはまだありません" : NULL);
    if (msg) { int w2 = text_w(&F_SMALL, msg, (int)wcslen(msg)); text_at(&F_SMALL, x + (cw - w2) / 2, y + PX(24), msg, (int)wcslen(msg), C_GRAY); }
    y += PX(64);
    return y - y0;
}

/* ------------------------------------------------------------ DM チャット */
typedef struct DmMsg {
    char id[40], created[40];
    UserLite s;
    wchar_t *w; BYTE *a; int wlen;
    int mine, system;
    Media media[2]; int nmedia;
    int lay_w, y, h, th, bw, media_h[2];
} DmMsg;
typedef struct {
    DmMsg **m; int n, cap;
    char cursor[64], conv[40], title[200];
    UserLite peer; int group, loaded, loading, typing;
    char typing_name[80];
} Chat;
static Chat g_chat;
static int g_chat_stick, g_typing_sent_at;
static DWORD g_typing_tick;

static void msg_free(DmMsg *m) { if (!m) return; free(m->w); free(m->a); free(m); }
static void chat_clear(void)
{
    for (int i = 0; i < g_chat.n; i++) msg_free(g_chat.m[i]);
    g_chat.n = 0;
}
static DmMsg *parse_msg(const cJSON *it)
{
    DmMsg *m = (DmMsg *)calloc(1, sizeof *m);
    jcopy(m->id, sizeof m->id, it, "id");
    if (!m->id[0]) { free(m); return NULL; }
    jcopy(m->created, sizeof m->created, it, "createdAt");
    ulite(&m->s, cJSON_GetObjectItemCaseSensitive(it, "sender"));
    m->mine = is_me(m->s.handle);
    m->system = jbool(it, "system");
    const cJSON *tx = cJSON_GetObjectItemCaseSensitive(it, "text");
    set_text(cJSON_IsString(tx) && tx->valuestring ? tx->valuestring : "", &m->w, &m->a, &m->wlen);
    const cJSON *md;
    cJSON_ArrayForEach(md, cJSON_GetObjectItemCaseSensitive(it, "media")) {
        if (m->nmedia >= 2) break;
        Media *d = &m->media[m->nmedia++];
        jcopy(d->url, sizeof d->url, md, "url");
        jcopy(d->thumb, sizeof d->thumb, md, "thumbnailUrl");
        if (!d->thumb[0]) strcpy(d->thumb, d->url);
        d->w = jint(md, "width"); d->h = jint(md, "height");
        char kind[24]; jcopy(kind, sizeof kind, md, "kind");
        d->kind = strcmp(kind, "PHOTO") == 0 ? 0 : 1;
    }
    return m;
}
static void chat_add(DmMsg *m)
{
    if (g_chat.n == g_chat.cap) { g_chat.cap = g_chat.cap ? g_chat.cap * 2 : 64; g_chat.m = (DmMsg **)realloc(g_chat.m, (size_t)g_chat.cap * sizeof(DmMsg *)); }
    g_chat.m[g_chat.n++] = m;
}
/* mode 0=初回(置換) 1=過去分(先頭に追加) 2=ポーリング(変化があれば置換) */
static int g_chat_new_incoming;
static void parse_chat(const char *json, int mode)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    DmMsg **nm = NULL; int nn = 0;
    const cJSON *it;
    int cnt = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(root, "items"));
    nm = (DmMsg **)calloc((size_t)(cnt > 0 ? cnt : 1), sizeof(DmMsg *));
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "items")) {
        DmMsg *m = parse_msg(it);
        if (m) nm[nn++] = m;
    }
    const cJSON *nc = cJSON_GetObjectItemCaseSensitive(root, "nextCursor");
    char ncur[64] = "";
    if (cJSON_IsString(nc) && nc->valuestring) strncpy(ncur, nc->valuestring, sizeof ncur - 1);

    if (mode == 2) {
        int same = nn == g_chat.n && (nn == 0 || strcmp(nm[nn - 1]->id, g_chat.m[g_chat.n - 1]->id) == 0);
        if (same) { for (int i = 0; i < nn; i++) msg_free(nm[i]); free(nm); cJSON_Delete(root); return; }
        if (nn > 0 && !nm[nn - 1]->mine) g_chat_new_incoming = 1;
        mode = 0;
        g_chat_stick = 1;
    } else if (mode == 0) g_chat_stick = 1;
    if (mode == 0) {
        chat_clear();
        for (int i = 0; i < nn; i++) chat_add(nm[i]);
        strncpy(g_chat.cursor, ncur, sizeof g_chat.cursor - 1);
        g_chat.cursor[sizeof g_chat.cursor - 1] = 0;
    } else {
        /* 過去分を先頭へ */
        DmMsg **old = g_chat.m; int on = g_chat.n;
        g_chat.m = NULL; g_chat.n = g_chat.cap = 0;
        for (int i = 0; i < nn; i++) chat_add(nm[i]);
        for (int i = 0; i < on; i++) chat_add(old[i]);
        free(old);
        strncpy(g_chat.cursor, ncur, sizeof g_chat.cursor - 1);
        g_chat.cursor[sizeof g_chat.cursor - 1] = 0;
    }
    free(nm);
    for (int i = 0; i < g_chat.n; i++) g_chat.m[i]->lay_w = -1;
    g_chat.loaded = 1;
    cJSON_Delete(root);
}

static void layout_msg(DmMsg *m, int cw)
{
    int maxw = cw * 72 / 100 - PX(24) - (g_chat.group && !m->mine ? PX(40) : 0);
    m->media_h[0] = m->media_h[1] = 0;
    if (m->system) { m->h = PX(40); m->lay_w = cw; return; }
    int h = PX(6);
    for (int i = 0; i < m->nmedia; i++) {
        int mw = maxw < PX(260) ? maxw : PX(260), mh = mw * 3 / 4;
        if (m->media[i].w > 0 && m->media[i].h > 0) mh = (int)((long long)mw * m->media[i].h / m->media[i].w);
        if (mh < PX(60)) mh = PX(60);
        if (mh > PX(300)) mh = PX(300);
        m->media_h[i] = mh;
        h += mh + PX(4);
    }
    m->th = m->wlen ? text_flow(&F_BODY, m->w, m->wlen, NULL, 0, 0, maxw, 1000, 0, 0, 0, 0) : 0;
    m->bw = maxw + PX(24);
    if (m->th == F_BODY.lh) { int tw = text_w(&F_BODY, m->w, m->wlen) + PX(24); if (tw < m->bw) m->bw = tw; }
    if (m->th) h += m->th + PX(16);
    h += PX(20);
    m->h = h;
    m->lay_w = cw;
}

static void fmt_clock(const char *iso, wchar_t *out, int n)
{
    int Y, M, D, h, mi;
    out[0] = 0;
    if (sscanf(iso, "%d-%d-%dT%d:%d", &Y, &M, &D, &h, &mi) != 5) return;
    SYSTEMTIME st, lt;
    memset(&st, 0, sizeof st);
    st.wYear = (WORD)Y; st.wMonth = (WORD)M; st.wDay = (WORD)D; st.wHour = (WORD)h; st.wMinute = (WORD)mi;
    if (!SystemTimeToTzSpecificLocalTime(NULL, &st, &lt)) lt = st;
    SYSTEMTIME now; GetLocalTime(&now);
    if (lt.wYear == now.wYear && lt.wMonth == now.wMonth && lt.wDay == now.wDay) _snwprintf(out, (size_t)n, L"%02d:%02d", lt.wHour, lt.wMinute);
    else _snwprintf(out, (size_t)n, L"%d/%d %02d:%02d", lt.wMonth, lt.wDay, lt.wHour, lt.wMinute);
    out[n - 1] = 0;
}

static int draw_chat(int x, int y, int cw)
{
    int y0 = y, pad = PX(12);
    if (g_chat.cursor[0]) {
        const wchar_t *t = L"以前のメッセージを読み込む";
        int w2 = text_w(&F_SMALL, t, (int)wcslen(t));
        text_at(&F_SMALL, x + (cw - w2) / 2, y + PX(14), t, (int)wcslen(t), C_ACCENT);
        hit(x, y, x + cw, y + PX(44), H_CHAT_OLDER, 0, 0);
        y += PX(44);
    } else y += PX(12);
    for (int i = 0; i < g_chat.n; i++) {
        DmMsg *m = g_chat.m[i];
        if (m->lay_w != cw) layout_msg(m, cw);
        m->y = y - y0;
        if (y + m->h > G.vy0 && y < G.vy1) {
            if (m->system) {
                int w2 = text_w(&F_SMALL, m->w, m->wlen);
                text_at(&F_SMALL, x + (cw - w2) / 2, y + PX(12), m->w, m->wlen, C_GRAY);
            } else {
                int maxw = cw * 72 / 100 - PX(24) - (g_chat.group && !m->mine ? PX(40) : 0);
                int bx = m->mine ? x + cw - pad - m->bw : x + pad + (g_chat.group ? PX(40) : 0);
                int cy = y + PX(6);
                if (g_chat.group && !m->mine) draw_avatar_ph(m->s.avatar, m->s.name, x + pad, y + PX(6), PX(32));
                for (int k = 0; k < m->nmedia; k++) {
                    int mw = maxw < PX(260) ? maxw : PX(260);
                    int mx = m->mine ? x + cw - pad - mw : bx;
                    g_draw_img(img_get(m->media[k].thumb, 800), (float)mx, (float)cy, (float)mw, (float)m->media_h[k], (float)PX(14));
                    hit(mx, cy, mx + mw, cy + m->media_h[k], H_DMMEDIA, i, k);
                    cy += m->media_h[k] + PX(4);
                }
                if (m->th) {
                    g_fill_rrect((float)bx, (float)cy, (float)m->bw, (float)(m->th + PX(16)), PX(18), m->mine ? C_ACCENT : RGB(32, 42, 72), 255);
                    text_flow(&F_BODY, m->w, m->wlen, m->mine ? NULL : m->a, bx + PX(12), cy + PX(8), m->bw - PX(24), 1000,
                              m->mine ? RGB(255, 255, 255) : C_TEXT, 1, i, 5);
                    cy += m->th + PX(16);
                }
                wchar_t tm[40];
                fmt_clock(m->created, tm, 40);
                int tw = text_w(&F_SMALL, tm, (int)wcslen(tm));
                int fw = m->mine ? tw : tw;
                text_at(&F_SMALL, m->mine ? x + cw - pad - fw : bx, cy + PX(2), tm, (int)wcslen(tm), C_GRAY);
            }
        }
        y += m->h;
    }
    if (g_chat.typing) {
        wchar_t b[160], *n = u8w(g_chat.typing_name[0] ? g_chat.typing_name : g_chat.title);
        _snwprintf(b, 160, L"%lsさんが入力中…", n); b[159] = 0; free(n);
        text_at(&F_SMALL, x + pad, y + PX(4), b, (int)wcslen(b), C_GRAY);
        y += PX(28);
    }
    y += PX(16);
    return y - y0;
}

static int chat_total(int cw)
{
    int y = g_chat.cursor[0] ? PX(44) : PX(12);
    for (int i = 0; i < g_chat.n; i++) {
        DmMsg *m = g_chat.m[i];
        if (m->lay_w != cw) layout_msg(m, cw);
        y += m->h;
    }
    if (g_chat.typing) y += PX(28);
    return y + PX(16);
}

static void open_chat_conv(const Conv *c)
{
    chat_clear();
    memset(&g_chat.peer, 0, sizeof g_chat.peer);
    g_chat.cursor[0] = 0; g_chat.loaded = 0; g_chat.loading = 1; g_chat.typing = 0;
    strncpy(g_chat.conv, c->id, sizeof g_chat.conv - 1);
    conv_title(c, g_chat.title, sizeof g_chat.title);
    g_chat.group = c->group;
    const UserLite *pe = conv_peer(c);
    if (pe) g_chat.peer = *pe;
    g_view = V_CHAT; g_drawer = 0;
    g_scroll[V_CHAT] = 0; g_chat_stick = 1;
    SetWindowTextW(g_ed[ED_DM], L"");
    char path[200];
    _snprintf(path, sizeof path, "/api/dm/conversations/%s/messages", g_chat.conv);
    net_api(T_CHAT, "GET", path, NULL, g_chat.conv, 0);
    _snprintf(path, sizeof path, "/api/dm/conversations/%s/read", g_chat.conv);
    net_api(T_CHAT_READ, "POST", path, NULL, NULL, 0);
    InvalidateRect(g_main, NULL, FALSE);
}
static void chat_poll(void)
{
    if (g_view != V_CHAT || !g_chat.conv[0]) return;
    char path[200];
    _snprintf(path, sizeof path, "/api/dm/conversations/%s/messages", g_chat.conv);
    net_api(T_CHAT, "GET", path, NULL, g_chat.conv, 2);
    _snprintf(path, sizeof path, "/api/dm/conversations/%s/typing", g_chat.conv);
    net_api(T_TYPING_GET, "GET", path, NULL, g_chat.conv, 0);
}
static void chat_older(void)
{
    if (!g_chat.cursor[0] || g_chat.loading) return;
    char path[300], cur[200];
    urlenc(g_chat.cursor, cur, sizeof cur);
    _snprintf(path, sizeof path, "/api/dm/conversations/%s/messages?cursor=%s", g_chat.conv, cur);
    g_chat.loading = 1;
    net_api(T_CHAT, "GET", path, NULL, g_chat.conv, 1);
}
static void typing_set(int active)
{
    if (!g_chat.conv[0]) return;
    char path[200];
    _snprintf(path, sizeof path, "/api/dm/conversations/%s/typing", g_chat.conv);
    net_api(T_TYPING_SET, "POST", path, active ? "{\"active\":true}" : "{\"active\":false}", NULL, 0);
}
static void chat_send(void)
{
    char t[4000];
    ed_get_u8(ED_DM, t, sizeof t);
    for (char *s = t, *d = t;; s++) { if (*s != '\r') *d++ = *s; if (!*s) break; }
    size_t n = strlen(t);
    while (n && (t[n - 1] == '\n' || t[n - 1] == ' ')) t[--n] = 0;
    if ((!n && att_ready(AT_DM) == 0) || !g_chat.conv[0]) return;
    if (att_pending(AT_DM)) { ui_toast(L"画像のアップロード中です"); return; }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "text", t);
    cJSON_AddItemToObject(o, "mediaIds", att_ids(AT_DM));
    cJSON_AddNullToObject(o, "replyToId");
    char *js = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    char path[200];
    _snprintf(path, sizeof path, "/api/dm/conversations/%s/messages", g_chat.conv);
    net_api(T_CHAT_SEND, "POST", path, js, g_chat.conv, 0);
    cJSON_free(js);
    SetWindowTextW(g_ed[ED_DM], L"");
    typing_set(0);
    g_typing_tick = 0;
}
static void dm_with(const char *handle)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "handle", handle);
    char *js = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    net_api(T_DM_CREATE, "POST", "/api/dm/conversations", js, handle, 0);
    cJSON_free(js);
    ui_toast(L"会話を開いています...");
}

/* チャット / スレッド下部の入力バー */
static void draw_chat_input(void)
{
    int c = g_view == V_THREAD ? AT_THR : AT_DM;
    int x = G.cx, w = G.cw, y = G.vy1, h = G.H - G.bot_h - G.vy1;
    g_rect(x, y, x + w, y + h, C_BG);
    g_hline(x, x + w, y, C_LINE);
    int bs = PX(44), pad = PX(12), row = PX(68), rowy = y + h - row;
    if (g_natt[c]) draw_atts(c, x + pad, y + PX(8));
    int py = rowy + (row - bs) / 2, can = g_natt[c] < 4;
    g_icon(IC_IMAGE, (float)(x + pad + PX(4)), (float)(py + PX(10)), (float)PX(24), can ? C_ACCENT : C_BTN_OFF, 0, PX(2) * 0.9f);
    if (can) hit(x + pad - PX(2), py, x + pad + PX(40), py + bs, H_ATTACH, c, 0);
    int px = x + pad + PX(40), pw = w - 2 * pad - bs - PX(8) - PX(40);
    g_fill_rrect((float)px, (float)py, (float)pw, (float)bs, bs / 2.0f, C_PANEL, 255);
    place_edit(ED_DM, 1, px + PX(16), py + PX(11), pw - PX(32), PX(22));
    int on = (GetWindowTextLengthW(g_ed[ED_DM]) > 0 || att_ready(c) > 0) && !att_pending(c);
    int bx = x + w - pad - bs;
    g_fill_circle(bx + bs / 2.0f, py + bs / 2.0f, bs / 2.0f, on ? C_ACCENT : C_BTN_OFF, 255);
    g_icon(IC_SEND, (float)(bx + PX(11)), (float)(py + PX(11)), (float)PX(22), on ? RGB(255, 255, 255) : RGB(150, 160, 180), 1, 1);
    if (on) hit(bx, py, bx + bs, py + bs, g_view == V_THREAD ? H_THR_SEND : H_DM_SEND, 0, 0);
}

/* ------------------------------------------------------------ ビュー切り替え / フォロー */
static void show_view(int v)
{
    g_drawer = 0;
    g_view = v;
    switch (v) {
    case V_NOTIF: g_scroll[V_NOTIF] = 0; load_notifs(0); break;
    case V_SEARCH: g_scroll[V_SEARCH] = 0; g_focus_search = 1; net_api(T_SHIST, "GET", "/api/search/history", NULL, NULL, 0); break;
    case V_DM: g_scroll[V_DM] = 0; g_dm_req = 0; load_convs(); break;
    case V_BM: g_scroll[V_BM] = 0; load_bookmarks(0); break;
    }
    InvalidateRect(g_main, NULL, FALSE);
}
static void toggle_follow(void)
{
    if (!g_prof.have) return;
    int cur = g_prof.u.is_following || g_prof.u.follow_req, want = !cur;
    if (want) { if (g_prof.u.prot) g_prof.u.follow_req = 1; else g_prof.u.is_following = 1; g_prof.u.followers++; }
    else { g_prof.u.is_following = g_prof.u.follow_req = 0; if (g_prof.u.followers > 0) g_prof.u.followers--; }
    char path[200];
    _snprintf(path, sizeof path, "/api/users/%s/follow", g_prof.handle);
    net_api(T_FOLLOW, want ? "POST" : "DELETE", path, NULL, g_prof.handle, want);
    InvalidateRect(g_main, NULL, FALSE);
}
static void dm_back_to_list(void)
{
    g_chat.conv[0] = 0;
    typing_set(0);
    show_view(V_DM);
}


/* ------------------------------------------------------------ スレッド / ブックマーク */
static void open_thread(const char *id)
{
    if (!id || !id[0]) return;
    if (g_view == V_THREAD && strcmp(g_thr_id, id) == 0) return;
    if (g_view != V_THREAD) g_back_to = g_view;
    g_view = V_THREAD; g_drawer = 0;
    strncpy(g_thr_id, id, sizeof g_thr_id - 1);
    feed_clear(&g_tfeed);
    g_tfeed.loaded = 0; g_tfeed.loading = 1; g_thr_main = -1;
    g_scroll[V_THREAD] = 0;
    SetWindowTextW(g_ed[ED_DM], L"");
    char path[120];
    _snprintf(path, sizeof path, "/api/tweets/%s/conversation", id);
    net_api(T_THREAD, "GET", path, NULL, id, 0);
    InvalidateRect(g_main, NULL, FALSE);
}
static void thread_reload(void)
{
    char path[120];
    _snprintf(path, sizeof path, "/api/tweets/%s/conversation", g_thr_id);
    net_api(T_THREAD, "GET", path, NULL, g_thr_id, 1);
}
static void parse_thread(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    const cJSON *it;
    feed_clear(&g_tfeed);
    g_thr_main = -1;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "ancestors")) { Post *p = parse_post(it); if (p) feed_add(&g_tfeed, p); }
    const cJSON *tw = cJSON_GetObjectItemCaseSensitive(root, "tweet");
    if (cJSON_IsObject(tw)) { Post *p = parse_post(tw); if (p) { g_thr_main = g_tfeed.n; feed_add(&g_tfeed, p); } }
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "replies")) { Post *p = parse_post(it); if (p) feed_add(&g_tfeed, p); }
    g_tfeed.cursor[0] = 0;
    g_tfeed.loaded = 1;
    cJSON_Delete(root);
}
static void thread_reply(void)
{
    char t[4000];
    ed_get_u8(ED_DM, t, sizeof t);
    for (char *s2 = t, *d = t;; s2++) { if (*s2 != '\r') *d++ = *s2; if (!*s2) break; }
    size_t n = strlen(t);
    while (n && (t[n - 1] == '\n' || t[n - 1] == ' ')) t[--n] = 0;
    if ((!n && att_ready(AT_THR) == 0) || g_thr_main < 0 || g_thr_main >= g_tfeed.n) return;
    if (att_pending(AT_THR)) { ui_toast(L"画像のアップロード中です"); return; }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "text", t);
    cJSON_AddStringToObject(o, "replyPolicy", "EVERYONE");
    cJSON_AddItemToObject(o, "mediaIds", att_ids(AT_THR));
    cJSON_AddStringToObject(o, "inReplyToId", g_tfeed.p[g_thr_main]->id);
    char *js = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    net_api(T_THREAD_REPLY, "POST", "/api/tweets", js, g_thr_id, 0);
    cJSON_free(js);
    SetWindowTextW(g_ed[ED_DM], L"");
    ui_toast(L"送信中...");
}
static void load_bookmarks(int append)
{
    char path[300], cur[200];
    if (g_bfeed.loading) return;
    if (append) {
        if (!g_bfeed.cursor[0]) return;
        urlenc(g_bfeed.cursor, cur, sizeof cur);
        _snprintf(path, sizeof path, "/api/bookmarks?cursor=%s", cur);
    } else strcpy(path, "/api/bookmarks");
    g_bfeed.loading = 1;
    net_api(T_BOOKMARKS, "GET", path, NULL, NULL, append);
}

/* ============================================================ 操作・API */
static int g_content_h;
static int g_menu_post = -1;
static int g_poll_n;

static void urlenc(const char *s, char *out, size_t n)
{
    size_t k = 0;
    for (; *s && k + 4 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[k++] = (char)c;
        else k += (size_t)_snprintf(out + k, n - k, "%%%02X", c);
    }
    out[k] = 0;
}
static void open_url(const char *u8)
{
    wchar_t *w = u8w(u8);
    ShellExecuteW(g_main, L"open", w, NULL, NULL, SW_SHOWNORMAL);
    free(w);
}
static void open_site(const char *path)
{
    char u[400];
    _snprintf(u, sizeof u, "https://north.rip%s", path);
    u[sizeof u - 1] = 0;
    open_url(u);
}
void ui_toast(const wchar_t *s)
{
    wcsncpy(g_toast, s, 159); g_toast[159] = 0;
    g_toast_on = 1;
    SetTimer(g_main, 2, 2400, NULL);
    InvalidateRect(g_main, NULL, FALSE);
}

static void relayout_all(void)
{
    for (int i = 0; i < g_home.n; i++) g_home.p[i]->lay_w = -1;
    for (int i = 0; i < g_prof.feed.n; i++) g_prof.feed.p[i]->lay_w = -1;
}

static void refresh_home(void)
{
    g_home.loading = 1;
    g_newcount = 0;
    net_api(T_HOME, "GET", "/api/timeline/home?ranked=1", NULL, NULL, 0);
    InvalidateRect(g_main, NULL, FALSE);
}
static void request_more(void)
{
    if (g_view == V_NOTIF) { load_notifs(1); return; }
    if (g_view == V_SEARCH) { if (g_sphase == 1) search_load(1); return; }
    if (g_view == V_BM) { load_bookmarks(1); return; }
    if (g_view == V_DM || g_view == V_CHAT || g_view == V_THREAD) return;
    Feed *f = cur_feed();
    if (f->loading || !f->cursor[0]) return;
    char cur[200], path[400];
    urlenc(f->cursor, cur, sizeof cur);
    f->loading = 1;
    if (g_view == V_HOME) {
        _snprintf(path, sizeof path, "/api/timeline/home?ranked=1&cursor=%s", cur);
        net_api(T_HOME_MORE, "GET", path, NULL, NULL, 0);
    } else {
        _snprintf(path, sizeof path, "/api/users/%s/tweets?tab=%s&cursor=%s", g_prof.handle, TAB_KEY[g_prof.tab], cur);
        net_api(T_PROFTL, "GET", path, NULL, g_prof.handle, g_prof.tab * 2 + 1);
    }
    path[sizeof path - 1] = 0;
}
static void load_profile_tab(int tab)
{
    char path[300];
    g_prof.tab = tab;
    feed_clear(&g_prof.feed);
    g_prof.feed.loaded = 0; g_prof.feed.loading = 1;
    _snprintf(path, sizeof path, "/api/users/%s/tweets?tab=%s", g_prof.handle, TAB_KEY[tab]);
    net_api(T_PROFTL, "GET", path, NULL, g_prof.handle, tab * 2);
}
static void open_profile(const char *handle)
{
    char path[200];
    if (!handle || !handle[0]) return;
    if (g_view != V_PROFILE) { g_back_to = g_view; }
    g_view = V_PROFILE;
    g_drawer = 0;
    g_scroll[V_PROFILE] = 0;
    memset(&g_prof.u, 0, sizeof g_prof.u);
    g_prof.have = 0;
    strncpy(g_prof.handle, handle, sizeof g_prof.handle - 1);
    free(g_prof.bio_w); free(g_prof.bio_a); g_prof.bio_w = NULL; g_prof.bio_a = NULL; g_prof.bio_len = 0;
    _snprintf(path, sizeof path, "/api/users/%s", g_prof.handle);
    net_api(T_PROFILE, "GET", path, NULL, g_prof.handle, 0);
    load_profile_tab(0);
    InvalidateRect(g_main, NULL, FALSE);
}
static void go_home(void)
{
    g_view = V_HOME; g_drawer = 0;
    InvalidateRect(g_main, NULL, FALSE);
}
static void focus_composer(void)
{
    if (g_view != V_HOME) go_home();
    g_scroll[V_HOME] = 0;
    InvalidateRect(g_main, NULL, FALSE);
    UpdateWindow(g_main);
    SetFocus(g_edit);
}

static void scroll_by(int dy)
{
    int max = g_content_h - (G.vy1 - G.vy0);
    if (max < 0) max = 0;
    int s = g_scroll[g_view] + dy;
    if (s < 0) s = 0;
    if (s > max) s = max;
    g_scroll[g_view] = s;
    if (max - s < PX(900)) request_more();
    InvalidateRect(g_main, NULL, FALSE);
}

static void apply_state(const char *id, int kind, int val)
{
    Feed *fs[6] = { &g_home, &g_prof.feed, &g_sfeed, &g_nfeed, &g_bfeed, &g_tfeed };
    for (int k = 0; k < 6; k++)
        for (int i = 0; i < fs[k]->n; i++) {
            Post *p = fs[k]->p[i];
            if (strcmp(p->id, id) != 0) continue;
            if (kind == 0 && p->liked != val) { p->liked = val; p->likes += val ? 1 : -1; if (p->likes < 0) p->likes = 0; }
            else if (kind == 1 && p->rted != val) { p->rted = val; p->rts += val ? 1 : -1; if (p->rts < 0) p->rts = 0; }
            else if (kind == 2) p->bm = val;
        }
}
static void toggle_action(Post *p, int kind)
{
    static const char *nm[3] = { "like", "retweet", "bookmark" };
    static const int tg[3] = { T_LIKE, T_RT, T_BM };
    int cur = kind == 0 ? p->liked : kind == 1 ? p->rted : p->bm, want = !cur;
    char path[200], id[40];
    strcpy(id, p->id);
    apply_state(id, kind, want);
    _snprintf(path, sizeof path, "/api/tweets/%s/%s", id, nm[kind]);
    net_api(tg[kind], want ? "POST" : "DELETE", path, NULL, id, want);
    InvalidateRect(g_main, NULL, FALSE);
}

static void copy_text(const wchar_t *w)
{
    if (!OpenClipboard(g_main)) return;
    EmptyClipboard();
    size_t n = (wcslen(w) + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
    memcpy(GlobalLock(h), w, n);
    GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
}
static void post_url(const Post *p, char *out, size_t n)
{
    _snprintf(out, n, "https://north.rip/%s/status/%s", p->au.handle, p->id);
    out[n - 1] = 0;
}

static void do_post(void)
{
    int n = GetWindowTextLengthW(g_edit);
    if (n <= 0 && att_ready(AT_POST) == 0) return;
    if (att_pending(AT_POST)) { ui_toast(L"画像のアップロード中です"); return; }
    wchar_t *w = (wchar_t *)malloc((size_t)(n + 1) * sizeof(wchar_t));
    GetWindowTextW(g_edit, w, n + 1);
    char *t = wu8(w);
    free(w);
    for (char *s = t, *d = t;; s++) { if (*s != '\r') *d++ = *s; if (!*s) break; }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "text", t);
    const wchar_t *perr = poll_error();
    if (perr) { ui_toast(perr); cJSON_Delete(o); free(t); return; }
    cJSON_AddStringToObject(o, "replyPolicy", POLICY_KEY[g_policy]);
    cJSON_AddItemToObject(o, "mediaIds", att_ids(AT_POST));
    if (g_poll_on) {
        cJSON *po = cJSON_CreateObject(), *arr = cJSON_CreateArray();
        for (int i = 0; i < g_poll_n; i++) { char ot[120]; ed_text_u8(ED_OPT0 + i, ot, sizeof ot); trim_inplace(ot); cJSON_AddItemToArray(arr, cJSON_CreateString(ot)); }
        cJSON_AddItemToObject(po, "options", arr);
        cJSON_AddNumberToObject(po, "durationMinutes", poll_minutes());
        cJSON_AddItemToObject(o, "poll", po);
    }
    if (g_reply_id[0]) cJSON_AddStringToObject(o, "inReplyToId", g_reply_id);
    char *js = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    free(t);
    net_api(T_POST, "POST", "/api/tweets", js, NULL, 0);
    cJSON_free(js);
    ui_toast(L"\u9001\u4FE1\u4E2D...");
}

static void do_logout(void)
{
    net_api(T_LOGOUT, "POST", "/api/auth/logout", "{}", NULL, 0);
    session_clear();
    feed_clear(&g_home); feed_clear(&g_prof.feed); feed_clear(&g_sfeed); feed_clear(&g_bfeed); feed_clear(&g_tfeed); g_thr_main = -1;
    for (int i = 0; i < NAT; i++) g_natt[i] = 0;
    lb_close();
    notif_free_all(); chat_clear(); g_chat.conv[0] = 0; g_nconv = 0; g_nhist = 0; g_nppl = 0; g_sq[0] = 0; g_sphase = 0;
    g_home.loaded = 0; g_have_me = 0; g_drawer = 0; g_view = V_HOME;
    SetWindowTextW(g_edit, L"");
    InvalidateRect(g_main, NULL, FALSE);
    g_login_prompted = 1;
    open_login();
}

static void fetch_all(void)
{
    net_api(T_ME, "GET", "/api/auth/me", NULL, NULL, 0);
    refresh_home();
    net_api(T_TRENDS, "GET", "/api/trends", NULL, NULL, 0);
    net_api(T_SUGG, "GET", "/api/users/suggestions?limit=3", NULL, NULL, 0);
    net_api(T_UNREAD_N, "GET", "/api/notifications/unread-count", NULL, NULL, 0);
    net_api(T_UNREAD_D, "GET", "/api/dm/unread-count", NULL, NULL, 0);
}

void ui_debug(const char *profile, int drawer, int scroll, const char *view, const char *q)
{
    if (view && !strcmp(view, "notif")) show_view(V_NOTIF);
    else if (view && !strcmp(view, "dm")) show_view(V_DM);
    else if (view && !strcmp(view, "search")) { show_view(V_SEARCH); if (q && q[0]) search_for(q); }
    else if (view && !strcmp(view, "bm")) show_view(V_BM);
    else if (view && !strcmp(view, "thread")) open_thread(q ? q : "");
    else if (view && !strcmp(view, "chat")) { Conv c; memset(&c, 0, sizeof c); strncpy(c.id, q ? q : "", 39); c.npart = 1; strcpy(c.part[0].name, "yy"); strcpy(c.part[0].handle, "yy"); open_chat_conv(&c); }
    if (profile && profile[0]) open_profile(profile);
    g_drawer = drawer;
    g_scroll[g_view] = scroll;
}

void ui_startup(void)
{
    if (g_is_demo) { strcpy(g_cookie, "demo=1"); fetch_all(); return; }
    if (session_load()) { net_reopen(); fetch_all(); }
    else { g_login_prompted = 1; open_login(); }
}

static void on_api(ApiRes *r)
{
    cJSON *root = NULL;
    if (r->status == 401 && !g_is_demo) {
        if (!g_login_prompted) { g_login_prompted = 1; session_clear(); ui_toast(L"\u30BB\u30C3\u30B7\u30E7\u30F3\u304C\u5207\u308C\u307E\u3057\u305F\u3002\u518D\u30ED\u30B0\u30A4\u30F3\u3057\u3066\u304F\u3060\u3055\u3044"); open_login(); }
        if (r->tag == T_HOME || r->tag == T_HOME_MORE) g_home.loading = 0;
        return;
    }
    int ok = r->status >= 200 && r->status < 300 && r->body;
    if (ok && r->tag != T_LOGOUT) root = cJSON_Parse(r->body);
    switch (r->tag) {
    case T_ME:
        if (root) {
            const cJSON *u = cJSON_GetObjectItemCaseSensitive(root, "user");
            parse_user(&g_me, cJSON_IsObject(u) ? u : root);
            g_have_me = g_me.handle[0] != 0;
            if (g_have_me && g_me.id[0]) stats_on_login(g_me.id);
        }
        break;
    case T_HOME:
    case T_HOME_MORE:
        g_home.loading = 0;
        if (ok) {
            feed_parse(&g_home, r->body, r->tag == T_HOME_MORE);
            dbg_lb_try();
            if (r->tag == T_HOME && root) { jcopy(g_newest, sizeof g_newest, root, "newestAt"); g_newcount = 0; }
        } else {
            wchar_t b[80]; _snwprintf(b, 80, L"\u30BF\u30A4\u30E0\u30E9\u30A4\u30F3\u3092\u53D6\u5F97\u3067\u304D\u307E\u305B\u3093 (HTTP %d)", r->status); b[79] = 0;
            ui_toast(b);
        }
        break;
    case T_TRENDS:
        g_ntrends = 0;
        if (root) {
            const cJSON *it;
            cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "items")) {
                if (g_ntrends >= 10) break;
                jcopy(g_trends[g_ntrends].tag, sizeof g_trends[0].tag, it, "tag");
                g_trends[g_ntrends].count = jint(it, "count");
                g_trends[g_ntrends].is_hash = jbool(it, "isHashtag");
                g_ntrends++;
            }
        }
        break;
    case T_SUGG:
        g_nsugg = 0;
        if (root) {
            const cJSON *it;
            cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, "items")) {
                if (g_nsugg >= 5) break;
                const cJSON *u = cJSON_GetObjectItemCaseSensitive(it, "user");
                ulite(&g_sugg[g_nsugg++], cJSON_IsObject(u) ? u : it);
            }
        }
        break;
    case T_UNREAD_N: if (root) g_unread_n = jint(root, "count"); break;
    case T_UNREAD_D: if (root) g_unread_d = jint(root, "count"); break;
    case T_NEWCOUNT: if (root) g_newcount = jint(root, "count"); break;
    case T_PROFILE:
        if (root && r->ctx && _stricmp(r->ctx, g_prof.handle) == 0) {
            const cJSON *u = cJSON_GetObjectItemCaseSensitive(root, "user");
            parse_user(&g_prof.u, cJSON_IsObject(u) ? u : root);
            g_prof.have = 1;
            free(g_prof.bio_w); free(g_prof.bio_a);
            set_text(g_prof.u.bio, &g_prof.bio_w, &g_prof.bio_a, &g_prof.bio_len);
        }
        break;
    case T_PROFTL:
        if (r->ctx && _stricmp(r->ctx, g_prof.handle) == 0 && (r->arg >> 1) == g_prof.tab) {
            g_prof.feed.loading = 0;
            if (ok) feed_parse(&g_prof.feed, r->body, r->arg & 1);
            else g_prof.feed.loaded = 1;
        }
        break;
    case T_LIKE: case T_RT: case T_BM:
        if (!ok && r->ctx) {
            apply_state(r->ctx, r->tag == T_LIKE ? 0 : r->tag == T_RT ? 1 : 2, !r->arg);
            ui_toast(L"\u64CD\u4F5C\u306B\u5931\u6557\u3057\u307E\u3057\u305F");
        }
        break;
    case T_NOTIFS:
        g_notif_loading = 0;
        if (ok && (r->arg >> 1) == g_ntab) {
            parse_notifs(r->body, r->arg & 1);
            if (!(r->arg & 1) && g_unread_n > 0) { g_unread_n = 0; net_api(T_NOTIF_READ, "POST", "/api/notifications/read", NULL, NULL, 0); }
        } else if (!ok) g_notif_loaded = 1;
        break;
    case T_SHIST: if (ok) parse_hist(r->body); break;
    case T_SHIST_ADD: case T_SHIST_DEL: net_api(T_SHIST, "GET", "/api/search/history", NULL, NULL, 0); break;
    case T_SUGGEST:
        if (ok && r->ctx) { char cur[200]; ed_get_u8(ED_SEARCH, cur, sizeof cur); if (strcmp(cur, r->ctx) == 0) parse_suggest(r->body); }
        break;
    case T_SEARCH:
        g_sfeed.loading = 0;
        if (ok && r->ctx && strcmp(r->ctx, g_sq) == 0 && (r->arg >> 1) == g_stab) parse_search(r->body, r->arg & 1);
        else if (!ok) g_sfeed.loaded = 1;
        break;
    case T_CONVS:
        g_dm_loading = 0;
        if (ok && r->arg == g_dm_req) parse_convs(r->body);
        else if (!ok) g_dm_loaded = 1;
        break;
    case T_CHAT:
        g_chat.loading = 0;
        if (ok && r->ctx && strcmp(r->ctx, g_chat.conv) == 0) {
            parse_chat(r->body, r->arg);
            if (g_chat_new_incoming) {
                char path[200];
                g_chat_new_incoming = 0;
                _snprintf(path, sizeof path, "/api/dm/conversations/%s/read", g_chat.conv);
                net_api(T_CHAT_READ, "POST", path, NULL, NULL, 0);
            }
        } else if (!ok && r->arg == 0) g_chat.loaded = 1;
        break;
    case T_CHAT_SEND:
        if (ok && root && r->ctx && strcmp(r->ctx, g_chat.conv) == 0) {
            DmMsg *m = parse_msg(root);
            if (m) { chat_add(m); g_chat_stick = 1; g_natt[AT_DM] = 0; }
        } else if (!ok) ui_toast(L"メッセージを送信できませんでした");
        break;
    case T_TYPING_GET:
        if (ok && root && r->ctx && strcmp(r->ctx, g_chat.conv) == 0) {
            const cJSON *us = cJSON_GetObjectItemCaseSensitive(root, "users"), *u0 = cJSON_IsArray(us) ? cJSON_GetArrayItem(us, 0) : NULL;
            g_chat.typing = 0;
            g_chat.typing_name[0] = 0;
            if (u0) {
                char hd[80] = "";
                if (cJSON_IsObject(u0)) { jcopy(hd, sizeof hd, u0, "handle"); jcopy(g_chat.typing_name, sizeof g_chat.typing_name, u0, "name"); }
                else if (cJSON_IsString(u0) && u0->valuestring) strncpy(hd, u0->valuestring, sizeof hd - 1);
                if (!is_me(hd)) g_chat.typing = 1;
            }
        }
        break;
    case T_FOLLOW:
        if (r->ctx && _stricmp(r->ctx, g_prof.handle) == 0) {
            if (!ok) {
                if (r->arg) { g_prof.u.is_following = g_prof.u.follow_req = 0; if (g_prof.u.followers > 0) g_prof.u.followers--; }
                else g_prof.u.is_following = 1;
                ui_toast(L"フォロー操作に失敗しました");
            } else if (r->arg && root && jbool(root, "requested")) { g_prof.u.follow_req = 1; g_prof.u.is_following = 0; }
        }
        break;
    case T_DM_CREATE:
        if (ok && root) {
            const cJSON *cv = cJSON_GetObjectItemCaseSensitive(root, "conversation");
            Conv c;
            parse_conv_obj(cJSON_IsObject(cv) ? cv : root, &c);
            if (c.id[0]) open_chat_conv(&c); else ui_toast(L"会話を開けませんでした");
        } else ui_toast(L"会話を開けませんでした");
        break;
    case T_THREAD:
        g_tfeed.loading = 0;
        if (ok && r->ctx && strcmp(r->ctx, g_thr_id) == 0) {
            parse_thread(r->body);
            if (r->arg == 0) g_thr_scroll_main = 1;
        } else if (!ok) { g_tfeed.loaded = 1; ui_toast(L"ポストを取得できませんでした"); }
        break;
    case T_THREAD_REPLY:
        if (ok) { ui_toast(L"返信しました"); g_natt[AT_THR] = 0; thread_reload(); }
        else ui_toast(L"返信に失敗しました");
        break;
    case T_BOOKMARKS:
        g_bfeed.loading = 0;
        if (ok) feed_parse(&g_bfeed, r->body, r->arg);
        else { g_bfeed.loaded = 1; ui_toast(L"ブックマークを取得できませんでした"); }
        break;
    case T_DELETE:
        if (ok && r->ctx) {
            Feed *fs[5] = { &g_home, &g_prof.feed, &g_sfeed, &g_bfeed, &g_tfeed };
            for (int k = 0; k < 5; k++)
                for (int i = fs[k]->n - 1; i >= 0; i--)
                    if (strcmp(fs[k]->p[i]->id, r->ctx) == 0) {
                        free_post(fs[k]->p[i]);
                        memmove(&fs[k]->p[i], &fs[k]->p[i + 1], (size_t)(fs[k]->n - i - 1) * sizeof(Post *));
                        fs[k]->n--;
                    }
            ui_toast(L"ポストを削除しました");
        } else ui_toast(L"削除に失敗しました");
        break;
    case T_PIN:
        if (ok) {
            ui_toast(r->arg ? L"ポストをプロフィールに固定しました" : L"固定を解除しました");
            if (g_view == V_PROFILE && g_have_me && _stricmp(g_prof.handle, g_me.handle) == 0) load_profile_tab(g_prof.tab);
        } else ui_toast(L"操作に失敗しました");
        break;
    case T_POLICY:
        if (ok && r->ctx) {
            Feed *fs[6] = { &g_home, &g_prof.feed, &g_sfeed, &g_nfeed, &g_bfeed, &g_tfeed };
            for (int k = 0; k < 6; k++) for (int i = 0; i < fs[k]->n; i++) if (strcmp(fs[k]->p[i]->id, r->ctx) == 0) fs[k]->p[i]->policy = r->arg;
            ui_toast(L"返信できるアカウントを変更しました");
        } else ui_toast(L"変更に失敗しました");
        break;
    case T_STATS: stats_on_result(r->status, r->arg); break;
    case T_VOTE:
        if (ok && root && r->ctx) {
            const cJSON *po = cJSON_GetObjectItemCaseSensitive(root, "poll");
            if (!cJSON_IsObject(po)) po = cJSON_GetObjectItemCaseSensitive(root, "options") ? root : NULL;
            if (po) {
                Feed *fs[6] = { &g_home, &g_prof.feed, &g_sfeed, &g_nfeed, &g_bfeed, &g_tfeed };
                for (int k = 0; k < 6; k++)
                    for (int i = 0; i < fs[k]->n; i++) if (strcmp(fs[k]->p[i]->id, r->ctx) == 0) parse_poll(fs[k]->p[i], po);
            }
        } else if (!ok) ui_toast(L"投票に失敗しました");
        break;
    case T_MEDIA: {
        int c = r->ctx ? atoi(r->ctx) : 0;
        if (c < 0 || c >= NAT) c = 0;
        for (int i = 0; i < g_natt[c]; i++) {
            Att *a = &g_att[c][i];
            if (a->serial != r->arg) continue;
            if (ok && root) { jcopy(a->id, sizeof a->id, root, "id"); a->state = a->id[0] ? 1 : 2; }
            else a->state = 2;
            if (a->state == 2) ui_toast(r->status == 413 ? L"画像が大きすぎます (50MBまで)" : L"画像のアップロードに失敗しました");
            break;
        }
        break; }
    case T_POST:
        if (ok) {
            SetWindowTextW(g_edit, L"");
            g_reply_id[0] = 0;
            g_natt[AT_POST] = 0;
            g_policy = 0;
            poll_reset();
            ui_toast(L"\u30DD\u30B9\u30C8\u3057\u307E\u3057\u305F");
            refresh_home();
        } else {
            wchar_t b[300] = L"\u30DD\u30B9\u30C8\u306B\u5931\u6557\u3057\u307E\u3057\u305F";
            cJSON *e = r->body ? cJSON_Parse(r->body) : NULL;
            const cJSON *m = e ? cJSON_GetObjectItemCaseSensitive(e, "message") : NULL;
            if (cJSON_IsString(m) && m->valuestring) {
                wchar_t *w = u8w(m->valuestring);
                _snwprintf(b, 300, L"\u30DD\u30B9\u30C8\u5931\u6557: %ls", w); b[299] = 0;
                free(w);
            }
            cJSON_Delete(e);
            ui_toast(b);
        }
        break;
    }
    cJSON_Delete(root);
    InvalidateRect(g_main, NULL, FALSE);
}

/* ============================================================ 画面枠 */
static void draw_badge_num(float cx, float cy, int n)
{
    if (n <= 0) return;
    wchar_t b[8];
    if (n > 99) wcscpy(b, L"99+"); else _snwprintf(b, 8, L"%d", n);
    int w = text_w(&F_SMALLB, b, (int)wcslen(b)) + PX(8);
    if (w < PX(18)) w = PX(18);
    g_fill_rrect(cx - w / 2.0f, cy - PX(9), (float)w, (float)PX(18), PX(9), C_ACCENT, 255);
    text_at(&F_SMALLB, (int)(cx - w / 2.0f) + PX(4), (int)cy - F_SMALLB.lh / 2, b, (int)wcslen(b), RGB(255, 255, 255));
}

static int own_profile(void) { return g_view == V_PROFILE && g_have_me && _stricmp(g_prof.handle, g_me.handle) == 0; }

static void draw_topbar(void)
{
    int x = G.cx, w = G.cw, h = G.top_h;
    g_rect(x, 0, x + w, h, C_BG);
    g_hline(x, x + w, h - 1, C_LINE);
    switch (g_view) {
    case V_HOME: case V_NOTIF: case V_DM: case V_BM: {
        int tx = x + PX(16);
        if (G.mode == 0) {
            draw_avatar_ph(g_me.avatar, g_me.name, x + PX(12), (h - PX(32)) / 2, PX(32));
            hit(x, 0, x + PX(56), h, H_ME_AVATAR, 0, 0);
            tx = x + PX(56);
        }
        const wchar_t *t = g_view == V_HOME ? L"ホーム" : g_view == V_NOTIF ? L"通知" : g_view == V_BM ? L"ブックマーク" : L"メッセージ";
        text_at(&F_TITLE, tx, (h - F_TITLE.lh) / 2, t, (int)wcslen(t), C_TEXT);
        if (g_view == V_HOME) g_icon(IC_SPARK, (float)(x + w - PX(40)), (float)((h - PX(20)) / 2), (float)PX(20), C_TEXT, 1, 1);
        break; }
    case V_SEARCH: {
        int px = x + PX(16), pw = w - PX(32), ph = PX(38), py = (h - ph) / 2;
        g_fill_rrect((float)px, (float)py, (float)pw, (float)ph, ph / 2.0f, C_PANEL, 255);
        g_icon(IC_SEARCH, (float)(px + PX(14)), (float)(py + PX(9)), (float)PX(20), C_GRAY, 0, PX(2) * 0.9f);
        place_edit(ED_SEARCH, 1, px + PX(44), py + (ph - PX(22)) / 2, pw - PX(60), PX(22));
        break; }
    case V_PROFILE: {
        g_icon(IC_BACK, (float)(x + PX(16)), (float)((h - PX(22)) / 2), (float)PX(22), C_TEXT, 0, PX(2) * 1.0f);
        hit(x, 0, x + PX(60), h, H_BACK, 0, 0);
        const char *nm = g_prof.u.name[0] ? g_prof.u.name : g_prof.handle;
        text_clip(&F_BOLD, x + PX(60), PX(6), w - PX(80), nm, C_TEXT);
        if (g_prof.have && g_prof.u.posts > 0) {
            wchar_t b[60], n[24];
            fmt_count(g_prof.u.posts, n, 24);
            _snwprintf(b, 60, L"%ls件のポスト", n); b[59] = 0;
            text_at(&F_SMALL, x + PX(60), PX(6) + F_BOLD.lh, b, (int)wcslen(b), C_GRAY);
        }
        break; }
    case V_THREAD:
        g_icon(IC_BACK, (float)(x + PX(16)), (float)((h - PX(22)) / 2), (float)PX(22), C_TEXT, 0, PX(2) * 1.0f);
        hit(x, 0, x + PX(60), h, H_BACK, 0, 0);
        text_at(&F_TITLE, x + PX(60), (h - F_TITLE.lh) / 2, L"ポスト", 3, C_TEXT);
        break;
    case V_CHAT: {
        g_icon(IC_BACK, (float)(x + PX(16)), (float)((h - PX(22)) / 2), (float)PX(22), C_TEXT, 0, PX(2) * 1.0f);
        hit(x, 0, x + PX(60), h, H_BACK, 0, 0);
        draw_avatar_ph(g_chat.peer.avatar, g_chat.title, x + PX(56), (h - PX(34)) / 2, PX(34));
        text_clip(&F_BOLD, x + PX(100), (h - F_BOLD.lh) / 2, w - PX(120), g_chat.title, C_TEXT);
        if (!g_chat.group && g_chat.peer.handle[0]) hit(x + PX(56), 0, x + w - PX(20), h, H_AVATAR_CHAT, 0, 0);
        break; }
    }
}

static void draw_nav(void)
{
    int nx = G.navx, nw = G.navw, full = G.mode == 2;
    g_icon(IC_SPARK, (float)(nx + PX(18)), (float)PX(16), (float)PX(26), C_TEXT, 1, 1);
    if (full) {
        text_at(&F_BIG, nx + PX(52), PX(14), L"north", (int)wcslen(L"north"), C_TEXT);
        g_stroke_rrect((float)(nx + PX(18)), (float)PX(52), (float)PX(58), (float)PX(20), PX(10), RGB(90, 102, 132), 1.0f);
        text_at(&F_SMALL, nx + PX(28), PX(53), L"client", (int)wcslen(L"client"), C_GRAY);
        text_at(&F_SMALL, nx + PX(84), PX(53), L"v0.6", (int)wcslen(L"v0.2"), C_GRAY);
    }
    int y = PX(92), ih = PX(54), icon = PX(26);
    for (int i = 0; i < NNAV; i++) {
        int act = NAVS[i].act;
        int active = (act == NAV_HOME && g_view == V_HOME) || (act == NAV_PROFILE && own_profile()) ||
                     (act == NAV_NOTIF && g_view == V_NOTIF) || (act == NAV_SEARCH && g_view == V_SEARCH) ||
                     (act == NAV_MSG && (g_view == V_DM || g_view == V_CHAT)) || (act == NAV_BM && g_view == V_BM);
        int ix = full ? nx + PX(20) : nx + (nw - icon) / 2;
        int lw = full ? text_w(active ? &F_NAVB : &F_NAV, NAVS[i].label, (int)wcslen(NAVS[i].label)) : 0;
        if (active) {
            int pw = full ? PX(56) + PX(10) + lw + PX(16) : PX(54);
            g_fill_rrect((float)(full ? nx + PX(8) : nx + (nw - PX(54)) / 2), (float)(y + PX(2)), (float)pw, (float)(ih - PX(4)), (ih - PX(4)) / 2.0f, RGB(28, 36, 64), 255);
        }
        g_icon(NAVS[i].ic, (float)ix, (float)(y + (ih - icon) / 2), (float)icon, C_TEXT, active && act == NAV_HOME, PX(2) * 1.0f);
        if (act == NAV_NOTIF) draw_badge_num((float)(ix + icon - PX(2)), (float)(y + PX(12)), g_unread_n);
        if (act == NAV_MSG) draw_badge_num((float)(ix + icon - PX(2)), (float)(y + PX(12)), g_unread_d);
        if (full) {
            TF *tf = active ? &F_NAVB : &F_NAV;
            text_at(tf, nx + PX(62), y + (ih - tf->lh) / 2, NAVS[i].label, (int)wcslen(NAVS[i].label), C_TEXT);
        }
        hit(full ? nx + PX(8) : nx + PX(10), y, full ? nx + PX(8) + PX(56) + PX(10) + lw + PX(16) : nx + nw - PX(10), y + ih, H_NAV, act, 0);
        y += ih;
    }
    y += PX(14);
    if (full) {
        int bw = nw - PX(48), bh = PX(52);
        g_fill_rrect((float)(nx + PX(12)), (float)y, (float)bw, (float)bh, bh / 2.0f, RGB(255, 255, 255), 255);
        const wchar_t *l = L"\u30DD\u30B9\u30C8\u3059\u308B";
        int lw = text_w(&F_BOLD, l, (int)wcslen(l));
        text_at(&F_BOLD, nx + PX(12) + (bw - lw) / 2, y + (bh - F_BOLD.lh) / 2, l, (int)wcslen(l), RGB(10, 15, 32));
        hit(nx + PX(12), y, nx + PX(12) + bw, y + bh, H_COMPOSE_BTN, 0, 0);
    } else {
        int bs = PX(52);
        g_fill_circle(nx + nw / 2.0f, y + bs / 2.0f, bs / 2.0f, RGB(255, 255, 255), 255);
        g_icon(IC_PEN, (float)(nx + nw / 2 - PX(12)), (float)(y + bs / 2 - PX(12)), (float)PX(24), RGB(10, 15, 32), 0, PX(2) * 1.0f);
        hit(nx + nw / 2 - bs / 2, y, nx + nw / 2 + bs / 2, y + bs, H_COMPOSE_BTN, 0, 0);
    }
    /* アカウント */
    int ay = G.H - PX(80);
    draw_avatar_ph(g_me.avatar, g_me.name, nx + PX(14), ay + PX(14), PX(42));
    if (full) {
        text_clip(&F_BOLD, nx + PX(66), ay + PX(10), nw - PX(120), g_me.name, C_TEXT);
        wchar_t hb[120], *hh = u8w(g_me.handle);
        _snwprintf(hb, 120, L"@%ls", hh); hb[119] = 0; free(hh);
        text_clipw(&F_SMALL, nx + PX(66), ay + PX(10) + F_BOLD.lh, nw - PX(120), hb, C_GRAY);
        g_icon(IC_MORE, (float)(nx + nw - PX(46)), (float)(ay + PX(22)), (float)PX(22), C_TEXT, 0, PX(2) * 0.9f);
    }
    hit(nx + PX(6), ay + PX(6), nx + nw - PX(6), ay + PX(70), H_ACCOUNT, 0, 0);
}

static void draw_right(void)
{
    int x = G.rx + PX(28), w = G.rw - PX(40), y = PX(8);
    g_fill_rrect((float)x, (float)y, (float)w, (float)PX(42), PX(21), C_PANEL, 255);
    g_icon(IC_SEARCH, (float)(x + PX(16)), (float)(y + PX(11)), (float)PX(20), C_GRAY, 0, PX(2) * 0.9f);
    text_at(&F_BODY, x + PX(46), y + (PX(42) - F_BODY.lh) / 2, L"\u691C\u7D22", (int)wcslen(L"\u691C\u7D22"), C_GRAY);
    hit(x, y, x + w, y + PX(42), H_SEARCH, 0, 0);
    y += PX(58);

    int n = g_ntrends > 5 ? 5 : g_ntrends;
    if (n > 0) {
        int ch = PX(56) + n * PX(66) + PX(44);
        g_fill_rrect((float)x, (float)y, (float)w, (float)ch, PX(16), C_PANEL, 255);
        text_at(&F_TITLE, x + PX(16), y + PX(12), L"\u3044\u307E\u3069\u3046\u3057\u3066\u308B\uFF1F", (int)wcslen(L"\u3044\u307E\u3069\u3046\u3057\u3066\u308B\uFF1F"), C_TEXT);
        int iy = y + PX(56);
        for (int i = 0; i < n; i++) {
            wchar_t b[200], *t = u8w(g_trends[i].tag), c[24];
            _snwprintf(b, 200, L"%d \u00B7 \u65E5\u672C\u306E\u30C8\u30EC\u30F3\u30C9", i + 1); b[199] = 0;
            text_at(&F_SMALL, x + PX(16), iy, b, (int)wcslen(b), C_GRAY);
            _snwprintf(b, 200, L"%ls%ls", g_trends[i].is_hash ? L"#" : L"", t); b[199] = 0;
            text_clipw(&F_TAG, x + PX(16), iy + PX(18), w - PX(32), b, C_TEXT);
            fmt_count(g_trends[i].count, c, 24);
            _snwprintf(b, 200, L"%ls\u4EF6\u306E\u30DD\u30B9\u30C8", c); b[199] = 0;
            text_at(&F_SMALL, x + PX(16), iy + PX(18) + F_TAG.lh - PX(2), b, (int)wcslen(b), C_GRAY);
            free(t);
            hit(x, iy - PX(6), x + w, iy + PX(60), H_TREND, i, 0);
            iy += PX(66);
        }
        text_at(&F_BODY, x + PX(16), iy + PX(2), L"\u3055\u3089\u306B\u8868\u793A", (int)wcslen(L"\u3055\u3089\u306B\u8868\u793A"), C_ACCENT);
        hit(x, iy, x + w, iy + PX(36), H_SEARCH, 0, 0);
        y += ch + PX(16);
    }
    if (g_nsugg > 0) {
        int ch = PX(56) + g_nsugg * PX(64) + PX(8);
        g_fill_rrect((float)x, (float)y, (float)w, (float)ch, PX(16), C_PANEL, 255);
        text_at(&F_TITLE, x + PX(16), y + PX(12), L"\u304A\u3059\u3059\u3081\u30E6\u30FC\u30B6\u30FC", (int)wcslen(L"\u304A\u3059\u3059\u3081\u30E6\u30FC\u30B6\u30FC"), C_TEXT);
        int iy = y + PX(56);
        for (int i = 0; i < g_nsugg; i++) {
            draw_avatar_ph(g_sugg[i].avatar, g_sugg[i].name, x + PX(16), iy, PX(40));
            text_clip(&F_BOLD, x + PX(64), iy, w - PX(80), g_sugg[i].name, C_TEXT);
            wchar_t hb[120], *hh = u8w(g_sugg[i].handle);
            _snwprintf(hb, 120, L"@%ls", hh); hb[119] = 0; free(hh);
            text_clipw(&F_SMALL, x + PX(64), iy + F_BOLD.lh, w - PX(80), hb, C_GRAY);
            hit(x, iy - PX(4), x + w, iy + PX(52), H_SUGG, i, 0);
            iy += PX(64);
        }
        y += ch + PX(16);
    }
    text_at(&F_SMALL, x + PX(4), y + PX(4), L"North \u975E\u516C\u5F0F\u30AF\u30E9\u30A4\u30A2\u30F3\u30C8 (\u8A66\u4F5C)", (int)wcslen(L"North \u975E\u516C\u5F0F\u30AF\u30E9\u30A4\u30A2\u30F3\u30C8 (\u8A66\u4F5C)"), C_GRAY);
}

static void draw_bottombar(void)
{
    int y0 = G.H - G.bot_h, w = G.W;
    g_rect(0, y0, w, G.H, C_BG);
    g_hline(0, w, y0, C_LINE);
    static const int ic[4] = { IC_HOME, IC_SEARCH, IC_BELL, IC_MAIL };
    static const int ac[4] = { NAV_HOME, NAV_SEARCH, NAV_NOTIF, NAV_MSG };
    int is = PX(24);
    for (int i = 0; i < 4; i++) {
        int cx = w * (2 * i + 1) / 8, cy = y0 + G.bot_h / 2;
        int active = (i == 0 && g_view == V_HOME) || (i == 1 && g_view == V_SEARCH) || (i == 2 && g_view == V_NOTIF) || (i == 3 && g_view == V_DM);
        g_icon(ic[i], (float)(cx - is / 2), (float)(cy - is / 2), (float)is, active ? C_TEXT : C_GRAY, active, PX(2) * 1.0f);
        if (i == 2) draw_badge_num((float)(cx + PX(10)), (float)(cy - PX(8)), g_unread_n);
        if (i == 3) draw_badge_num((float)(cx + PX(10)), (float)(cy - PX(8)), g_unread_d);
        hit(w * i / 4, y0, w * (i + 1) / 4, G.H, H_NAV, ac[i], 0);
    }
}

static void draw_fab(void)
{
    int s = PX(56), x = G.W - PX(16) - s, y = G.H - G.bot_h - PX(16) - s;
    g_fill_circle(x + s / 2.0f + 0, y + s / 2.0f + PX(2), s / 2.0f, RGB(0, 0, 0), 90);
    g_fill_circle(x + s / 2.0f, y + s / 2.0f, s / 2.0f, RGB(255, 255, 255), 255);
    g_icon(IC_PEN, (float)(x + s / 2 - PX(12)), (float)(y + s / 2 - PX(12)), (float)PX(24), RGB(10, 15, 32), 0, PX(2) * 1.0f);
    hit(x, y, x + s, y + s, H_FAB, 0, 0);
}

static void draw_drawer(void)
{
    hit(0, 0, G.W, G.H, H_DRAWER_BG, 0, 0);
    g_fill_rrect(0, 0, (float)G.W, (float)G.H, 0, RGB(0, 0, 0), 150);
    int pw = PX(300);
    if (pw > G.W * 80 / 100) pw = G.W * 80 / 100;
    g_rect(0, 0, pw, G.H, RGB(8, 13, 30));
    g_rect(pw, 0, pw + 1, G.H, C_LINE);
    hit(0, 0, pw, G.H, H_DRAWER_PANEL, 0, 0);
    int x = PX(16), y = PX(16);
    text_at(&F_TITLE, x, y, L"\u30A2\u30AB\u30A6\u30F3\u30C8\u60C5\u5831", (int)wcslen(L"\u30A2\u30AB\u30A6\u30F3\u30C8\u60C5\u5831"), C_TEXT);
    g_icon(IC_CLOSE, (float)(pw - PX(44)), (float)(y + PX(2)), (float)PX(22), C_TEXT, 0, PX(2) * 1.0f);
    hit(pw - PX(56), y - PX(6), pw, y + PX(36), H_DRAWER_ITEM, -1, 0);
    y += PX(52);
    draw_avatar_ph(g_me.avatar, g_me.name, x, y, PX(44));
    y += PX(52);
    text_clip(&F_BOLD, x, y, pw - 2 * x, g_me.name, C_TEXT);
    y += F_BOLD.lh;
    { wchar_t hb[120], *hh = u8w(g_me.handle); _snwprintf(hb, 120, L"@%ls", hh); hb[119] = 0; free(hh);
      text_clipw(&F_SMALL, x, y, pw - 2 * x, hb, C_GRAY); }
    y += F_SMALL.lh + PX(8);
    { wchar_t n1[24], n2[24]; int fx = x;
      fmt_count(g_me.following, n1, 24); fmt_count(g_me.followers, n2, 24);
      text_at(&F_SMALLB, fx, y, n1, (int)wcslen(n1), C_TEXT); fx += text_w(&F_SMALLB, n1, (int)wcslen(n1)) + PX(4);
      text_at(&F_SMALL, fx, y, L"\u30D5\u30A9\u30ED\u30FC\u4E2D", (int)wcslen(L"\u30D5\u30A9\u30ED\u30FC\u4E2D"), C_GRAY); fx += text_w(&F_SMALL, L"\u30D5\u30A9\u30ED\u30FC\u4E2D", (int)wcslen(L"\u30D5\u30A9\u30ED\u30FC\u4E2D")) + PX(14);
      text_at(&F_SMALLB, fx, y, n2, (int)wcslen(n2), C_TEXT); fx += text_w(&F_SMALLB, n2, (int)wcslen(n2)) + PX(4);
      text_at(&F_SMALL, fx, y, L"\u30D5\u30A9\u30ED\u30EF\u30FC", (int)wcslen(L"\u30D5\u30A9\u30ED\u30EF\u30FC"), C_GRAY); }
    y += F_SMALL.lh + PX(12);
    g_hline(0, pw, y, C_LINE);
    y += PX(6);
    static const struct { int ic; const wchar_t *l; int a; } items[4] = {
        { IC_PERSON, L"\u30D7\u30ED\u30D5\u30A3\u30FC\u30EB", NAV_PROFILE },
        { IC_BOOKMARK, L"\u30D6\u30C3\u30AF\u30DE\u30FC\u30AF", NAV_BM },
        { IC_LIST, L"\u30EA\u30B9\u30C8", NAV_LIST },
        { IC_MORE, L"\u8A2D\u5B9A\u3068\u30D7\u30E9\u30A4\u30D0\u30B7\u30FC", NAV_SETTINGS } };
    for (int i = 0; i < 4; i++) {
        g_icon(items[i].ic, (float)x, (float)(y + PX(14)), (float)PX(24), C_TEXT, 0, PX(2) * 1.0f);
        text_at(&F_BOLD, x + PX(40), y + (PX(52) - F_BOLD.lh) / 2, items[i].l, (int)wcslen(items[i].l), C_TEXT);
        hit(0, y, pw, y + PX(52), H_DRAWER_ITEM, items[i].a, 0);
        y += PX(52);
    }
    if (stats_configured()) {
        wchar_t sb[80];
        _snwprintf(sb, 80, L"利用者数の集計に協力: %ls", stats_consent() == 1 ? L"オン" : L"オフ"); sb[79] = 0;
        text_at(&F_BODY, x + PX(40), y + (PX(44) - F_BODY.lh) / 2, sb, (int)wcslen(sb), C_GRAY);
        hit(0, y, pw, y + PX(44), H_DRAWER_ITEM, NAV_STATS, 0);
        y += PX(44);
    }
    y += PX(6);
    g_hline(0, pw, y, C_LINE);
    y += PX(6);
    text_at(&F_BOLD, x, y + (PX(48) - F_BOLD.lh) / 2, L"\u30ED\u30B0\u30A2\u30A6\u30C8", (int)wcslen(L"\u30ED\u30B0\u30A2\u30A6\u30C8"), C_TEXT);
    hit(0, y, pw, y + PX(48), H_DRAWER_ITEM, NAV_LOGOUT, 0);
    y += PX(60);
    text_at(&F_SMALL, x, y, APP_VER, (int)wcslen(APP_VER), C_GRAY);
}

/* ============================================================ 描画本体 */
static HBITMAP g_bb;
static int g_bbw, g_bbh;

static void draw_background(int W, int H)
{
    TRIVERTEX v[4] = {
        { 0, 0, 0x0E00, 0x1E00, 0x4600, 0xFF00 },
        { W / 2, H, 0x0A00, 0x0F00, 0x2600, 0xFF00 },
        { W / 2, 0, 0x0A00, 0x0F00, 0x2600, 0xFF00 },
        { W, H, 0x2600, 0x0E00, 0x3E00, 0xFF00 },
    };
    GRADIENT_RECT gr[2] = { { 0, 1 }, { 2, 3 } };
    GradientFill(P_dc, v, 4, gr, 2, GRADIENT_FILL_RECT_H);
}

static void paint(HDC dc, int W, int H)
{
    calc_geo(W, H);
    if (G.mode != 0 && g_drawer) g_drawer = 0;
    gfx_begin(dc);
    g_link_cb = link_cb;
    g_nhit = 0;
    SetRect(&g_hclip, 0, 0, W, H);
    draw_background(W, H);
    g_rect(G.cx, 0, G.cx + G.cw, H, C_BG);
    if (G.mode > 0) { g_rect(G.cx - 1, 0, G.cx, H, C_LINE); g_rect(G.cx + G.cw, 0, G.cx + G.cw + 1, H, C_LINE); }

    /* コンテンツ */
    SetRect(&g_hclip, G.cx, G.vy0, G.cx + G.cw, G.vy1);
    int sc = g_scroll[g_view];
    int start = G.vy0 - sc, y = start;
    for (int i = 0; i < NED; i++) place_edit(i, 0, 0, 0, 0, 0);
    if (g_view == V_CHAT && g_chat_stick) {
        int tot = chat_total(G.cw), mx = tot - (G.vy1 - G.vy0);
        g_scroll[V_CHAT] = mx > 0 ? mx : 0;
        g_chat_stick = 0;
        sc = g_scroll[V_CHAT];
        start = G.vy0 - sc; y = start;
    }
    if (g_view == V_THREAD && g_thr_scroll_main && g_thr_main >= 0 && g_thr_main < g_tfeed.n) {
        feed_layout(&g_tfeed, G.cw);
        int my = g_tfeed.p[g_thr_main]->y - PX(4);
        g_scroll[V_THREAD] = my > 0 ? my : 0;
        g_thr_scroll_main = 0;
        sc = g_scroll[V_THREAD];
        start = G.vy0 - sc; y = start;
    }
    switch (g_view) {
    case V_HOME:
        draw_composer(G.cx, y, G.cw);
        y += composer_h();
        if (g_newcount > 0) {
            wchar_t b[80]; _snwprintf(b, 80, L"%d\u4EF6\u306E\u65B0\u3057\u3044\u30DD\u30B9\u30C8\u3092\u8868\u793A", g_newcount); b[79] = 0;
            int w2 = text_w(&F_BODY, b, (int)wcslen(b));
            text_at(&F_BODY, G.cx + (G.cw - w2) / 2, y + (PX(52) - F_BODY.lh) / 2, b, (int)wcslen(b), C_ACCENT);
            hit(G.cx, y, G.cx + G.cw, y + PX(52), H_NEWBANNER, 0, 0);
            g_hline(G.cx, G.cx + G.cw, y + PX(52) - 1, C_LINE);
            y += PX(52);
        }
        y += draw_feed(&g_home, G.cx, y, G.cw);
        break;
    case V_PROFILE:
        y += draw_profile_header(G.cx, y, G.cw);
        y += draw_feed(&g_prof.feed, G.cx, y, G.cw);
        break;
    case V_NOTIF: y += draw_notifs(G.cx, y, G.cw); break;
    case V_SEARCH: y += draw_search(G.cx, y, G.cw); break;
    case V_DM: y += draw_dm_list(G.cx, y, G.cw); break;
    case V_CHAT: y += draw_chat(G.cx, y, G.cw); break;
    case V_THREAD: y += draw_feed(&g_tfeed, G.cx, y, G.cw); break;
    case V_BM: y += draw_feed(&g_bfeed, G.cx, y, G.cw); break;
    }
    g_content_h = y - start;

    /* 枠 (コンテンツの上に重ねる) */
    SetRect(&g_hclip, 0, 0, W, H);
    draw_topbar();
    if (G.mode >= 1) draw_nav();
    if (G.mode == 2) draw_right();
    if (G.mode == 0) {
        if (g_view == V_HOME) draw_fab();
        draw_bottombar();
    }
    /* スクロールバー(表示のみ) */
    {
        int vh = G.vy1 - G.vy0, max = g_content_h - vh;
        if (max > 0) {
            int th = vh * vh / g_content_h; if (th < PX(30)) th = PX(30);
            int ty = G.vy0 + (int)((long long)(vh - th) * sc / max);
            g_fill_rrect((float)(G.cx + G.cw - PX(7)), (float)ty, (float)PX(4), (float)th, PX(2), RGB(120, 132, 160), 110);
        }
    }
    if (g_view == V_CHAT || g_view == V_THREAD) draw_chat_input();
    if (g_drawer) { draw_drawer(); for (int i = 0; i < NED; i++) place_edit(i, 0, 0, 0, 0, 0); }
    if (g_lb.on) { draw_lightbox(); for (int i = 0; i < NED; i++) place_edit(i, 0, 0, 0, 0, 0); }
    if (g_toast_on) {
        int w2 = text_w(&F_BODY, g_toast, (int)wcslen(g_toast)) + PX(40), h2 = PX(40);
        int tx = G.cx + (G.cw - w2) / 2, ty = H - G.bot_h - PX(80);
        g_fill_rrect((float)tx, (float)ty, (float)w2, (float)h2, h2 / 2.0f, RGB(40, 48, 76), 245);
        text_at(&F_BODY, tx + PX(20), ty + (h2 - F_BODY.lh) / 2, g_toast, (int)wcslen(g_toast), C_TEXT);
    }
    gfx_end();
    apply_edits();
}

/* ============================================================ 入力 */
static Hit *hit_at(int x, int y)
{
    for (int i = g_nhit - 1; i >= 0; i--)
        if (x >= g_hits[i].r.left && x < g_hits[i].r.right && y >= g_hits[i].r.top && y < g_hits[i].r.bottom) return &g_hits[i];
    return NULL;
}

static void nav_action(int act)
{
    g_drawer = 0;
    switch (act) {
    case NAV_HOME:
        if (g_view == V_HOME) { g_scroll[V_HOME] = 0; refresh_home(); } else go_home();
        break;
    case NAV_PROFILE: if (g_have_me) open_profile(g_me.handle); break;
    case NAV_SEARCH: show_view(V_SEARCH); break;
    case NAV_NOTIF: show_view(V_NOTIF); break;
    case NAV_MSG: show_view(V_DM); break;
    case NAV_BM: show_view(V_BM); break;
    case NAV_LIST: open_site("/lists"); ui_toast(L"\u30D6\u30E9\u30A6\u30B6\u3067\u958B\u304D\u307E\u3059"); break;
    case NAV_SETTINGS: open_site("/settings"); ui_toast(L"\u30D6\u30E9\u30A6\u30B6\u3067\u958B\u304D\u307E\u3059"); break;
    case NAV_MORE: open_site("/settings"); ui_toast(L"\u30D6\u30E9\u30A6\u30B6\u3067\u958B\u304D\u307E\u3059"); break;
    case NAV_LOGOUT: do_logout(); break;
    case NAV_STATS: stats_toggle(); break;
    }
    InvalidateRect(g_main, NULL, FALSE);
}

static void open_link(const Hit *h)
{
    const wchar_t *w = NULL;
    int len = 0;
    Feed *f = cur_feed();
    if (h->b == 2) { w = g_prof.bio_w; len = g_prof.bio_len; }
    else if (h->b == 5) {
        if (h->a < 0 || h->a >= g_chat.n) return;
        w = g_chat.m[h->a]->w; len = g_chat.m[h->a]->wlen;
    } else {
        if (h->a < 0 || h->a >= f->n) return;
        Post *p = f->p[h->a];
        if (h->b == 1) { w = p->q_wtext; len = p->q_wlen; } else if (h->b == 0) { w = p->wtext; len = p->wlen; }
    }
    if (!w || h->c < 0 || h->c + h->d > len) return;
    wchar_t tok[600];
    int n = h->d < 599 ? h->d : 599;
    wcsncpy(tok, w + h->c, (size_t)n); tok[n] = 0;
    char *u = wu8(tok);
    if (h->e == 1) open_url(u);
    else if (h->e == 2) search_for(u);
    else if (h->e == 3) open_profile(u + 1);
    free(u);
}

static void post_menu(int idx)
{
    Feed *f = cur_feed();
    if (idx < 0 || idx >= f->n) return;
    g_menu_post = idx;
    HMENU m = CreatePopupMenu();
    Post *mp = f->p[idx];
    if (is_me(mp->au.handle) && !mp->rt_name[0]) {
        AppendMenuW(m, MF_STRING, 103, mp->pinned ? L"プロフィールへの固定を解除" : L"プロフィールに固定する");
        static const wchar_t *plab[3] = { L"返信をフォロー中のみに制限", L"返信を@した人のみに制限", L"返信を全員に開放" };
        AppendMenuW(m, MF_STRING, 104, plab[mp->policy % 3]);
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    }
    AppendMenuW(m, MF_STRING, 101, L"リンクをコピー");
    AppendMenuW(m, MF_STRING, 102, L"ブラウザで開く");
    if (is_me(mp->au.handle) && !mp->rt_name[0]) AppendMenuW(m, MF_STRING, 105, L"ポストを削除");
    POINT pt; GetCursorPos(&pt);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_main, NULL);
    DestroyMenu(m);
}

static void on_click(const Hit *h)
{
    Feed *f = cur_feed();
    Post *p = (h->a >= 0 && h->a < f->n && ((h->type >= H_AVATAR && h->type <= H_QUOTE) || h->type == H_POSTBODY || h->type == H_VOTE)) ? f->p[h->a] : NULL;
    switch (h->type) {
    case H_NAV: nav_action(h->a); break;
    case H_COMPOSE_BTN: case H_FAB: focus_composer(); break;
    case H_POSTBTN: do_post(); break;
    case H_AVATAR: if (p) open_profile(p->au.handle); break;
    case H_LIKE: if (p) toggle_action(p, 0); break;
    case H_RT: if (p) toggle_action(p, 1); break;
    case H_BM: if (p) toggle_action(p, 2); break;
    case H_REPLY:
        if (p) {
            strcpy(g_reply_id, p->id); strcpy(g_reply_handle, p->au.handle);
            focus_composer();
        }
        break;
    case H_REPLY_CLR: g_reply_id[0] = 0; InvalidateRect(g_main, NULL, FALSE); break;
    case H_SHARE:
        if (p) { char u[300]; post_url(p, u, sizeof u); wchar_t *w = u8w(u); copy_text(w); free(w); ui_toast(L"\u30EA\u30F3\u30AF\u3092\u30B3\u30D4\u30FC\u3057\u307E\u3057\u305F"); }
        break;
    case H_MORE: post_menu(h->a); break;
    case H_LINK: open_link(h); break;
    case H_MEDIA:
        if (p && h->b >= 0 && h->b < p->nmedia) {
            if (p->media[h->b].kind == 0) {            /* 画像はアプリ内で表示。動画/GIFはこれまでどおりブラウザ */
                LbImg l[4]; int n = 0, idx = 0;
                for (int i = 0; i < p->nmedia; i++) {
                    if (p->media[i].kind != 0) continue;
                    if (i == h->b) idx = n;
                    strncpy(l[n].url, p->media[i].url, sizeof l[n].url - 1); l[n].url[sizeof l[n].url - 1] = 0;
                    strncpy(l[n].thumb, p->media[i].thumb, sizeof l[n].thumb - 1); l[n].thumb[sizeof l[n].thumb - 1] = 0;
                    n++;
                }
                lb_open(l, n, idx);
            } else { const char *u = p->media[h->b].url; if (u[0] == '/') open_site(u); else open_url(u); }
        }
        break;
    case H_CARD: if (p && p->lp_url[0]) open_url(p->lp_url); break;
    case H_QUOTE: if (p && p->q_id[0]) open_thread(p->q_id); break;
    case H_POSTBODY: if (p) open_thread(p->id); break;
    case H_THR_SEND: thread_reply(); break;
    case H_VOTE:
        if (p && h->b >= 0 && h->b < p->npopt && !p->p_ended && !p->p_viewer[0]) {
            char path[120];
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "optionId", p->popt[h->b].id);
            char *js = cJSON_PrintUnformatted(o);
            cJSON_Delete(o);
            _snprintf(path, sizeof path, "/api/tweets/%s/poll/vote", p->id);
            net_api(T_VOTE, "POST", path, js, p->id, 0);
            cJSON_free(js);
        }
        break;
    case H_POLICY: g_policy = (g_policy + 1) % 3; InvalidateRect(g_main, NULL, FALSE); break;
    case H_POLL_TOG:
        if (g_natt[AT_POST]) ui_toast(L"画像とアンケートは同時に使えません");
        else { g_poll_on = !g_poll_on; if (!g_poll_on) poll_reset(); }
        InvalidateRect(g_main, NULL, FALSE);
        break;
    case H_POLL_OFF: poll_reset(); InvalidateRect(g_main, NULL, FALSE); break;
    case H_OPT_ADD: if (g_poll_n < 4) g_poll_n++; InvalidateRect(g_main, NULL, FALSE); break;
    case H_OPT_DEL:
        if (g_poll_n > 2 && h->a >= 0 && h->a < g_poll_n) {
            for (int k = h->a; k + 1 < g_poll_n; k++) { char t2[120]; wchar_t *w2; ed_text_u8(ED_OPT0 + k + 1, t2, sizeof t2); w2 = u8w(t2); SetWindowTextW(g_ed[ED_OPT0 + k], w2); free(w2); }
            SetWindowTextW(g_ed[ED_OPT0 + g_poll_n - 1], L"");
            g_poll_n--;
            InvalidateRect(g_main, NULL, FALSE);
        }
        break;
    case H_POLL_D: case H_POLL_H: case H_POLL_M: {
        int base = h->type == H_POLL_D ? 300 : h->type == H_POLL_H ? 400 : 500;
        int cnt = h->type == H_POLL_D ? 8 : h->type == H_POLL_H ? 24 : 60;
        int cur = h->type == H_POLL_D ? g_poll_d : h->type == H_POLL_H ? g_poll_h : g_poll_m;
        HMENU m = CreatePopupMenu();
        for (int i = 0; i < cnt; i++) { wchar_t b[8]; _snwprintf(b, 8, L"%d", i); b[7] = 0; AppendMenuW(m, MF_STRING | (i == cur ? MF_CHECKED : 0) | ((i % 20 == 0 && i) ? MF_MENUBREAK : 0), (UINT_PTR)(base + i), b); }
        POINT pt; GetCursorPos(&pt);
        TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_main, NULL);
        DestroyMenu(m);
        break; }
    case H_BOLD: wrap_sel(L"<太字>", L"</太字>"); break;
    case H_ITALIC: wrap_sel(L"<斜体>", L"</斜体>"); break;
    case H_EMOJI: open_emoji_panel(); break;
    case H_ATTACH:
        if (h->a == AT_POST && g_poll_on) { ui_toast(L"画像とアンケートは同時に使えません"); break; }
        att_pick(h->a);
        break;

    case H_ATT_DEL: att_del(h->a, h->b); InvalidateRect(g_main, NULL, FALSE); break;
    case H_TAB: if (h->a != g_prof.tab) { g_scroll[V_PROFILE] = 0; load_profile_tab(h->a); InvalidateRect(g_main, NULL, FALSE); } break;
    case H_BACK:
        if (g_view == V_CHAT) dm_back_to_list();
        else { g_view = g_back_to; InvalidateRect(g_main, NULL, FALSE); }
        break;
    case H_AVATAR_CHAT: if (g_chat.peer.handle[0]) open_profile(g_chat.peer.handle); break;
    case H_ACTOR: if (h->a < g_nnotif && h->b < g_notifs[h->a]->nact) open_profile(g_notifs[h->a]->act[h->b].handle); break;
    case H_NOTIF:
        if (h->a < g_nnotif) {
            Notif *n = g_notifs[h->a];
            if (!strcmp(n->kind, "FOLLOW") && n->nact) open_profile(n->act[0].handle);
            else if (n->tweet) open_thread(n->tweet->id);
        }
        break;
    case H_NTAB:
        if (h->a != g_ntab) { g_ntab = h->a; g_scroll[V_NOTIF] = 0; notif_free_all(); g_notif_loaded = 0; load_notifs(0); InvalidateRect(g_main, NULL, FALSE); }
        break;
    case H_STAB:
        if (h->a != g_stab) { g_stab = h->a; g_scroll[V_SEARCH] = 0; search_reset_results(); search_load(0); InvalidateRect(g_main, NULL, FALSE); }
        break;
    case H_PERSON: if (h->a < g_nppl) open_profile(g_ppl[h->a].u.handle); break;
    case H_SUGU: if (h->a < g_nsu) open_profile(g_su[h->a].handle); break;
    case H_SUGT: if (h->a < g_nst) { char q[120]; _snprintf(q, sizeof q, "#%s", g_st[h->a]); search_for(q); } break;
    case H_SUGQ: do_search(); break;
    case H_HIST:
        if (h->a < g_nhist) { if (g_hist[h->a].has_user) open_profile(g_hist[h->a].user.handle); else search_for(g_hist[h->a].query); }
        break;
    case H_HIST_DEL:
        if (h->a < g_nhist) {
            char path[100];
            _snprintf(path, sizeof path, "/api/search/history/%s", g_hist[h->a].id);
            net_api(T_SHIST_DEL, "DELETE", path, NULL, NULL, 0);
            for (int i = h->a; i + 1 < g_nhist; i++) g_hist[i] = g_hist[i + 1];
            g_nhist--;
            InvalidateRect(g_main, NULL, FALSE);
        }
        break;
    case H_DM_REQ: g_dm_req = !g_dm_req; g_scroll[V_DM] = 0; load_convs(); InvalidateRect(g_main, NULL, FALSE); break;
    case H_CONV: if (h->a < g_nconv) { Conv c = g_convs[h->a]; open_chat_conv(&c); } break;
    case H_CHAT_OLDER: chat_older(); break;
    case H_DMMEDIA:
        if (h->a < g_chat.n && h->b < g_chat.m[h->a]->nmedia) {
            DmMsg *dm = g_chat.m[h->a];
            if (dm->media[h->b].kind != 0) { open_site(dm->media[h->b].url); break; }
            LbImg l[4]; int n = 0, idx = 0;
            for (int i = 0; i < dm->nmedia; i++) {
                if (dm->media[i].kind != 0) continue;
                if (i == h->b) idx = n;
                strncpy(l[n].url, dm->media[i].url, sizeof l[n].url - 1); l[n].url[sizeof l[n].url - 1] = 0;
                strncpy(l[n].thumb, dm->media[i].thumb, sizeof l[n].thumb - 1); l[n].thumb[sizeof l[n].thumb - 1] = 0;
                n++;
            }
            lb_open(l, n, idx);
        }
        break;
    case H_PROF_AVATAR: case H_PROF_HEADER: {
        const char *u = h->type == H_PROF_AVATAR ? g_prof.u.avatar : g_prof.u.header;
        if (g_prof.have && u[0]) { LbImg l; strncpy(l.url, u, sizeof l.url - 1); l.url[sizeof l.url - 1] = 0; strcpy(l.thumb, l.url); lb_open(&l, 1, 0); }
        break; }
    case H_ATT_VIEW:
        if (h->a >= 0 && h->a < NAT && h->b >= 0 && h->b < g_natt[h->a]) {
            LbImg l; strncpy(l.url, g_att[h->a][h->b].key, sizeof l.url - 1); l.url[sizeof l.url - 1] = 0; strcpy(l.thumb, l.url);
            lb_open(&l, 1, 0);
        }
        break;
    case H_LB_BG: case H_LB_CLOSE: lb_close(); break;
    case H_LB_IMG: break;
    case H_LB_PREV: lb_step(-1); break;
    case H_LB_NEXT: lb_step(1); break;
    case H_LB_WEB: { const char *u = g_lb.im[g_lb.idx].url; if (u[0] == '/') open_site(u); else open_url(u); break; }
    case H_DM_SEND: chat_send(); break;
    case H_FOLLOW: toggle_follow(); break;
    case H_DM_BTN: dm_with(g_prof.handle); break;
    case H_NEWBANNER: g_scroll[V_HOME] = 0; refresh_home(); break;
    case H_ME_AVATAR: g_drawer = 1; InvalidateRect(g_main, NULL, FALSE); break;
    case H_ACCOUNT: {
        HMENU m = CreatePopupMenu();
        wchar_t b[160], *hh = u8w(g_me.handle);
        _snwprintf(b, 160, L"@%ls \u3092\u30ED\u30B0\u30A2\u30A6\u30C8", hh); b[159] = 0; free(hh);
        if (stats_configured()) AppendMenuW(m, MF_STRING | (stats_consent() == 1 ? MF_CHECKED : 0), 111, L"利用者数の集計に協力する");
        AppendMenuW(m, MF_STRING, 110, b);
        POINT pt; GetCursorPos(&pt);
        TrackPopupMenu(m, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_main, NULL);
        DestroyMenu(m);
        break; }
    case H_DRAWER_ITEM: if (h->a < 0) { g_drawer = 0; InvalidateRect(g_main, NULL, FALSE); } else nav_action(h->a); break;
    case H_DRAWER_BG: g_drawer = 0; InvalidateRect(g_main, NULL, FALSE); break;
    case H_TREND: if (h->a < g_ntrends) { char t[130]; _snprintf(t, sizeof t, "%s%s", g_trends[h->a].is_hash ? "#" : "", g_trends[h->a].tag); search_for(t); } break;
    case H_SUGG: if (h->a < g_nsugg) open_profile(g_sugg[h->a].handle); break;
    case H_SEARCH: show_view(V_SEARCH); break;
    case H_PROF_BTN: open_site("/settings"); ui_toast(L"ブラウザで開きます"); break;
    }
}

/* ---------------------------------------------------------- 入力欄 */
static HBRUSH g_edit_br, g_edit_br2;
static LRESULT CALLBACK edit_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    int idx = GetDlgCtrlID(h) - 200;
    if (idx < 0 || idx >= NED) idx = ED_POST;
    int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0, ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (m == WM_KEYDOWN && idx == ED_POST && ctrl && (w == 'B' || w == 'I')) {
        if (w == 'B') wrap_sel(L"<太字>", L"</太字>"); else wrap_sel(L"<斜体>", L"</斜体>");
        return 0;
    }
    if (m == WM_KEYDOWN && w == VK_RETURN && idx >= ED_OPT0) return 0;
    if (m == WM_KEYDOWN && w == VK_RETURN) {
        if (idx == ED_POST && ctrl) { do_post(); return 0; }
        if (idx == ED_SEARCH) { do_search(); return 0; }
        if (idx == ED_DM && g_view == V_CHAT && !shift) { chat_send(); return 0; }
        if (idx == ED_DM && g_view == V_THREAD && ctrl) { thread_reply(); return 0; }
    }
    if (m == WM_CHAR) {
        if (idx == ED_POST && (w == '\n' || (ctrl && (w == 2 || w == 9)))) return 0;
        if (idx >= ED_OPT0 && (w == '\r' || w == '\n')) return 0;
        if (idx == ED_SEARCH && (w == '\r' || w == '\n')) return 0;
        if (idx == ED_DM && g_view == V_CHAT && (w == '\r' || w == '\n') && !shift) return 0;
        if (idx == ED_DM && g_view == V_THREAD && w == '\n') return 0;
    }
    LRESULT r = CallWindowProcW(g_ed_old[idx], h, m, w, l);
    if ((m == WM_PAINT || m == WM_SETFOCUS || m == WM_KILLFOCUS) && GetWindowTextLengthW(h) == 0) {
        static const wchar_t *ph[NED] = { L"いまどうしてる？", L"検索", L"メッセージを入力", L"", L"", L"", L"" };
        const wchar_t *phs = (idx == ED_DM && g_view == V_THREAD) ? L"返信をポスト" : ph[idx];
        HDC dc = GetDC(h);
        HGDIOBJ old = SelectObject(dc, F_BODY.f);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, C_GRAY);
        TextOutW(dc, idx == ED_POST ? 1 : 0, idx == ED_POST ? 1 : 0, phs, (int)wcslen(phs));
        SelectObject(dc, old);
        ReleaseDC(h, dc);
    }
    return r;
}

static int dpi_of(HWND h)
{
    typedef UINT (WINAPI *PFN)(HWND);
    PFN f = (PFN)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (f) { UINT d = f(h); if (d) return (int)d; }
    HDC dc = GetDC(h);
    int d = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(h, dc);
    return d > 0 ? d : 96;
}

LRESULT CALLBACK main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        g_main = h;
        g_dpi = dpi_of(h);
        gfx_init();
        net_init();
        g_edit_br = CreateSolidBrush(C_BG);
        g_edit_br2 = CreateSolidBrush(C_PANEL);
        for (int i = 0; i < NED; i++) {
            DWORD st = WS_CHILD | ((i == ED_SEARCH || i >= ED_OPT0) ? ES_AUTOHSCROLL : (ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN));
            g_ed[i] = CreateWindowExW(0, L"EDIT", L"", st, 0, 0, 10, 10, h, (HMENU)(INT_PTR)(200 + i), GetModuleHandleW(NULL), NULL);
            SendMessageW(g_ed[i], WM_SETFONT, (WPARAM)F_BODY.f, TRUE);
            if (i >= ED_OPT0) SendMessageW(g_ed[i], EM_SETLIMITTEXT, 30, 0);
            g_ed_old[i] = (WNDPROC)SetWindowLongPtrW(g_ed[i], GWLP_WNDPROC, (LONG_PTR)edit_proc);
        }
        SetTimer(h, 4, 4000, NULL);
        DragAcceptFiles(h, TRUE);
        SetTimer(h, 1, 30000, NULL);
        return 0;
    case WM_DROPFILES: {
        int c = att_ctx_view();
        HDROP d = (HDROP)w;
        UINT cnt = DragQueryFileW(d, 0xFFFFFFFF, NULL, 0);
        if (c < 0) ui_toast(L"この画面には画像を添付できません");
        else for (UINT i = 0; i < cnt; i++) {
            wchar_t p[MAX_PATH * 2];
            if (DragQueryFileW(d, i, p, MAX_PATH * 2)) att_add_file(c, p);
        }
        DragFinish(d);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(h, NULL, FALSE); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)l)->ptMinTrackSize.x = 340; ((MINMAXINFO *)l)->ptMinTrackSize.y = 480; return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        int W = rc.right, H = rc.bottom;
        if (W > 0 && H > 0) {
            if (!g_bb || g_bbw != W || g_bbh != H) {
                if (g_bb) DeleteObject(g_bb);
                g_bb = CreateCompatibleBitmap(dc, W, H);
                g_bbw = W; g_bbh = H;
            }
            HDC mdc = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(mdc, g_bb);
            paint(mdc, W, H);
            BitBlt(dc, 0, 0, W, H, mdc, 0, 0, SRCCOPY);
            SelectObject(mdc, old);
            DeleteDC(mdc);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        int id = GetDlgCtrlID((HWND)l);
        int panel = id == 200 + ED_SEARCH || id == 200 + ED_DM;
        SetTextColor((HDC)w, C_TEXT);
        SetBkColor((HDC)w, panel ? C_PANEL : C_BG);
        return (LRESULT)(panel ? g_edit_br2 : g_edit_br);
    }
    case WM_COMMAND:
        if (LOWORD(w) >= 200 && LOWORD(w) < 200 + NED && HIWORD(w) == EN_CHANGE) {
            int id = LOWORD(w) - 200;
            if (id == ED_SEARCH) {
                char cur[200];
                ed_get_u8(ED_SEARCH, cur, sizeof cur);
                if (strcmp(cur, g_sq) != 0) g_sphase = 0;
                SetTimer(h, 5, 250, NULL);
            } else if (id == ED_DM && g_view == V_CHAT && GetWindowTextLengthW(g_ed[ED_DM]) > 0) {
                DWORD now = GetTickCount();
                if (now - g_typing_tick > 3000) { typing_set(1); g_typing_tick = now; }
            }
            InvalidateRect(h, NULL, FALSE);
        }
        else if (LOWORD(w) == 101 || LOWORD(w) == 102) {
            Feed *f = cur_feed();
            if (g_menu_post >= 0 && g_menu_post < f->n) {
                char u[300]; post_url(f->p[g_menu_post], u, sizeof u);
                if (LOWORD(w) == 101) { wchar_t *ww = u8w(u); copy_text(ww); free(ww); ui_toast(L"\u30EA\u30F3\u30AF\u3092\u30B3\u30D4\u30FC\u3057\u307E\u3057\u305F"); }
                else open_url(u);
            }
        } else if (LOWORD(w) >= 103 && LOWORD(w) <= 105) {
            Feed *f = cur_feed();
            if (g_menu_post >= 0 && g_menu_post < f->n) {
                Post *p = f->p[g_menu_post];
                char path[120];
                if (LOWORD(w) == 103) {
                    if (p->pinned) net_api(T_PIN, "DELETE", "/api/users/me/pinned", NULL, p->id, 0);
                    else { cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "tweetId", p->id); char *js = cJSON_PrintUnformatted(o); cJSON_Delete(o); net_api(T_PIN, "PUT", "/api/users/me/pinned", js, p->id, 1); cJSON_free(js); }
                } else if (LOWORD(w) == 104) {
                    int nx = p->policy == 0 ? 1 : p->policy == 1 ? 2 : 0;
                    cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "replyPolicy", POLICY_KEY[nx]); char *js = cJSON_PrintUnformatted(o); cJSON_Delete(o);
                    _snprintf(path, sizeof path, "/api/tweets/%s", p->id);
                    net_api(T_POLICY, "PATCH", path, js, p->id, nx);
                    cJSON_free(js);
                } else if (MessageBoxW(h, L"このポストを削除しますか？", L"north", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    _snprintf(path, sizeof path, "/api/tweets/%s", p->id);
                    net_api(T_DELETE, "DELETE", path, NULL, p->id, 0);
                }
            }
        } else if ((LOWORD(w) >= 300 && LOWORD(w) < 308) || (LOWORD(w) >= 400 && LOWORD(w) < 424) || (LOWORD(w) >= 500 && LOWORD(w) < 560)) {
            int v = LOWORD(w);
            if (v < 400) g_poll_d = v - 300; else if (v < 500) g_poll_h = v - 400; else g_poll_m = v - 500;
            InvalidateRect(h, NULL, FALSE);
        } else if (LOWORD(w) == 111) stats_toggle();
        else if (LOWORD(w) == 110) do_logout();
        return 0;
    case WM_MOUSEWHEEL:
        if (g_lb.on) {
            POINT pt = { (short)LOWORD(l), (short)HIWORD(l) };
            ScreenToClient(h, &pt);
            lb_zoom_at((short)HIWORD(w) > 0 ? 1.18f : 1.0f / 1.18f, pt.x, pt.y);
            return 0;
        }
        scroll_by(-(int)((short)HIWORD(w)) * PX(90) / 120);
        return 0;
    case WM_KEYDOWN: {
        if (g_lb.on) {
            switch (w) {
            case VK_ESCAPE: lb_close(); break;
            case VK_LEFT: lb_step(-1); break;
            case VK_RIGHT: lb_step(1); break;
            case VK_ADD: case VK_OEM_PLUS: lb_zoom_at(1.25f, G.W / 2, G.H / 2); break;
            case VK_SUBTRACT: case VK_OEM_MINUS: lb_zoom_at(0.8f, G.W / 2, G.H / 2); break;
            }
            return 0;
        }
        int page = G.vy1 - G.vy0 - PX(60);
        switch (w) {
        case VK_PRIOR: scroll_by(-page); break;
        case VK_NEXT: case VK_SPACE: scroll_by(page); break;
        case VK_UP: scroll_by(-PX(60)); break;
        case VK_DOWN: scroll_by(PX(60)); break;
        case VK_HOME: scroll_by(-g_content_h); break;
        case VK_END: scroll_by(g_content_h); break;
        case VK_F5: if (g_view == V_HOME) refresh_home(); break;
        case VK_ESCAPE: if (g_drawer) { g_drawer = 0; InvalidateRect(h, NULL, FALSE); } break;
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        POINT pt = { (short)LOWORD(l), (short)HIWORD(l) };
        Hit *hh = hit_at(pt.x, pt.y);
        if (g_lb.on && hh && hh->type == H_LB_IMG) {         /* ダブルクリックで 等倍 ⇔ 拡大 */
            if (g_lb.zoom > 1.01f) { g_lb.zoom = 1.0f; g_lb.px = g_lb.py = 0; InvalidateRect(h, NULL, FALSE); }
            else lb_zoom_at(2.5f, pt.x, pt.y);
            return 0;
        }
    }
    /* fall through: 通常のクリックとして扱う */
    case WM_LBUTTONDOWN: {
        POINT pt = { (short)LOWORD(l), (short)HIWORD(l) };
        Hit *hh = hit_at(pt.x, pt.y);
        Hit copy;
        if (g_lb.on && hh && hh->type == H_LB_IMG) {         /* 画像をつかんで移動 */
            g_lb.drag = 1; g_lb.lx = pt.x; g_lb.ly = pt.y;
            SetCapture(h);
            return 0;
        }
        if (hh) copy = *hh;
        if (!hh || hh->type != H_LINK) SetFocus(h);
        if (hh) on_click(&copy);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_lb.on && g_lb.drag) {
            int mx = (short)LOWORD(l), my = (short)HIWORD(l);
            g_lb.px += (float)(mx - g_lb.lx); g_lb.py += (float)(my - g_lb.ly);
            g_lb.lx = mx; g_lb.ly = my;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_lb.drag) { g_lb.drag = 0; ReleaseCapture(); }
        return 0;
    case WM_CAPTURECHANGED: g_lb.drag = 0; return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
            Hit *hh = hit_at(pt.x, pt.y);
            if (g_lb.on && hh && hh->type == H_LB_IMG) { SetCursor(LoadCursorW(NULL, (LPCWSTR)(g_lb.zoom > 1.01f ? IDC_SIZEALL : IDC_ARROW))); return TRUE; }
            if (g_lb.on && hh && hh->type == H_LB_BG) { SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW)); return TRUE; }
            if (hh && hh->type != H_DRAWER_PANEL) { SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND)); return TRUE; }
        }
        break;
    case WM_TIMER:
        if (w == 1) {
            if (!g_cookie[0]) return 0;
            net_api(T_UNREAD_N, "GET", "/api/notifications/unread-count", NULL, NULL, 0);
            net_api(T_UNREAD_D, "GET", "/api/dm/unread-count", NULL, NULL, 0);
            if (g_newest[0]) {
                char enc[120], path[200];
                urlenc(g_newest, enc, sizeof enc);
                _snprintf(path, sizeof path, "/api/timeline/home/new-count?since=%s", enc);
                net_api(T_NEWCOUNT, "GET", path, NULL, NULL, 0);
            }
            if (++g_poll_n % 10 == 0) net_api(T_TRENDS, "GET", "/api/trends", NULL, NULL, 0);
        } else if (w == 4) {
            static int tick;
            tick++;
            if (!g_cookie[0] && !g_is_demo) return 0;
            if (g_view == V_CHAT) chat_poll();
            else if (g_view == V_DM) load_convs();
            else if (g_view == V_NOTIF && g_scroll[V_NOTIF] == 0 && tick % 3 == 0) load_notifs(0);
        } else if (w == 5) {
            KillTimer(h, 5);
            char cur[200], enc[400], path[500];
            ed_get_u8(ED_SEARCH, cur, sizeof cur);
            if (!cur[0]) { g_nsu = g_nst = 0; }
            else if (g_sphase == 0) {
                urlenc(cur, enc, sizeof enc);
                _snprintf(path, sizeof path, "/api/search/suggest?q=%s", enc);
                net_api(T_SUGGEST, "GET", path, NULL, cur, 0);
            }
            InvalidateRect(h, NULL, FALSE);
        } else if (w == 2) { KillTimer(h, 2); g_toast_on = 0; InvalidateRect(h, NULL, FALSE); }
        else if (w == 3) { KillTimer(h, 3); if (g_img_dirty) { g_img_dirty = 0; InvalidateRect(h, NULL, FALSE); } }
        return 0;
    case WM_APP_API: {
        ApiRes *r = (ApiRes *)l;
        on_api(r);
        free(r->body); free(r->ctx); free(r);
        return 0;
    }
    case WM_APP_IMG:
        g_img_dirty = 1;
        SetTimer(h, 3, 40, NULL);
        return 0;
    case WM_APP_LOGIN_DONE:
        g_login_prompted = 0;
        net_reopen();
        feed_clear(&g_home);
        g_home.loaded = 0;
        fetch_all();
        return 0;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        gfx_fonts();
        for (int i = 0; i < NED; i++) SendMessageW(g_ed[i], WM_SETFONT, (WPARAM)F_BODY.f, TRUE);
        relayout_all();
        RECT *rc = (RECT *)l;
        SetWindowPos(h, NULL, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_DESTROY:
        net_shutdown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
