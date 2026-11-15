/* vidro.c - efeitos do visual moderno (H74): vidro, sombra e cantos.
 *
 * Vidro: o que está atrás de uma barra é desfocado e misturado com uma cor.
 * O desfoque é uma média de caixa (cada pixel vira a média dos vizinhos num
 * quadrado de (2R+1) x (2R+1)), feita em duas passadas: primeiro nas linhas,
 * depois nas colunas, com somas corridas (cada pixel novo soma o que entra e
 * tira o que sai da janela), então o custo não depende do raio. Como a
 * imagem de trás tem de estar pronta, o vidro é pintado na hora em que a
 * janela (ou a barra) é desenhada, por cima do que já foi composto. Quem
 * redesenha garante que o recorte inclua R pixels em volta de cada vidro
 * (janelas.c, expandir_vidro), senão a borda do desfoque leria pixels velhos.
 *
 * Sombra: um degradê escuro em volta da janela, mais forte perto dela e um
 * pouco deslocado para baixo, como uma luz vinda de cima.
 *
 * Cantos arredondados: uma tabela diz, para cada uma das primeiras linhas,
 * quantos pixels pular em cada ponta (um quarto de círculo de raio RAIO). */
#include "vidro.h"
#include "memoria.h"
#include "suave.h"


static uint32_t *buf;            /* linhas desfocadas (largura da tela + margem) x altura máxima */
static int buf_l, buf_a;

bool vidro_iniciar(int largura_tela)
{
    buf_l = largura_tela + 2 * VIDRO_RAIO + 8;
    buf_a = VIDRO_ALT_MAX + 2 * VIDRO_RAIO;
    buf = kmalloc((uint32_t)(buf_l * buf_a) * 4);
    return buf != NULL;
}

/* Mistura a e b: k/256 de b. */
static inline uint32_t mistura(uint32_t a, uint32_t b, int k)
{
    uint32_t rb = ((a & 0xFF00FF) * (uint32_t)(256 - k) + (b & 0xFF00FF) * (uint32_t)k) >> 8;
    uint32_t g = ((a & 0x00FF00) * (uint32_t)(256 - k) + (b & 0x00FF00) * (uint32_t)k) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

/* Escurece um pixel: fica com (256 - k)/256 do brilho. */
static inline uint32_t escurecer(uint32_t p, int k)
{
    uint32_t rb = ((p & 0xFF00FF) * (uint32_t)(256 - k)) >> 8;
    uint32_t g = ((p & 0x00FF00) * (uint32_t)(256 - k)) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

/* Canto arredondado suavizado (H88): quanto do pixel (de 0 a 256) fica
 * dentro de um quarto de círculo de raio 8, contado com 8 x 8 amostras por
 * pixel (tools: a conta está no comentário de vidro_cobertura). Linha 0 e
 * coluna 0 são as da ponta. Antes (H74) o canto só pulava pixels inteiros,
 * e a borda ficava em escada. */
static const uint16_t canto[VIDRO_CANTO][VIDRO_CANTO] = {
    { 0, 0, 0, 0, 48, 156, 216, 256 },
    { 0, 0, 12, 156, 256, 256, 256, 256 },
    { 0, 12, 196, 256, 256, 256, 256, 256 },
    { 0, 156, 256, 256, 256, 256, 256, 256 },
    { 48, 256, 256, 256, 256, 256, 256, 256 },
    { 156, 256, 256, 256, 256, 256, 256, 256 },
    { 216, 256, 256, 256, 256, 256, 256, 256 },
    { 256, 256, 256, 256, 256, 256, 256, 256 },
};

/* Cobertura do pixel (i, j) num retângulo com cantos arredondados: 256 =
 * todo dentro. Para cada pixel, cobertura = amostras dentro do círculo / 64. */
int vidro_cobertura(int i, int j, int x, int y, int l, int a, int cantos)
{
    int lin = -1;
    if ((cantos & CANTOS_EM_CIMA) && j - y < VIDRO_CANTO) lin = j - y;
    else if ((cantos & CANTOS_EMBAIXO) && y + a - 1 - j < VIDRO_CANTO) lin = y + a - 1 - j;
    if (lin < 0) return 256;
    int col = i - x < VIDRO_CANTO ? i - x : (x + l - 1 - i < VIDRO_CANTO ? x + l - 1 - i : -1);
    if (col < 0) return 256;
    return canto[lin][col];
}

/* Cobertura com raio qualquer (H88b, a doca usa raio maior): o raio 8 usa a
 * tabela; os outros contam 4 x 4 amostras (suave.h). */
static int cobertura_r(int i, int j, int x, int y, int l, int a, int cantos, int r)
{
    if (r == VIDRO_CANTO) return vidro_cobertura(i, j, x, y, l, a, cantos);
    int li = i - x, lj = j - y;
    if (lj < r && !(cantos & CANTOS_EM_CIMA)) return 256;
    if (lj >= a - r && !(cantos & CANTOS_EMBAIXO)) return 256;
    return suave_amostras_ret(li, lj, l, a, r) * 16;
}

int vidro_canto(int r)
{
    int n = 0;
    if (r >= 0 && r < VIDRO_CANTO) while (n < VIDRO_CANTO && canto[r][n] == 0) n++;
    return n;
}

/* Brilho do vidro (H88): uma linha clara na beira de cima e um degradê que
 * clareia o terço de cima, como luz batendo num vidro curvo. Só nos efeitos
 * completos: nos leves a barra continua na cor pura. */
/* Quanto de branco o brilho põe na linha d (contada do topo) de um vidro de
 * altura a, calculado uma vez por linha (H93; antes era por pixel). */
static inline int brilho_k(int d, int a)
{
    if (!vidro_efeitos) return 0;
    if (d == 0) return 46;
    int t = a / 3 > 0 ? a / 3 : 1;
    return d < t ? 22 * (t - d) / t : 0;
}

/* 1/n em 16 bits, para trocar as três divisões por pixel do desfoque por
 * multiplicações (H93: no QEMU a divisão custa caro). Com n de 1 a 32, o
 * erro fica abaixo de meio tom. */
static uint32_t inverso[33];
static inline uint32_t media_de(uint32_t sr, uint32_t sg, uint32_t sb, int n)
{
    uint32_t r = inverso[n];
    return (((sr * r) >> 16) << 16) | (((sg * r) >> 16) << 8) | ((sb * r) >> 16);
}

void vidro_pintar(superficie_t *s, int x, int y, int l, int a, uint32_t tinta, int k, int cantos)
{
    vidro_pintar_r(s, x, y, l, a, tinta, k, cantos, VIDRO_CANTO);
}

void vidro_pintar_r(superficie_t *s, int x, int y, int l, int a, uint32_t tinta, int k, int cantos, int raio)
{
    if (2 * raio > a) raio = a / 2;
    if (2 * raio > l) raio = l / 2;
    int x0 = x < s->cx0 ? s->cx0 : x, y0 = y < s->cy0 ? s->cy0 : y;
    int x1 = x + l > s->cx1 ? s->cx1 : x + l, y1 = y + a > s->cy1 ? s->cy1 : y + a;
    if (x0 >= x1 || y0 >= y1) return;
    if (!inverso[1]) for (int n = 1; n <= 32; n++) inverso[n] = (65536u + (uint32_t)n / 2) / (uint32_t)n;
    /* H93: a cobertura só é calculada nos cantos. Fora deles (quase tudo),
     * o pixel é todo do vidro. */
    int canto_cima = (cantos & CANTOS_EM_CIMA) ? y + raio : y;
    int canto_baixo = (cantos & CANTOS_EMBAIXO) ? y + a - raio : y + a;
    if (!buf || a > VIDRO_ALT_MAX || !vidro_efeitos) {
        /* Sem desfoque (efeitos leves, ou vidro alto): só a cor, um pouco transparente. */
        for (int j = y0; j < y1; j++) {
            uint32_t *p = s->px + j * s->larg;
            int g = brilho_k(j - y, a);
            bool linha_canto = j < canto_cima || j >= canto_baixo;
            for (int i = x0; i < x1; i++) {
                int cob = 256;
                if (linha_canto && (i < x + raio || i >= x + l - raio)) {
                    cob = cobertura_r(i, j, x, y, l, a, cantos, raio);
                    if (!cob) continue;
                }
                uint32_t c = vidro_efeitos ? mistura(p[i], tinta, k) : tinta;
                if (g) c = mistura(c, 0xFFFFFF, g);
                p[i] = cob == 256 ? c : mistura(p[i], c, cob);
            }
        }
        return;
    }
    const int R = VIDRO_RAIO;
    /* A fonte: o destino com R de margem, dentro do recorte. */
    int sx0 = x0 - R < s->cx0 ? s->cx0 : x0 - R, sx1 = x1 + R > s->cx1 ? s->cx1 : x1 + R;
    int sy0 = y0 - R < s->cy0 ? s->cy0 : y0 - R, sy1 = y1 + R > s->cy1 ? s->cy1 : y1 + R;
    int W = sx1 - sx0, H = sy1 - sy0;
    if (W > buf_l || H > buf_a) return;
    /* Passada 1: cada linha da fonte, média horizontal, para buf. */
    for (int j = 0; j < H; j++) {
        const uint32_t *o = s->px + (sy0 + j) * s->larg + sx0;
        uint32_t *d = buf + j * W;
        uint32_t sr = 0, sg = 0, sb = 0;
        int n = 0;
        /* Janela inicial: [0, R] (o que está à esquerda de 0 não existe). */
        for (int i = 0; i <= R && i < W; i++) { sr += (o[i] >> 16) & 255; sg += (o[i] >> 8) & 255; sb += o[i] & 255; n++; }
        for (int i = 0; i < W; i++) {
            d[i] = media_de(sr, sg, sb, n);
            int entra = i + R + 1, sai = i - R;
            if (entra < W) { sr += (o[entra] >> 16) & 255; sg += (o[entra] >> 8) & 255; sb += o[entra] & 255; n++; }
            if (sai >= 0) { sr -= (o[sai] >> 16) & 255; sg -= (o[sai] >> 8) & 255; sb -= o[sai] & 255; n--; }
        }
    }
    /* O brilho de cada linha, uma vez só (a <= VIDRO_ALT_MAX aqui). */
    uint8_t gl[VIDRO_ALT_MAX];
    for (int d = 0; d < a; d++) gl[d] = (uint8_t)brilho_k(d, a);
    /* Passada 2: cada coluna do destino, média vertical sobre buf, misturada com a tinta. */
    for (int i = x0; i < x1; i++) {
        int c = i - sx0;
        bool coluna_canto = i < x + raio || i >= x + l - raio;
        uint32_t sr = 0, sg = 0, sb = 0;
        int n = 0;
        int jj0 = y0 - sy0;                          /* linha do buf do primeiro destino */
        for (int j = jj0 - R; j <= jj0 + R; j++) {
            if (j < 0 || j >= H) continue;
            uint32_t v = buf[j * W + c];
            sr += (v >> 16) & 255; sg += (v >> 8) & 255; sb += v & 255; n++;
        }
        for (int yy = y0; yy < y1; yy++) {
            int jb = yy - sy0;
            int cob = coluna_canto && (yy < canto_cima || yy >= canto_baixo) ? cobertura_r(i, yy, x, y, l, a, cantos, raio) : 256;
            if (cob) {
                uint32_t cor = mistura(media_de(sr, sg, sb, n), tinta, k);
                int g = gl[yy - y];
                if (g) cor = mistura(cor, 0xFFFFFF, g);
                uint32_t *p = &s->px[yy * s->larg + i];
                *p = cob == 256 ? cor : mistura(*p, cor, cob);
            }
            int entra = jb + R + 1, sai = jb - R;
            if (entra < H) { uint32_t v = buf[entra * W + c]; sr += (v >> 16) & 255; sg += (v >> 8) & 255; sb += v & 255; n++; }
            if (sai >= 0) { uint32_t v = buf[sai * W + c]; sr -= (v >> 16) & 255; sg -= (v >> 8) & 255; sb -= v & 255; n--; }
        }
    }
}

/* A sombra custa pouco (só a faixa em volta), então fica também nos efeitos
 * leves (H88): sem ela, a janela era um retângulo chapado sobre o fundo. */
void vidro_sombra(superficie_t *s, int x, int y, int l, int a)
{
    vidro_sombra_r(s, x, y, l, a, VIDRO_CANTO);
}

/* Raiz quadrada inteira (valores pequenos). */
static int raiz(int v)
{
    int r = 0, b = 1 << 30;                     /* bit a bit: até 16 passos */
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* H88b: a sombra segue o retângulo arredondado (antes era quadrada: nos
 * cantos de fora da curva o fundo ficava claro, cercado de sombra, e a
 * quina aparecia). Distância em oitavos de pixel até o retângulo de raio r,
 * deslocado para baixo; os pixels todos cobertos pelo vidro são pulados. */
/* H93: a sombra por tabela. O escurecimento depende só da distância até a
 * borda (em oitavos de pixel): uma tabela de 96 posições para as bordas
 * retas, e uma por raio para os cantos (a raiz era calculada em cada pixel
 * de canto, a cada quadro). Os raios em uso são poucos (8, 14, 18), e as
 * tabelas ficam guardadas. */
#define SOMBRA_D (VIDRO_SOMBRA * 8)
#define SOMBRA_RMAX 20
#define SOMBRA_M (VIDRO_SOMBRA + SOMBRA_RMAX)
static uint8_t sombra_reta[SOMBRA_D];
static struct { int r; uint8_t t[SOMBRA_M][SOMBRA_M]; } sombra_canto[4];
static int sombra_cantos_qtd;

static uint8_t sombra_k(int d)
{
    if (d < 0) d = 0;
    if (d >= SOMBRA_D) return 0;
    int f = SOMBRA_D - d;
    return (uint8_t)(f * f * 90 / (SOMBRA_D * SOMBRA_D));
}

static const uint8_t (*tabela_canto(int r))[SOMBRA_M]
{
    if (!sombra_reta[0]) for (int d = 0; d < SOMBRA_D; d++) sombra_reta[d] = sombra_k(d);
    for (int k = 0; k < sombra_cantos_qtd; k++) if (sombra_canto[k].r == r) return sombra_canto[k].t;
    int k = sombra_cantos_qtd < 4 ? sombra_cantos_qtd++ : 3;
    sombra_canto[k].r = r;
    for (int m = 1; m <= SOMBRA_M; m++)
        for (int n = 1; n <= SOMBRA_M; n++) {
            int dx = 8 * m - 4, dy = 8 * n - 4;
            sombra_canto[k].t[m - 1][n - 1] = sombra_k(raiz(dx * dx + dy * dy) - r * 8);
        }
    return sombra_canto[k].t;
}

void vidro_sombra_r(superficie_t *s, int x, int y, int l, int a, int r)
{
    const int S = VIDRO_SOMBRA, desce = 4;
    if (2 * r > a) r = a / 2;
    if (2 * r > l) r = l / 2;
    if (r > SOMBRA_RMAX) r = SOMBRA_RMAX;
    if (r < 0) r = 0;
    const uint8_t (*canto_t)[SOMBRA_M] = tabela_canto(r);
    int bx0 = x - S, by0 = y - S + desce, bx1 = x + l + S, by1 = y + a + S + desce;
    int x0 = bx0 < s->cx0 ? s->cx0 : bx0, y0 = by0 < s->cy0 ? s->cy0 : by0;
    int x1 = bx1 > s->cx1 ? s->cx1 : bx1, y1 = by1 > s->cy1 ? s->cy1 : by1;
    /* retângulo interno (sem o raio), em pixels, já deslocado */
    int ix0 = x + r, ix1 = x + l - r, iy0 = y + desce + r, iy1 = y + desce + a - r;
    for (int j = y0; j < y1; j++) {
        uint32_t *p = s->px + j * s->larg;
        int n = j < iy0 ? iy0 - j : (j >= iy1 ? j - iy1 + 1 : 0);      /* pixels fora, na vertical */
        /* Debaixo da janela não há sombra: nas linhas do meio (longe dos
         * cantos), pula a largura toda; nas dos cantos, só o miolo. */
        int pula0 = x + r, pula1 = x + l - r;
        if (j >= y + r && j < y + a - r) { pula0 = x; pula1 = x + l; }
        bool linha_janela = j >= y && j < y + a;
        for (int i = x0; i < x1; i++) {
            if (linha_janela && i >= pula0 && i < pula1) { i = pula1 - 1; continue; }
            if (linha_janela && i >= x && i < x + l &&
                suave_amostras_ret(i - x, j - y, l, a, r) == 16) continue;   /* debaixo do vidro */
            int m = i < ix0 ? ix0 - i : (i >= ix1 ? i - ix1 + 1 : 0);
            int k;
            if (m && n) k = m <= SOMBRA_M && n <= SOMBRA_M ? canto_t[m - 1][n - 1] : 0;
            else {
                int d = 8 * (m + n) - 4 - r * 8;                         /* oitavos fora da borda */
                k = d < 0 ? sombra_reta[0] : d >= SOMBRA_D ? 0 : sombra_reta[d];
            }
            if (k) p[i] = escurecer(p[i], k);
        }
    }
}

bool vidro_efeitos = true;

/* Cantos de baixo das janelas (H88). O conteúdo da janela é copiado por
 * cima de tudo, então os cantos são feitos assim: antes de pintar a janela,
 * guarda os pixels de fundo dos dois cantos de baixo; depois de copiar o
 * conteúdo, mistura o fundo guardado de volta onde o canto fica de fora. */
static uint32_t fundo_canto[2][VIDRO_CANTO][VIDRO_CANTO];

void vidro_guardar_cantos(superficie_t *s, int x, int y, int l, int a)
{
    for (int lado = 0; lado < 2; lado++)
        for (int r = 0; r < VIDRO_CANTO; r++)
            for (int c = 0; c < VIDRO_CANTO; c++) {
                int px = lado ? x + l - 1 - c : x + c, py = y + a - 1 - r;
                if (px >= s->cx0 && px < s->cx1 && py >= s->cy0 && py < s->cy1)
                    fundo_canto[lado][r][c] = s->px[py * s->larg + px];
            }
}

void vidro_aplicar_cantos(superficie_t *s, int x, int y, int l, int a)
{
    for (int lado = 0; lado < 2; lado++)
        for (int r = 0; r < VIDRO_CANTO; r++)
            for (int c = 0; c < VIDRO_CANTO; c++) {
                int cob = canto[r][c];
                if (cob == 256) continue;
                int px = lado ? x + l - 1 - c : x + c, py = y + a - 1 - r;
                if (px >= s->cx0 && px < s->cx1 && py >= s->cy0 && py < s->cy1) {
                    uint32_t *p = &s->px[py * s->larg + px];
                    *p = mistura(fundo_canto[lado][r][c], *p, cob);
                }
            }
}
