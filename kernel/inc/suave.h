/* suave.h - formas com a borda suavizada (H88), usadas pelo kernel
 * (desenho.c) e pelos programas (libsagui, gui.c).
 *
 * Cada pixel da borda é dividido em 4 x 4 amostras; a cor entra na
 * proporção das amostras que caem dentro da forma. As contas são em
 * oitavos de pixel e só com inteiros (o kernel não usa ponto flutuante):
 * a amostra k (0 a 3) do pixel p fica em p*8 + 2k + 1.
 *
 * As funções recebem uma área de pixels crua (px, largura da linha) e o
 * retângulo de recorte [x0, x1) x [y0, y1), para servir às duas pontas. */
#ifndef SAGUI_SUAVE_H
#define SAGUI_SUAVE_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t *px;
    int larg;                 /* pixels por linha */
    int x0, y0, x1, y1;       /* recorte */
} suave_t;

/* a + (b - a) * k / 256, canal a canal. */
static inline uint32_t suave_mistura(uint32_t a, uint32_t b, int k)
{
    uint32_t rb = ((a & 0xFF00FF) * (uint32_t)(256 - k) + (b & 0xFF00FF) * (uint32_t)k) >> 8;
    uint32_t g = ((a & 0x00FF00) * (uint32_t)(256 - k) + (b & 0x00FF00) * (uint32_t)k) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

static inline void suave_ponto(const suave_t *s, int x, int y, uint32_t cor, int k)
{
    if (k <= 0 || x < s->x0 || x >= s->x1 || y < s->y0 || y >= s->y1) return;
    uint32_t *p = &s->px[y * s->larg + x];
    *p = k >= 256 ? cor : suave_mistura(*p, cor, k);
}

/* Amostras (de 16) do pixel (i, j), relativo ao canto do retângulo l x a com
 * raio r, que caem dentro dele. */
static inline int suave_amostras_ret(int i, int j, int l, int a, int r)
{
    if (r <= 0) return 16;
    /* longe dos cantos: tudo dentro */
    if ((i >= r && i < l - r) || (j >= r && j < a - r)) return 16;
    int n = 0, R = r * 8, cx = i < r ? R : (l - r) * 8, cy = j < r ? R : (a - r) * 8;
    for (int u = 0; u < 4; u++)
        for (int v = 0; v < 4; v++) {
            int sx = i * 8 + 2 * u + 1, sy = j * 8 + 2 * v + 1;
            int dx = (i < r ? (sx < cx ? cx - sx : 0) : (sx > cx ? sx - cx : 0));
            int dy = (j < r ? (sy < cy ? cy - sy : 0) : (sy > cy ? sy - cy : 0));
            if (dx * dx + dy * dy <= R * R) n++;
        }
    return n;
}

/* Retângulo com cantos arredondados, com degradê vertical de topo a base e
 * opacidade alfa (0 a 256). */
static inline void suave_ret(const suave_t *s, int x, int y, int l, int a, int r,
                             uint32_t topo, uint32_t base, int alfa)
{
    if (l <= 0 || a <= 0) return;
    if (2 * r > l) r = l / 2;
    if (2 * r > a) r = a / 2;
    for (int j = 0; j < a; j++) {
        if (y + j < s->y0 || y + j >= s->y1) continue;
        uint32_t cor = topo == base || a < 2 ? topo : suave_mistura(topo, base, j * 256 / (a - 1));
        for (int i = 0; i < l; i++) {
            if (x + i < s->x0 || x + i >= s->x1) continue;
            int n = suave_amostras_ret(i, j, l, a, r);
            if (n) suave_ponto(s, x + i, y + j, cor, n * alfa / 16);
        }
    }
}

/* Contorno de 1 pixel de um retângulo de cantos arredondados. */
static inline void suave_contorno(const suave_t *s, int x, int y, int l, int a, int r, uint32_t cor, int alfa)
{
    if (l <= 2 || a <= 2) return;
    if (2 * r > l) r = l / 2;
    if (2 * r > a) r = a / 2;
    int ri = r > 0 ? r - 1 : 0;
    for (int j = 0; j < a; j++) {
        if (y + j < s->y0 || y + j >= s->y1) continue;
        for (int i = 0; i < l; i++) {
            bool perto = i < r + 1 || i >= l - r - 1 || j < r + 1 || j >= a - r - 1;
            if (!perto) { i = l - r - 2; continue; }
            int fora = suave_amostras_ret(i, j, l, a, r);
            int dentro = (i == 0 || j == 0 || i == l - 1 || j == a - 1) ? 0
                       : suave_amostras_ret(i - 1, j - 1, l - 2, a - 2, ri);
            int n = fora - dentro;
            if (n > 0) suave_ponto(s, x + i, y + j, cor, n * alfa / 16);
        }
    }
}

/* Traço reto de (x0, y0) a (x1, y1), em oitavos de pixel, com a largura w
 * (também em oitavos) e pontas redondas. Só para traços curtos (até uns
 * 30 pixels): as contas cabem em 32 bits sem divisão de 64 bits. */
static inline void suave_traco(const suave_t *s, int x0, int y0, int x1, int y1, int w, uint32_t cor)
{
    int h = w / 2;
    int bx0 = ((x0 < x1 ? x0 : x1) - h) / 8 - 1, bx1 = ((x0 > x1 ? x0 : x1) + h) / 8 + 1;
    int by0 = ((y0 < y1 ? y0 : y1) - h) / 8 - 1, by1 = ((y0 > y1 ? y0 : y1) + h) / 8 + 1;
    int dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int py = by0; py <= by1; py++)
        for (int px = bx0; px <= bx1; px++) {
            int n = 0;
            for (int u = 0; u < 4; u++)
                for (int v = 0; v < 4; v++) {
                    int sx = px * 8 + 2 * u + 1 - x0, sy = py * 8 + 2 * v + 1 - y0;
                    int t = len2 ? sx * dx + sy * dy : 0;          /* projeção * len2 */
                    int ex, ey;
                    if (t <= 0) { ex = sx; ey = sy; }
                    else if (t >= len2) { ex = sx - dx; ey = sy - dy; }
                    else { ex = sx - dx * t / len2; ey = sy - dy * t / len2; }
                    if (ex * ex + ey * ey <= h * h) n++;
                }
            if (n) suave_ponto(s, px, py, cor, n * 16);
        }
}

#endif
