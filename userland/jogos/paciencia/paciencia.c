/* paciencia - Paciência Banana, a paciência de dez colunas do SaguiOS (H92).
 *
 * Trazido do spider do fork SaguiOS-Extended de Alejandro Anuncibay e
 * reescrito sobre o main atual, com o tema de banana do SaguiOS: a mesa é um
 * feltro verde de floresta, o verso das cartas tem uma banana, as figuras
 * (valete, dama e rei) são o sagui (com banana, flor ou coroa) e os
 * destaques são amarelo-banana. Os quatro naipes de sempre continuam bem
 * visíveis, para dar para jogar. Tudo é desenhado aqui: as cartas são feitas
 * uma vez, em memória, e depois só copiadas para a janela.
 *
 * As regras estão em regras.c. Mouse: arrastar e soltar; um clique numa
 * carta leva a carta (ou a escadinha) para o melhor lugar; um clique no
 * monte dá mais cartas. Teclado: F2 partida nova, Ctrl+Z ou U desfaz, D dá
 * cartas, H mostra uma dica. Opções: --naipes 1|2|4, --semente N, --quase
 * (uma partida a uma jogada da vitória, para ver a tela final). */
#include "sagui.h"
#include "gui.h"
#include "regras.h"

#define LARG     800
#define ALT      584
#define BARRA_A  34
#define CL       68            /* tamanho da carta */
#define CA       92
#define MARGEM_X 10
#define PASSO_X  (CL + 11)
#define TOPO_Y   (BARRA_A + 10)
#define BASE_Y   (ALT - CA - 10) /* linha do monte e das sequências feitas */
#define MAX_DESFAZER 200

#define COR_VERMELHA 0xC8161F
#define COR_PRETA    0x15171C
#define COR_BANANA   0xFFD52E

static jan_t *jan;
static mesa_t mesa;
static mesa_t *desfazer;
static int qtd_desfazer;
static int naipes = 1;
static uint32_t semente;
static uint32_t *feltro;                 /* o fundo verde, pronto */
static uint32_t *cena;                   /* a mesa sem o que está sendo arrastado */
static uint32_t *spr_frente[4][14], *spr_verso;
static botao_t bt_nova, bt_desfazer, bt_dica, bt_naipes;
static char aviso[80];
static char rotulo_naipes[16];
static int ordem_dica;

/* ------------------------------------------------------------ desenho de base */

static int abs_i(int v) { return v < 0 ? -v : v; }

static uint32_t mistura(uint32_t a, uint32_t b, int t)   /* t de 0 (a) a 256 (b) */
{
    uint32_t r = (((a >> 16) & 255) * (uint32_t)(256 - t) + ((b >> 16) & 255) * (uint32_t)t) >> 8;
    uint32_t g = (((a >> 8) & 255) * (uint32_t)(256 - t) + ((b >> 8) & 255) * (uint32_t)t) >> 8;
    uint32_t bb = ((a & 255) * (uint32_t)(256 - t) + (b & 255) * (uint32_t)t) >> 8;
    return r << 16 | g << 8 | bb;
}

/* Lado de dentro da reta (a)->(b)? (produto vetorial; serve para triângulos) */
static int lado(int ax, int ay, int bx, int by, int x, int y) { return (bx - ax) * (y - ay) - (by - ay) * (x - ax); }

static bool no_triangulo(int ax, int ay, int bx, int by, int cx, int cy, int x, int y)
{
    int d1 = lado(ax, ay, bx, by, x, y), d2 = lado(bx, by, cx, cy, x, y), d3 = lado(cx, cy, ax, ay, x, y);
    bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

static bool no_circulo(int x, int y, int cx, int cy, int r) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; }

/* O desenho de cada naipe numa grade de -256 a 256 (o "raio" do naipe):
 * copas = dois círculos e um triângulo; ouros = losango; espadas = copas de
 * cabeça para baixo com um pezinho; paus = três círculos e o pezinho. */
static bool dentro_naipe(int naipe, int u, int v)
{
    switch (naipe) {
    case COPAS:
        return no_circulo(u, v, -122, -70, 132) || no_circulo(u, v, 122, -70, 132) ||
               no_triangulo(-248, -20, 248, -20, 0, 250, u, v);
    case OUROS:
        return abs_i(u) * 256 / 190 + abs_i(v) <= 256;
    case ESPADAS:
        return no_circulo(u, v, -118, 50, 124) || no_circulo(u, v, 118, 50, 124) ||
               no_triangulo(-240, 20, 240, 20, 0, -256, u, v) || no_triangulo(0, 60, -90, 256, 90, 256, u, v);
    default:   /* PAUS */
        return no_circulo(u, v, 0, -128, 104) || no_circulo(u, v, -118, 40, 104) || no_circulo(u, v, 118, 40, 104) ||
               no_circulo(u, v, 0, 10, 60) || no_triangulo(0, 20, -84, 256, 84, 256, u, v);
    }
}

static bool vermelho(int naipe) { return naipe == COPAS || naipe == OUROS; }

/* Um naipe de raio r em (cx, cy), com as bordas suavizadas por 4 amostras
 * por pixel (a cor se mistura com o que já estava, na proporção). */
static void naipe_desenhar(jan_t *s, int naipe, int cx, int cy, int r, uint32_t cor)
{
    for (int y = cy - r - 1; y <= cy + r + 1; y++)
        for (int x = cx - r - 1; x <= cx + r + 1; x++) {
            if (x < 0 || y < 0 || x >= s->larg || y >= s->alt) continue;
            int n = 0;
            for (int k = 0; k < 4; k++) {
                int sx = (x - cx) * 4 + (k & 1) * 2 - 1, sy = (y - cy) * 4 + (k >> 1) * 2 - 1;   /* em quartos de pixel */
                if (dentro_naipe(naipe, sx * 64 / r, sy * 64 / r)) n++;
            }
            if (n) s->px[y * s->larg + x] = mistura(s->px[y * s->larg + x], cor, n * 64);
        }
}

/* ------------------------------------------------------------ pixel art (nossa) */

static const char *const sp_sagui[16] = {
    "................",
    ".WW..........WW.",
    "WWWW.EEEEEE.WWWW",
    "WWWEEEEWWEEEEWWW",
    ".WWEERRRRRREEWW.",
    "...ERRRRRRRRE...",
    "...ERPPRRPPRE...",
    "...ERPPRRPPRE...",
    "...ERRRRRRRRE...",
    "...ERRRPPRRRE...",
    "....ERRRRRRE....",
    "....ERPPPPRE....",
    ".....ERRRRE.....",
    "....EEEEEEEE....",
    "...EEMMMMMMEE...",
    "..EEMMMMMMMMEE..",
};
static const char *const sp_banana[12] = {
    "......SS",
    ".....SYD",
    ".....YYD",
    "....YYYD",
    "...YYYD.",
    "..YYYYD.",
    ".YYYYD..",
    "YYYYD...",
    "YYYD....",
    ".DD.....",
    "........",
    "........",
};
static const char *const sp_coroa[5] = {
    "Y..Y..Y",
    "YY.Y.YY",
    "YYYYYYY",
    "YRYYYRY",
    "YYYYYYY",
};
static const char *const sp_flor[5] = {
    ".F.F.",
    "FFCFF",
    ".FCF.",
    "FFCFF",
    ".F.F.",
};
static const char *const sp_cacho[12] = {
    ".....MM.....",
    ".....MM.....",
    "....DMMD....",
    "...YDYYDY...",
    "..YYDYYDYY..",
    ".YYDYYYYDYY.",
    ".YYDYYYYDYY.",
    "YYDYYYYYYDYY",
    "YYDYYYYYYDYY",
    "DYDYYYYYYDYD",
    ".D.DYYYYD.D.",
    "....DDDD....",
};

static uint32_t paleta(char c)
{
    switch (c) {
    case 'W': return 0xF5F5F0; case 'E': return 0x3C2D23; case 'R': return 0xC08E78; case 'P': return 0x17120F;
    case 'M': return 0x6E553C; case 'Y': return COR_BANANA; case 'D': return 0xB07A12; case 'S': return 0x5A3E1A;
    case 'F': return 0xF06AA0; case 'C': return 0xFFF3A8;
    default:  return 0xFF000000;
    }
}

/* ------------------------------------------------------------ as cartas */

static const char *const nome_valor[14] = { "", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K" };

/* Onde vão os naipes do meio de cada carta, de 2 a 10 (a arrumação de
 * sempre dos baralhos): coluna 0/1/2 (esquerda, meio, direita) e linha de 0
 * a 12, em meias-linhas. */
static const uint8_t pips[11][10][2] = {
    [2] = { {1, 0}, {1, 12} },
    [3] = { {1, 0}, {1, 6}, {1, 12} },
    [4] = { {0, 0}, {2, 0}, {0, 12}, {2, 12} },
    [5] = { {0, 0}, {2, 0}, {1, 6}, {0, 12}, {2, 12} },
    [6] = { {0, 0}, {2, 0}, {0, 6}, {2, 6}, {0, 12}, {2, 12} },
    [7] = { {0, 0}, {2, 0}, {1, 3}, {0, 6}, {2, 6}, {0, 12}, {2, 12} },
    [8] = { {0, 0}, {2, 0}, {1, 3}, {0, 6}, {2, 6}, {1, 9}, {0, 12}, {2, 12} },
    [9] = { {0, 0}, {2, 0}, {0, 4}, {2, 4}, {1, 6}, {0, 8}, {2, 8}, {0, 12}, {2, 12} },
    [10] = { {0, 0}, {2, 0}, {1, 2}, {0, 4}, {2, 4}, {0, 8}, {2, 8}, {1, 10}, {0, 12}, {2, 12} },
};

/* Recorta os cantos arredondados (raio 7): fora fica transparente (alfa 0),
 * na borda o alfa é a parte do pixel coberta (16 amostras). */
static void cantos(uint32_t *px)
{
    const int r = 7;
    for (int y = 0; y < CA; y++)
        for (int x = 0; x < CL; x++) {
            int cx = x < r ? r : x >= CL - r ? CL - r : x, cy = y < r ? r : y >= CA - r ? CA - r : y;
            int n = 0;
            for (int k = 0; k < 16; k++) {
                int sx = x * 4 + (k & 3), sy = y * 4 + (k >> 2);
                int dx = sx - cx * 4, dy = sy - cy * 4;
                if ((x >= r && x < CL - r) || (y >= r && y < CA - r) || dx * dx + dy * dy <= r * r * 16) n++;
            }
            uint32_t a = n == 16 ? 255 : (uint32_t)n * 16;
            px[y * CL + x] = n ? (px[y * CL + x] & 0xFFFFFF) | a << 24 : 0;
        }
}

/* Dentro do retângulo arredondado (x0,y0)-(x1,y1) com raio r? Medido no
 * centro do pixel, em meios pixels, para os cantos ficarem simétricos. */
static bool no_arredondado(int x, int y, int x0, int y0, int x1, int y1, int r)
{
    int px = 2 * x + 1, py = 2 * y + 1;
    int cx = px < 2 * (x0 + r) ? 2 * (x0 + r) : px > 2 * (x1 - r) ? 2 * (x1 - r) : px;
    int cy = py < 2 * (y0 + r) ? 2 * (y0 + r) : py > 2 * (y1 - r) ? 2 * (y1 - r) : py;
    return px >= 2 * x0 && py >= 2 * y0 && px <= 2 * x1 && py <= 2 * y1 &&
           (px - cx) * (px - cx) + (py - cy) * (py - cy) <= 4 * r * r;
}

/* A borda de 1 pixel, que segue o arredondado: o que está no retângulo de
 * fora e não no de dentro. */
static void borda(jan_t *s, uint32_t cor)
{
    for (int y = 0; y < CA; y++)
        for (int x = 0; x < CL; x++)
            if (no_arredondado(x, y, 0, 0, CL, CA, 7) && !no_arredondado(x, y, 1, 1, CL - 1, CA - 1, 6))
                s->px[y * CL + x] = cor;
}

static uint32_t *criar_frente(int naipe, int valor)
{
    uint32_t *px = aloca(CL * CA * 4);
    if (!px) return NULL;
    jan_t s = { -1, px, CL, CA };
    uint32_t cor = vermelho(naipe) ? COR_VERMELHA : COR_PRETA;
    g_retangulo(&s, 0, 0, CL, CA, 0xFFFFFC);
    borda(&s, 0x9AA3AC);
    /* O canto: o valor (em "negrito") e um naipe pequeno; repetido embaixo. */
    const char *t = nome_valor[valor];
    g_texto(&s, 4, 3, t, cor);
    g_texto(&s, 5, 3, t, cor);
    naipe_desenhar(&s, naipe, 9, 25, 5, cor);
    int lt = g_largura_texto(t);
    g_texto(&s, CL - 5 - lt, CA - 19, t, cor);
    g_texto(&s, CL - 4 - lt, CA - 19, t, cor);
    naipe_desenhar(&s, naipe, CL - 10, CA - 27, 5, cor);
    if (valor == 1) {
        naipe_desenhar(&s, naipe, CL / 2, CA / 2 - 6, 15, cor);
        g_desenho(&s, CL / 2 - 8, CA / 2 + 14, sp_banana, 8, 12, paleta, 2);   /* o ás leva uma banana */
    } else if (valor <= 10) {
        for (int k = 0; k < valor; k++) {
            int x = 24 + pips[valor][k][0] * 10, y = 18 + pips[valor][k][1] * 9 / 2;
            naipe_desenhar(&s, naipe, x, y, 5, cor);
        }
    } else {
        /* Figuras: o sagui num quadro amarelo-claro, com um enfeite por
         * figura (valete: banana; dama: flor; rei: coroa). */
        g_retangulo(&s, 15, 13, CL - 30, CA - 26, 0xFFF3B0);
        g_moldura(&s, 15, 13, CL - 30, CA - 26, cor);
        g_desenho(&s, CL / 2 - 16, 26, sp_sagui, 16, 16, paleta, 2);
        if (valor == 13) g_desenho(&s, CL / 2 - 7, 18, sp_coroa, 7, 5, paleta, 2);
        else if (valor == 12) g_desenho(&s, CL / 2 + 4, 22, sp_flor, 5, 5, paleta, 2);
        else g_desenho(&s, CL / 2 + 10, 40, sp_banana, 8, 12, paleta, 1);
        naipe_desenhar(&s, naipe, 21, 19, 4, cor);
        naipe_desenhar(&s, naipe, CL - 22, CA - 20, 4, cor);
    }
    cantos(px);
    return px;
}

static uint32_t *criar_verso(void)
{
    uint32_t *px = aloca(CL * CA * 4);
    if (!px) return NULL;
    jan_t s = { -1, px, CL, CA };
    g_retangulo(&s, 0, 0, CL, CA, 0xFFFFFC);
    /* Losangos verde-escuro e verde-claro, e uma banana no meio. */
    for (int y = 4; y < CA - 4; y++)
        for (int x = 4; x < CL - 4; x++) {
            int d = ((x + y) / 6 + (x - y + 200) / 6) & 1;
            px[y * CL + x] = d ? 0x1F7A3A : 0x2E9E52;
        }
    g_moldura(&s, 4, 4, CL - 8, CA - 8, COR_BANANA);
    g_retangulo(&s, CL / 2 - 17, CA / 2 - 21, 34, 42, 0x1F7A3A);
    g_moldura(&s, CL / 2 - 17, CA / 2 - 21, 34, 42, COR_BANANA);
    g_desenho(&s, CL / 2 - 12, CA / 2 - 18, sp_banana, 8, 12, paleta, 3);
    borda(&s, 0x9AA3AC);
    cantos(px);
    return px;
}

static const uint32_t *sprite(carta_t c)
{
    if (!ABERTA(c)) { if (!spr_verso) spr_verso = criar_verso(); return spr_verso; }
    uint32_t **p = &spr_frente[NAIPE(c)][VALOR(c)];
    if (!*p) *p = criar_frente(NAIPE(c), VALOR(c));
    return *p;
}

static void carta_desenhar(carta_t c, int x, int y)
{
    const uint32_t *s = sprite(c);
    if (s) g_imagem(jan, x, y, s, CL, CA);
}

/* ------------------------------------------------------------ a mesa */

static void feltro_criar(void)
{
    /* Verde de floresta, mais claro no meio, com pintinhas (um sorteio fixo)
     * para parecer pano. */
    uint32_t r = 0x1234567u;
    for (int y = 0; y < ALT; y++)
        for (int x = 0; x < LARG; x++) {
            int dx = x - LARG / 2, dy = y - ALT / 2;
            int d = (dx * dx / 4 + dy * dy) / 600;
            if (d > 120) d = 120;
            uint32_t c = mistura(0x2E8B4A, 0x14522A, d * 2);
            r ^= r << 13; r ^= r >> 17; r ^= r << 5;
            if ((r & 31) == 0) c = mistura(c, 0x000000, 24);
            else if ((r & 31) == 1) c = mistura(c, 0xFFFFFF, 14);
            feltro[y * LARG + x] = c;
        }
}

static int x_col(int c) { return MARGEM_X + c * PASSO_X; }

/* Os passos entre as cartas de uma coluna: fechadas mais juntas que as
 * abertas; se a coluna não cabe até o monte, os dois diminuem. */
static void passos(int c, int *fechada, int *aberta)
{
    int nf = 0, na = 0;
    for (int i = 0; i + 1 < mesa.n[c]; i++) { if (ABERTA(mesa.col[c][i])) na++; else nf++; }
    int disp = BASE_Y - 12 - TOPO_Y - CA;
    *fechada = 8; *aberta = 22;
    while (nf * *fechada + na * *aberta > disp && *aberta > 9) (*aberta)--;
    while (nf * *fechada + na * *aberta > disp && *fechada > 3) (*fechada)--;
}

static int y_carta(int c, int i)
{
    int pf, pa, y = TOPO_Y;
    passos(c, &pf, &pa);
    for (int k = 0; k < i; k++) y += ABERTA(mesa.col[c][k]) ? pa : pf;
    return y;
}

/* Que carta da coluna c está no ponto y (a de cima vence); -1 = nenhuma. */
static int carta_em(int c, int y)
{
    for (int i = mesa.n[c] - 1; i >= 0; i--) {
        int yi = y_carta(c, i);
        if (y >= yi && y < yi + CA) return i;
    }
    return -1;
}

static int col_em(int x)
{
    if (x < MARGEM_X) return -1;
    int c = (x - MARGEM_X) / PASSO_X;
    return c < COLUNAS && x < x_col(c) + CL ? c : -1;
}

static void monte_ret(int *x, int *y, int *l)
{
    int rodadas = (mesa.n_monte + 9) / 10;
    *l = CL + (rodadas > 0 ? rodadas - 1 : 0) * 10;
    *x = LARG - MARGEM_X - *l;
    *y = BASE_Y;
}

static void desenhar_barra(void)
{
    for (int y = 0; y < BARRA_A; y++) g_retangulo(jan, 0, y, LARG, 1, mistura(0x1F6B2A, 0x0E3E1A, y * 256 / BARRA_A));
    g_retangulo(jan, 0, BARRA_A - 2, LARG, 2, COR_BANANA);
    formatar_texto(rotulo_naipes, sizeof rotulo_naipes, "Naipes: %d", naipes);
    botao_desenhar(jan, &bt_nova);
    botao_desenhar(jan, &bt_desfazer);
    botao_desenhar(jan, &bt_dica);
    botao_desenhar(jan, &bt_naipes);
    char t[64];
    formatar_texto(t, sizeof t, "Pontos: %d   Jogadas: %d", mesa.pontos, mesa.jogadas);
    g_texto(jan, LARG - 12 - g_largura_texto(t), 9, t, 0xFFF3B0);
}

/* Desenha a mesa inteira, menos as cartas da coluna "sem_col" a partir de
 * "sem_i" (as que estão sendo arrastadas). */
static void desenhar_mesa(int sem_col, int sem_i)
{
    memcpy(jan->px, feltro, (size_t)LARG * ALT * 4);
    desenhar_barra();
    for (int c = 0; c < COLUNAS; c++) {
        int x = x_col(c);
        g_moldura(jan, x + 2, TOPO_Y + 2, CL - 4, CA - 4, 0x5CC46E);    /* o lugar da coluna */
        int ate = c == sem_col ? sem_i : mesa.n[c];
        for (int i = 0; i < ate; i++) carta_desenhar(mesa.col[c][i], x, y_carta(c, i));
    }
    /* As sequências feitas: o rei de cada uma, em leque, à esquerda. */
    for (int k = 0; k < mesa.feitas && k < SEQUENCIAS; k++)
        carta_desenhar(CARTA(mesa.naipe_feita[k], 13, 1), MARGEM_X + k * 22, BASE_Y);
    if (!mesa.feitas) g_moldura(jan, MARGEM_X + 2, BASE_Y + 2, CL - 4, CA - 4, 0x5CC46E);
    /* O monte: um verso por rodada que falta. */
    int mx, my, ml;
    monte_ret(&mx, &my, &ml);
    for (int k = 0; k < (mesa.n_monte + 9) / 10; k++) carta_desenhar(CARTA(0, 1, 0), mx + k * 10, my);
    if (aviso[0]) {
        int l = g_largura_texto(aviso) + 20, x = (LARG - l) / 2, y = BASE_Y + CA / 2 - 14;
        g_retangulo(jan, x, y, l, 28, 0x0E3E1A);
        g_moldura(jan, x, y, l, 28, COR_BANANA);
        g_texto(jan, x + 10, y + 6, aviso, 0xFFF3B0);
    }
}

static void desenhar_vitoria(void)
{
    int l = 360, a = 150, x = (LARG - l) / 2, y = 150;
    /* Bananas espalhadas pela mesa, em festa. */
    uint32_t r = 0xBA7A7Au;
    for (int k = 0; k < 24; k++) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        g_desenho(jan, (int)(r % (LARG - 24)), TOPO_Y + (int)((r >> 12) % (BASE_Y - TOPO_Y - 24)), sp_banana, 8, 12, paleta, 3);
    }
    g_retangulo(jan, x, y, l, a, 0xFFF3B0);
    g_moldura(jan, x, y, l, a, 0xB07A12);
    g_moldura(jan, x + 3, y + 3, l - 6, a - 6, 0xB07A12);
    g_desenho(jan, x + 18, y + 22, sp_sagui, 16, 16, paleta, 4);
    g_desenho(jan, x + 70, y + 70, sp_cacho, 12, 12, paleta, 5);
    g_texto(jan, x + 150, y + 36, "Você ganhou!", COR_TEXTO);
    g_texto(jan, x + 151, y + 36, "Você ganhou!", COR_TEXTO);
    char t[48];
    formatar_texto(t, sizeof t, "%d pontos em %d jogadas", mesa.pontos, mesa.jogadas);
    g_texto(jan, x + 150, y + 64, t, COR_TEXTO);
    g_texto(jan, x + 150, y + 96, "F2: outra partida", COR_TEXTO_FRACO);
}

static void redesenhar(void)
{
    desenhar_mesa(-1, 0);
    if (MESA_GANHOU(&mesa)) desenhar_vitoria();
    jan_mostrar(jan);
}

/* ------------------------------------------------------------ registro (para os testes) */

static void registrar_estado(const char *acao)
{
    registrar("paciencia: %s -> monte=%d feitas=%d pontos=%d jogadas=%d", acao, mesa.n_monte, mesa.feitas,
              mesa.pontos, mesa.jogadas);
}

/* ------------------------------------------------------------ jogadas */

static void guardar_para_desfazer(void)
{
    if (!desfazer) return;
    if (qtd_desfazer == MAX_DESFAZER) { memmove(desfazer, desfazer + 1, sizeof(mesa_t) * (MAX_DESFAZER - 1)); qtd_desfazer--; }
    desfazer[qtd_desfazer++] = mesa;
}

static void avisar(const char *t) { copia_texto(aviso, t, sizeof aviso); }

static void depois_da_jogada(int feitas_antes, const char *acao)
{
    if (mesa.feitas > feitas_antes) {
        static const nota_t viva[] = { { 659, 70 }, { 784, 70 }, { 1047, 140 } };
        tocar(viva, 3, BIP_SUBSTITUIR);
        registrar("paciencia: sequencia completa (%d de 8)", mesa.feitas);
    }
    ordem_dica = 0;
    redesenhar();
    registrar_estado(acao);
    if (MESA_GANHOU(&mesa)) registrar("paciencia: ganhou (pontos %d)", mesa.pontos);
}

static bool mover(int de, int i, int para)
{
    mesa_t antes = mesa;
    int feitas = mesa.feitas;
    if (!mesa_mover(&mesa, de, i, para)) return false;
    mesa_t depois = mesa;
    mesa = antes;
    guardar_para_desfazer();
    mesa = depois;
    aviso[0] = 0;
    char acao[48];
    formatar_texto(acao, sizeof acao, "jogada col %d carta %d -> col %d", de, i, para);
    depois_da_jogada(feitas, acao);
    return true;
}

static void dar(void)
{
    mesa_t antes = mesa;
    int feitas = mesa.feitas;
    int r = mesa_dar(&mesa);
    if (r == DAR_SEM_CARTAS) { avisar("O monte acabou."); redesenhar(); return; }
    if (r == DAR_COLUNA_VAZIA) { avisar("Preencha as colunas vazias antes de dar cartas."); redesenhar(); return; }
    mesa_t depois = mesa;
    mesa = antes;
    guardar_para_desfazer();
    mesa = depois;
    aviso[0] = 0;
    static const nota_t som[] = { { 523, 30 }, { 659, 30 } };
    tocar(som, 2, BIP_SUBSTITUIR);
    depois_da_jogada(feitas, "dar");
}

static void voltar(void)
{
    if (!qtd_desfazer) { avisar("Nada para desfazer."); redesenhar(); return; }
    mesa = desfazer[--qtd_desfazer];
    aviso[0] = 0;
    redesenhar();
    registrar_estado("desfazer");
}

static void nova(uint32_t s)
{
    if (!s) { bytes_aleatorios(&s, sizeof s); s = (s ^ tempo_ms()) | 1; }
    semente = s;
    mesa_nova(&mesa, naipes, semente);
    qtd_desfazer = 0;
    ordem_dica = 0;
    aviso[0] = 0;
    redesenhar();
    registrar("paciencia: partida nova (naipes %d, semente %u)", naipes, semente);
    registrar_estado("nova");
}

/* Mostra uma dica: contorna a carta e o destino em amarelo-banana. */
static void dica(void)
{
    jogada_t jg;
    if (!mesa_dica(&mesa, ordem_dica++, &jg)) {
        avisar(mesa.n_monte ? "Sem jogadas: dê mais cartas (clique no monte)." : "Sem jogadas.");
        redesenhar();
        registrar("paciencia: sem dica");
        return;
    }
    aviso[0] = 0;
    redesenhar();
    int x1 = x_col(jg.de), y1 = y_carta(jg.de, jg.i);
    int x2 = x_col(jg.para), y2 = mesa.n[jg.para] ? y_carta(jg.para, mesa.n[jg.para] - 1) : TOPO_Y;
    for (int k = 0; k < 3; k++) {
        g_moldura(jan, x1 - k, y1 - k, CL + 2 * k, CA + 2 * k, COR_BANANA);
        g_moldura(jan, x2 - k, y2 - k, CL + 2 * k, CA + 2 * k, COR_BANANA);
    }
    jan_mostrar(jan);
    registrar("paciencia: dica col %d carta %d (%d,%d) -> col %d (%d,%d)", jg.de, jg.i, x1 + CL / 2, y1 + 10,
              jg.para, x2 + CL / 2, y2 + CA / 2);
}

/* ------------------------------------------------------------ mouse: arrastar */

static bool apertado, arrastando;
static int ar_col, ar_i, ar_dx, ar_dy, ar_x, ar_y, ar_x0, ar_y0;

static int altura_pilha(void)
{
    int n = mesa.n[ar_col] - ar_i;
    return CA + (n - 1) * 22;
}

static void desenhar_pilha(int x, int y)
{
    for (int i = ar_i; i < mesa.n[ar_col]; i++) carta_desenhar(mesa.col[ar_col][i], x, y + (i - ar_i) * 22);
}

static void mostrar_uniao(int xa, int ya, int xb, int yb, int l, int a)
{
    int x0 = xa < xb ? xa : xb, y0 = ya < yb ? ya : yb;
    int x1 = (xa > xb ? xa : xb) + l, y1 = (ya > yb ? ya : yb) + a;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > LARG) x1 = LARG;
    if (y1 > ALT) y1 = ALT;
    if (x1 > x0 && y1 > y0) jan_mostrar_area(jan, x0, y0, x1 - x0, y1 - y0);
}

static void comecar_arrasto(void)
{
    arrastando = true;
    desenhar_mesa(ar_col, ar_i);
    memcpy(cena, jan->px, (size_t)LARG * ALT * 4);
}

static void arrastar_para(int mx, int my)
{
    int nx = mx - ar_dx, ny = my - ar_dy, a = altura_pilha();
    /* Apaga onde a pilha estava (copiando a cena) e desenha no lugar novo. */
    for (int y = ar_y; y < ar_y + a; y++) {
        if (y < 0 || y >= ALT) continue;
        int x0 = ar_x < 0 ? 0 : ar_x, x1 = ar_x + CL > LARG ? LARG : ar_x + CL;
        if (x1 > x0) memcpy(jan->px + y * LARG + x0, cena + y * LARG + x0, (size_t)(x1 - x0) * 4);
    }
    desenhar_pilha(nx, ny);
    mostrar_uniao(ar_x, ar_y, nx, ny, CL, a);
    ar_x = nx; ar_y = ny;
}

static void clique(const evento_t *e)
{
    if (MESA_GANHOU(&mesa)) return;
    int mx, my, ml;
    monte_ret(&mx, &my, &ml);
    if (e->x >= mx && e->x < mx + ml && e->y >= my && e->y < my + CA) { dar(); return; }
    int c = col_em(e->x);
    if (c < 0) return;
    int i = carta_em(c, e->y);
    if (i < 0) return;
    if (!mesa_pode_pegar(&mesa, c, i)) {
        if (ABERTA(mesa.col[c][i])) { avisar("Só sai junto uma escadinha do mesmo naipe."); redesenhar(); }
        return;
    }
    apertado = true;
    arrastando = false;
    ar_col = c; ar_i = i;
    ar_x = ar_x0 = x_col(c);
    ar_y = ar_y0 = y_carta(c, i);
    ar_dx = e->x - ar_x;
    ar_dy = e->y - ar_y;
}

static void soltar(const evento_t *e)
{
    if (!apertado) return;
    apertado = false;
    if (!arrastando) {
        /* Foi só um clique: leva para o melhor lugar, se houver. */
        int t = mesa_melhor_destino(&mesa, ar_col, ar_i);
        if (t < 0 || !mover(ar_col, ar_i, t)) { avisar("Essa carta não tem para onde ir."); redesenhar(); }
        return;
    }
    arrastando = false;
    /* A coluna em que cai o meio da carta de cima da pilha. */
    int t = col_em(e->x - ar_dx + CL / 2);
    if (t < 0 || !mover(ar_col, ar_i, t)) {
        if (t >= 0 && t != ar_col) avisar("Ali não: precisa de uma carta um ponto maior.");
        redesenhar();
    }
}

/* ------------------------------------------------------------ principal */

static void posicionar_botoes(void)
{
    bt_nova = (botao_t){ 10, 6, 70, 22, "Nova", false };
    bt_desfazer = (botao_t){ 88, 6, 90, 22, "Desfazer", false };
    bt_dica = (botao_t){ 186, 6, 60, 22, "Dica", false };
    bt_naipes = (botao_t){ 254, 6, 100, 22, rotulo_naipes, false };
}

int principal(int argc, char **argv)
{
    bool quase = false;
    for (int k = 1; k < argc; k++) {
        if (compara_texto(argv[k], "--naipes") == 0 && k + 1 < argc) naipes = texto_para_int(argv[++k]);
        else if (compara_texto(argv[k], "--semente") == 0 && k + 1 < argc) semente = (uint32_t)texto_para_int(argv[++k]);
        else if (compara_texto(argv[k], "--quase") == 0) quase = true;
        else { imprimir("uso: paciencia [--naipes 1|2|4] [--semente N] [--quase]\n"); return 2; }
    }
    if (naipes != 2 && naipes != 4) naipes = 1;
    feltro = aloca((size_t)LARG * ALT * 4);
    cena = aloca((size_t)LARG * ALT * 4);
    desfazer = aloca(sizeof(mesa_t) * MAX_DESFAZER);
    jan = jan_abrir("Paciência Banana", LARG, ALT);
    if (!jan || !feltro || !cena) return 1;
    feltro_criar();
    posicionar_botoes();
    registrar("paciencia: layout x0=%d passo=%d topo=%d carta=%dx%d base=%d", MARGEM_X, PASSO_X, TOPO_Y, CL, CA, BASE_Y);
    if (quase) {
        mesa_quase_ganha(&mesa);
        redesenhar();
        registrar_estado("quase");
    } else nova(semente);
    registrar("paciencia: pronto (naipes %d)", naipes);

    evento_t e;
    for (;;) {
        if (proximo_evento(jan->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        if (botao_tratar(jan, &bt_nova, &e)) { nova(0); continue; }
        if (botao_tratar(jan, &bt_desfazer, &e)) { voltar(); continue; }
        if (botao_tratar(jan, &bt_dica, &e)) { dica(); continue; }
        if (botao_tratar(jan, &bt_naipes, &e)) { naipes = naipes == 1 ? 2 : naipes == 2 ? 4 : 1; nova(0); continue; }
        switch (e.tipo) {
        case EV_CLIQUE: if (e.y > BARRA_A) clique(&e); break;
        case EV_MOVER:
            if (!apertado) break;
            if (!arrastando && (abs_i(e.x - ar_dx - ar_x0) > 3 || abs_i(e.y - ar_dy - ar_y0) > 3)) comecar_arrasto();
            if (arrastando) arrastar_para(e.x, e.y);
            break;
        case EV_SOLTOU: soltar(&e); break;
        case EV_TECLA:
            if (e.tecla == TECLA_F2) nova(0);
            else if (e.tecla == 26 || e.tecla == 'u' || e.tecla == 'U') voltar();       /* 26 = Ctrl+Z */
            else if (e.tecla == 'd' || e.tecla == 'D' || e.tecla == ' ') dar();
            else if (e.tecla == 'h' || e.tecla == 'H') dica();
            break;
        default: break;
        }
    }
    registrar("paciencia: fechado");
    jan_fechar(jan);
    return 0;
}
