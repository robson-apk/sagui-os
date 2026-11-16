/* fonte.c - fontes da web: TrueType (glyf), WOFF (zlib) e WOFF2 (Brotli +
 * transformação de glyf), contornos com composições e rasterizador com
 * antialias por área acumulada (cobertura exata de cada pixel).
 *
 * Sem hinting: o desenho é o contorno escalado, como no macOS. CFF (OpenType
 * com contornos PostScript, charstrings Type 2) também, inclusive CID. */
#include "sagui.h"
#include "cipo.h"
#include "../../../terceiros/font-rs/acumulador.h"

/* DEFLATE com o cabeçalho zlib, o mesmo das imagens PNG (na libsagui, compactar.c, desde a H93). */
int inflar_zlib(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max);

static unsigned u16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static int s16(const uint8_t *p) { return (int16_t)(p[0] << 8 | p[1]); }
static unsigned u32(const uint8_t *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }

typedef struct { int16_t x, y; uint8_t on; } fpt;

/* glifo já decodificado (WOFF2) */
typedef struct { uint8_t tipo; uint16_t ncont, npts; uint32_t pts, ends; const uint8_t *comp; } gdec;

#define CACHE_G 512
typedef struct { uint16_t gid; uint16_t px; int16_t l, a, ox, oy; uint8_t *cob; } gcache;

struct fonte {
    uint8_t *blocos[24]; int nblocos;      /* memória a liberar */
    const uint8_t *cmap, *glyf, *loca, *aw;
    unsigned n_cmap, n_glyf, n_loca;
    int cmap_fmt, aw_passo;
    int upem, ngl, nhm, loca_longo, asc, desc, gap;
    gdec *gd;                               /* WOFF2: glifos decodificados */
    /* CFF (OpenType com contornos PostScript) */
    const uint8_t *cff; unsigned n_cff;
    const uint8_t *cs_idx, *gsub_idx, *lsub_idx;  /* INDEX das charstrings, subrotinas globais e locais */
    const uint8_t *fdsel; const uint8_t *fd_lsub[32]; int n_fd;
    fpt *gpts; uint16_t *gends;
    gcache cache[CACHE_G];
    unsigned cache_bytes;
    /* variações (OpenType Font Variations: fvar, avar, gvar) */
    const uint8_t *fvar, *avar, *gvar; unsigned n_fvar, n_avar, n_gvar;
    int neixos; uint32_t eixo_tag[16]; float eixo_min[16], eixo_def[16], eixo_max[16];
    float coord[16]; int variada;          /* coordenadas normalizadas desta instância */
    int16_t *aw_var;                        /* avanços desta instância; -32768 = ainda não calculado */
    struct fonte *base;                     /* instância: as tabelas são da base */
    int obliquo;                            /* itálico sintético: inclina 12 graus */
};

static void *guardar(fonte_t *f, unsigned n)
{
    if (f->nblocos >= 24) return NULL;
    uint8_t *p = aloca(n ? n : 1);
    if (p) f->blocos[f->nblocos++] = p;
    return p;
}

void fonte_fechar(fonte_t *f)
{
    if (!f) return;
    for (int i = 0; i < CACHE_G; i++) if (f->cache[i].cob) libera(f->cache[i].cob);
    if (f->aw_var) libera(f->aw_var);
    if (!f->base) for (int i = 0; i < f->nblocos; i++) libera(f->blocos[i]);
    libera(f);
}

/* ------------------------------------------------------------ tabelas */

typedef struct { uint32_t tag; const uint8_t *p; unsigned n; int transf; } tabela;
#define TAG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

static const tabela *achar(const tabela *t, int n, uint32_t tag)
{
    for (int i = 0; i < n; i++) if (t[i].tag == tag) return &t[i];
    return NULL;
}

static int ler_cmap(fonte_t *f, const uint8_t *p, unsigned n)
{
    if (n < 4) return 0;
    unsigned nt = u16(p + 2);
    const uint8_t *melhor = NULL; int fmt = 0;
    for (unsigned i = 0; i < nt && 4 + i * 8 + 8 <= n; i++) {
        unsigned plat = u16(p + 4 + i * 8), enc = u16(p + 6 + i * 8), off = u32(p + 8 + i * 8);
        if (off + 4 > n) continue;
        unsigned fm = u16(p + off);
        bool uni = plat == 0 || (plat == 3 && (enc == 1 || enc == 10));
        if (!uni) continue;
        if (fm == 12 && fmt != 12) { melhor = p + off; fmt = 12; }
        else if (fm == 4 && !fmt) { melhor = p + off; fmt = 4; }
    }
    if (!melhor) return 0;
    f->cmap = melhor; f->cmap_fmt = fmt; f->n_cmap = n - (unsigned)(melhor - p);
    return 1;
}

static unsigned gid_de(const fonte_t *f, unsigned cp)
{
    const uint8_t *c = f->cmap;
    if (f->cmap_fmt == 4) {
        if (cp > 0xFFFF) return 0;
        unsigned segx2 = u16(c + 6);
        const uint8_t *fim = c + 14, *ini = fim + segx2 + 2, *delta = ini + segx2, *rng = delta + segx2;
        if ((unsigned)(rng + segx2 - c) > f->n_cmap) return 0;
        for (unsigned i = 0; i < segx2; i += 2) {
            if (cp > u16(fim + i)) continue;
            unsigned s = u16(ini + i);
            if (cp < s) return 0;
            unsigned ro = u16(rng + i);
            if (!ro) return (cp + u16(delta + i)) & 0xFFFF;
            const uint8_t *g = rng + i + ro + (cp - s) * 2;
            if ((unsigned)(g + 2 - c) > f->n_cmap) return 0;
            unsigned v = u16(g);
            return v ? (v + u16(delta + i)) & 0xFFFF : 0;
        }
        return 0;
    }
    unsigned ng = u32(c + 12);
    for (unsigned i = 0; i < ng && 16 + i * 12 + 12 <= f->n_cmap; i++) {
        const uint8_t *g = c + 16 + i * 12;
        if (cp >= u32(g) && cp <= u32(g + 4)) return u32(g + 8) + (cp - u32(g));
    }
    return 0;
}

/* ------------------------------------------------------------ WOFF2 */

static unsigned base128(const uint8_t **p, const uint8_t *fim)
{
    unsigned v = 0;
    for (int i = 0; i < 5 && *p < fim; i++) { uint8_t b = *(*p)++; v = v << 7 | (b & 0x7F); if (!(b & 0x80)) break; }
    return v;
}
static unsigned u255(const uint8_t **p, const uint8_t *fim)
{
    if (*p >= fim) return 0;
    uint8_t c = *(*p)++;
    if (c == 253) { if (*p + 2 > fim) return 0; unsigned v = u16(*p); *p += 2; return v; }
    if (c == 255) { if (*p >= fim) return 0; return 253u + *(*p)++; }
    if (c == 254) { if (*p >= fim) return 0; return 506u + *(*p)++; }
    return c;
}

static const uint32_t woff2_tags[18] = {
    TAG('c','m','a','p'), TAG('h','e','a','d'), TAG('h','h','e','a'), TAG('h','m','t','x'), TAG('m','a','x','p'), TAG('n','a','m','e'),
    TAG('O','S','/','2'), TAG('p','o','s','t'), TAG('c','v','t',' '), TAG('f','p','g','m'), TAG('g','l','y','f'), TAG('l','o','c','a'),
    TAG('p','r','e','p'), TAG('C','F','F',' '), TAG('V','O','R','G'), TAG('E','B','D','T'), TAG('E','B','L','C'), TAG('g','a','s','p'),
};

/* tamanho em bytes de uma composição (formato glyf) e se tem instruções */
static int tam_composto(const uint8_t *p, const uint8_t *fim, int *instr)
{
    const uint8_t *q = p;
    *instr = 0;
    for (;;) {
        if (q + 4 > fim) return -1;
        unsigned fl = u16(q);
        q += 4 + ((fl & 1) ? 4 : 2);
        if (fl & 8) q += 2; else if (fl & 0x40) q += 4; else if (fl & 0x80) q += 8;
        if (fl & 0x100) *instr = 1;
        if (!(fl & 0x20)) break;
    }
    return q > fim ? -1 : (int)(q - p);
}

static int woff2_glyf(fonte_t *f, const uint8_t *d, unsigned n)
{
    if (n < 36) return 0;
    unsigned ng = u16(d + 4);
    unsigned tam[7];
    for (int i = 0; i < 7; i++) tam[i] = u32(d + 8 + i * 4);
    const uint8_t *s[7], *fs[7];
    unsigned off = 36;
    for (int i = 0; i < 7; i++) { if (off + tam[i] > n) return 0; s[i] = d + off; fs[i] = d + off + tam[i]; off += tam[i]; }
    const uint8_t *ncont = s[0], *npts = s[1], *flags = s[2], *gl = s[3], *comp = s[4];
    (void)ncont;
    f->ngl = (int)ng;
    f->gd = guardar(f, ng * (unsigned)sizeof(gdec));
    unsigned tot_pts = tam[2] + 1, tot_cont = 0;
    for (unsigned g = 0; g < ng && s[0] + 2 * g + 2 <= fs[0]; g++) { int c = s16(s[0] + 2 * g); if (c > 0) tot_cont += (unsigned)c; }
    f->gpts = guardar(f, tot_pts * (unsigned)sizeof(fpt));
    f->gends = guardar(f, (tot_cont + 1) * 2);
    if (!f->gd || !f->gpts || !f->gends) return 0;
    unsigned pi = 0, ei = 0;
    for (unsigned g = 0; g < ng; g++) {
        gdec *G = &f->gd[g];
        memset(G, 0, sizeof *G);
        if (s[0] + 2 > fs[0]) return 0;
        int nc = s16(s[0]); s[0] += 2;
        if (nc == 0) continue;
        if (nc < 0) {
            int instr;
            int k = tam_composto(comp, fs[4], &instr);
            if (k < 0) return 0;
            G->tipo = 2; G->comp = comp; comp += k;
            if (instr) { unsigned il = u255(&gl, fs[3]); s[6] += il; }
            continue;
        }
        G->tipo = 1; G->ncont = (uint16_t)nc; G->ends = ei; G->pts = pi;
        unsigned tot = 0;
        for (int c = 0; c < nc; c++) {
            tot += u255(&npts, fs[1]);
            if (ei >= tot_cont) return 0;
            f->gends[ei++] = (uint16_t)(tot - 1);
        }
        if (pi + tot > tot_pts) return 0;
        int x = 0, y = 0;
        for (unsigned i = 0; i < tot; i++) {
            if (flags >= fs[2]) return 0;
            unsigned fl = *flags++, t = fl & 0x7F;
            int dx, dy;
            #define SINAL(f, v) (((f) & 1) ? (v) : -(v))
            if (t < 10) { if (gl + 1 > fs[3]) return 0; dx = 0; dy = SINAL(t, (int)(((t & 14) << 7) + gl[0])); gl += 1; }
            else if (t < 20) { if (gl + 1 > fs[3]) return 0; dx = SINAL(t, (int)((((t - 10) & 14) << 7) + gl[0])); dy = 0; gl += 1; }
            else if (t < 84) { if (gl + 1 > fs[3]) return 0; unsigned b0 = t - 20, b1 = gl[0]; dx = SINAL(t, (int)(1 + (b0 & 0x30) + (b1 >> 4))); dy = SINAL(t >> 1, (int)(1 + ((b0 & 0x0c) << 2) + (b1 & 0x0f))); gl += 1; }
            else if (t < 120) { if (gl + 2 > fs[3]) return 0; unsigned b0 = t - 84; dx = SINAL(t, (int)(1 + ((b0 / 12) << 8) + gl[0])); dy = SINAL(t >> 1, (int)(1 + (((b0 % 12) >> 2) << 8) + gl[1])); gl += 2; }
            else if (t < 124) { if (gl + 3 > fs[3]) return 0; unsigned b2 = gl[1]; dx = SINAL(t, (int)((gl[0] << 4) + (b2 >> 4))); dy = SINAL(t >> 1, (int)(((b2 & 0x0f) << 8) + gl[2])); gl += 3; }
            else { if (gl + 4 > fs[3]) return 0; dx = SINAL(t, (int)((gl[0] << 8) + gl[1])); dy = SINAL(t >> 1, (int)((gl[2] << 8) + gl[3])); gl += 4; }
            #undef SINAL
            x += dx; y += dy;
            fpt *p = &f->gpts[pi + i];
            p->x = (int16_t)x; p->y = (int16_t)y; p->on = !(fl & 0x80);
        }
        G->npts = (uint16_t)tot; pi += tot;
        unsigned il = u255(&gl, fs[3]); s[6] += il;
    }
    return 1;
}

/* ------------------------------------------------------------ CFF */

/* INDEX: count, offSize, offsets[count+1], dados. Devolve o fim do INDEX. */
static const uint8_t *idx_fim(const uint8_t *p, const uint8_t *fim)
{
    if (p + 2 > fim) return NULL;
    unsigned cnt = u16(p);
    if (!cnt) return p + 2;
    if (p + 3 > fim) return NULL;
    unsigned os = p[2];
    if (os < 1 || os > 4) return NULL;
    const uint8_t *o = p + 3 + cnt * os;
    if (o + os > fim) return NULL;
    unsigned v = 0; for (unsigned k = 0; k < os; k++) v = v << 8 | o[k];
    return o + os - 1 + v;
}
static int idx_obj(const uint8_t *p, unsigned i, const uint8_t **d, unsigned *n)
{
    if (!p) return 0;
    unsigned cnt = u16(p);
    if (i >= cnt) return 0;
    unsigned os = p[2];
    const uint8_t *o = p + 3;
    unsigned a = 0, b = 0;
    for (unsigned k = 0; k < os; k++) { a = a << 8 | o[i * os + k]; b = b << 8 | o[(i + 1) * os + k]; }
    if (b < a) return 0;
    *d = o + (cnt + 1) * os - 1 + a; *n = b - a;
    return 1;
}
/* DICT: devolve os operandos (inteiros) do operador op (12 xx = 1200 + xx) */
static int dict_achar(const uint8_t *p, unsigned n, int op, int *v, int max)
{
    int pilha[48], np = 0;
    for (unsigned i = 0; i < n;) {
        uint8_t b = p[i];
        if (b <= 21) {
            int o = b; i++;
            if (b == 12) { if (i >= n) return 0; o = 1200 + p[i++]; }
            if (o == op) { for (int k = 0; k < np && k < max; k++) v[k] = pilha[k]; return np; }
            np = 0;
        } else if (b == 28) { if (i + 3 > n) return 0; if (np < 48) pilha[np++] = (int16_t)(p[i + 1] << 8 | p[i + 2]); i += 3; }
        else if (b == 29) { if (i + 5 > n) return 0; if (np < 48) pilha[np++] = (int)u32(p + i + 1); i += 5; }
        else if (b == 30) { i++; while (i < n) { uint8_t q = p[i++]; if ((q & 15) == 15 || (q >> 4) == 15) break; } if (np < 48) pilha[np++] = 0; }
        else if (b >= 32 && b <= 246) { if (np < 48) pilha[np++] = b - 139; i++; }
        else if (b >= 247 && b <= 250) { if (i + 2 > n) return 0; if (np < 48) pilha[np++] = (b - 247) * 256 + p[i + 1] + 108; i += 2; }
        else if (b >= 251 && b <= 254) { if (i + 2 > n) return 0; if (np < 48) pilha[np++] = -(b - 251) * 256 - p[i + 1] - 108; i += 2; }
        else i++;
    }
    return -1;
}
static const uint8_t *cff_subrs_privado(const uint8_t *c, unsigned nc, const uint8_t *top, unsigned ntop)
{
    int pv[2];
    if (dict_achar(top, ntop, 18, pv, 2) != 2 || pv[1] < 0 || (unsigned)pv[1] + (unsigned)pv[0] > nc) return NULL;
    const uint8_t *priv = c + pv[1];
    int sb[1];
    if (dict_achar(priv, (unsigned)pv[0], 19, sb, 1) != 1) return NULL;
    const uint8_t *ls = priv + sb[0];
    if (ls < c || ls + 2 > c + nc || !idx_fim(ls, c + nc)) return NULL;
    return ls;
}
static int cff_abrir(fonte_t *f, const uint8_t *c, unsigned n)
{
    if (n < 4) return 0;
    const uint8_t *fim = c + n;
    const uint8_t *nomes = c + c[2];
    const uint8_t *tops = idx_fim(nomes, fim);
    const uint8_t *strs = tops ? idx_fim(tops, fim) : NULL;
    const uint8_t *gsub = strs ? idx_fim(strs, fim) : NULL;
    if (!gsub || !idx_fim(gsub, fim)) return 0;
    const uint8_t *top; unsigned ntop;
    if (!idx_obj(tops, 0, &top, &ntop)) return 0;
    int v[4];
    if (dict_achar(top, ntop, 17, v, 1) != 1 || v[0] <= 0 || (unsigned)v[0] >= n) return 0;
    f->cff = c; f->n_cff = n;
    f->cs_idx = c + v[0];
    if (!idx_fim(f->cs_idx, fim)) return 0;
    f->gsub_idx = gsub;
    if (dict_achar(top, ntop, 1230, v, 4) >= 0) {   /* ROS: CID, uma Private por FD */
        int fa[1], fs[1];
        if (dict_achar(top, ntop, 1236, fa, 1) != 1 || dict_achar(top, ntop, 1237, fs, 1) != 1) return 0;
        const uint8_t *fda = c + fa[0];
        if (fa[0] <= 0 || (unsigned)fa[0] >= n || !idx_fim(fda, fim)) return 0;
        f->n_fd = (int)u16(fda) < 32 ? (int)u16(fda) : 32;
        for (int i = 0; i < f->n_fd; i++) { const uint8_t *fd; unsigned nfd; f->fd_lsub[i] = idx_obj(fda, (unsigned)i, &fd, &nfd) ? cff_subrs_privado(c, n, fd, nfd) : NULL; }
        f->fdsel = fs[0] > 0 && (unsigned)fs[0] < n ? c + fs[0] : NULL;
    } else f->lsub_idx = cff_subrs_privado(c, n, top, ntop);
    return 1;
}
static int cff_fd(const fonte_t *f, unsigned gid)
{
    const uint8_t *s = f->fdsel, *fim = f->cff + f->n_cff;
    if (!s) return 0;
    if (s[0] == 0) return s + 1 + gid < fim ? s[1 + gid] : 0;
    if (s[0] == 3) {
        unsigned nr = u16(s + 1);
        for (unsigned i = 0; i < nr && s + 3 + i * 3 + 5 <= fim; i++) {
            unsigned a = u16(s + 3 + i * 3), b = u16(s + 3 + (i + 1) * 3);
            if (gid >= a && gid < b) return s[3 + i * 3 + 2];
        }
    }
    return 0;
}

/* destino do contorno: caixa (passo 1) ou acumulador (passo 2) */
typedef struct acum acum;
typedef struct {
    int modo;                 /* 0 caixa, 1 desenha */
    float esc, ox, oy;        /* fonte -> pixels do acumulador */
    float xmin, xmax, ymin, ymax;
    float cx, cy, sx, sy; int aberto;
    acum *r;
} trilha;
static void linha(acum *r, float x0, float y0, float x1, float y1);
static void t_ponto(trilha *t, float x, float y)
{
    if (x < t->xmin) t->xmin = x;
    if (x > t->xmax) t->xmax = x;
    if (y < t->ymin) t->ymin = y;
    if (y > t->ymax) t->ymax = y;
}
static void t_lin(trilha *t, float x, float y)
{
    if (t->modo == 0) t_ponto(t, x, y);
    else linha(t->r, t->cx * t->esc - t->ox, -t->cy * t->esc - t->oy, x * t->esc - t->ox, -y * t->esc - t->oy);
    t->cx = x; t->cy = y;
}
static void t_fechar(trilha *t) { if (t->aberto && (t->cx != t->sx || t->cy != t->sy)) t_lin(t, t->sx, t->sy); t->aberto = 0; }
static void t_mover(trilha *t, float x, float y) { t_fechar(t); t->cx = t->sx = x; t->cy = t->sy = y; t->aberto = 1; if (t->modo == 0) t_ponto(t, x, y); }
static void t_cubica(trilha *t, float x1, float y1, float x2, float y2, float x3, float y3)
{
    if (t->modo == 0) { t_ponto(t, x1, y1); t_ponto(t, x2, y2); t_ponto(t, x3, y3); t->cx = x3; t->cy = y3; return; }
    float x0 = t->cx, y0 = t->cy;
    float d = (x1 - x0 < 0 ? x0 - x1 : x1 - x0) + (y1 - y0 < 0 ? y0 - y1 : y1 - y0) + (x3 - x2 < 0 ? x2 - x3 : x3 - x2) + (y3 - y2 < 0 ? y2 - y3 : y3 - y2);
    int n = 2 + (int)(d * t->esc / 3);
    if (n > 24) n = 24;
    for (int i = 1; i <= n; i++) {
        float s = (float)i / (float)n, u = 1 - s;
        float x = u * u * u * x0 + 3 * u * u * s * x1 + 3 * u * s * s * x2 + s * s * s * x3;
        float y = u * u * u * y0 + 3 * u * u * s * y1 + 3 * u * s * s * y2 + s * s * s * y3;
        t_lin(t, x, y);
    }
}

/* interpretador de charstrings Type 2 */
typedef struct { const fonte_t *f; const uint8_t *lsub; trilha *t; float pilha[48]; int np, nstem, largura_feita; float x, y; int prof; } cs_estado;
static int cs_bias(const uint8_t *idx) { unsigned c = idx ? u16(idx) : 0; return c < 1240 ? 107 : c < 33900 ? 1131 : 32768; }
static int cs_rodar(cs_estado *e, const uint8_t *p, unsigned n)
{
    if (++e->prof > 10) return 0;
    for (unsigned i = 0; i < n;) {
        uint8_t b = p[i];
        if (b >= 32 || b == 28) {
            float v;
            if (b == 28) { if (i + 3 > n) return 0; v = (float)(int16_t)(p[i + 1] << 8 | p[i + 2]); i += 3; }
            else if (b <= 246) { v = (float)(b - 139); i++; }
            else if (b <= 250) { if (i + 2 > n) return 0; v = (float)((b - 247) * 256 + p[i + 1] + 108); i += 2; }
            else if (b <= 254) { if (i + 2 > n) return 0; v = (float)(-(b - 251) * 256 - p[i + 1] - 108); i += 2; }
            else { if (i + 5 > n) return 0; v = (float)(int32_t)u32(p + i + 1) / 65536.0f; i += 5; }
            if (e->np < 48) e->pilha[e->np++] = v;
            continue;
        }
        i++;
        float *s = e->pilha; int k = e->np;
        trilha *t = e->t;
        #define LARGURA(par) do { if (!e->largura_feita) { e->largura_feita = 1; if ((k & 1) != (par)) { s++; k--; } } } while (0)
        switch (b) {
        case 1: case 3: case 18: case 23:   /* hstem vstem hstemhm vstemhm */
            LARGURA(0); e->nstem += k / 2; e->np = 0; break;
        case 19: case 20:                    /* hintmask cntrmask */
            LARGURA(0); e->nstem += k / 2; i += (unsigned)(e->nstem + 7) / 8; e->np = 0; break;
        case 21: LARGURA(0); if (k >= 2) { e->x += s[0]; e->y += s[1]; } t_mover(t, e->x, e->y); e->np = 0; break;
        case 22: LARGURA(1); if (k >= 1) e->x += s[0]; t_mover(t, e->x, e->y); e->np = 0; break;
        case 4: LARGURA(1); if (k >= 1) e->y += s[0]; t_mover(t, e->x, e->y); e->np = 0; break;
        case 5: for (int j = 0; j + 1 < k; j += 2) { e->x += s[j]; e->y += s[j + 1]; t_lin(t, e->x, e->y); } e->np = 0; break;
        case 6: case 7:
            for (int j = 0; j < k; j++) { if ((j & 1) == (b == 6 ? 0 : 1)) e->x += s[j]; else e->y += s[j]; t_lin(t, e->x, e->y); }
            e->np = 0; break;
        case 8: case 24: {   /* rrcurveto, rcurveline */
            int j = 0;
            for (; j + 5 < k && !(b == 24 && k - j < 8); j += 6) {
                float x1 = e->x + s[j], y1 = e->y + s[j + 1], x2 = x1 + s[j + 2], y2 = y1 + s[j + 3];
                e->x = x2 + s[j + 4]; e->y = y2 + s[j + 5]; t_cubica(t, x1, y1, x2, y2, e->x, e->y);
            }
            if (b == 24 && j + 1 < k) { e->x += s[j]; e->y += s[j + 1]; t_lin(t, e->x, e->y); }
            e->np = 0; break;
        }
        case 25: {   /* rlinecurve */
            int j = 0;
            for (; j + 1 < k - 6; j += 2) { e->x += s[j]; e->y += s[j + 1]; t_lin(t, e->x, e->y); }
            if (j + 5 < k) { float x1 = e->x + s[j], y1 = e->y + s[j + 1], x2 = x1 + s[j + 2], y2 = y1 + s[j + 3]; e->x = x2 + s[j + 4]; e->y = y2 + s[j + 5]; t_cubica(t, x1, y1, x2, y2, e->x, e->y); }
            e->np = 0; break;
        }
        case 26: case 27: {   /* vvcurveto, hhcurveto */
            int j = 0; float d0 = 0;
            if (k & 1) { d0 = s[0]; j = 1; }
            for (; j + 3 < k; j += 4) {
                float x1, y1;
                if (b == 26) { x1 = e->x + d0; y1 = e->y + s[j]; } else { x1 = e->x + s[j]; y1 = e->y + d0; }
                float x2 = x1 + s[j + 1], y2 = y1 + s[j + 2];
                if (b == 26) { e->x = x2; e->y = y2 + s[j + 3]; } else { e->x = x2 + s[j + 3]; e->y = y2; }
                t_cubica(t, x1, y1, x2, y2, e->x, e->y);
                d0 = 0;
            }
            e->np = 0; break;
        }
        case 30: case 31: {   /* vhcurveto, hvcurveto */
            int h = b == 31;
            for (int j = 0; j + 3 < k; j += 4) {
                float x1, y1, x2, y2, x3, y3, ult = (k - j == 5) ? s[j + 4] : 0;
                if (h) { x1 = e->x + s[j]; y1 = e->y; x2 = x1 + s[j + 1]; y2 = y1 + s[j + 2]; y3 = y2 + s[j + 3]; x3 = x2 + ult; }
                else { x1 = e->x; y1 = e->y + s[j]; x2 = x1 + s[j + 1]; y2 = y1 + s[j + 2]; x3 = x2 + s[j + 3]; y3 = y2 + ult; }
                t_cubica(t, x1, y1, x2, y2, x3, y3);
                e->x = x3; e->y = y3; h = !h;
            }
            e->np = 0; break;
        }
        case 10: case 29: {   /* callsubr, callgsubr */
            if (!k) return 0;
            const uint8_t *idx = b == 10 ? e->lsub : e->f->gsub_idx;
            int nr = (int)s[k - 1] + cs_bias(idx);
            e->np--;
            const uint8_t *d; unsigned nd;
            if (!idx_obj(idx, (unsigned)nr, &d, &nd)) return 0;
            int r = cs_rodar(e, d, nd);
            if (r != 1) { e->prof--; return r; }
            break;
        }
        case 11: e->prof--; return 1;   /* return */
        case 14: LARGURA(0); t_fechar(t); e->prof--; return 2;   /* endchar */
        case 12: {
            if (i >= n) return 0;
            uint8_t x = p[i++];
            float dx[6], dy[6];
            if (x == 35 && k >= 12) { for (int j = 0; j < 6; j++) { dx[j] = s[2 * j]; dy[j] = s[2 * j + 1]; } }
            else if (x == 34 && k >= 7) { dx[0] = s[0]; dy[0] = 0; dx[1] = s[1]; dy[1] = s[2]; dx[2] = s[3]; dy[2] = 0; dx[3] = s[4]; dy[3] = 0; dx[4] = s[5]; dy[4] = -s[2]; dx[5] = s[6]; dy[5] = 0; }
            else if (x == 36 && k >= 9) { dx[0] = s[0]; dy[0] = s[1]; dx[1] = s[2]; dy[1] = s[3]; dx[2] = s[4]; dy[2] = 0; dx[3] = s[5]; dy[3] = 0; dx[4] = s[6]; dy[4] = s[7]; dx[5] = s[8]; dy[5] = -(s[1] + s[3] + s[7]); }
            else if (x == 37 && k >= 11) {
                float sx = s[0] + s[2] + s[4] + s[6] + s[8], sy = s[1] + s[3] + s[5] + s[7] + s[9];
                for (int j = 0; j < 5; j++) { dx[j] = s[2 * j]; dy[j] = s[2 * j + 1]; }
                if ((sx < 0 ? -sx : sx) > (sy < 0 ? -sy : sy)) { dx[5] = s[10]; dy[5] = -sy; } else { dx[5] = -sx; dy[5] = s[10]; }
            } else { e->np = 0; break; }
            for (int c2 = 0; c2 < 2; c2++) {
                float x1 = e->x + dx[3 * c2], y1 = e->y + dy[3 * c2], x2 = x1 + dx[3 * c2 + 1], y2 = y1 + dy[3 * c2 + 1];
                e->x = x2 + dx[3 * c2 + 2]; e->y = y2 + dy[3 * c2 + 2];
                t_cubica(t, x1, y1, x2, y2, e->x, e->y);
            }
            e->np = 0; break;
        }
        default: e->np = 0; break;
        }
        #undef LARGURA
    }
    e->prof--;
    return 1;
}
static void cff_glifo(const fonte_t *f, unsigned gid, trilha *t)
{
    const uint8_t *d; unsigned nd;
    if (!idx_obj(f->cs_idx, gid, &d, &nd)) return;
    cs_estado e; memset(&e, 0, sizeof e);
    e.f = f; e.t = t;
    e.lsub = f->fdsel || f->n_fd ? f->fd_lsub[cff_fd(f, gid) & 31] : f->lsub_idx;
    cs_rodar(&e, d, nd);
    t_fechar(t);
}

static fonte_t *montar_fonte(fonte_t *f, tabela *t, int nt)
{
    const tabela *head = achar(t, nt, TAG('h','e','a','d')), *hhea = achar(t, nt, TAG('h','h','e','a')), *maxp = achar(t, nt, TAG('m','a','x','p'));
    const tabela *hmtx = achar(t, nt, TAG('h','m','t','x')), *cmap = achar(t, nt, TAG('c','m','a','p')), *glyf = achar(t, nt, TAG('g','l','y','f')), *loca = achar(t, nt, TAG('l','o','c','a'));
    const tabela *cff = achar(t, nt, TAG('C','F','F',' '));
    const tabela *fvar = achar(t, nt, TAG('f','v','a','r')), *avar = achar(t, nt, TAG('a','v','a','r')), *gvar = achar(t, nt, TAG('g','v','a','r'));
    if (!head || head->n < 54 || !hhea || hhea->n < 36 || !maxp || maxp->n < 6 || !hmtx || !cmap || (!glyf && !cff)) goto ruim;
    f->upem = (int)u16(head->p + 18);
    f->loca_longo = s16(head->p + 50);
    f->ngl = (int)u16(maxp->p + 4);
    f->asc = s16(hhea->p + 4); f->desc = s16(hhea->p + 6); f->gap = s16(hhea->p + 8);
    f->nhm = (int)u16(hhea->p + 34);
    if (f->upem < 16 || f->nhm < 1) goto ruim;
    if (!ler_cmap(f, cmap->p, cmap->n)) goto ruim;
    if (hmtx->transf) { f->aw = hmtx->p + 1; f->aw_passo = 2; if (hmtx->n < 1 + (unsigned)f->nhm * 2) goto ruim; }
    else { f->aw = hmtx->p; f->aw_passo = 4; if (hmtx->n < (unsigned)f->nhm * 4) goto ruim; }
    if (fvar && fvar->n >= 16) {
        const uint8_t *p = fvar->p;
        unsigned off = u16(p + 4), na = u16(p + 8), tam = u16(p + 10);
        for (unsigned i = 0; i < na && i < 16 && off + (i + 1) * tam <= fvar->n && tam >= 20; i++) {
            const uint8_t *e = p + off + i * tam;
            f->eixo_tag[i] = u32(e);
            f->eixo_min[i] = (float)(int32_t)u32(e + 4) / 65536.0f;
            f->eixo_def[i] = (float)(int32_t)u32(e + 8) / 65536.0f;
            f->eixo_max[i] = (float)(int32_t)u32(e + 12) / 65536.0f;
            f->neixos = (int)i + 1;
        }
        f->fvar = fvar->p; f->n_fvar = fvar->n;
        if (avar) { f->avar = avar->p; f->n_avar = avar->n; }
        if (gvar && glyf) { f->gvar = gvar->p; f->n_gvar = gvar->n; }
    }
    if (!glyf) { if (!cff_abrir(f, cff->p, cff->n)) goto ruim; }
    else if (glyf->transf) { if (!woff2_glyf(f, glyf->p, glyf->n)) goto ruim; }
    else {
        if (!loca) goto ruim;
        f->glyf = glyf->p; f->n_glyf = glyf->n; f->loca = loca->p; f->n_loca = loca->n;
    }
    return f;
ruim:
    fonte_fechar(f);
    return NULL;
}

fonte_t *fonte_abrir(const uint8_t *d, unsigned n)
{
    if (n < 12) return NULL;
    fonte_t *f = aloca(sizeof(fonte_t));
    if (!f) return NULL;
    memset(f, 0, sizeof *f);
    tabela t[64]; int nt = 0;
    uint32_t sig = u32(d);
    if (sig == 0x00010000 || sig == TAG('t','r','u','e') || sig == TAG('O','T','T','O')) {
        uint8_t *c = guardar(f, n);
        if (!c) { fonte_fechar(f); return NULL; }
        memcpy(c, d, n);
        unsigned k = u16(c + 4);
        for (unsigned i = 0; i < k && nt < 64 && 12 + i * 16 + 16 <= n; i++) {
            const uint8_t *e = c + 12 + i * 16;
            unsigned off = u32(e + 8), len = u32(e + 12);
            if (off > n || len > n - off) continue;
            t[nt].tag = u32(e); t[nt].p = c + off; t[nt].n = len; t[nt].transf = 0; nt++;
        }
        return montar_fonte(f, t, nt);
    }
    if (sig == TAG('w','O','F','F')) {
        if (n < 44) { fonte_fechar(f); return NULL; }
        unsigned k = u16(d + 12);
        for (unsigned i = 0; i < k && nt < 64 && 44 + i * 20 + 20 <= n; i++) {
            const uint8_t *e = d + 44 + i * 20;
            uint32_t tag = u32(e);
            unsigned off = u32(e + 4), clen = u32(e + 8), olen = u32(e + 12);
            if (off > n || clen > n - off || olen > (8u << 20)) continue;
            bool util = tag == TAG('c','m','a','p') || tag == TAG('h','e','a','d') || tag == TAG('h','h','e','a') || tag == TAG('h','m','t','x') ||
                        tag == TAG('m','a','x','p') || tag == TAG('g','l','y','f') || tag == TAG('l','o','c','a') || tag == TAG('C','F','F',' ') ||
                        tag == TAG('f','v','a','r') || tag == TAG('a','v','a','r') || tag == TAG('g','v','a','r');
            if (!util) continue;
            uint8_t *c = guardar(f, olen);
            if (!c) { fonte_fechar(f); return NULL; }
            if (clen < olen) { if (inflar_zlib(d + off, clen, c, olen) != (int)olen) { fonte_fechar(f); return NULL; } }
            else memcpy(c, d + off, olen);
            t[nt].tag = tag; t[nt].p = c; t[nt].n = olen; t[nt].transf = 0; nt++;
        }
        return montar_fonte(f, t, nt);
    }
    if (sig == TAG('w','O','F','2')) {
        if (n < 48 || u32(d + 4) == TAG('t','t','c','f')) { fonte_fechar(f); return NULL; }
        unsigned k = u16(d + 12), comp = u32(d + 20);
        const uint8_t *p = d + 48, *fim = d + n;
        unsigned soma = 0;
        struct { uint32_t tag; unsigned tam; int transf; } dir[64];
        for (unsigned i = 0; i < k && i < 64; i++) {
            if (p >= fim) { fonte_fechar(f); return NULL; }
            uint8_t fl = *p++;
            uint32_t tag;
            if ((fl & 63) == 63) { if (p + 4 > fim) { fonte_fechar(f); return NULL; } tag = u32(p); p += 4; }
            else tag = (fl & 63) < 18 ? woff2_tags[fl & 63] : TAG('?', '?', '?', (unsigned)(fl & 63));
            unsigned ver = fl >> 6, olen = base128(&p, fim), tlen = olen;
            bool gl = tag == TAG('g','l','y','f') || tag == TAG('l','o','c','a');
            int transf = gl ? ver == 0 : ver != 0;
            if (transf) tlen = base128(&p, fim);
            dir[i].tag = tag; dir[i].tam = tlen; dir[i].transf = transf;
            soma += tlen;
        }
        if (k > 64 || p + comp > fim) { fonte_fechar(f); return NULL; }
        unsigned dn = 0;
        uint8_t *dd = brotli_descomprimir(p, comp, &dn, soma + 16);
        if (!dd) { fonte_fechar(f); return NULL; }
        f->blocos[f->nblocos++] = dd;
        unsigned off = 0;
        for (unsigned i = 0; i < k; i++) {
            if (off + dir[i].tam > dn) { fonte_fechar(f); return NULL; }
            t[nt].tag = dir[i].tag; t[nt].p = dd + off; t[nt].n = dir[i].tam; t[nt].transf = dir[i].transf; nt++;
            off += dir[i].tam;
        }
        return montar_fonte(f, t, nt);
    }
    fonte_fechar(f);
    return NULL;
}

/* ------------------------------------------------------------ contornos */

#define MAXP 4096
typedef struct { fpt *p; uint16_t *ends; int np, nc; } contorno;

/* transformação afim em 1/16384 (F2Dot14) + deslocamento */
typedef struct { int a, b, c, d, dx, dy; } afim;

static void por_ponto(contorno *k, int x, int y, int on, const afim *m)
{
    if (k->np >= MAXP) return;
    fpt *q = &k->p[k->np++];
    q->x = (int16_t)(((long long)x * m->a + (long long)y * m->c) / 16384 + m->dx);
    q->y = (int16_t)(((long long)x * m->b + (long long)y * m->d) / 16384 + m->dy);
    q->on = (uint8_t)on;
}

/* ------------------------------------------------------------ variações */
static int16_t f2d14(const uint8_t *p) { return (int16_t)s16(p); }

/* pontos empacotados: devolve quantos (0 = todos) e preenche idx */
static const uint8_t *ler_pontos(const uint8_t *p, const uint8_t *fim, uint16_t *idx, int *n, int max)
{
    if (p >= fim) { *n = -1; return p; }
    int c = *p++;
    if (c & 0x80) { if (p >= fim) { *n = -1; return p; } c = (c & 0x7F) << 8 | *p++; }
    if (!c) { *n = 0; return p; }
    int k = 0, v = 0;
    while (k < c) {
        if (p >= fim) { *n = -1; return p; }
        int ctl = *p++, run = (ctl & 0x7F) + 1, words = ctl & 0x80;
        for (int i = 0; i < run && k < c; i++) {
            if (words) { if (p + 2 > fim) { *n = -1; return p; } v += (int)u16(p); p += 2; }
            else { if (p >= fim) { *n = -1; return p; } v += *p++; }
            if (k < max) idx[k] = (uint16_t)v;
            k++;
        }
    }
    *n = c > max ? max : c;
    return p;
}
static const uint8_t *ler_deltas(const uint8_t *p, const uint8_t *fim, int16_t *d, int n)
{
    int k = 0;
    while (k < n) {
        if (p >= fim) { while (k < n) d[k++] = 0; return p; }
        int ctl = *p++, run = (ctl & 0x3F) + 1;
        for (int i = 0; i < run && k < n; i++) {
            if (ctl & 0x80) d[k++] = 0;
            else if (ctl & 0x40) { if (p + 2 > fim) return p; d[k++] = (int16_t)s16(p); p += 2; }
            else { if (p >= fim) return p; d[k++] = (int8_t)*p++; }
        }
    }
    return p;
}
/* escalar da região (peak/intermediate) nas coordenadas da instância */
static float escalar(const fonte_t *f, const uint8_t *pico, const uint8_t *ini, const uint8_t *fim_)
{
    /* OpenType "Algorithm for calculating scalars": sem região intermediária,
     * ela vai de 0 ao pico */
    float s = 1;
    for (int i = 0; i < f->neixos; i++) {
        float pk = f2d14(pico + i * 2) / 16384.0f, v = f->coord[i];
        if (pk == 0) continue;
        float a = pk < 0 ? pk : 0, b = pk < 0 ? 0 : pk;
        if (ini) {
            a = f2d14(ini + i * 2) / 16384.0f; b = f2d14(fim_ + i * 2) / 16384.0f;
            if (a > pk || pk > b || (a < 0 && b > 0)) continue;   /* região inválida: o eixo não pesa */
        }
        if (v == 0) return 0;
        if (v < a || v > b) return 0;
        if (v == pk) continue;
        if (v < pk) s *= (v - a) / (pk - a); else s *= (b - v) / (b - pk);
    }
    return s;
}
/* IUP: deltas de pontos não citados, por contorno, a partir dos citados vizinhos */
static void iup(const float *orig, float *dlt, const uint8_t *tocado, int ini, int fim)
{
    int prim = -1;
    for (int i = ini; i <= fim; i++) if (tocado[i]) { prim = i; break; }
    if (prim < 0) return;
    int n = fim - ini + 1;
    int a = prim;
    do {
        int b = a;
        do { b = b == fim ? ini : b + 1; } while (!tocado[b]);
        /* pontos entre a e b (cíclico) */
        for (int i = a == fim ? ini : a + 1; i != b; i = i == fim ? ini : i + 1) {
            float c1 = orig[a], c2 = orig[b], d1 = dlt[a], d2 = dlt[b], c = orig[i];
            if (c1 == c2) dlt[i] = d1 == d2 ? d1 : 0;
            else {
                float lo = c1 < c2 ? c1 : c2, hi = c1 < c2 ? c2 : c1, dlo = c1 < c2 ? d1 : d2, dhi = c1 < c2 ? d2 : d1;
                if (c <= lo) dlt[i] = dlo;
                else if (c >= hi) dlt[i] = dhi;
                else dlt[i] = d1 + (c - c1) * (d2 - d1) / (c2 - c1);
            }
        }
        a = b;
    } while (a != prim);
    (void)n;
}
/* soma os deltas de todas as tuplas do glifo em dx/dy (n pontos, já com os 4 fantasmas) */
static void gvar_deltas(const fonte_t *f, unsigned gid, int n, const float *ox, const float *oy, const uint16_t *ends, int nc, float *dx, float *dy)
{
    for (int i = 0; i < n; i++) dx[i] = dy[i] = 0;
    if (!f->gvar || !f->variada || f->n_gvar < 20) return;
    const uint8_t *g = f->gvar;
    unsigned neixos = u16(g + 4), nshared = u16(g + 6), off_sh = u32(g + 8), ngl = u16(g + 12), fl = u16(g + 14), off_dados = u32(g + 16);
    if ((int)neixos != f->neixos || gid >= ngl) return;
    unsigned o0, o1;
    if (fl & 1) { if (20 + (gid + 2) * 4 > f->n_gvar) return; o0 = u32(g + 20 + gid * 4); o1 = u32(g + 24 + gid * 4); }
    else { if (20 + (gid + 2) * 2 > f->n_gvar) return; o0 = u16(g + 20 + gid * 2) * 2u; o1 = u16(g + 22 + gid * 2) * 2u; }
    if (o1 <= o0 || off_dados + o1 > f->n_gvar) return;
    const uint8_t *d = g + off_dados + o0, *dfim = g + off_dados + o1;
    if (d + 4 > dfim) return;
    unsigned cnt = u16(d), doff = u16(d + 2);
    const uint8_t *h = d + 4, *ser = d + doff;
    int ntup = (int)(cnt & 0x0FFF);
    static uint16_t comp_idx[MAXP + 8], priv_idx[MAXP + 8];
    static int16_t ddx[MAXP + 8], ddy[MAXP + 8];
    static float tx[MAXP + 8], ty[MAXP + 8];
    static uint8_t toc[MAXP + 8];
    int ncomp = -1;
    if (cnt & 0x8000) ser = ler_pontos(ser, dfim, comp_idx, &ncomp, MAXP + 8);
    if (n > MAXP + 8) return;
    for (int t = 0; t < ntup; t++) {
        if (h + 4 > dfim) return;
        unsigned tam = u16(h), ti = u16(h + 2);
        h += 4;
        const uint8_t *pico, *ini = NULL, *fimr = NULL;
        if (ti & 0x8000) { pico = h; h += neixos * 2; }
        else { if ((ti & 0x0FFF) >= nshared || off_sh + ((ti & 0x0FFF) + 1) * neixos * 2 > f->n_gvar) return; pico = g + off_sh + (ti & 0x0FFF) * neixos * 2; }
        if (ti & 0x4000) { ini = h; fimr = h + neixos * 2; h += neixos * 4; }
        if (h > dfim) return;
        const uint8_t *dados = ser, *dfim_t = ser + tam;
        ser += tam;
        if (dfim_t > dfim) return;
        float esc = escalar(f, pico, ini, fimr);
        if (esc == 0) continue;
        uint16_t *idx = comp_idx; int np = ncomp;
        if (ti & 0x2000) { dados = ler_pontos(dados, dfim_t, priv_idx, &np, MAXP + 8); idx = priv_idx; }
        if (np < 0) continue;
        int todos = np == 0, m = todos ? n : np;
        dados = ler_deltas(dados, dfim_t, ddx, m);
        ler_deltas(dados, dfim_t, ddy, m);
        if (todos) { for (int i = 0; i < n; i++) { dx[i] += esc * ddx[i]; dy[i] += esc * ddy[i]; } continue; }
        for (int i = 0; i < n; i++) { tx[i] = ty[i] = 0; toc[i] = 0; }
        for (int i = 0; i < m; i++) if (idx[i] < n) { tx[idx[i]] = ddx[i]; ty[idx[i]] = ddy[i]; toc[idx[i]] = 1; }
        if (ends) {   /* glifo simples: infere os não citados dentro de cada contorno */
            int c0 = 0;
            for (int c = 0; c < nc; c++) {
                int c1 = ends[c];
                if (c1 >= n - 4) break;
                iup(ox, tx, toc, c0, c1); iup(oy, ty, toc, c0, c1);
                c0 = c1 + 1;
            }
        }
        for (int i = 0; i < n; i++) { dx[i] += esc * tx[i]; dy[i] += esc * ty[i]; }
    }
}

/* Instância de uma fonte variável: eixos pelo tag (wght, wdth, slnt, ital, opsz...). */
fonte_t *fonte_instancia(fonte_t *b, const uint32_t *tags, const int *valores100, int n)
{
    if (!b) return NULL;
    if (b->base) b = b->base;
    fonte_t *f = aloca(sizeof(fonte_t));
    if (!f) return NULL;
    memcpy(f, b, sizeof *f);
    memset(f->cache, 0, sizeof f->cache);
    f->cache_bytes = 0; f->nblocos = 0; f->base = b; f->aw_var = NULL; f->variada = 0;
    for (int i = 0; i < f->neixos; i++) {
        float v = f->eixo_def[i];
        for (int k = 0; k < n; k++) if (tags[k] == f->eixo_tag[i]) v = (float)valores100[k] / 100.0f;
        if (v < f->eixo_min[i]) v = f->eixo_min[i];
        if (v > f->eixo_max[i]) v = f->eixo_max[i];
        float c = 0;
        if (v < f->eixo_def[i] && f->eixo_def[i] > f->eixo_min[i]) c = -(f->eixo_def[i] - v) / (f->eixo_def[i] - f->eixo_min[i]);
        else if (v > f->eixo_def[i] && f->eixo_max[i] > f->eixo_def[i]) c = (v - f->eixo_def[i]) / (f->eixo_max[i] - f->eixo_def[i]);
        /* avar: mapa por trechos lineares */
        if (f->avar && f->n_avar >= 8) {
            const uint8_t *p = f->avar + 8, *fim = f->avar + f->n_avar;
            for (int e = 0; e < i && p + 2 <= fim; e++) p += 2 + u16(p) * 4;
            if (p + 2 <= fim) {
                int nm = (int)u16(p); p += 2;
                for (int j = 1; j < nm && p + j * 4 + 4 <= fim; j++) {
                    float a0 = f2d14(p + (j - 1) * 4) / 16384.0f, b0 = f2d14(p + (j - 1) * 4 + 2) / 16384.0f;
                    float a1 = f2d14(p + j * 4) / 16384.0f, b1 = f2d14(p + j * 4 + 2) / 16384.0f;
                    if (c >= a0 && c <= a1) { c = a1 == a0 ? b0 : b0 + (c - a0) * (b1 - b0) / (a1 - a0); break; }
                }
            }
        }
        f->coord[i] = c;
        if (c != 0) f->variada = 1;
    }
    if (f->variada && f->ngl > 0) {
        f->aw_var = aloca((unsigned)f->ngl * sizeof(int16_t));
        if (f->aw_var) for (int i = 0; i < f->ngl; i++) f->aw_var[i] = -32768;
    }
    return f;
}
bool fonte_variavel(fonte_t *f) { return f && f->neixos > 0; }

/* Itálico sintético (o navegador faz o mesmo quando a família não tem a face):
 * a mesma fonte, contornos inclinados 12 graus. Compartilha as tabelas. */
fonte_t *fonte_obliqua(fonte_t *b)
{
    if (!b) return NULL;
    fonte_t *f = aloca(sizeof(fonte_t));
    if (!f) return NULL;
    memcpy(f, b, sizeof *f);
    memset(f->cache, 0, sizeof f->cache);
    f->cache_bytes = 0; f->nblocos = 0; f->aw_var = NULL;
    f->base = b->base ? b->base : b;
    f->obliquo = 1;
    return f;
}

static void glifo_contorno(const fonte_t *f, unsigned gid, contorno *k, const afim *m, int prof)
{
    if (gid >= (unsigned)f->ngl || prof > 6) return;
    const uint8_t *comp = NULL, *cfim = NULL;
    if (f->gd) {
        const gdec *G = &f->gd[gid];
        if (G->tipo == 1) {
            int base = k->np;
            if (f->variada && G->npts + 4 <= MAXP) {
                static float ox[MAXP + 8], oy[MAXP + 8], dx[MAXP + 8], dy[MAXP + 8];
                static uint16_t en[MAXP];
                int np = (int)G->npts;
                for (int i = 0; i < np; i++) { ox[i] = f->gpts[G->pts + i].x; oy[i] = f->gpts[G->pts + i].y; }
                for (int i = np; i < np + 4; i++) ox[i] = oy[i] = 0;
                for (unsigned c = 0; c < G->ncont && c < MAXP; c++) en[c] = f->gends[G->ends + c];
                gvar_deltas(f, gid, np + 4, ox, oy, en, (int)G->ncont, dx, dy);
                for (int i = 0; i < np; i++) por_ponto(k, (int)(ox[i] + dx[i] + (dx[i] < 0 ? -0.5f : 0.5f)), (int)(oy[i] + dy[i] + (dy[i] < 0 ? -0.5f : 0.5f)), f->gpts[G->pts + i].on, m);
            } else
                for (unsigned i = 0; i < G->npts; i++) { const fpt *p = &f->gpts[G->pts + i]; por_ponto(k, p->x, p->y, p->on, m); }
            for (unsigned c = 0; c < G->ncont && k->nc < MAXP; c++) k->ends[k->nc++] = (uint16_t)(base + f->gends[G->ends + c]);
            return;
        }
        if (G->tipo != 2) return;
        comp = G->comp; cfim = comp + 65536;
    } else {
        unsigned o0, o1;
        if (f->loca_longo) { if ((gid + 2) * 4 > f->n_loca) return; o0 = u32(f->loca + gid * 4); o1 = u32(f->loca + gid * 4 + 4); }
        else { if ((gid + 2) * 2 > f->n_loca) return; o0 = u16(f->loca + gid * 2) * 2; o1 = u16(f->loca + gid * 2 + 2) * 2; }
        if (o1 <= o0 || o1 > f->n_glyf || o1 - o0 < 10) return;
        const uint8_t *g = f->glyf + o0, *gfim = f->glyf + o1;
        int nc = s16(g);
        if (nc < 0) { comp = g + 10; cfim = gfim; }
        else {
            const uint8_t *e = g + 10;
            if (e + nc * 2 + 2 > gfim) return;
            int np = (int)u16(e + (nc - 1) * 2) + 1;
            int base = k->np;
            for (int c = 0; c < nc && k->nc < MAXP; c++) k->ends[k->nc++] = (uint16_t)(base + (int)u16(e + c * 2));
            const uint8_t *q = e + nc * 2;
            q += 2 + u16(q);   /* instruções */
            /* flags */
            static uint8_t fl[MAXP];
            if (np > MAXP || k->np + np > MAXP) return;
            for (int i = 0; i < np;) {
                if (q >= gfim) return;
                uint8_t v = *q++;
                fl[i++] = v;
                if (v & 8) { if (q >= gfim) return; int r = *q++; while (r-- && i < np) fl[i++] = v; }
            }
            int x = 0;
            const uint8_t *qx = q;
            /* x */
            static int16_t vx[MAXP];
            for (int i = 0; i < np; i++) {
                if (fl[i] & 2) { if (qx >= gfim) return; int v = *qx++; x += (fl[i] & 16) ? v : -v; }
                else if (!(fl[i] & 16)) { if (qx + 2 > gfim) return; x += s16(qx); qx += 2; }
                vx[i] = (int16_t)x;
            }
            int y = 0;
            static int16_t vy[MAXP];
            for (int i = 0; i < np; i++) {
                if (fl[i] & 4) { if (qx >= gfim) return; int v = *qx++; y += (fl[i] & 32) ? v : -v; }
                else if (!(fl[i] & 32)) { if (qx + 2 > gfim) return; y += s16(qx); qx += 2; }
                vy[i] = (int16_t)y;
            }
            if (f->variada && np + 4 <= MAXP) {
                static float ox[MAXP + 8], oy[MAXP + 8], dx[MAXP + 8], dy[MAXP + 8];
                static uint16_t en[MAXP];
                for (int i = 0; i < np; i++) { ox[i] = vx[i]; oy[i] = vy[i]; }
                for (int i = np; i < np + 4; i++) ox[i] = oy[i] = 0;
                for (int c = 0; c < nc; c++) en[c] = (uint16_t)u16(e + c * 2);
                gvar_deltas(f, gid, np + 4, ox, oy, en, nc, dx, dy);
                for (int i = 0; i < np; i++) por_ponto(k, (int)(ox[i] + dx[i] + (dx[i] < 0 ? -0.5f : 0.5f)), (int)(oy[i] + dy[i] + (dy[i] < 0 ? -0.5f : 0.5f)), fl[i] & 1, m);
            } else
                for (int i = 0; i < np; i++) por_ponto(k, vx[i], vy[i], fl[i] & 1, m);
            return;
        }
    }
    /* composição; numa instância variável, os deltas deslocam cada componente */
    float cdx[64], cdy[64];
    int ncomp = 0;
    for (int i = 0; i < 64; i++) cdx[i] = cdy[i] = 0;
    if (f->variada) {
        const uint8_t *q = comp;
        for (;;) {
            if (q + 4 > cfim || ncomp >= 60) break;
            unsigned fl = u16(q);
            q += 4 + ((fl & 1) ? 4 : 2) + ((fl & 8) ? 2 : (fl & 0x40) ? 4 : (fl & 0x80) ? 8 : 0);
            ncomp++;
            if (!(fl & 0x20)) break;
        }
        static float zx[72], zy[72], ddx[72], ddy[72];
        for (int i = 0; i < ncomp + 4; i++) zx[i] = zy[i] = 0;
        gvar_deltas(f, gid, ncomp + 4, zx, zy, NULL, 0, ddx, ddy);
        for (int i = 0; i < ncomp; i++) { cdx[i] = ddx[i]; cdy[i] = ddy[i]; }
    }
    int ci = 0;
    const uint8_t *q = comp;
    for (;;) {
        if (q + 4 > cfim) return;
        unsigned fl = u16(q), sub = u16(q + 2);
        q += 4;
        int a1, a2;
        if (fl & 1) { a1 = s16(q); a2 = s16(q + 2); q += 4; } else { a1 = (int8_t)q[0]; a2 = (int8_t)q[1]; q += 2; }
        afim t = { 16384, 0, 0, 16384, 0, 0 };
        if (fl & 8) { t.a = t.d = s16(q); q += 2; }
        else if (fl & 0x40) { t.a = s16(q); t.d = s16(q + 2); q += 4; }
        else if (fl & 0x80) { t.a = s16(q); t.b = s16(q + 2); t.c = s16(q + 4); t.d = s16(q + 6); q += 8; }
        if (fl & 2) {
            t.dx = a1; t.dy = a2;
            if (ci < 64) { t.dx += (int)(cdx[ci] + (cdx[ci] < 0 ? -0.5f : 0.5f)); t.dy += (int)(cdy[ci] + (cdy[ci] < 0 ? -0.5f : 0.5f)); }
        }
        ci++;
        /* compõe com a transformação de fora */
        afim r;
        r.a = (int)(((long long)t.a * m->a + (long long)t.b * m->c) / 16384);
        r.b = (int)(((long long)t.a * m->b + (long long)t.b * m->d) / 16384);
        r.c = (int)(((long long)t.c * m->a + (long long)t.d * m->c) / 16384);
        r.d = (int)(((long long)t.c * m->b + (long long)t.d * m->d) / 16384);
        r.dx = (int)(((long long)t.dx * m->a + (long long)t.dy * m->c) / 16384) + m->dx;
        r.dy = (int)(((long long)t.dx * m->b + (long long)t.dy * m->d) / 16384) + m->dy;
        glifo_contorno(f, sub, k, &r, prof + 1);
        if (!(fl & 0x20)) break;
    }
}

/* ------------------------------------------------------------ rasterizador */

static inline float fpiso(float v) { int i = (int)v; return (float)(v < (float)i ? i - 1 : i); }
static inline float fteto(float v) { int i = (int)v; return (float)(v > (float)i ? i + 1 : i); }
static inline float fabs_f(float v) { return v < 0 ? -v : v; }

struct acum { float *a; int w, h; };

/* A linha que acumula a área coberta em cada pixel vem do font-rs (Raph
 * Levien/Google, Apache 2.0): terceiros/font-rs/acumulador.c. */
static void linha(acum *r, float x0, float y0, float x1, float y1) { fontrs_linha(r->a, r->w, r->h, x0, y0, x1, y1); }

static void quad(acum *r, float x0, float y0, float x1, float y1, float x2, float y2)
{
    float ddx = x0 - 2 * x1 + x2, ddy = y0 - 2 * y1 + y2;
    float dd = fabs_f(ddx) + fabs_f(ddy);
    int n = 1 + (int)(dd * 0.7f);
    if (n > 16) n = 16;
    float px = x0, py = y0;
    for (int i = 1; i <= n; i++) {
        float t = (float)i / (float)n, u = 1 - t;
        float qx = u * u * x0 + 2 * u * t * x1 + t * t * x2, qy = u * u * y0 + 2 * u * t * y1 + t * t * y2;
        linha(r, px, py, qx, qy);
        px = qx; py = qy;
    }
}

static int px_cache(int px) { return px > 200 ? 200 : px; }

static const uint8_t *guardar_cache(fonte_t *f, gcache *c, unsigned gid, int px, acum *r, int w, int hh, int bx0, int by0, int *l, int *a, int *ox, int *oy);
static const uint8_t *cff_rasterizar(fonte_t *f, unsigned gid, int px, gcache *c, int *l, int *a, int *ox, int *oy)
{
    trilha t; memset(&t, 0, sizeof t);
    t.xmin = t.ymin = 1e9f; t.xmax = t.ymax = -1e9f;
    cff_glifo(f, gid, &t);
    if (t.xmin > t.xmax) return NULL;
    float esc = (float)px / (float)f->upem;
    int bx0 = (int)fpiso(t.xmin * esc) - 1, bx1 = (int)fteto(t.xmax * esc) + 1;
    int by0 = (int)fpiso(-t.ymax * esc) - 1, by1 = (int)fteto(-t.ymin * esc) + 1;
    int w = bx1 - bx0 + 2, hh = by1 - by0;
    if (w <= 0 || hh <= 0 || w > 600 || hh > 600) return NULL;
    acum r = { aloca((unsigned)(w * hh + 4) * sizeof(float)), w, hh };
    if (!r.a) return NULL;
    memset(r.a, 0, (unsigned)(w * hh + 4) * sizeof(float));
    trilha d; memset(&d, 0, sizeof d);
    d.modo = 1; d.esc = esc; d.ox = (float)bx0; d.oy = (float)by0; d.r = &r;
    cff_glifo(f, gid, &d);
    return guardar_cache(f, c, gid, px, &r, w, hh, bx0, by0, l, a, ox, oy);
}

const uint8_t *fonte_glifo(fonte_t *f, unsigned cp, int px, int *l, int *a, int *ox, int *oy)
{
    px = px_cache(px);
    unsigned gid = gid_de(f, cp);
    unsigned h = (gid * 31u + (unsigned)px * 7u) % CACHE_G;
    gcache *c = &f->cache[h];
    if (c->cob && c->gid == gid && c->px == px) { *l = c->l; *a = c->a; *ox = c->ox; *oy = c->oy; return c->cob; }
    if (f->cff) return cff_rasterizar(f, gid, px, c, l, a, ox, oy);
    static fpt pts[MAXP]; static uint16_t ends[MAXP];
    contorno k = { pts, ends, 0, 0 };
    afim id = { 16384, 0, f->obliquo ? 3483 : 0, 16384, 0, 0 };   /* tan(12 graus) x 16384 */
    glifo_contorno(f, gid, &k, &id, 0);
    if (!k.np) return NULL;
    float esc = (float)px / (float)f->upem;
    int xmin = 32767, xmax = -32768, ymin = 32767, ymax = -32768;
    for (int i = 0; i < k.np; i++) {
        if (pts[i].x < xmin) xmin = pts[i].x;
        if (pts[i].x > xmax) xmax = pts[i].x;
        if (pts[i].y < ymin) ymin = pts[i].y;
        if (pts[i].y > ymax) ymax = pts[i].y;
    }
    int bx0 = (int)fpiso((float)xmin * esc) - 1, bx1 = (int)fteto((float)xmax * esc) + 1;
    int by0 = (int)fpiso((float)-ymax * esc) - 1, by1 = (int)fteto((float)-ymin * esc) + 1;
    int w = bx1 - bx0 + 2, hh = by1 - by0;
    if (w <= 0 || hh <= 0 || w > 600 || hh > 600) return NULL;
    acum r = { aloca((unsigned)(w * hh + 4) * sizeof(float)), w, hh };
    if (!r.a) return NULL;
    memset(r.a, 0, (unsigned)(w * hh + 4) * sizeof(float));
    int ini = 0;
    for (int c2 = 0; c2 < k.nc; c2++) {
        int fim = ends[c2];
        if (fim < ini || fim >= k.np) break;
        int n = fim - ini + 1;
        /* ponto de partida: um ponto sobre a curva (ou o meio de dois de controle) */
        float sx, sy; int i0 = -1;
        for (int i = 0; i < n; i++) if (pts[ini + i].on) { i0 = i; break; }
        #define PX(p) ((float)(p).x * esc - (float)bx0)
        #define PY(p) ((float)-(p).y * esc - (float)by0)
        if (i0 < 0) { sx = (PX(pts[ini]) + PX(pts[ini + (1 % n)])) / 2; sy = (PY(pts[ini]) + PY(pts[ini + (1 % n)])) / 2; i0 = 0; }
        else { sx = PX(pts[ini + i0]); sy = PY(pts[ini + i0]); }
        float cx = sx, cy = sy, qx = 0, qy = 0; int tem_q = 0;
        for (int j = 1; j <= n; j++) {
            const fpt *p = &pts[ini + (i0 + j) % n];
            float x = PX(*p), y = PY(*p);
            if (p->on) {
                if (tem_q) quad(&r, cx, cy, qx, qy, x, y); else linha(&r, cx, cy, x, y);
                cx = x; cy = y; tem_q = 0;
            } else {
                if (tem_q) { float mx = (qx + x) / 2, my = (qy + y) / 2; quad(&r, cx, cy, qx, qy, mx, my); cx = mx; cy = my; }
                qx = x; qy = y; tem_q = 1;
            }
        }
        if (tem_q) quad(&r, cx, cy, qx, qy, sx, sy); else linha(&r, cx, cy, sx, sy);
        #undef PX
        #undef PY
        ini = fim + 1;
    }
    return guardar_cache(f, c, gid, px, &r, w, hh, bx0, by0, l, a, ox, oy);
}

static const uint8_t *guardar_cache(fonte_t *f, gcache *c, unsigned gid, int px, acum *rp, int w, int hh, int bx0, int by0, int *l, int *a, int *ox, int *oy)
{
    acum r = *rp;
    uint8_t *cob = aloca((unsigned)(w * hh));
    if (!cob) { libera(r.a); return NULL; }
    fontrs_acumular(r.a, w, hh, cob);   /* soma de cada linha: terceiros/font-rs */
    libera(r.a);
    if (c->cob) { f->cache_bytes -= (unsigned)(c->l * c->a); libera(c->cob); }
    c->cob = cob; c->gid = (uint16_t)gid; c->px = (uint16_t)px; c->l = (int16_t)w; c->a = (int16_t)hh; c->ox = (int16_t)bx0; c->oy = (int16_t)by0;
    f->cache_bytes += (unsigned)(w * hh);
    *l = w; *a = hh; *ox = bx0; *oy = by0;
    return cob;
}

/* avanço em 1/64 de pixel */
/* número de pontos do glifo (simples) ou de componentes (composto), para os fantasmas do gvar */
static int pontos_glifo(const fonte_t *f, unsigned gid)
{
    const uint8_t *comp = NULL, *cfim = NULL;
    if (f->gd) {
        const gdec *G = &f->gd[gid];
        if (G->tipo == 1) return (int)G->npts;
        if (G->tipo != 2) return 0;
        comp = G->comp; cfim = comp + 65536;
    } else {
        unsigned o0, o1;
        if (f->loca_longo) { if ((gid + 2) * 4 > f->n_loca) return 0; o0 = u32(f->loca + gid * 4); o1 = u32(f->loca + gid * 4 + 4); }
        else { if ((gid + 2) * 2 > f->n_loca) return 0; o0 = u16(f->loca + gid * 2) * 2; o1 = u16(f->loca + gid * 2 + 2) * 2; }
        if (o1 <= o0 || o1 > f->n_glyf || o1 - o0 < 10) return 0;
        const uint8_t *g = f->glyf + o0;
        int nc = s16(g);
        if (nc >= 0) return nc ? (int)u16(g + 10 + (nc - 1) * 2) + 1 : 0;
        comp = g + 10; cfim = f->glyf + o1;
    }
    int n = 0;
    for (const uint8_t *q = comp;;) {
        if (q + 4 > cfim || n > 60) break;
        unsigned fl = u16(q);
        q += 4 + ((fl & 1) ? 4 : 2) + ((fl & 8) ? 2 : (fl & 0x40) ? 4 : (fl & 0x80) ? 8 : 0);
        n++;
        if (!(fl & 0x20)) break;
    }
    return n;
}

int fonte_avanco(fonte_t *f, unsigned cp, int px)
{
    unsigned gid = gid_de(f, cp);
    unsigned i = gid < (unsigned)f->nhm ? gid : (unsigned)f->nhm - 1;
    unsigned aw = u16(f->aw + i * (unsigned)f->aw_passo);
    if (f->aw_var && gid < (unsigned)f->ngl) {   /* instância: o fantasma 2 menos o 1 dá o avanço */
        if (f->aw_var[gid] == -32768) {
            int np = pontos_glifo(f, gid);
            static float zx[MAXP + 8], zy[MAXP + 8], dx[MAXP + 8], dy[MAXP + 8];
            int v = (int)aw;
            if (np + 4 <= MAXP) {
                for (int j = 0; j < np + 4; j++) zx[j] = zy[j] = 0;
                gvar_deltas(f, gid, np + 4, zx, zy, NULL, 0, dx, dy);
                float d = dx[np + 1] - dx[np];
                v += (int)(d + (d < 0 ? -0.5f : 0.5f));
            }
            f->aw_var[gid] = (int16_t)(v < 0 ? 0 : v > 32767 ? 32767 : v);
        }
        aw = (unsigned)f->aw_var[gid];
    }
    if (px > 500) px = 500;
    return (int)(aw * (unsigned)px * 64u / (unsigned)f->upem);   /* sem divisão de 64 bits (não há libgcc) */
}
bool fonte_tem(fonte_t *f, unsigned cp) { return gid_de(f, cp) != 0; }
int fonte_ascendente(fonte_t *f, int px) { return f->asc * px / f->upem; }
int fonte_descendente(fonte_t *f, int px) { return -f->desc * px / f->upem; }
/* line-height: normal = ascendente + descendente + entrelinha (hhea), cada um arredondado como no Chrome */
int fonte_altura_normal(fonte_t *f, int px)
{
    int u = f->upem;
    return (f->asc * px + u / 2) / u + (-f->desc * px + u / 2) / u + (f->gap > 0 ? (f->gap * px + u / 2) / u : 0);
}

/* Fontes do sistema (DejaVu em /fontes): família genérica x negrito x itálico.
 * Carregadas uma vez por processo e nunca liberadas (servem a todas as páginas). */
static fonte_t *sist[3][2][2]; static uint8_t sist_tentou[3][2][2];
fonte_t *fonte_sistema(int g, int negrito, int italico)
{
    /* NULL no itálico: inclinado na hora a partir da regular/negrito (fonte_obliqua) */
    static const char *const nomes[3][2][2] = {
        { { "/fontes/DejaVuSans.ttf", NULL }, { "/fontes/DejaVuSans-Bold.ttf", NULL } },
        { { "/fontes/DejaVuSerif.ttf", "/fontes/DejaVuSerif-Italic.ttf" }, { "/fontes/DejaVuSerif-Bold.ttf", NULL } },
        { { "/fontes/DejaVuSansMono.ttf", NULL }, { "/fontes/DejaVuSansMono-Bold.ttf", NULL } } };
    if (g < 0 || g > 2) g = 0;
    negrito = !!negrito; italico = !!italico;
    if (sist[g][negrito][italico] || sist_tentou[g][negrito][italico]) return sist[g][negrito][italico];
    sist_tentou[g][negrito][italico] = 1;
    const char *c = nomes[g][negrito][italico];
    if (!c) {
        fonte_t *reta = fonte_sistema(g, negrito, 0);
        sist[g][negrito][italico] = reta ? fonte_obliqua(reta) : NULL;
        return sist[g][negrito][italico];
    }
#ifdef MOTOR_HOST
    /* no computador (testes): a pasta rootfs do repositório */
#ifndef PASTA_FONTES_HOST
#define PASTA_FONTES_HOST "rootfs"
#endif
    char cam[600]; formatar_texto(cam, sizeof cam, "%s%s", PASTA_FONTES_HOST, c);
    FILE *fp = fopen(cam, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    if (fread(d, 1, (size_t)n, fp) != (size_t)n) { fclose(fp); free(d); return NULL; }
    fclose(fp);
#else
    int fd = abrir(c, 0);
    if (fd < 0) return NULL;
    unsigned cap = 1u << 20, n = 0;
    uint8_t *d = aloca(cap);
    int r;
    while (d && (r = ler(fd, d + n, cap - n)) > 0) {
        n += (unsigned)r;
        if (n == cap) { uint8_t *q = realoca(d, cap * 2); if (!q) break; d = q; cap *= 2; }
    }
    fechar(fd);
    if (!d) return NULL;
#endif
    sist[g][negrito][italico] = fonte_abrir(d, (unsigned)n);
    libera(d);
    return sist[g][negrito][italico];
}

