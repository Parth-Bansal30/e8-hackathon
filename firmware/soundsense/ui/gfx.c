/* Sound Sense drawing primitives. See gfx.h. */
#include "gfx.h"

#include <math.h>
#include <stddef.h>

static uint8_t (*cv)[GFX_W][3];
static int dirty_y0 = GFX_H, dirty_y1 = -1;

void gfx_init(uint8_t (*canvas)[GFX_W][3])
{
    cv = canvas;
    gfx_dirty_reset();
}

void gfx_dirty_reset(void)
{
    dirty_y0 = GFX_H;
    dirty_y1 = -1;
}

bool gfx_dirty_rows(int *y0, int *y1)
{
    *y0 = dirty_y0;
    *y1 = dirty_y1;
    return dirty_y1 >= dirty_y0;
}

/* Clip a box to the screen and mark its rows dirty. False when nothing is left. */
static bool clip(int *x0, int *y0, int *x1, int *y1)
{
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > GFX_W) *x1 = GFX_W;
    if (*y1 > GFX_H) *y1 = GFX_H;
    if (*x0 >= *x1 || *y0 >= *y1) return false;
    if (*y0 < dirty_y0) dirty_y0 = *y0;
    if (*y1 - 1 > dirty_y1) dirty_y1 = *y1 - 1;
    return true;
}

static inline void put(int x, int y, gfx_color c)
{
    uint8_t *d = cv[y][x];
    d[0] = c.b;
    d[1] = c.g;
    d[2] = c.r;
}

/* Blend c over the pixel with coverage a in 0..1. */
static inline void blend(int x, int y, gfx_color c, float a)
{
    if (a <= 0.0f) return;
    if (a >= 1.0f) { put(x, y, c); return; }
    uint8_t *d = cv[y][x];
    d[0] = (uint8_t)(d[0] + (c.b - d[0]) * a + 0.5f);
    d[1] = (uint8_t)(d[1] + (c.g - d[1]) * a + 0.5f);
    d[2] = (uint8_t)(d[2] + (c.r - d[2]) * a + 0.5f);
}

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float minf(float a, float b) { return a < b ? a : b; }

void gfx_fill(int x, int y, int w, int h, gfx_color c)
{
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (!clip(&x0, &y0, &x1, &y1)) return;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) put(i, j, c);
}

/* Coverage of pixel (px, py) by a rounded rectangle (signed-distance with a 1 px ramp). */
static float rr_cov(int px, int py, float x, float y, float w, float h, float r)
{
    /* signed distance to the rounded rectangle: negative inside */
    const float qx = fabsf(px + 0.5f - (x + w * 0.5f)) - (w * 0.5f - r);
    const float qy = fabsf(py + 0.5f - (y + h * 0.5f)) - (h * 0.5f - r);
    const float ox = maxf(qx, 0.0f), oy = maxf(qy, 0.0f);
    const float d = sqrtf(ox * ox + oy * oy) + minf(maxf(qx, qy), 0.0f) - r;
    return clamp01(0.5f - d);
}

void gfx_round_rect(int x, int y, int w, int h, int r, gfx_color c)
{
    if (2 * r > w) r = w / 2;
    if (2 * r > h) r = h / 2;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (!clip(&x0, &y0, &x1, &y1)) return;
    for (int j = y0; j < y1; j++) {
        const bool corner_row = j < y + r || j >= y + h - r;
        for (int i = x0; i < x1; i++) {
            if (!corner_row && i >= x + r && i < x + w - r) { put(i, j, c); continue; }
            blend(i, j, c, rr_cov(i, j, x, y, w, h, r));
        }
    }
}

void gfx_round_frame(int x, int y, int w, int h, int r, int t, gfx_color c)
{
    if (2 * r > w) r = w / 2;
    if (2 * r > h) r = h / 2;
    const int ri = r > t ? r - t : 0;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (!clip(&x0, &y0, &x1, &y1)) return;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            const float a = rr_cov(i, j, x, y, w, h, r) *
                            (1.0f - rr_cov(i, j, x + t, y + t, w - 2 * t, h - 2 * t, ri));
            blend(i, j, c, a);
        }
}

void gfx_circle(float cx, float cy, float r, gfx_color c)
{
    int x0 = (int)floorf(cx - r - 1), y0 = (int)floorf(cy - r - 1);
    int x1 = (int)ceilf(cx + r + 1), y1 = (int)ceilf(cy + r + 1);
    if (!clip(&x0, &y0, &x1, &y1)) return;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            const float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
            blend(i, j, c, clamp01(r + 0.5f - sqrtf(dx * dx + dy * dy)));
        }
}

static void ring_span(float cx, float cy, float r, float t, gfx_color c, bool arc, float a0, float a1)
{
    int x0 = (int)floorf(cx - r - 1), y0 = (int)floorf(cy - r - 1);
    int x1 = (int)ceilf(cx + r + 1), y1 = (int)ceilf(cy + r + 1);
    if (!clip(&x0, &y0, &x1, &y1)) return;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            const float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
            const float d = sqrtf(dx * dx + dy * dy);
            float a = clamp01(r + 0.5f - d) * clamp01(d - (r - t) + 0.5f);
            if (a <= 0.0f) continue;
            if (arc) {   /* angle into [a0, a0 + 2 pi), then soften the ends over a pixel */
                float ang = atan2f(-dy, dx);
                while (ang < a0) ang += 6.2831853f;
                while (ang >= a0 + 6.2831853f) ang -= 6.2831853f;
                const float in = ang <= a1 ? fminf(ang - a0, a1 - ang)
                                           : -fminf(ang - a1, a0 + 6.2831853f - ang);
                a *= clamp01(in * d + 0.5f);
                if (a <= 0.0f) continue;
            }
            blend(i, j, c, a);
        }
}

void gfx_ring(float cx, float cy, float r, float t, gfx_color c)
{
    ring_span(cx, cy, r, t, c, false, 0, 0);
}

void gfx_arc(float cx, float cy, float r, float t, float a0, float a1, gfx_color c)
{
    while (a0 < 0) { a0 += 6.2831853f; a1 += 6.2831853f; }
    ring_span(cx, cy, r, t, c, true, a0, a1);
}

void gfx_line(float x0, float y0, float x1, float y1, float t, gfx_color c)
{
    const float h = t * 0.5f;
    int bx0 = (int)floorf(fminf(x0, x1) - h - 1), by0 = (int)floorf(fminf(y0, y1) - h - 1);
    int bx1 = (int)ceilf(fmaxf(x0, x1) + h + 1), by1 = (int)ceilf(fmaxf(y0, y1) + h + 1);
    if (!clip(&bx0, &by0, &bx1, &by1)) return;
    const float vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
    for (int j = by0; j < by1; j++)
        for (int i = bx0; i < bx1; i++) {
            const float px = i + 0.5f - x0, py = j + 0.5f - y0;
            float u = len2 > 0 ? (px * vx + py * vy) / len2 : 0.0f;
            u = clamp01(u);
            const float dx = px - u * vx, dy = py - u * vy;
            blend(i, j, c, clamp01(h + 0.5f - sqrtf(dx * dx + dy * dy)));
        }
}

/* Convex polygon; coverage is the distance to the nearest edge, so edges are smooth. */
void gfx_poly(const float *xy, int n, gfx_color c)
{
    if (n < 3 || n > 8) return;
    float bxf0 = xy[0], byf0 = xy[1], bxf1 = xy[0], byf1 = xy[1], area = 0;
    for (int k = 0; k < n; k++) {
        const float *p = xy + 2 * k, *q = xy + 2 * ((k + 1) % n);
        bxf0 = fminf(bxf0, p[0]); bxf1 = fmaxf(bxf1, p[0]);
        byf0 = fminf(byf0, p[1]); byf1 = fmaxf(byf1, p[1]);
        area += p[0] * q[1] - q[0] * p[1];
    }
    if (area == 0.0f) return;
    /* each edge as a x + b y + c >= 0 inside, scaled so the value is the distance in pixels */
    float ea[8], eb[8], ec[8];
    const float s = area > 0 ? 1.0f : -1.0f;
    for (int k = 0; k < n; k++) {
        const float *p = xy + 2 * k, *q = xy + 2 * ((k + 1) % n);
        const float dx = q[0] - p[0], dy = q[1] - p[1], len = sqrtf(dx * dx + dy * dy);
        if (len == 0.0f) { ea[k] = eb[k] = 0; ec[k] = 1e9f; continue; }
        ea[k] = -s * dy / len;
        eb[k] = s * dx / len;
        ec[k] = -(ea[k] * p[0] + eb[k] * p[1]);
    }
    int x0 = (int)floorf(bxf0) - 1, y0 = (int)floorf(byf0) - 1;
    int x1 = (int)ceilf(bxf1) + 1, y1 = (int)ceilf(byf1) + 1;
    if (!clip(&x0, &y0, &x1, &y1)) return;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            const float px = i + 0.5f, py = j + 0.5f;
            float d = 1e9f;
            for (int k = 0; k < n; k++) d = minf(d, ea[k] * px + eb[k] * py + ec[k]);
            if (d > -0.5f) blend(i, j, c, clamp01(d + 0.5f));
        }
}

void gfx_triangle(float x0, float y0, float x1, float y1, float x2, float y2, gfx_color c)
{
    const float xy[6] = {x0, y0, x1, y1, x2, y2};
    gfx_poly(xy, 3, c);
}

void gfx_hbar(int x, int y, int w, int h, float frac, gfx_color fg, gfx_color bg)
{
    frac = clamp01(frac);
    gfx_round_rect(x, y, w, h, h / 2, bg);
    const int fw = (int)(w * frac + 0.5f);
    if (fw > 0) gfx_round_rect(x, y, fw < h ? h : fw, h, h / 2, fg);
}

/* ---------- text ---------- */

static const uint8_t *glyph(const gfx_font *f, char ch, int *w, float *adv)
{
    if ((uint8_t)ch < f->first || (uint8_t)ch > f->last) return NULL;
    const int i = (uint8_t)ch - f->first;
    if (f->widths) {
        *w = f->widths[i];
        *adv = f->adv ? f->adv[i] * (1.0f / 16.0f) : (float)*w;
        return f->data + f->offsets[i];
    }
    *w = f->fixed_w;
    *adv = (float)f->fixed_w;
    return f->data + (size_t)i * f->fixed_w * (f->h + 2 * f->pad);
}

static float sample(const uint8_t *g, int w, int h, float u, float v)
{
    if (u < 0) u = 0;
    if (v < 0) v = 0;
    if (u > w - 1) u = (float)(w - 1);
    if (v > h - 1) v = (float)(h - 1);
    const int iu = (int)u, iv = (int)v;
    const int iu1 = iu + 1 < w ? iu + 1 : iu, iv1 = iv + 1 < h ? iv + 1 : iv;
    const float fu = u - iu, fv = v - iv;
    const float top = g[iv * w + iu] * (1 - fu) + g[iv * w + iu1] * fu;
    const float bot = g[iv1 * w + iu] * (1 - fu) + g[iv1 * w + iu1] * fu;
    return top * (1 - fv) + bot * fv;
}

int gfx_text_width(const gfx_font *f, const char *s, int px)
{
    const float k = (float)px / f->h;
    float x = 0, adv;
    int w;
    for (; *s; s++)
        if (glyph(f, *s, &w, &adv)) x += adv * k;
    return (int)(x + 0.5f);
}

int gfx_text(const gfx_font *f, int x, int y, const char *s, gfx_color c, int px)
{
    const float k = (float)px / f->h, inv = 1.0f / k;
    const int gh = f->h + 2 * f->pad;
    /* distance fonts: stored units -> output pixels from the edge */
    const float dk = f->sdf_scale ? k / f->sdf_scale : 0.0f;
    const bool direct = px == f->h && !f->sdf_scale;
    float pen = (float)x;
    for (; *s; s++) {
        int gw;
        float adv;
        const uint8_t *g = glyph(f, *s, &gw, &adv);
        if (!g) continue;
        const float gx = pen - f->pad * k, gy = y - f->pad * k;  /* bitmap origin on screen */
        const int ox = (int)floorf(gx + 0.5f), oy = (int)floorf(gy + 0.5f);   /* direct path */
        int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
        int x1 = (int)ceilf(gx + gw * k), y1 = (int)ceilf(gy + gh * k);
        if (direct) { x0 = ox; y0 = oy; x1 = ox + gw; y1 = oy + gh; }
        if (*s != ' ' && clip(&x0, &y0, &x1, &y1)) {
            for (int j = y0; j < y1; j++)
                for (int i = x0; i < x1; i++) {
                    float a;
                    if (direct) {
                        a = g[(j - oy) * gw + (i - ox)] * (1.0f / 255.0f);
                    } else {
                        const float v = sample(g, gw, gh, (i + 0.5f - gx) * inv - 0.5f, (j + 0.5f - gy) * inv - 0.5f);
                        if (f->sdf_scale) {
                            a = clamp01((v - 128.0f) * dk + 0.5f);
                        } else {
                            /* Scaled up, the bilinear edge ramp is k pixels wide. Read the alpha
                             * as a rough distance to the edge and squeeze the ramp to about 1 px. */
                            a = v * (1.0f / 255.0f);
                            if (k > 1.0f) a = clamp01((a - 0.5f) * k + 0.5f);
                        }
                    }
                    blend(i, j, c, a);
                }
        }
        pen += adv * k;
    }
    return (int)(pen + 0.5f);
}

void gfx_text_center(const gfx_font *f, int cx, int y, const char *s, gfx_color c, int px)
{
    gfx_text(f, cx - gfx_text_width(f, s, px) / 2, y, s, c, px);
}

int gfx_fit_px(const gfx_font *f, const char *s, int max_w, int max_px)
{
    const int w = gfx_text_width(f, s, max_px);
    if (w <= max_w) return max_px;
    return max_px * max_w / w;
}
