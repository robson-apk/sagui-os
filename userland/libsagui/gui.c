/* gui.c - kit gráfico da libsagui (H22): desenho e componentes no anel 3.
 *
 * O programa desenha nos pixels da própria janela (memória que o kernel
 * compartilha com ele) e chama jan_mostrar para o kernel exibir. Os
 * componentes não guardam nada escondido: cada um é uma estrutura do
 * programa, e o programa decide quando desenhar e para quem mandar cada
 * evento. É simples de entender e de testar. */
#include "gui.h"
#include "suave.h"

extern const unsigned char fonte_sagui[256][16];

/* ------------------------------------------------------------ janela e desenho */

jan_t *jan_abrir(const char *titulo, int largura, int altura)
{
    jan_t *j = aloca(sizeof(jan_t));
    if (!j) return NULL;
    j->id = criar_janela(titulo, largura, altura, &j->px);
    if (j->id < 0) { libera(j); return NULL; }
    j->larg = largura;
    j->alt = altura;
    g_retangulo(j, 0, 0, largura, altura, COR_FUNDO);
    return j;
}

void jan_fechar(jan_t *j)
{
    if (!j) return;
    fechar_janela(j->id);
    libera(j);
}

void jan_mostrar(jan_t *j) { atualizar_janela(j->id, NULL); }

void jan_mostrar_area(jan_t *j, int x, int y, int l, int a)
{
    retangulo_t r = { x, y, l, a };
    atualizar_janela(j->id, &r);
}

void g_retangulo(jan_t *j, int x, int y, int l, int a, uint32_t cor)
{
    int x1 = x + l, y1 = y + a;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > j->larg) x1 = j->larg;
    if (y1 > j->alt) y1 = j->alt;
    for (int yy = y; yy < y1; yy++) {
        uint32_t *p = j->px + yy * j->larg;
        for (int xx = x; xx < x1; xx++) p[xx] = cor;
    }
}

void g_moldura(jan_t *j, int x, int y, int l, int a, uint32_t cor)
{
    g_retangulo(j, x, y, l, 1, cor);
    g_retangulo(j, x, y + a - 1, l, 1, cor);
    g_retangulo(j, x, y, 1, a, cor);
    g_retangulo(j, x + l - 1, y, 1, a, cor);
}

/* Área de desenho suavizado (suave.h) que cobre a janela toda. */
static suave_t sv_de(jan_t *j)
{
    suave_t s = { j->px, j->larg, 0, 0, j->larg, j->alt };
    return s;
}

/* Cor do pixel (x, y) da janela, ou a cor de fundo se estiver fora. */
static uint32_t cor_em(jan_t *j, int x, int y)
{
    if ((unsigned)x < (unsigned)j->larg && (unsigned)y < (unsigned)j->alt) return j->px[y * j->larg + x] & 0xFFFFFF;
    return COR_FUNDO;
}

/* ---- H88: formas com borda suavizada ---- */

void g_ret_redondo(jan_t *j, int x, int y, int l, int a, int raio, uint32_t topo, uint32_t base)
{
    suave_t s = sv_de(j);
    suave_ret(&s, x, y, l, a, raio, topo, base, 256);
}

void g_contorno_redondo(jan_t *j, int x, int y, int l, int a, int raio, uint32_t cor)
{
    suave_t s = sv_de(j);
    suave_contorno(&s, x, y, l, a, raio, cor, 256);
}

/* Ícone ARGB (tools/mkarte.py --programas, icones.h): o alfa mistura com o fundo. */
void g_icone(jan_t *j, int x, int y, const uint32_t *argb, int lado)
{
    suave_t s = sv_de(j);
    for (int v = 0; v < lado; v++)
        for (int u = 0; u < lado; u++) {
            uint32_t c = argb[v * lado + u];
            if (c >> 24) suave_ponto(&s, x + u, y + v, c & 0xFFFFFF, (int)(c >> 24) + ((c >> 24) == 255));
        }
}

/* Escreve o texto cortado em max pixels; se não couber, termina em "...". */
void g_texto_cortado(jan_t *j, int x, int y, const char *t, int max, uint32_t cor)
{
    int n = (int)tam_texto(t), cabem = max / LETRA_L;
    if (n <= cabem) { g_texto(j, x, y, t, cor); return; }
    if (cabem <= 3) { g_texto_n(j, x, y, t, cabem, cor); return; }
    g_texto_n(j, x, y, t, cabem - 2, cor);
    g_texto(j, x + (cabem - 2) * LETRA_L, y, "..", cor);
}

static inline void ponto(jan_t *j, int x, int y, uint32_t cor)
{
    if ((unsigned)x < (unsigned)j->larg && (unsigned)y < (unsigned)j->alt)
        j->px[y * j->larg + x] = cor;
}

void g_linha(jan_t *j, int x0, int y0, int x1, int y1, uint32_t cor)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1, sy = y0 < y1 ? 1 : -1;
    int passos = dx > dy ? dx : dy, acum = 0;
    for (int k = 0; k <= passos; k++) {
        ponto(j, x0, y0, cor);
        if (dx >= dy) { x0 += sx; acum += dy; if (2 * acum >= dx) { y0 += sy; acum -= dx; } }
        else          { y0 += sy; acum += dx; if (2 * acum >= dy) { x0 += sx; acum -= dy; } }
    }
}

/* Mistura de duas cores, canal a canal: num/den de b. */
static uint32_t mistura(uint32_t a, uint32_t b, int num, int den)
{
    uint32_t r = 0;
    for (int s = 0; s <= 16; s += 8) {
        int ca = (int)((a >> s) & 255), cb = (int)((b >> s) & 255);
        r |= (uint32_t)(ca + (cb - ca) * num / den) << s;
    }
    return r;
}

/* Suaviza a escada das diagonais, como o kernel (H81, do pull request #1 do
 * Robson): um pixel apagado com vizinhos acesos em "L" recebe 1/3 da cor. */
static inline bool aceso(const unsigned char *g, int i, int l)
{
    return i >= 0 && i < 8 && l >= 0 && l < 16 && (g[l] & (0x80 >> i));
}

static inline void quina(jan_t *j, const unsigned char *g, int x, int y, int i, int l, uint32_t cor)
{
    bool e = aceso(g, i - 1, l), d = aceso(g, i + 1, l), c = aceso(g, i, l - 1), b = aceso(g, i, l + 1);
    if (e != d && c != b && (unsigned)x < (unsigned)j->larg && (unsigned)y < (unsigned)j->alt) {
        uint32_t *p = &j->px[y * j->larg + x];
        *p = mistura(*p, cor, 1, 3);
    }
}

void g_texto_n(jan_t *j, int x, int y, const char *t, int n, uint32_t cor)
{
    for (int k = 0; k < n && t[k]; k++, x += LETRA_L) {
        if (x >= j->larg) break;
        const unsigned char *g = fonte_sagui[(uint8_t)t[k]];
        for (int lin = 0; lin < 16; lin++)
            for (int col = 0; col < 8; col++)
                if (g[lin] & (0x80 >> col)) ponto(j, x + col, y + lin, cor);
                else quina(j, g, x + col, y + lin, col, lin, cor);
    }
}

void g_texto(jan_t *j, int x, int y, const char *t, uint32_t cor)
{
    g_texto_n(j, x, y, t, 1 << 30, cor);
}

int g_largura_texto(const char *t) { return (int)tam_texto(t) * LETRA_L; }

void g_desenho(jan_t *j, int x, int y, const char *const *linhas, int l, int a,
               uint32_t (*paleta)(char), int escala)
{
    for (int lin = 0; lin < a; lin++)
        for (int col = 0; col < l; col++) {
            uint32_t c = paleta(linhas[lin][col]);
            if (c != 0xFF000000)
                g_retangulo(j, x + col * escala, y + lin * escala, escala, escala, c);
        }
}

/* Moldura em relevo: clara em cima e à esquerda, escura embaixo e à direita
 * (ou o contrário, para parecer afundada). */
void g_relevo(jan_t *j, int x, int y, int l, int a, bool afundado)
{
    uint32_t claro = 0xFFFFFF, escuro = COR_BORDA;
    if (afundado) { uint32_t t = claro; claro = escuro; escuro = t; }
    g_retangulo(j, x, y, l, 1, claro);
    g_retangulo(j, x, y, 1, a, claro);
    g_retangulo(j, x, y + a - 1, l, 1, escuro);
    g_retangulo(j, x + l - 1, y, 1, a, escuro);
}

static bool dentro(int px, int py, int x, int y, int l, int a)
{
    return px >= x && py >= y && px < x + l && py < y + a;
}

/* ------------------------------------------------------------ rótulo */

void rotulo(jan_t *j, int x, int y, const char *texto, uint32_t cor)
{
    g_texto(j, x, y, texto, cor);
}

/* ------------------------------------------------------------ botão */

void botao_desenhar(jan_t *j, botao_t *b)
{
    /* H88: cantos redondos suavizados, degradê claro, contorno fino e, se
     * houver, o ícone de 20 pixels à esquerda do texto. Antes de pintar, o
     * retângulo volta à cor do fundo (lida logo à esquerda do botão), para
     * que redesenhar não vá escurecendo os pixels dos cantos. O texto que
     * não cabe é cortado com ".." em vez de sair do botão. */
    uint32_t fundo = cor_em(j, b->x - 2, b->y + b->a / 2);
    g_retangulo(j, b->x - 1, b->y - 1, b->l + 2, b->a + 2, fundo);
    suave_t s = sv_de(j);
    uint32_t topo, base, borda;
    if (b->ativo && !b->apertado) { topo = 0xE3F2E9; base = 0xCFE8D9; borda = COR_DESTAQUE; }
    else if (b->apertado)         { topo = 0xD2D9E0; base = 0xDDE3E8; borda = 0x7E8892; }
    else                          { topo = 0xFFFFFF; base = 0xE6EAEE; borda = 0xA9B2BB; }
    suave_ret(&s, b->x, b->y + 1, b->l, b->a, 6, 0x000000, 0x000000, 22);     /* sombra de 1 pixel */
    suave_ret(&s, b->x, b->y, b->l, b->a, 6, topo, base, 256);
    suave_contorno(&s, b->x, b->y, b->l, b->a, 6, borda, 256);
    int d = b->apertado ? 1 : 0;
    int il = b->icone ? 20 + (b->texto && b->texto[0] ? 8 : 0) : 0;
    int livre = b->l - 12 - il;
    int tl = b->texto ? g_largura_texto(b->texto) : 0;
    if (tl > livre) tl = livre / LETRA_L * LETRA_L;
    int x0 = b->alinhar_esquerda ? b->x + 10 : b->x + (b->l - il - tl) / 2;
    if (b->icone) g_icone(j, x0 + d, b->y + (b->a - 20) / 2 + d, b->icone, 20);
    if (b->texto) g_texto_cortado(j, x0 + il + d, b->y + (b->a - LETRA_A) / 2 + d, b->texto, livre,
                                  b->ativo ? 0x0B5C30 : COR_TEXTO);
    jan_mostrar_area(j, b->x - 1, b->y - 1, b->l + 2, b->a + 2);
}

/* Clicado = apertou e soltou dentro do botão (soltar fora cancela). */
bool botao_tratar(jan_t *j, botao_t *b, const evento_t *e)
{
    if (e->tipo == EV_CLIQUE && (e->botoes & 1) && dentro(e->x, e->y, b->x, b->y, b->l, b->a)) {
        b->apertado = true;
        botao_desenhar(j, b);
        return false;
    }
    if (e->tipo == EV_SOLTOU && b->apertado) {
        b->apertado = false;
        botao_desenhar(j, b);
        return dentro(e->x, e->y, b->x, b->y, b->l, b->a);
    }
    return false;
}

/* ------------------------------------------------------------ caixa de texto */

void caixa_definir(caixa_t *c, const char *texto)
{
    copia_texto(c->texto, texto, sizeof c->texto);
    c->cursor = (int)tam_texto(c->texto);
}

void caixa_desenhar(jan_t *j, caixa_t *c)
{
    int a = LETRA_A + 8;
    /* H88: cantos redondos; com foco, contorno verde de 2 pixels. */
    g_retangulo(j, c->x, c->y, c->l, a, cor_em(j, c->x - 2, c->y + a / 2));
    suave_t s = sv_de(j);
    suave_ret(&s, c->x, c->y, c->l, a, 4, COR_BRANCO, COR_BRANCO, 256);
    suave_contorno(&s, c->x, c->y, c->l, a, 4, c->foco ? COR_DESTAQUE : 0xA9B2BB, 256);
    if (c->foco) suave_contorno(&s, c->x + 1, c->y + 1, c->l - 2, a - 2, 3, COR_DESTAQUE, 160);
    int cabem = (c->l - 10) / LETRA_L;
    int ini = c->cursor > cabem ? c->cursor - cabem : 0;
    g_texto_n(j, c->x + 5, c->y + 4, c->texto + ini, cabem, COR_TEXTO);
    if (c->foco)
        g_retangulo(j, c->x + 5 + (c->cursor - ini) * LETRA_L, c->y + 4, 2, LETRA_A, COR_TEXTO);
    jan_mostrar_area(j, c->x, c->y, c->l, a);
}

bool caixa_tratar(jan_t *j, caixa_t *c, const evento_t *e)
{
    if (e->tipo == EV_CLIQUE) {
        bool estava = c->foco;
        c->foco = dentro(e->x, e->y, c->x, c->y, c->l, LETRA_A + 8);
        if (c->foco) {
            int col = (e->x - c->x - 5 + LETRA_L / 2) / LETRA_L;
            int n = (int)tam_texto(c->texto);
            c->cursor = col < 0 ? 0 : col > n ? n : col;
        }
        if (c->foco || estava) caixa_desenhar(j, c);
        return false;
    }
    if (e->tipo != EV_TECLA || !c->foco) return false;
    int n = (int)tam_texto(c->texto);
    unsigned t = e->tecla;
    if (t == '\n') return true;
    if (t == '\b') {
        if (c->cursor > 0) { memmove(c->texto + c->cursor - 1, c->texto + c->cursor, (size_t)(n - c->cursor + 1)); c->cursor--; }
    } else if (t == TECLA_DELETE) {
        if (c->cursor < n) memmove(c->texto + c->cursor, c->texto + c->cursor + 1, (size_t)(n - c->cursor));
    } else if (t == TECLA_ESQUERDA) { if (c->cursor > 0) c->cursor--; }
    else if (t == TECLA_DIREITA) { if (c->cursor < n) c->cursor++; }
    else if (t == TECLA_HOME) c->cursor = 0;
    else if (t == TECLA_END) c->cursor = n;
    else if (t >= 0x20 && t < 0x100 && t != 0x7F && n < (int)sizeof c->texto - 1) {
        memmove(c->texto + c->cursor + 1, c->texto + c->cursor, (size_t)(n - c->cursor + 1));
        c->texto[c->cursor++] = (char)t;
    } else return false;
    caixa_desenhar(j, c);
    return false;
}

/* ------------------------------------------------------------ área de texto */

#define BARRA_ROLAGEM 12

int area_linhas_visiveis(area_t *a) { return (a->a - 8) / LETRA_A; }

bool area_iniciar(area_t *a, int x, int y, int l, int al)
{
    memset(a, 0, sizeof *a);
    a->x = x; a->y = y; a->l = l; a->a = al;
    a->cap = 4096;
    a->buf = aloca((size_t)a->cap);
    if (!a->buf) return false;
    a->buf[0] = 0;
    return true;
}

static bool garantir(area_t *a, int precisa)
{
    if (precisa < a->cap) return true;
    int nova = a->cap;
    while (nova <= precisa) nova *= 2;
    char *b = realoca(a->buf, (size_t)nova);
    if (!b) return false;
    a->buf = b;
    a->cap = nova;
    return true;
}

void area_definir(area_t *a, const char *texto, int n)
{
    if (!garantir(a, n + 1)) n = a->cap - 1;
    memcpy(a->buf, texto, (size_t)n);
    a->buf[n] = 0;
    a->tam = n;
    a->cursor = 0;
    a->topo = 0;
    a->modificado = false;
}

/* Linha e coluna de uma posição do buffer. */
static void linha_coluna(area_t *a, int pos, int *lin, int *col)
{
    int l = 0, ini = 0;
    for (int i = 0; i < pos; i++)
        if (a->buf[i] == '\n') { l++; ini = i + 1; }
    *lin = l;
    *col = pos - ini;
}

static int inicio_da_linha(area_t *a, int lin)
{
    int pos = 0;
    for (int l = 0; l < lin && pos < a->tam; pos++)
        if (a->buf[pos] == '\n') l++;
    return pos;
}

static int tamanho_da_linha(area_t *a, int ini)
{
    int n = 0;
    while (ini + n < a->tam && a->buf[ini + n] != '\n') n++;
    return n;
}

static int total_linhas(area_t *a)
{
    int n = 1;
    for (int i = 0; i < a->tam; i++) if (a->buf[i] == '\n') n++;
    return n;
}

static void cursor_visivel(area_t *a)
{
    int lin, col, vis = area_linhas_visiveis(a);
    linha_coluna(a, a->cursor, &lin, &col);
    if (lin < a->topo) a->topo = lin;
    if (lin >= a->topo + vis) a->topo = lin - vis + 1;
}

void area_desenhar(jan_t *j, area_t *a)
{
    int texto_l = a->l - BARRA_ROLAGEM;
    g_retangulo(j, a->x, a->y, a->l, a->a, COR_BRANCO);
    g_moldura(j, a->x, a->y, a->l, a->a, a->foco ? COR_DESTAQUE : 0xA9B2BB);
    int vis = area_linhas_visiveis(a), cabem = (texto_l - 10) / LETRA_L;
    int lin_cur, col_cur;
    linha_coluna(a, a->cursor, &lin_cur, &col_cur);
    int pos = inicio_da_linha(a, a->topo);
    for (int k = 0; k < vis && pos <= a->tam; k++) {
        int n = tamanho_da_linha(a, pos);
        g_texto_n(j, a->x + 5, a->y + 4 + k * LETRA_A, a->buf + pos, n < cabem ? n : cabem, COR_TEXTO);
        if (a->foco && !a->somente_leitura && a->topo + k == lin_cur && col_cur <= cabem)
            g_retangulo(j, a->x + 5 + col_cur * LETRA_L, a->y + 4 + k * LETRA_A, 2, LETRA_A, COR_TEXTO);
        pos += n + 1;
        if (pos > a->tam) break;
    }
    /* Barra de rolagem: o "polegar" mostra que parte do texto está à vista. */
    int bx = a->x + a->l - BARRA_ROLAGEM, total = total_linhas(a);
    g_retangulo(j, bx, a->y + 1, BARRA_ROLAGEM - 1, a->a - 2, 0xF0F2F4);
    int trilho = a->a - 4;
    int pol_a = total <= vis ? trilho : trilho * vis / total;
    if (pol_a < 12) pol_a = 12;
    int pol_y = total <= vis ? 0 : (trilho - pol_a) * a->topo / (total - vis > 0 ? total - vis : 1);
    if (total > vis) g_ret_redondo(j, bx + 3, a->y + 2 + pol_y, BARRA_ROLAGEM - 7, pol_a, 3, 0xA9B2BB, 0xA9B2BB);
    jan_mostrar_area(j, a->x, a->y, a->l, a->a);
}

static void inserir(area_t *a, const char *t, int n)
{
    if (!garantir(a, a->tam + n + 1)) return;
    memmove(a->buf + a->cursor + n, a->buf + a->cursor, (size_t)(a->tam - a->cursor + 1));
    memcpy(a->buf + a->cursor, t, (size_t)n);
    a->tam += n;
    a->cursor += n;
    a->modificado = true;
}

static void remover(area_t *a, int pos)
{
    if (pos < 0 || pos >= a->tam) return;
    memmove(a->buf + pos, a->buf + pos + 1, (size_t)(a->tam - pos));
    a->tam--;
    a->modificado = true;
}

static void mover_vertical(area_t *a, int delta)
{
    int lin, col;
    linha_coluna(a, a->cursor, &lin, &col);
    int nova = lin + delta, total = total_linhas(a);
    if (nova < 0) nova = 0;
    if (nova >= total) nova = total - 1;
    int ini = inicio_da_linha(a, nova), n = tamanho_da_linha(a, ini);
    a->cursor = ini + (col < n ? col : n);
}

bool area_tratar(jan_t *j, area_t *a, const evento_t *e)
{
    bool mudou = false;
    if (e->tipo == EV_CLIQUE) {
        bool estava = a->foco;
        a->foco = dentro(e->x, e->y, a->x, a->y, a->l, a->a);
        if (a->foco) {
            if (e->x >= a->x + a->l - BARRA_ROLAGEM) {
                /* Clique na barra de rolagem: uma página para cima ou para baixo. */
                int vis = area_linhas_visiveis(a), total = total_linhas(a);
                a->topo += e->y < a->y + a->a / 2 ? -vis : vis;
                if (a->topo > total - vis) a->topo = total - vis;
                if (a->topo < 0) a->topo = 0;
            } else {
                int lin = a->topo + (e->y - a->y - 4) / LETRA_A, col = (e->x - a->x - 5 + LETRA_L / 2) / LETRA_L;
                int total = total_linhas(a);
                if (lin >= total) lin = total - 1;
                if (lin < 0) lin = 0;
                int ini = inicio_da_linha(a, lin), n = tamanho_da_linha(a, ini);
                a->cursor = ini + (col < 0 ? 0 : col > n ? n : col);
            }
        }
        if (a->foco || estava) area_desenhar(j, a);
        return false;
    }
    if (e->tipo != EV_TECLA || !a->foco) return false;
    unsigned t = e->tecla;
    int vis = area_linhas_visiveis(a);
    if (t == TECLA_ESQUERDA) { if (a->cursor > 0) a->cursor--; }
    else if (t == TECLA_DIREITA) { if (a->cursor < a->tam) a->cursor++; }
    else if (t == TECLA_CIMA) mover_vertical(a, -1);
    else if (t == TECLA_BAIXO) mover_vertical(a, 1);
    else if (t == TECLA_PGUP) { mover_vertical(a, -vis); a->topo -= vis; if (a->topo < 0) a->topo = 0; }
    else if (t == TECLA_PGDN) { mover_vertical(a, vis); a->topo += vis; }
    else if (t == TECLA_HOME) { int l, c; linha_coluna(a, a->cursor, &l, &c); a->cursor -= c; }
    else if (t == TECLA_END) { int l, c; linha_coluna(a, a->cursor, &l, &c); a->cursor += tamanho_da_linha(a, a->cursor - c) - c; }
    else if (a->somente_leitura) return false;
    else if (t == '\b') { if (a->cursor > 0) { remover(a, a->cursor - 1); a->cursor--; mudou = true; } }
    else if (t == TECLA_DELETE) { if (a->cursor < a->tam) { remover(a, a->cursor); mudou = true; } }
    else if (t == '\n') { inserir(a, "\n", 1); mudou = true; }
    else if (t == '\t') { inserir(a, "    ", 4); mudou = true; }
    else if (t >= 0x20 && t < 0x100 && t != 0x7F) { char c = (char)t; inserir(a, &c, 1); mudou = true; }
    else return false;
    int total = total_linhas(a);
    if (a->topo > total - 1) a->topo = total - 1 > 0 ? total - 1 : 0;
    cursor_visivel(a);
    area_desenhar(j, a);
    return mudou;
}

/* ------------------------------------------------------------ lista */

void lista_desenhar(jan_t *j, lista_t *l)
{
    g_retangulo(j, l->x, l->y, l->l, l->a, COR_BRANCO);
    g_moldura(j, l->x, l->y, l->l, l->a, 0xA9B2BB);
    int vis = (l->a - 4) / LISTA_ALTURA_ITEM;
    for (int k = 0; k < vis && l->topo + k < l->qtd; k++) {
        int i = l->topo + k, y = l->y + 2 + k * LISTA_ALTURA_ITEM;
        bool sel = i == l->sel;
        if (sel) g_ret_redondo(j, l->x + 3, y, l->l - 6 - (l->qtd > vis ? 6 : 0), LISTA_ALTURA_ITEM, 4, COR_SELECAO, COR_SELECAO);
        int tx = l->x + 6;
        if (l->icone) { l->icone(j, tx, y + 2, i); tx += 22; }
        g_texto_n(j, tx, y + 2, l->itens[i], (l->x + l->l - tx - 4) / LETRA_L, sel ? COR_BRANCO : COR_TEXTO);
    }
    if (l->qtd > vis) {   /* indicador simples de que há mais itens */
        int total = l->qtd, trilho = l->a - 4;
        int pol_a = trilho * vis / total, pol_y = (trilho - pol_a) * l->topo / (total - vis);
        g_ret_redondo(j, l->x + l->l - 7, l->y + 2 + pol_y, 5, pol_a, 2, 0xA9B2BB, 0xA9B2BB);
    }
    jan_mostrar_area(j, l->x, l->y, l->l, l->a);
}

static void lista_garantir_visivel(lista_t *l)
{
    int vis = (l->a - 4) / LISTA_ALTURA_ITEM;
    if (l->sel < l->topo) l->topo = l->sel;
    if (l->sel >= l->topo + vis) l->topo = l->sel - vis + 1;
    if (l->topo < 0) l->topo = 0;
}

int lista_tratar(jan_t *j, lista_t *l, const evento_t *e)
{
    if (e->tipo == EV_CLIQUE && dentro(e->x, e->y, l->x, l->y, l->l, l->a)) {
        int i = l->topo + (e->y - l->y - 2) / LISTA_ALTURA_ITEM;
        if (i < 0 || i >= l->qtd) return LISTA_NADA;
        l->sel = i;
        lista_desenhar(j, l);
        return e->cliques >= 2 ? LISTA_ATIVOU : LISTA_SELECIONOU;
    }
    if (e->tipo != EV_TECLA || l->qtd == 0) return LISTA_NADA;
    if (e->tecla == '\n' && l->sel >= 0) return LISTA_ATIVOU;
    int antes = l->sel;
    if (e->tecla == TECLA_BAIXO && l->sel < l->qtd - 1) l->sel++;
    else if (e->tecla == TECLA_CIMA && l->sel > 0) l->sel--;
    else if (e->tecla == TECLA_HOME) l->sel = 0;
    else if (e->tecla == TECLA_END) l->sel = l->qtd - 1;
    if (l->sel == antes) return LISTA_NADA;
    lista_garantir_visivel(l);
    lista_desenhar(j, l);
    return LISTA_SELECIONOU;
}

/* ------------------------------------------------------------ diálogos */

/* Escreve um texto com '\n' em várias linhas. */
static void texto_multilinha(jan_t *j, int x, int y, const char *t)
{
    while (*t) {
        const char *fim = acha_char(t, '\n');
        int n = fim ? (int)(fim - t) : (int)tam_texto(t);
        g_texto_n(j, x, y, t, n, COR_TEXTO);
        y += LETRA_A + 2;
        t += n;
        if (*t == '\n') t++;
    }
}

static int linhas_de(const char *t)
{
    int n = 1;
    for (; *t; t++) if (*t == '\n') n++;
    return n;
}

static int dialogo(const char *titulo, const char *texto, const char *b1, const char *b2, caixa_t *caixa)
{
    int alt = 70 + linhas_de(texto) * (LETRA_A + 2) + (caixa ? 34 : 0);
    /* Largura: 380, ou a da linha mais comprida (até 900), para mensagens
     * como as do compilador caberem inteiras. */
    int larg = 380, n = 0;
    for (const char *p = texto; ; p++) {
        if (*p == '\n' || !*p) { if (n * LETRA_L + 32 > larg) larg = n * LETRA_L + 32; n = 0; if (!*p) break; }
        else n++;
    }
    if (larg > 900) larg = 900;
    if (alt > 680) alt = 680;
    jan_t *j = jan_abrir(titulo, larg, alt);
    if (!j) return 0;
    texto_multilinha(j, 16, 16, texto);
    botao_t bs[2] = { { larg - 16 - 90, alt - 40, 90, 26, b1, false }, { larg - 16 - 190, alt - 40, 90, 26, b2, false } };
    int qtd = b2 ? 2 : 1;
    if (caixa) {
        caixa->x = 16; caixa->y = alt - 40 - 36; caixa->l = larg - 32; caixa->foco = true;
        caixa_desenhar(j, caixa);
    }
    for (int i = 0; i < qtd; i++) botao_desenhar(j, &bs[i]);
    jan_mostrar(j);
    int resposta = 0;
    evento_t e;
    while (!resposta) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) { resposta = 2; break; }
        if (e.tipo == EV_TECLA && e.tecla == TECLA_ESC) { resposta = qtd == 2 ? 2 : 1; break; }
        if (caixa && caixa_tratar(j, caixa, &e)) { resposta = 1; break; }
        if (!caixa && e.tipo == EV_TECLA && e.tecla == '\n') { resposta = 1; break; }
        for (int i = 0; i < qtd; i++)
            if (botao_tratar(j, &bs[i], &e)) resposta = i + 1;
    }
    jan_fechar(j);
    return resposta;
}

bool confirmar(const char *titulo, const char *pergunta)
{
    return dialogo(titulo, pergunta, "Sim", "Não", NULL) == 1;
}

void mensagem(const char *titulo, const char *texto)
{
    dialogo(titulo, texto, "OK", NULL, NULL);
}

bool pedir_texto(const char *titulo, const char *pergunta, char *resposta, int max)
{
    caixa_t c;
    memset(&c, 0, sizeof c);
    caixa_definir(&c, resposta);
    if (dialogo(titulo, pergunta, "OK", "Cancelar", &c) != 1) return false;
    copia_texto(resposta, c.texto, (size_t)max);
    return true;
}

/* ------------------------------------------------------------ tamanho (H78) */

bool jan_redimensionavel(jan_t *j, int larg_min, int alt_min)
{
    return janela_opcoes(j->id, JAN_REDIMENSIONAVEL, larg_min, alt_min) >= 0;
}

void jan_novo_tamanho(jan_t *j, const evento_t *e)
{
    if (e->tipo != EV_REDIMENSIONAR) return;
    j->larg = e->x;
    j->alt = e->y;
}

/* Menu do próprio programa no ponto (x, y) da janela (H81): itens separados
 * por "|"; a escolha chega como EV_MENU com o índice em e.tecla. */
void jan_menu(jan_t *j, int x, int y, const char *itens) { menu_janela(j->id, x, y, itens); }
