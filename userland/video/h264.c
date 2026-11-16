/* h264.c - decodificador H.264 perfis Baseline, Main e High (CAVLC e CABAC, quadros I, P e B).
 *
 * É o vídeo da maioria dos MP4 (avc1.42001E, avc1.640028 e parecidos).
 * Veio do pull request #6 (Robson de Lima Barbosa); as tabelas que seguem o
 * FFmpeg estão em terceiros/ffmpeg-h264-tabelas (D043). O perfil High foi
 * acrescentado aqui, a partir do texto da Recomendação ITU-T H.264 (D048);
 * os comentários dessas partes dizem o item da norma de onde cada regra saiu.
 *
 * Cobre: SPS/PPS, fatias I e P, vários quadros de referência com janela deslizante e reordenação da lista, predição intra 4x4 e 16x16,
 * croma 4:2:0, compensação de movimento com quarto de pixel e o filtro de
 * desbloqueio; e o perfil Main: CABAC, quadros B (predição direta espacial
 * e temporal), predição ponderada explícita e implícita e saída na ordem de
 * exibição (POC); e o perfil High progressivo, 4:2:0, 8 bits: a transformada
 * 8x8, a predição intra 8x8 e as matrizes de escala. Fica de fora: campos
 * entrelaçados, 4:2:2/4:4:4, mais de 8 bits e o modo sem perdas.
 *
 * Só inteiros. Compila também no Linux (H264_HOST) para comparar quadro a
 * quadro com o ffmpeg. */
#ifdef H264_HOST
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define DBG(...) do { if (getenv("H264_DBG")) fprintf(stderr, __VA_ARGS__); } while (0)
#define aloca malloc
#define libera free
static void *aloca_zerado(size_t n) { return calloc(1, n); }
/* Só no PC: contagens para o teste saber que o vídeo exercitou o perfil High
 * (veja tests/video_pc). 0: MBs; 1: MBs intra 8x8; 2: MBs inter com a
 * transformada 8x8; 3: fatias CABAC; 4: fatias CAVLC; 5: PPS com matrizes
 * de escala; 6: fatias B; 7 a 15: blocos intra 8x8 por modo (0 a 8); 16:
 * fatias CABAC cujos dados não acabaram junto com o NAL (sinal de que algum
 * símbolo foi lido errado no caminho). */
long h264_contagem[17];
#define CONTA(i) (h264_contagem[i]++)
#else
#include "sagui.h"
#define DBG(...) do { } while (0)
#define CONTA(i) do { } while (0)
#endif
#include "h264.h"
#include "../../terceiros/ffmpeg-h264-tabelas/cabac_tab.h"   /* FFmpeg, LGPL 2.1+ (D043) */
#include "h264_alto_tab.h"                                    /* números da norma para o High (D048) */

/* ------------------------------------------------------------------ bits */

typedef struct {
    const uint8_t *d;
    uint32_t pos, fim;      /* em bits; fim = bit de parada do RBSP */
} bits_t;

static inline uint32_t espiar32(const bits_t *b)
{
    uint32_t i = b->pos >> 3, s = b->pos & 7;
    uint32_t v = ((uint32_t)b->d[i] << 24) | ((uint32_t)b->d[i + 1] << 16) |
                 ((uint32_t)b->d[i + 2] << 8) | b->d[i + 3];
    if (s) v = (v << s) | (b->d[i + 4] >> (8 - s));
    return v;
}
static inline uint32_t ler_bits(bits_t *b, int n)
{
    if (!n) return 0;
    uint32_t v = espiar32(b) >> (32 - n);
    b->pos += n;
    return v;
}
static inline int ler_bit(bits_t *b)
{
    int v = (b->d[b->pos >> 3] >> (7 - (b->pos & 7))) & 1;
    b->pos++;
    return v;
}
static inline uint32_t ue(bits_t *b)
{
    uint32_t v = espiar32(b);
    int z = v ? __builtin_clz(v) : 32;
    if (z > 15) {           /* raro: código longo */
        b->pos += z;
        ler_bit(b);
        return (1u << z) - 1 + ler_bits(b, z > 31 ? 31 : z);
    }
    b->pos += 2 * z + 1;
    return (v >> (31 - 2 * z)) - 1;
}
static inline int se(bits_t *b)
{
    uint32_t k = ue(b);
    return (k & 1) ? (int)((k + 1) >> 1) : -(int)(k >> 1);
}
static inline bool mais_dados(const bits_t *b) { return b->pos < b->fim; }

/* ------------------------------------------------------------- tabelas */

/* As tabelas do CAVLC e do coded_block_pattern seguem a disposição do FFmpeg
 * (LGPL 2.1+): ficam em terceiros/ffmpeg-h264-tabelas (D043). */
#include "../../terceiros/ffmpeg-h264-tabelas/cavlc_tab.h"

static const uint8_t zigzag[16] = { 0,1,4,8,5,2,3,6,9,12,13,10,7,11,14,15 };
static const uint8_t dq_v[6][3] = { {10,16,13},{11,18,14},{13,20,16},{14,23,18},{16,25,20},{18,29,23} };
static const uint8_t qp_croma[52] = {
 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,
 29,30,31,32,32,33,34,34,35,35,36,36,37,37,37,38,38,38,39,39,39,39 };
static const uint8_t alfa_t[52] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 4,4,5,6,7,8,9,10,12,13,15,17,20,22,25,28,32,36,40,45,50,56,63,71,80,90,101,113,127,144,162,182,203,226,255,255 };
static const uint8_t beta_t[52] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 2,2,2,3,3,3,3,4,4,4,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,14,14,15,15,16,16,17,17,18,18 };
static const uint8_t tc0_t[52][3] = {
 {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
 {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,1,1},{0,1,1},{1,1,1},{1,1,1},{1,1,1},
 {1,1,1},{1,1,2},{1,1,2},{1,1,2},{1,1,2},{1,2,3},{1,2,3},{2,2,3},{2,2,4},{2,3,4},{2,3,4},{3,3,5},{3,4,6},
 {3,4,6},{4,5,7},{4,5,8},{4,6,9},{5,7,10},{6,8,11},{6,8,13},{7,10,14},{8,11,16},{9,12,18},{10,13,20},
 {11,15,23},{13,17,25} };

/* Bloco 4x4 de luma: índice de decodificação -> posição (x,y) em blocos. */
static const uint8_t blk_x[16] = { 0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3 };
static const uint8_t blk_y[16] = { 0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3 };
static uint8_t blk_ordem[4][4];   /* [y][x] -> índice */

/* Varredura em ziguezague de um bloco 8x8 de quadro (tabela 8-13 da norma):
 * posição na varredura -> y * 8 + x. Não é guardada, é montada (veja
 * montar_zigzag8): é o mesmo passeio pelas diagonais do zigzag[] 4x4 acima. */
static uint8_t zigzag8[64];

/* O ziguezague começa no canto de cima à esquerda e percorre as diagonais,
 * uma subindo para a direita, a seguinte descendo para a esquerda, de modo
 * que os coeficientes de frequência baixa (os que quase sempre existem)
 * venham primeiro. Nas diagonais de soma x + y par se sobe; nas ímpares se
 * desce; ao bater numa borda, anda-se uma casa ao longo dela. */
static void montar_zigzag8(void)
{
    int x = 0, y = 0;
    for (int k = 0; k < 64; k++) {
        zigzag8[k] = (uint8_t)(y * 8 + x);
        if (((x + y) & 1) == 0) {
            if (x == 7) y++;
            else if (y == 0) x++;
            else { x++; y--; }
        } else {
            if (y == 7) x++;
            else if (x == 0) y++;
            else { x--; y++; }
        }
    }
}

/* Tabelas de consulta direta: espia N bits, sai (tamanho << 8) | valor. */
static uint16_t *vlc_ct[5];        /* 4 de nC + croma DC; 16 bits */
static uint16_t *vlc_tz[15], *vlc_tzc[3], *vlc_rb[7];
static bool vlc_pronto;

static uint16_t *vlc_montar(int nbits, int qtd, const uint8_t *len, const uint8_t *cod, const uint8_t *val)
{
    uint16_t *t = aloca_zerado(sizeof(uint16_t) << nbits);
    if (!t) return NULL;
    for (int i = 0; i < qtd; i++) {
        int l = len[i];
        if (!l) continue;
        uint32_t base = (uint32_t)cod[i] << (nbits - l), n = 1u << (nbits - l);
        for (uint32_t k = 0; k < n; k++) t[base + k] = (uint16_t)((l << 8) | val[i]);
    }
    return t;
}

static bool vlc_iniciar(void)
{
    if (vlc_pronto) return true;
    uint8_t val[68];
    for (int i = 0; i < 68; i++) val[i] = (uint8_t)(((i >> 2) << 2) | (i & 3));   /* TC<<2 | T1 */
    for (int k = 0; k < 4; k++) vlc_ct[k] = vlc_montar(16, 68, ct_len[k], ct_bits[k], val);
    vlc_ct[4] = vlc_montar(16, 20, cdc_len, cdc_bits, val);
    uint8_t seq[16];
    for (int i = 0; i < 16; i++) seq[i] = (uint8_t)i;
    for (int k = 0; k < 15; k++) vlc_tz[k] = vlc_montar(9, 16, tz_len[k], tz_bits[k], seq);
    for (int k = 0; k < 3; k++) vlc_tzc[k] = vlc_montar(3, 4, tzc_len[k], tzc_bits[k], seq);
    for (int k = 0; k < 7; k++) vlc_rb[k] = vlc_montar(11, 16, rb_len[k], rb_bits[k], seq);
    for (int i = 0; i < 16; i++) blk_ordem[blk_y[i]][blk_x[i]] = (uint8_t)i;
    montar_zigzag8();
    vlc_pronto = true;
    return true;
}

static inline int vlc_ler(bits_t *b, const uint16_t *t, int nbits)
{
    uint16_t e = t[espiar32(b) >> (32 - nbits)];
    if (!e) return -1;
    b->pos += e >> 8;
    return e & 255;
}

/* ------------------------------------------------------------- estado */

#define MAX_REF 16
#define N_QUADROS (MAX_REF + 4)
#define REF_NADA (-2)     /* vizinho fora da imagem/fatia */
#define REF_INTRA (-1)

typedef struct {        /* informação guardada por macrobloco */
    int8_t  intra;          /* 0 inter, 1 intra, 2 I_PCM */
    int8_t  i4x4, i16, skip, direto;   /* i4x4 = I_NxN: intra 4x4 ou, com t8, intra 8x8 */
    int8_t  t8;             /* transform_size_8x8_flag: a luma deste MB usa a transformada 8x8 (High) */
    int8_t  qp, qpc[2], qpd, cbp, modo_croma;   /* qpc: QP da croma, Cb e Cr (podem diferir no High) */
    int     fatia;
    uint8_t nz[16 + 8];     /* coeficientes por bloco 4x4 (luma raster, depois Cb 4, Cr 4);
                               num bloco 8x8 lido pelo CABAC, os quatro 4x4 dele levam a conta do 8x8 */
    uint8_t dc;             /* CABAC: DC codificado (bit 0 luma, 1 Cb, 2 Cr) */
    int8_t  modo4[16];      /* raster; na intra 8x8 o modo do bloco vale nos seus quatro 4x4 */
    int8_t  refidx[2][4];   /* por 8x8 e lista; -1 = lista não usada */
    int8_t  ref[2][4];      /* id do quadro de referência (&127) ou -1 */
    int16_t mv[2][16][2];   /* raster */
    uint8_t mvd[2][16][2];  /* |mvd| (contexto do CABAC) */
    uint8_t dir[16];        /* bloco feito por predição direta */
} mbinfo_t;

typedef struct {
    uint8_t *y, *u, *v;
    mbinfo_t *mb;
    int frame_num, pic_num, poc, ordem;
    bool ref, saida;
    int id;
} quadro_t;

typedef struct { int16_t w, o; } peso_t;

struct h264 {
    /* SPS */
    bool sps_ok, pps_ok;
    int perfil;
    int larg_mb, alt_mb, larg, alt;
    int corte[4];               /* esq, dir, cima, baixo em pixels */
    int log2_frame_num, poc_tipo, log2_poc_lsb, delta_poc_zero;
    int max_ref, direto_8x8, reordenar;
    /* PPS */
    int cabac, ref_padrao[2], qp_inicial, croma_qp_off, ctrl_desbloq, redundante, poc_inferior;
    int ponderada, bipred;
    /* High: SPS e PPS */
    int t8_modo;                    /* transform_8x8_mode_flag: os MBs podem escolher a transformada 8x8 */
    int croma_qp_off2;              /* second_chroma_qp_index_offset: o ajuste de QP do Cr */
    bool sps_matriz, pps_matriz;    /* quem mandou matrizes de escala */
    uint8_t sps_l[8][64], pps_l[8][64];   /* as 8 listas de escala (6 de 4x4, 2 de 8x8), em ziguezague */
    uint16_t ls4[6][6][16];         /* LevelScale4x4 [lista][QP % 6][y * 4 + x] */
    uint16_t ls8[2][6][64];         /* LevelScale8x8 [intra, inter][QP % 6][y * 8 + x] */
    const char *motivo;             /* por que o SPS ou o PPS foi recusado */
    /* fatia */
    int tipo, frame_num, nal_ref, idr, qp, num_ref[2];
    int direto_espacial, cabac_init;
    int fatia_atual;
    quadro_t *lista[2][MAX_REF + 1];
    int log2_peso[2];
    peso_t peso[2][MAX_REF + 1][3];
    bool tem_peso[2][MAX_REF + 1];
    /* POC */
    int poc, poc_msb_ant, poc_lsb_ant, fn_off, fn_ant, fn_off_ant, geracao;
    /* quadros */
    quadro_t q[N_QUADROS];
    quadro_t *atual;
    int prox_id;
    mbinfo_t *mb;
    int passo, passo_c;
    bool quadro_aberto, viu_b, esvaziar;
    bool adaptativo;                /* marcação por MMCO (sem janela deslizante) */
    int mmco[32][2], n_mmco;        /* aplicadas ao fechar o quadro */
    /* rascunho para RBSP */
    uint8_t *rbsp;
    int rbsp_cap;
    int erros;
};

/* --------------------------------------------------------- utilidades */

static inline int clip(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static inline uint8_t clip1(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
static inline int absi(int v) { return v < 0 ? -v : v; }
static inline int mediana(int a, int b, int c)
{
    int mx = a > b ? a : b, mn = a < b ? a : b;
    return c > mx ? mx : c < mn ? mn : c;
}

h264_t *h264_novo(void)
{
    if (!vlc_iniciar()) return NULL;
    h264_t *h = aloca_zerado(sizeof(h264_t));
    return h;
}

static void liberar_quadros(h264_t *h)
{
    for (int i = 0; i < N_QUADROS; i++) {
        if (h->q[i].y) libera(h->q[i].y);
        if (h->q[i].mb) libera(h->q[i].mb);
        h->q[i].y = h->q[i].u = h->q[i].v = NULL;
        h->q[i].mb = NULL;
        h->q[i].saida = h->q[i].ref = false;
    }
    h->mb = NULL;
    h->atual = NULL;
}

void h264_fim(h264_t *h)
{
    if (!h) return;
    liberar_quadros(h);
    if (h->rbsp) libera(h->rbsp);
    libera(h);
}

/* ---------------------------------------------------------- SPS / PPS */

/* Matrizes de escala (High). O codificador pode dar um peso a cada
 * frequência do bloco: o passo da quantização deixa de ser um só para o
 * bloco inteiro, e as frequências altas (que o olho nota menos) podem ser
 * guardadas com menos precisão. São 8 listas: seis para blocos 4x4 (Y, Cb e
 * Cr de MBs intra, depois os três dos inter) e duas para 8x8 (Y intra e
 * inter). Sem matrizes, todo peso é 16 ("Flat_4x4_16"), que dá a mesma conta
 * do Baseline e do Main.
 *
 * Uma lista (item 7.3.2.1.1.1, scaling_list) vem na ordem do ziguezague, cada
 * valor como diferença para o anterior (começando em 8). Uma diferença que
 * leve a zero quer dizer "daqui para a frente repete o último"; se isso
 * acontece logo no primeiro valor, a lista inteira é a padrão da norma.
 * Devolve false nesse último caso. */
static bool ler_lista_escala(bits_t *b, uint8_t *lista, int n)
{
    int ultimo = 8, prox = 8;
    bool usar_padrao = false;
    for (int j = 0; j < n; j++) {
        if (prox) {
            prox = (ultimo + se(b) + 256) & 255;
            if (j == 0 && prox == 0) usar_padrao = true;
        }
        lista[j] = (uint8_t)(prox ? prox : ultimo);
        ultimo = lista[j];
    }
    return !usar_padrao;
}

/* Lê as "qtd" primeiras das 8 listas; cada uma tem um bit dizendo se veio.
 * Para a que não veio vale a tabela 7-2 da norma: a primeira de cada grupo
 * (listas 0, 3, 6 e 7) cai na lista padrão (regra A, base == NULL) ou na
 * lista de mesmo número do SPS (regra B, usada no PPS quando o SPS mandou
 * matrizes); as outras (Cb e Cr) copiam a anterior do mesmo grupo. */
static void ler_matrizes(bits_t *b, uint8_t l[8][64], int qtd, uint8_t (*base)[64])
{
    for (int i = 0; i < 8; i++) {
        int n = i < 6 ? 16 : 64;
        bool veio = i < qtd && ler_bit(b);
        if (veio && ler_lista_escala(b, l[i], n)) continue;
        const uint8_t *fonte;
        if (!veio && i != 0 && i != 3 && i < 6) fonte = l[i - 1];
        else if (veio || !base) fonte = i < 3 ? escala4_intra : i < 6 ? escala4_inter : i == 6 ? escala8_intra : escala8_inter;
        else fonte = base[i];
        memcpy(l[i], fonte, (size_t)n);
    }
}

/* A que classe uma posição do bloco 8x8 pertence para o normAdjust8x8
 * (equação 8-317): depende só de linha e coluna serem múltiplas de 4, ímpares
 * ou "2 mais múltiplo de 4", porque é isso que decide o tamanho das funções
 * de base da transformada 8x8 que se cruzam ali. */
static int classe8(int x, int y)
{
    int a = x & 3, b = y & 3;
    if (a == 0 && b == 0) return 0;
    if ((a & 1) && (b & 1)) return 1;
    if (a == 2 && b == 2) return 2;
    if ((a == 0 && (b & 1)) || ((a & 1) && b == 0)) return 3;
    if ((a == 0 && b == 2) || (a == 2 && b == 0)) return 4;
    return 5;
}

/* LevelScale (item 8.5.9, equações 8-313 e 8-315): o peso da matriz de
 * escala vezes o normAdjust da posição, para cada QP % 6. Fica calculado uma
 * vez por PPS, já na posição (linha, coluna) do bloco, para a desquantização
 * de cada coeficiente ser uma multiplicação. */
static void montar_escalas(h264_t *h, uint8_t l[8][64])
{
    for (int i = 0; i < 6; i++)
        for (int m = 0; m < 6; m++)
            for (int k = 0; k < 16; k++) {
                int p = zigzag[k], x = p & 3, y = p >> 2;
                int cl = (!(x & 1) && !(y & 1)) ? 0 : ((x & 1) && (y & 1)) ? 1 : 2;
                h->ls4[i][m][p] = (uint16_t)(l[i][k] * dq_v[m][cl]);
            }
    for (int i = 0; i < 2; i++)
        for (int m = 0; m < 6; m++)
            for (int k = 0; k < 64; k++) {
                int p = zigzag8[k];
                h->ls8[i][m][p] = (uint16_t)(l[6 + i][k] * norma8[m][classe8(p & 7, p >> 3)]);
            }
}

static void pular_hrd(bits_t *b)
{
    int n = (int)ue(b) + 1;
    ler_bits(b, 8);
    for (int i = 0; i < n && i < 32; i++) { ue(b); ue(b); ler_bit(b); }
    ler_bits(b, 20);
}

static int ler_sps(h264_t *h, bits_t *b)
{
    int perfil = (int)ler_bits(b, 8);
    ler_bits(b, 16);   /* restrições + nível */
    ue(b);
    h->perfil = perfil;
    h->motivo = NULL;
    bool matriz = false;
    /* Os perfis do High para cima têm estes campos a mais no SPS (7.3.2.1.1). */
    if (perfil == 100 || perfil == 110 || perfil == 122 || perfil == 244 || perfil == 44 ||
        perfil == 83 || perfil == 86 || perfil == 118 || perfil == 128) {
        int croma = (int)ue(b);
        if (croma != 1) { h->motivo = "croma que nao e 4:2:0"; return H264_SEM_SUPORTE; }
        int prof_y = (int)ue(b), prof_c = (int)ue(b);
        if (prof_y || prof_c) { h->motivo = "mais de 8 bits por amostra"; return H264_SEM_SUPORTE; }
        if (ler_bit(b)) { h->motivo = "modo sem perdas (transform bypass)"; return H264_SEM_SUPORTE; }
        matriz = ler_bit(b);
        if (matriz) ler_matrizes(b, h->sps_l, 8, NULL);
    }
    if (!matriz) memset(h->sps_l, 16, sizeof h->sps_l);   /* Flat_4x4_16 e Flat_8x8_16 */
    h->sps_matriz = matriz;
    h->log2_frame_num = (int)ue(b) + 4;
    h->poc_tipo = (int)ue(b);
    if (h->poc_tipo == 0) h->log2_poc_lsb = (int)ue(b) + 4;
    else if (h->poc_tipo == 1) {
        h->delta_poc_zero = ler_bit(b);
        se(b); se(b);
        int n = (int)ue(b);
        for (int i = 0; i < n; i++) se(b);
    }
    h->max_ref = (int)ue(b);
    if (h->max_ref > MAX_REF) h->max_ref = MAX_REF;
    if (h->max_ref < 1) h->max_ref = 1;
    ler_bit(b);
    int lm = (int)ue(b) + 1, am = (int)ue(b) + 1;
    if (!ler_bit(b)) { h->motivo = "video entrelacado"; return H264_SEM_SUPORTE; }
    h->direto_8x8 = ler_bit(b);
    int c[4] = { 0, 0, 0, 0 };
    if (ler_bit(b)) for (int i = 0; i < 4; i++) c[i] = (int)ue(b) * 2;
    /* quantos quadros podem sair fora de ordem: Baseline nenhum; senão a VUI diz, ou o DPB inteiro */
    h->reordenar = perfil == 66 ? 0 : h->max_ref;
    if (ler_bit(b)) {   /* VUI */
        if (ler_bit(b)) { if (ler_bits(b, 8) == 255) ler_bits(b, 32); }
        if (ler_bit(b)) ler_bit(b);
        if (ler_bit(b)) { ler_bits(b, 4); if (ler_bit(b)) ler_bits(b, 24); }
        if (ler_bit(b)) { ue(b); ue(b); }
        if (ler_bit(b)) { ler_bits(b, 32); ler_bits(b, 32); ler_bit(b); }
        int nal = ler_bit(b); if (nal) pular_hrd(b);
        int vcl = ler_bit(b); if (vcl) pular_hrd(b);
        if (nal || vcl) ler_bit(b);
        ler_bit(b);
        if (ler_bit(b)) {
            ler_bit(b); ue(b); ue(b); ue(b); ue(b);
            int r = (int)ue(b); ue(b);
            if (r <= MAX_REF) h->reordenar = r;
        }
    }
    if (lm > 256 || am > 256) { h->motivo = "imagem grande demais"; return H264_SEM_SUPORTE; }
    if (lm != h->larg_mb || am != h->alt_mb || !h->q[0].y) {
        liberar_quadros(h);
        h->larg_mb = lm;
        h->alt_mb = am;
        h->larg = lm * 16;
        h->alt = am * 16;
        h->passo = h->larg;
        h->passo_c = h->larg / 2;
        for (int i = 0; i < N_QUADROS; i++) {
            uint8_t *p = aloca(h->larg * h->alt * 3 / 2);
            h->q[i].mb = aloca_zerado(sizeof(mbinfo_t) * lm * am);
            if (!p || !h->q[i].mb) return H264_SEM_MEMORIA;
            h->q[i].y = p;
            h->q[i].u = p + h->larg * h->alt;
            h->q[i].v = h->q[i].u + h->larg * h->alt / 4;
        }
    }
    memcpy(h->corte, c, sizeof c);
    /* um SPS repetido no meio do vídeo não desfaz as matrizes que o PPS mandou */
    montar_escalas(h, h->pps_matriz ? h->pps_l : h->sps_l);
    h->sps_ok = true;
    return 0;
}

static int ler_pps(h264_t *h, bits_t *b)
{
    ue(b); ue(b);
    h->cabac = ler_bit(b);
    h->poc_inferior = ler_bit(b);
    if (ue(b) != 0) { h->motivo = "grupos de fatias (FMO)"; return H264_SEM_SUPORTE; }
    h->ref_padrao[0] = (int)ue(b) + 1;
    h->ref_padrao[1] = (int)ue(b) + 1;
    h->ponderada = ler_bit(b);
    h->bipred = (int)ler_bits(b, 2);
    h->qp_inicial = 26 + se(b);
    se(b);
    h->croma_qp_off = se(b);
    h->ctrl_desbloq = ler_bit(b);
    ler_bit(b);          /* intra restrita: tratada como 0 */
    h->redundante = ler_bit(b);
    /* O que o High acrescenta ao PPS vem no fim, e só se sobrarem bits
     * (7.3.2.2): se os MBs podem usar a transformada 8x8, matrizes de escala
     * que trocam as do SPS, e um ajuste de QP só para o Cr (sem ele, o Cr usa
     * o mesmo do Cb). */
    h->t8_modo = 0;
    h->pps_matriz = false;
    h->croma_qp_off2 = h->croma_qp_off;
    if (mais_dados(b)) {
        h->t8_modo = ler_bit(b);
        if (ler_bit(b)) {
            ler_matrizes(b, h->pps_l, 6 + 2 * h->t8_modo, h->sps_matriz ? h->sps_l : NULL);
            h->pps_matriz = true;
            CONTA(5);
        }
        h->croma_qp_off2 = se(b);
    }
    montar_escalas(h, h->pps_matriz ? h->pps_l : h->sps_l);
    h->pps_ok = true;
    return 0;
}

/* ------------------------------------------------- quadros de referência */

static void calc_pic_num(h264_t *h)
{
    int maxf = 1 << h->log2_frame_num;
    for (int i = 0; i < N_QUADROS; i++)
        if (h->q[i].ref) h->q[i].pic_num = h->q[i].frame_num > h->frame_num ? h->q[i].frame_num - maxf : h->q[i].frame_num;
}

static void janela_deslizante(h264_t *h)
{
    int n = 0;
    calc_pic_num(h);
    for (int i = 0; i < N_QUADROS; i++) if (h->q[i].ref) n++;
    while (n > h->max_ref) {
        quadro_t *velho = NULL;
        for (int i = 0; i < N_QUADROS; i++)
            if (h->q[i].ref && (!velho || h->q[i].pic_num < velho->pic_num)) velho = &h->q[i];
        velho->ref = false;
        n--;
    }
}

static void ordenar(quadro_t **l, int k, int (*maior)(quadro_t *, quadro_t *))
{
    for (int i = 1; i < k; i++)
        for (int j = i; j > 0 && maior(l[j], l[j - 1]); j--) { quadro_t *t = l[j]; l[j] = l[j - 1]; l[j - 1] = t; }
}
static int pn_maior(quadro_t *a, quadro_t *b) { return a->pic_num > b->pic_num; }
static int poc_maior(quadro_t *a, quadro_t *b) { return a->poc > b->poc; }
static int poc_menor(quadro_t *a, quadro_t *b) { return a->poc < b->poc; }

static void montar_listas(h264_t *h)
{
    calc_pic_num(h);
    quadro_t *r[N_QUADROS]; int k = 0;
    for (int i = 0; i < N_QUADROS; i++) if (h->q[i].ref && &h->q[i] != h->atual) r[k++] = &h->q[i];
    for (int l = 0; l < 2; l++) for (int i = 0; i <= MAX_REF; i++) h->lista[l][i] = NULL;
    if (h->tipo == 0) {
        ordenar(r, k, pn_maior);
        for (int i = 0; i < k && i <= MAX_REF; i++) h->lista[0][i] = r[i];
    } else if (h->tipo == 1) {
        quadro_t *antes[N_QUADROS], *depois[N_QUADROS]; int na = 0, nd = 0;
        for (int i = 0; i < k; i++) { if (r[i]->poc < h->poc) antes[na++] = r[i]; else depois[nd++] = r[i]; }
        ordenar(antes, na, poc_maior);
        ordenar(depois, nd, poc_menor);
        int p = 0;
        for (int i = 0; i < na && p <= MAX_REF; i++) h->lista[0][p++] = antes[i];
        for (int i = 0; i < nd && p <= MAX_REF; i++) h->lista[0][p++] = depois[i];
        p = 0;
        for (int i = 0; i < nd && p <= MAX_REF; i++) h->lista[1][p++] = depois[i];
        for (int i = 0; i < na && p <= MAX_REF; i++) h->lista[1][p++] = antes[i];
        if (k > 1) {
            bool iguais = true;
            for (int i = 0; i < k && i <= MAX_REF; i++) if (h->lista[0][i] != h->lista[1][i]) iguais = false;
            if (iguais) { quadro_t *t = h->lista[1][0]; h->lista[1][0] = h->lista[1][1]; h->lista[1][1] = t; }
        }
    }
}

/* Depois da modificação: posições sem quadro repetem os existentes (fluxos defeituosos). */
static void completar_listas(h264_t *h)
{
    for (int l = 0; l < 2; l++) {
        int k = 0;
        while (k <= MAX_REF && h->lista[l][k]) k++;
        for (int i = k; i <= MAX_REF; i++) h->lista[l][i] = k ? h->lista[l][i % k] : NULL;
    }
}

static void modificar_lista(h264_t *h, bits_t *b, int l)
{
    if (!ler_bit(b)) return;
    int maxf = 1 << h->log2_frame_num, pred = h->frame_num, idx = 0;
    for (;;) {
        int op = (int)ue(b);
        if (op == 3) break;
        if (op > 2) { h->erros++; break; }
        int v = (int)ue(b);
        if (op == 2) continue;                      /* longo prazo: não suportado */
        int sem_volta = op == 0 ? pred - (v + 1) : pred + (v + 1);
        if (sem_volta < 0) sem_volta += maxf;
        if (sem_volta >= maxf) sem_volta -= maxf;
        pred = sem_volta;
        int num = sem_volta > h->frame_num ? sem_volta - maxf : sem_volta;
        quadro_t *alvo = NULL;
        for (int i = 0; i < N_QUADROS; i++)
            if (h->q[i].ref && &h->q[i] != h->atual && h->q[i].pic_num == num) alvo = &h->q[i];
        if (!alvo || idx >= h->num_ref[l]) continue;
        int n = h->num_ref[l];
        quadro_t **L = h->lista[l];
        for (int i = n; i > idx; i--) L[i] = L[i - 1];
        L[idx++] = alvo;
        int k = idx;
        for (int i = idx; i <= n; i++) if (L[i] != alvo) L[k++] = L[i];
    }
}

/* Ordem de exibição (POC). */
static void calc_poc(h264_t *h, int lsb, int delta_inf)
{
    int maxf = 1 << h->log2_frame_num;
    if (h->poc_tipo == 0) {
        if (h->idr) { h->poc_msb_ant = 0; h->poc_lsb_ant = 0; }
        int maxl = 1 << h->log2_poc_lsb, msb;
        if (lsb < h->poc_lsb_ant && h->poc_lsb_ant - lsb >= maxl / 2) msb = h->poc_msb_ant + maxl;
        else if (lsb > h->poc_lsb_ant && lsb - h->poc_lsb_ant > maxl / 2) msb = h->poc_msb_ant - maxl;
        else msb = h->poc_msb_ant;
        int topo = msb + lsb, fundo = topo + delta_inf;
        h->poc = topo < fundo ? topo : fundo;
        if (h->nal_ref) { h->poc_msb_ant = msb; h->poc_lsb_ant = lsb; }
    } else {
        if (h->idr) h->fn_off = 0;
        else if (h->fn_ant > h->frame_num) h->fn_off = h->fn_off_ant + maxf;
        else h->fn_off = h->fn_off_ant;
        h->poc = h->idr ? 0 : 2 * (h->fn_off + h->frame_num) - (h->nal_ref ? 0 : 1);
        h->fn_ant = h->frame_num; h->fn_off_ant = h->fn_off;
    }
}

/* ------------------------------------------------------ predição intra */

static void pred4x4(uint8_t *d, int passo, int modo, const int *t, const int *l, int lt, bool tem_t, bool tem_l)
{
    if (modo == 0) { for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) d[y * passo + x] = (uint8_t)t[x]; return; }
    if (modo == 1) { for (int y = 0; y < 4; y++) memset(d + y * passo, l[y], 4); return; }
    if (modo == 2) {
        int v;
        if (tem_t && tem_l) v = (t[0] + t[1] + t[2] + t[3] + l[0] + l[1] + l[2] + l[3] + 4) >> 3;
        else if (tem_l) v = (l[0] + l[1] + l[2] + l[3] + 2) >> 2;
        else if (tem_t) v = (t[0] + t[1] + t[2] + t[3] + 2) >> 2;
        else v = 128;
        for (int y = 0; y < 4; y++) memset(d + y * passo, v, 4);
        return;
    }
    int p[4][4];
#define T(i) ((i) < 0 ? lt : t[i])
#define L(i) ((i) < 0 ? lt : l[i])
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            int v;
            switch (modo) {
            case 0: v = t[x]; break;
            case 1: v = l[y]; break;
            case 2:
                if (tem_t && tem_l) v = (t[0] + t[1] + t[2] + t[3] + l[0] + l[1] + l[2] + l[3] + 4) >> 3;
                else if (tem_l) v = (l[0] + l[1] + l[2] + l[3] + 2) >> 2;
                else if (tem_t) v = (t[0] + t[1] + t[2] + t[3] + 2) >> 2;
                else v = 128;
                break;
            case 3:
                v = (x == 3 && y == 3) ? (t[6] + 3 * t[7] + 2) >> 2
                                       : (t[x + y] + 2 * t[x + y + 1] + t[x + y + 2] + 2) >> 2;
                break;
            case 4:
                if (x > y) v = (T(x - y - 2) + 2 * T(x - y - 1) + T(x - y) + 2) >> 2;
                else if (x < y) v = (L(y - x - 2) + 2 * L(y - x - 1) + L(y - x) + 2) >> 2;
                else v = (t[0] + 2 * lt + l[0] + 2) >> 2;
                break;
            case 5: {
                int z = 2 * x - y;
                if (z >= 0 && !(z & 1)) v = (T(x - (y >> 1) - 1) + T(x - (y >> 1)) + 1) >> 1;
                else if (z >= 0) v = (T(x - (y >> 1) - 2) + 2 * T(x - (y >> 1) - 1) + T(x - (y >> 1)) + 2) >> 2;
                else if (z == -1) v = (l[0] + 2 * lt + t[0] + 2) >> 2;
                else v = (L(y - 1) + 2 * L(y - 2) + L(y - 3) + 2) >> 2;
                break;
            }
            case 6: {
                int z = 2 * y - x;
                if (z >= 0 && !(z & 1)) v = (L(y - (x >> 1) - 1) + L(y - (x >> 1)) + 1) >> 1;
                else if (z >= 0) v = (L(y - (x >> 1) - 2) + 2 * L(y - (x >> 1) - 1) + L(y - (x >> 1)) + 2) >> 2;
                else if (z == -1) v = (l[0] + 2 * lt + t[0] + 2) >> 2;
                else v = (T(x - 1) + 2 * T(x - 2) + T(x - 3) + 2) >> 2;
                break;
            }
            case 7:
                if (!(y & 1)) v = (t[x + (y >> 1)] + t[x + (y >> 1) + 1] + 1) >> 1;
                else v = (t[x + (y >> 1)] + 2 * t[x + (y >> 1) + 1] + t[x + (y >> 1) + 2] + 2) >> 2;
                break;
            default: {
                int z = x + 2 * y;
                if (z < 5 && !(z & 1)) v = (l[y + (x >> 1)] + l[y + (x >> 1) + 1] + 1) >> 1;
                else if (z < 5) v = (l[y + (x >> 1)] + 2 * l[y + (x >> 1) + 1] + l[y + (x >> 1) + 2] + 2) >> 2;
                else if (z == 5) v = (l[2] + 3 * l[3] + 2) >> 2;
                else v = l[3];
            }
            }
            p[y][x] = v;
        }
#undef T
#undef L
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) d[y * passo + x] = (uint8_t)p[y][x];
}

static void pred_plano(uint8_t *d, int passo, int n, const uint8_t *t, const uint8_t *l, int lt)
{
    /* n = 16 (luma) ou 8 (croma 4:2:0) */
    int m = n / 2, H = 0, V = 0;
    for (int i = 0; i < m; i++) {
        int a = (m - 2 - i) < 0 ? lt : t[m - 2 - i];
        int c = (m - 2 - i) < 0 ? lt : l[m - 2 - i];
        H += (i + 1) * (t[m + i] - a);
        V += (i + 1) * (l[m + i] - c);
    }
    int a = 16 * (l[n - 1] + t[n - 1]);
    int bb = n == 16 ? (5 * H + 32) >> 6 : (34 * H + 32) >> 6;
    int cc = n == 16 ? (5 * V + 32) >> 6 : (34 * V + 32) >> 6;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            d[y * passo + x] = clip1((a + bb * (x - (m - 1)) + cc * (y - (m - 1)) + 16) >> 5);
}

/* ------------------------------------------------------- transformadas */

static void idct4_somar(uint8_t *d, int passo, int *c)
{
    int t[16];
    for (int i = 0; i < 4; i++) {
        int *r = &t[i * 4];
        int *s = &c[i * 4];
        int e0 = s[0] + s[2], e1 = s[0] - s[2], e2 = (s[1] >> 1) - s[3], e3 = s[1] + (s[3] >> 1);
        r[0] = e0 + e3; r[1] = e1 + e2; r[2] = e1 - e2; r[3] = e0 - e3;
    }
    for (int i = 0; i < 4; i++) {
        int e0 = t[i] + t[8 + i], e1 = t[i] - t[8 + i];
        int e2 = (t[4 + i] >> 1) - t[12 + i], e3 = t[4 + i] + (t[12 + i] >> 1);
        int f[4] = { e0 + e3, e1 + e2, e1 - e2, e0 - e3 };
        for (int k = 0; k < 4; k++) d[k * passo + i] = clip1(d[k * passo + i] + ((f[k] + 32) >> 6));
    }
}

/* Uma passada (linha ou coluna) da transformada inversa 8x8 do High, no
 * próprio vetor: v[0], v[p], ..., v[7p]. São as equações 8-338 a 8-353 do
 * item 8.5.13.2, com os nomes de lá (a0..a7 e b0..b7). As posições pares
 * (0, 2, 4, 6) fazem a mesma conta da transformada 4x4; as ímpares somam os
 * quatro termos restantes. Só há somas e deslocamentos: a norma escolheu uma
 * transformada inteira justamente para todo decodificador dar o mesmo
 * resultado, bit por bit. */
static inline void idct8_passada(int *v, int p)
{
    int a0 = v[0] + v[4 * p], a4 = v[0] - v[4 * p];
    int a2 = (v[2 * p] >> 1) - v[6 * p], a6 = v[2 * p] + (v[6 * p] >> 1);
    int b0 = a0 + a6, b2 = a4 + a2, b4 = a4 - a2, b6 = a0 - a6;
    int a1 = -v[3 * p] + v[5 * p] - v[7 * p] - (v[7 * p] >> 1);
    int a3 = v[p] + v[7 * p] - v[3 * p] - (v[3 * p] >> 1);
    int a5 = -v[p] + v[7 * p] + v[5 * p] + (v[5 * p] >> 1);
    int a7 = v[3 * p] + v[5 * p] + v[p] + (v[p] >> 1);
    int b1 = a1 + (a7 >> 2), b7 = a7 - (a1 >> 2);
    int b3 = a3 + (a5 >> 2), b5 = (a3 >> 2) - a5;
    v[0] = b0 + b7;     v[p] = b2 + b5;     v[2 * p] = b4 + b3; v[3 * p] = b6 + b1;
    v[4 * p] = b6 - b1; v[5 * p] = b4 - b3; v[6 * p] = b2 - b5; v[7 * p] = b0 - b7;
}

/* Transformada inversa 8x8 somada à predição que já está em d (8.5.13.2 e
 * 8.5.14): primeiro cada linha, depois cada coluna (a ordem importa por causa
 * dos arredondamentos), e o resultado dividido por 64 com arredondamento. */
static void idct8_somar(uint8_t *d, int passo, int *c)
{
    for (int i = 0; i < 8; i++) idct8_passada(c + i * 8, 1);
    for (int i = 0; i < 8; i++) idct8_passada(c + i, 8);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) d[y * passo + x] = clip1(d[y * passo + x] + ((c[y * 8 + x] + 32) >> 6));
}

/* ---------------------------------------------------------- CAVLC */

static int residuo(bits_t *b, int nC, int16_t *coef, int inicio, int max)
{
    int r;
    if (nC == -1) r = vlc_ler(b, vlc_ct[4], 16);
    else r = vlc_ler(b, vlc_ct[nC < 2 ? 0 : nC < 4 ? 1 : nC < 8 ? 2 : 3], 16);
    if (r < 0) return -1;
    int total = r >> 2, t1 = r & 3;
    if (!total) return 0;
    if (total > max) return -1;
    int nivel[16], corrida[16];
    int sufixo = (total > 10 && t1 < 3) ? 1 : 0;
    for (int i = 0; i < total; i++) {
        if (i < t1) { nivel[i] = ler_bit(b) ? -1 : 1; continue; }
        uint32_t v = espiar32(b);
        int pref = v ? __builtin_clz(v) : 32;
        if (pref > 25) return -1;
        b->pos += pref + 1;
        int codigo = (pref < 15 ? pref : 15) << sufixo;
        int tam = (pref == 14 && sufixo == 0) ? 4 : pref >= 15 ? pref - 3 : sufixo;
        if (tam) codigo += (int)ler_bits(b, tam);
        if (pref >= 15 && sufixo == 0) codigo += 15;
        if (pref >= 16) codigo += (1 << (pref - 3)) - 4096;
        if (i == t1 && t1 < 3) codigo += 2;
        nivel[i] = (codigo & 1) ? (-codigo - 1) >> 1 : (codigo + 2) >> 1;
        if (!sufixo) sufixo = 1;
        if (absi(nivel[i]) > (3 << (sufixo - 1)) && sufixo < 6) sufixo++;
    }
    int zeros = 0;
    if (total < max) {
        zeros = nC == -1 ? vlc_ler(b, vlc_tzc[total - 1], 3) : vlc_ler(b, vlc_tz[total - 1], 9);
        if (zeros < 0) return -1;
    }
    for (int i = 0; i < total - 1; i++) {
        if (zeros > 0) {
            int rr = vlc_ler(b, vlc_rb[(zeros > 7 ? 7 : zeros) - 1], 11);
            if (rr < 0 || rr > zeros) return -1;
            corrida[i] = rr;
            zeros -= rr;
        } else corrida[i] = 0;
    }
    corrida[total - 1] = zeros;
    int pos = -1;
    for (int i = total - 1; i >= 0; i--) {
        pos += corrida[i] + 1;
        if (pos >= max) return -1;
        coef[inicio + pos] = (int16_t)nivel[i];
    }
    return total;
}

/* ------------------------------------------------- vizinhança do MB */

typedef struct {
    int mbx, mby, idx;
    mbinfo_t *m;
    mbinfo_t *a, *b, *c, *d;   /* esquerda, cima, cima-direita, cima-esquerda (NULL = indisponível) */
} ctx_t;

static mbinfo_t *vizinho(h264_t *h, int x, int y)
{
    if (x < 0 || y < 0 || x >= h->larg_mb || y >= h->alt_mb) return NULL;
    mbinfo_t *m = &h->mb[y * h->larg_mb + x];
    return m->fatia == h->fatia_atual ? m : NULL;
}

/* Coeficientes (para nC) do bloco 4x4 em (bx,by), relativo ao MB, -1 = fora. */
static int nz_luma(ctx_t *c, int bx, int by, bool *ok)
{
    mbinfo_t *m = c->m;
    if (bx < 0) { m = c->a; bx += 4; }
    else if (by < 0) { m = c->b; by += 4; }
    if (!m) { *ok = false; return 0; }
    *ok = true;
    return m->nz[by * 4 + bx];
}

static int calc_nc(int na, bool oa, int nb, bool ob)
{
    if (oa && ob) return (na + nb + 1) >> 1;
    if (oa) return na;
    if (ob) return nb;
    return 0;
}

static int nc_luma(ctx_t *c, int bx, int by)
{
    bool oa, ob;
    int na = nz_luma(c, bx - 1, by, &oa), nb = nz_luma(c, bx, by - 1, &ob);
    return calc_nc(na, oa, nb, ob);
}

static int nc_croma(ctx_t *c, int comp, int bx, int by)
{
    int base = 16 + comp * 4;
    bool oa = true, ob = true;
    int na, nb;
    if (bx > 0) na = c->m->nz[base + by * 2 + bx - 1];
    else if (c->a) na = c->a->nz[base + by * 2 + 1];
    else { na = 0; oa = false; }
    if (by > 0) nb = c->m->nz[base + (by - 1) * 2 + bx];
    else if (c->b) nb = c->b->nz[base + 2 + bx];
    else { nb = 0; ob = false; }
    return calc_nc(na, oa, nb, ob);
}

/* ------------------------------------------------- compensação de movimento */

static inline int tap6(const uint8_t *p, int s)
{
    return p[-2 * s] - 5 * p[-s] + 20 * p[0] + 20 * p[s] - 5 * p[2 * s] + p[3 * s];
}

/* Copia a região [x-2, x+w+3) x [y-2, y+h+3) do plano, repetindo a borda. */
static void buscar(uint8_t *dst, int dp, const uint8_t *src, int sp, int sl, int sa, int x, int y, int w, int hh)
{
    if (x >= 0 && y >= 0 && x + w <= sl && y + hh <= sa) {
        for (int j = 0; j < hh; j++) memcpy(dst + j * dp, src + (y + j) * sp + x, w);
        return;
    }
    for (int j = 0; j < hh; j++) {
        int yy = clip(y + j, 0, sa - 1);
        for (int i = 0; i < w; i++) dst[j * dp + i] = src[yy * sp + clip(x + i, 0, sl - 1)];
    }
}

static void mc_luma(h264_t *h, quadro_t *ref, uint8_t *d, int ps, int x, int y, int w, int hh, int mvx, int mvy)
{
    uint8_t tmp[21 * 21];
    int fx = mvx & 3, fy = mvy & 3;
    int xi = x + (mvx >> 2), yi = y + (mvy >> 2);
    buscar(tmp, 21, ref->y, h->passo, h->larg, h->alt, xi - 2, yi - 2, w + 5, hh + 5);
    const uint8_t *s = tmp + 2 * 21 + 2;
    if (!fx && !fy) {
        for (int j = 0; j < hh; j++) memcpy(d + j * ps, s + j * 21, w);
        return;
    }
    /* Meios-pixels calculados uma vez por bloco. */
    int braw[21][16];                 /* horizontal sem corte, linhas -2..hh+2 */
    uint8_t B[17][16], V[16][17], J[16][16];
    bool usa_b = fx != 0, usa_v = fy != 0, usa_j = (fx == 2 && fy != 0) || (fy == 2 && fx != 0);
    if (usa_b || usa_j) {
        int l0 = usa_j ? -2 : 0, l1 = usa_j ? hh + 3 : hh + 1;
        for (int j = l0; j < l1; j++) {
            const uint8_t *p = s + j * 21;
            for (int i = 0; i < w; i++) {
                int v = tap6(p + i, 1);
                braw[j + 2][i] = v;
                if (j >= 0 && j <= hh) B[j][i] = clip1((v + 16) >> 5);
            }
        }
    }
    if (usa_v)
        for (int j = 0; j < hh; j++) {
            const uint8_t *p = s + j * 21;
            for (int i = 0; i <= w; i++) V[j][i] = clip1((tap6(p + i, 21) + 16) >> 5);
        }
    if (usa_j)
        for (int j = 0; j < hh; j++)
            for (int i = 0; i < w; i++) {
                int v = braw[j][i] - 5 * braw[j + 1][i] + 20 * braw[j + 2][i] + 20 * braw[j + 3][i] - 5 * braw[j + 4][i] + braw[j + 5][i];
                J[j][i] = clip1((v + 512) >> 10);
            }
    int lb = fy == 3 ? 1 : 0, cv = fx == 3 ? 1 : 0;
    for (int j = 0; j < hh; j++) {
        uint8_t *o = d + j * ps;
        const uint8_t *g = s + j * 21;
        for (int i = 0; i < w; i++) {
            int v;
            if (fy == 0) v = fx == 2 ? B[j][i] : (g[i + cv] + B[j][i] + 1) >> 1;
            else if (fx == 0) v = fy == 2 ? V[j][i] : (g[i + lb * 21] + V[j][i] + 1) >> 1;
            else if (fx == 2 && fy == 2) v = J[j][i];
            else if (fx == 2) v = (B[j + lb][i] + J[j][i] + 1) >> 1;
            else if (fy == 2) v = (V[j][i + cv] + J[j][i] + 1) >> 1;
            else v = (B[j + lb][i] + V[j][i + cv] + 1) >> 1;
            o[i] = (uint8_t)v;
        }
    }
}

static void mc_croma(h264_t *h, const uint8_t *plano, uint8_t *d, int pd, int x, int y, int w, int hh, int mvx, int mvy)
{
    uint8_t tmp[9 * 9];
    int fx = mvx & 7, fy = mvy & 7;
    int lc = h->larg / 2, ac = h->alt / 2;
    buscar(tmp, 9, plano, h->passo_c, lc, ac, x + (mvx >> 3), y + (mvy >> 3), w + 1, hh + 1);
    int a = (8 - fx) * (8 - fy), b = fx * (8 - fy), c = (8 - fx) * fy, dd = fx * fy;
    for (int j = 0; j < hh; j++)
        for (int i = 0; i < w; i++) {
            const uint8_t *p = tmp + j * 9 + i;
            d[j * pd + i] = (uint8_t)((a * p[0] + b * p[1] + c * p[9] + dd * p[10] + 32) >> 6);
        }
}

static void bordas_luma(h264_t *h, ctx_t *c, int bx, int by, int *t, int *l, int *lt, bool *tt, bool *tl)
{
    /* bx,by em blocos 4x4 dentro do MB; vizinhos intra 4x4 */
    quadro_t *q = h->atual;
    int px = c->mbx * 16 + bx * 4, pyy = c->mby * 16 + by * 4;
    const uint8_t *p = q->y + pyy * h->passo + px;
    bool ta = by > 0 || c->b, la = bx > 0 || c->a;
    bool lta = (bx > 0 && by > 0) || (bx == 0 && by > 0 && c->a) || (by == 0 && bx > 0 && c->b) ||
               (bx == 0 && by == 0 && c->d);
    bool tra;
    if (by == 0) tra = bx < 3 ? (c->b != NULL) : (c->c != NULL);
    else if (bx == 3) tra = false;
    else tra = blk_ordem[by - 1][bx + 1] < blk_ordem[by][bx];
    *tt = ta; *tl = la;
    for (int i = 0; i < 4; i++) {
        t[i] = ta ? p[-h->passo + i] : 128;
        l[i] = la ? p[i * h->passo - 1] : 128;
    }
    for (int i = 4; i < 8; i++) t[i] = tra ? p[-h->passo + i] : t[3];
    *lt = lta ? p[-h->passo - 1] : 128;
}

/* Predição intra 8x8 da luma (High, item 8.3.2.2) do bloco b8 (0 a 3, em
 * ordem de leitura) do MB, escrita direto no quadro.
 *
 * Como na intra 4x4, o bloco é "adivinhado" a partir das amostras já
 * decodificadas em volta: as 16 de cima (8 sobre o bloco e 8 à direita
 * delas), as 8 da esquerda e a do canto. A diferença do 8x8 é que essas
 * vizinhas passam antes por um filtro suave [1 2 1]/4 (8.3.2.2.1): num bloco
 * maior, o ruído de uma amostra de referência se espalharia por 8 linhas.
 * As nove direções são as mesmas da 4x4, com as fórmulas de 8.3.2.2.2 a
 * 8.3.2.2.10; T(i) é a linha de cima filtrada, L(i) a coluna da esquerda, e
 * T(-1) = L(-1) é o canto. */
static void pred8x8(h264_t *h, ctx_t *c, int b8, int modo)
{
    int bx = b8 & 1, by = b8 >> 1, passo = h->passo;
    uint8_t *d = h->atual->y + (c->mby * 16 + by * 8) * passo + c->mbx * 16 + bx * 8;
    /* quem existe: dentro do MB, o que já foi decodificado; fora, o MB vizinho */
    bool tem_c = by || c->b, tem_e = bx || c->a;
    bool tem_q = (bx && by) || (!bx && by && c->a) || (bx && !by && c->b) || (!bx && !by && c->d);
    /* as 8 de cima à direita: para o bloco 3 seriam de um MB que ainda não
     * veio, e para o 2 são do bloco 1; na fileira de cima, do MB de cima (ou
     * do de cima à direita). Quando faltam, repete-se a última que existe. */
    bool tem_d = by ? !bx : (bx ? c->c != NULL : c->b != NULL);
    int rc[16], re[8], rq = tem_q ? d[-passo - 1] : 128;
    for (int i = 0; i < 8; i++) {
        rc[i] = tem_c ? d[-passo + i] : 128;
        re[i] = tem_e ? d[i * passo - 1] : 128;
    }
    for (int i = 8; i < 16; i++) rc[i] = tem_c && tem_d ? d[-passo + i] : rc[7];
    /* o filtro das vizinhas; nas pontas, onde falta um dos lados, a amostra
     * da ponta conta em dobro */
    int cima[17], esq[9];
#define T(i) cima[(i) + 1]
#define L(i) esq[(i) + 1]
    for (int i = 0; i < 16; i++) T(i) = rc[i];
    for (int i = 0; i < 8; i++) L(i) = re[i];
    T(-1) = rq;
    if (tem_c) {
        T(0) = tem_q ? (rq + 2 * rc[0] + rc[1] + 2) >> 2 : (3 * rc[0] + rc[1] + 2) >> 2;
        for (int i = 1; i < 15; i++) T(i) = (rc[i - 1] + 2 * rc[i] + rc[i + 1] + 2) >> 2;
        T(15) = (rc[14] + 3 * rc[15] + 2) >> 2;
    }
    if (tem_e) {
        L(0) = tem_q ? (rq + 2 * re[0] + re[1] + 2) >> 2 : (3 * re[0] + re[1] + 2) >> 2;
        for (int i = 1; i < 7; i++) L(i) = (re[i - 1] + 2 * re[i] + re[i + 1] + 2) >> 2;
        L(7) = (re[6] + 3 * re[7] + 2) >> 2;
    }
    if (tem_q) {
        if (tem_c && tem_e) T(-1) = (rc[0] + 2 * rq + re[0] + 2) >> 2;
        else if (tem_c) T(-1) = (3 * rq + rc[0] + 2) >> 2;
        else if (tem_e) T(-1) = (3 * rq + re[0] + 2) >> 2;
    }
    L(-1) = T(-1);
    int media = 128;    /* modo 2 (DC): a média das vizinhas que existem */
    if (modo == 2) {
        int sc = 0, se_ = 0;
        for (int i = 0; i < 8; i++) { sc += T(i); se_ += L(i); }
        if (tem_c && tem_e) media = (sc + se_ + 8) >> 4;
        else if (tem_e) media = (se_ + 4) >> 3;
        else if (tem_c) media = (sc + 4) >> 3;
    }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int v;
            switch (modo) {
            case 0: v = T(x); break;                                   /* vertical */
            case 1: v = L(y); break;                                   /* horizontal */
            case 2: v = media; break;
            case 3:                                                     /* diagonal para baixo e esquerda */
                v = (x == 7 && y == 7) ? (T(14) + 3 * T(15) + 2) >> 2
                                       : (T(x + y) + 2 * T(x + y + 1) + T(x + y + 2) + 2) >> 2;
                break;
            case 4:                                                     /* diagonal para baixo e direita */
                if (x > y) v = (T(x - y - 2) + 2 * T(x - y - 1) + T(x - y) + 2) >> 2;
                else if (x < y) v = (L(y - x - 2) + 2 * L(y - x - 1) + L(y - x) + 2) >> 2;
                else v = (T(0) + 2 * T(-1) + L(0) + 2) >> 2;
                break;
            case 5: {                                                   /* vertical para a direita */
                int z = 2 * x - y, i = x - (y >> 1);
                if (z >= 0 && !(z & 1)) v = (T(i - 1) + T(i) + 1) >> 1;
                else if (z >= 0) v = (T(i - 2) + 2 * T(i - 1) + T(i) + 2) >> 2;
                else if (z == -1) v = (L(0) + 2 * T(-1) + T(0) + 2) >> 2;
                else v = (L(y - 2 * x - 1) + 2 * L(y - 2 * x - 2) + L(y - 2 * x - 3) + 2) >> 2;
                break;
            }
            case 6: {                                                   /* horizontal para baixo */
                int z = 2 * y - x, i = y - (x >> 1);
                if (z >= 0 && !(z & 1)) v = (L(i - 1) + L(i) + 1) >> 1;
                else if (z >= 0) v = (L(i - 2) + 2 * L(i - 1) + L(i) + 2) >> 2;
                else if (z == -1) v = (L(0) + 2 * T(-1) + T(0) + 2) >> 2;
                else v = (T(x - 2 * y - 1) + 2 * T(x - 2 * y - 2) + T(x - 2 * y - 3) + 2) >> 2;
                break;
            }
            case 7: {                                                   /* vertical para a esquerda */
                int i = x + (y >> 1);
                if (!(y & 1)) v = (T(i) + T(i + 1) + 1) >> 1;
                else v = (T(i) + 2 * T(i + 1) + T(i + 2) + 2) >> 2;
                break;
            }
            default: {                                                  /* 8: horizontal para cima */
                int z = x + 2 * y, i = y + (x >> 1);
                if (z > 13) v = L(7);
                else if (z == 13) v = (L(6) + 3 * L(7) + 2) >> 2;
                else if (!(z & 1)) v = (L(i) + L(i + 1) + 1) >> 1;
                else v = (L(i) + 2 * L(i + 1) + L(i + 2) + 2) >> 2;
            }
            }
            d[y * passo + x] = (uint8_t)v;
        }
#undef T
#undef L
}

/* --------------------------------------------- predição de vetor de movimento */

/* Cache 6x5 por lista: linha 0 = acima, coluna 0 = esquerda, 5 = direita. */
typedef struct {
    int8_t  ref[5][6];
    int16_t mv[5][6][2];
    uint8_t mvd[5][6][2];
    uint8_t dir[5][6];
} mvc_t;

static void mvc_preencher(ctx_t *c, mvc_t *k, int l)
{
    for (int r = 0; r < 5; r++)
        for (int q = 0; q < 6; q++) {
            k->ref[r][q] = REF_NADA; k->mv[r][q][0] = k->mv[r][q][1] = 0;
            k->mvd[r][q][0] = k->mvd[r][q][1] = 0; k->dir[r][q] = 0;
        }
    mbinfo_t *ns[4] = { c->a, c->b, c->c, c->d };
    for (int n = 0; n < 4; n++) {
        mbinfo_t *m = ns[n];
        if (!m) continue;
        int qtd = n == 0 || n == 1 ? 4 : 1;
        for (int i = 0; i < qtd; i++) {
            int by, bx, r, q;
            if (n == 0) { by = i; bx = 3; r = i + 1; q = 0; }
            else if (n == 1) { by = 3; bx = i; r = 0; q = i + 1; }
            else if (n == 2) { by = 3; bx = 0; r = 0; q = 5; }
            else { by = 3; bx = 3; r = 0; q = 0; }
            if (m->intra) { k->ref[r][q] = REF_INTRA; continue; }
            int b = by * 4 + bx;
            k->ref[r][q] = m->refidx[l][(by >> 1) * 2 + (bx >> 1)];
            if (k->ref[r][q] < 0) k->ref[r][q] = REF_INTRA;
            k->mv[r][q][0] = m->mv[l][b][0];
            k->mv[r][q][1] = m->mv[l][b][1];
            k->mvd[r][q][0] = m->mvd[l][b][0];
            k->mvd[r][q][1] = m->mvd[l][b][1];
            k->dir[r][q] = m->dir[b];
        }
    }
}

static void mvc_escrever(mvc_t *k, int bx, int by, int w, int hh, int ref, int mx, int my)
{
    for (int y = by; y < by + hh; y++)
        for (int x = bx; x < bx + w; x++) {
            k->ref[y + 1][x + 1] = (int8_t)ref;
            k->mv[y + 1][x + 1][0] = (int16_t)mx;
            k->mv[y + 1][x + 1][1] = (int16_t)my;
        }
}

/* forma: 0 normal, 1 16x8 de cima, 2 16x8 de baixo, 3 8x16 esquerda, 4 8x16 direita */
static void prever_mv(mvc_t *k, int bx, int by, int w, int ref, int forma, int *px, int *py)
{
    int ra = k->ref[by + 1][bx], rb = k->ref[by][bx + 1], rc = k->ref[by][bx + w + 1];
    const int16_t *ma = k->mv[by + 1][bx], *mb = k->mv[by][bx + 1], *mc = k->mv[by][bx + w + 1];
    bool c_ok = rc != REF_NADA;
    if (bx + w >= 4 && by > 0) c_ok = false;
    if (!c_ok) { rc = k->ref[by][bx]; mc = k->mv[by][bx]; }
    if (forma == 1 && rb == ref) { *px = mb[0]; *py = mb[1]; return; }
    if (forma == 2 && ra == ref) { *px = ma[0]; *py = ma[1]; return; }
    if (forma == 3 && ra == ref) { *px = ma[0]; *py = ma[1]; return; }
    if (forma == 4 && rc == ref) { *px = mc[0]; *py = mc[1]; return; }
    if (rb == REF_NADA && rc == REF_NADA && ra != REF_NADA) { *px = ma[0]; *py = ma[1]; return; }
    int iguais = (ra == ref) + (rb == ref) + (rc == ref);
    if (iguais == 1) {
        const int16_t *m = ra == ref ? ma : rb == ref ? mb : mc;
        *px = m[0]; *py = m[1];
        return;
    }
    *px = mediana(ma[0], mb[0], mc[0]);
    *py = mediana(ma[1], mb[1], mc[1]);
}

/* P_Skip */
static void mv_skip_p(mvc_t *k, int *mx, int *my)
{
    int ra = k->ref[1][0], rb = k->ref[0][1];
    *mx = *my = 0;
    if (ra == REF_NADA || rb == REF_NADA || (ra == 0 && !k->mv[1][0][0] && !k->mv[1][0][1]) ||
        (rb == 0 && !k->mv[0][1][0] && !k->mv[0][1][1])) return;
    prever_mv(k, 0, 0, 4, 0, 0, mx, my);
}

/* Predição direta dos blocos 4x4 marcados em "quais" (bit por bloco raster).
 * Escreve ref e mv nos dois caches. */
static void prever_direto(h264_t *h, ctx_t *c, mvc_t *k, unsigned quais)
{
    quadro_t *col = h->lista[1][0];
    mbinfo_t *mc = col && col->mb ? &col->mb[c->idx] : NULL;
    if (h->direto_espacial) {
        int ref[2], mv[2][2];
        for (int l = 0; l < 2; l++) {
            int ra = k[l].ref[1][0], rb = k[l].ref[0][1], rc = k[l].ref[0][5];
            if (rc == REF_NADA) rc = k[l].ref[0][0];
            int r = -1;
            int cand[3] = { ra, rb, rc };
            for (int i = 0; i < 3; i++) if (cand[i] >= 0 && (r < 0 || cand[i] < r)) r = cand[i];
            ref[l] = r;
        }
        bool zero = ref[0] < 0 && ref[1] < 0;
        if (zero) ref[0] = ref[1] = 0;
        for (int l = 0; l < 2; l++) {
            mv[l][0] = mv[l][1] = 0;
            if (!zero && ref[l] >= 0) prever_mv(&k[l], 0, 0, 4, ref[l], 0, &mv[l][0], &mv[l][1]);
        }
        for (int b = 0; b < 16; b++) {
            if (!(quais & (1u << b))) continue;
            int bx = b & 3, by = b >> 2;
            int cx = bx, cy = by;
            if (h->direto_8x8) { cx = (bx >> 1) * 3; cy = (by >> 1) * 3; }
            bool col_zero = false;
            if (mc && !mc->intra && !zero) {
                int b8 = (cy >> 1) * 2 + (cx >> 1), lc = mc->refidx[0][b8] >= 0 ? 0 : 1;
                int rcol = mc->refidx[lc][b8];
                const int16_t *mvc = mc->mv[lc][cy * 4 + cx];
                col_zero = rcol == 0 && mvc[0] >= -1 && mvc[0] <= 1 && mvc[1] >= -1 && mvc[1] <= 1;
            }
            for (int l = 0; l < 2; l++) {
                int mx = mv[l][0], my = mv[l][1];
                if (ref[l] < 0) { mvc_escrever(&k[l], bx, by, 1, 1, REF_INTRA, 0, 0); continue; }
                if (zero || (ref[l] == 0 && col_zero)) mx = my = 0;
                mvc_escrever(&k[l], bx, by, 1, 1, ref[l], mx, my);
            }
        }
        return;
    }
    /* temporal */
    for (int b = 0; b < 16; b++) {
        if (!(quais & (1u << b))) continue;
        int bx = b & 3, by = b >> 2, cx = bx, cy = by;
        if (h->direto_8x8) { cx = (bx >> 1) * 3; cy = (by >> 1) * 3; }
        int r0 = 0, mvc0 = 0, mvc1 = 0;
        if (mc && !mc->intra) {
            int b8 = (cy >> 1) * 2 + (cx >> 1), lc = mc->refidx[0][b8] >= 0 ? 0 : 1;
            int id = mc->ref[lc][b8];
            mvc0 = mc->mv[lc][cy * 4 + cx][0]; mvc1 = mc->mv[lc][cy * 4 + cx][1];
            for (int i = 0; i < h->num_ref[0]; i++) if (h->lista[0][i] && (h->lista[0][i]->id & 127) == id) { r0 = i; break; }
        }
        quadro_t *q0 = h->lista[0][r0];
        int tb = clip(h->poc - (q0 ? q0->poc : 0), -128, 127), td = clip((col ? col->poc : 0) - (q0 ? q0->poc : 0), -128, 127);
        int m0x = mvc0, m0y = mvc1;
        if (td) {
            int tx = (16384 + absi(td / 2)) / td;
            int dsf = clip((tb * tx + 32) >> 6, -1024, 1023);
            m0x = (dsf * mvc0 + 128) >> 8; m0y = (dsf * mvc1 + 128) >> 8;
        }
        mvc_escrever(&k[0], bx, by, 1, 1, r0, m0x, m0y);
        mvc_escrever(&k[1], bx, by, 1, 1, 0, m0x - mvc0, m0y - mvc1);
    }
}

/* ------------------------------------------------ compensação com pesos */

static void pesos_implicitos(h264_t *h, int r0, int r1, int *w0, int *w1)
{
    quadro_t *a = h->lista[0][r0], *b = h->lista[1][r1];
    *w0 = *w1 = 32;
    if (!a || !b) return;
    int tb = clip(h->poc - a->poc, -128, 127), td = clip(b->poc - a->poc, -128, 127);
    if (!td) return;
    int tx = (16384 + absi(td / 2)) / td;
    int dsf = clip((tb * tx + 32) >> 6, -1024, 1023);
    if ((dsf >> 2) < -64 || (dsf >> 2) > 128) return;
    *w0 = 64 - (dsf >> 2); *w1 = dsf >> 2;
}

/* Bloco w x hh em (x, y) do quadro atual: componente 0 luma, 1 Cb, 2 Cr. */
static void combinar(h264_t *h, uint8_t *d, int passo, const uint8_t *p0, const uint8_t *p1, int w, int hh,
                     int comp, int r0, int r1)
{
    bool bi = p0 && p1;
    int modo = 0;   /* 0 média/cópia, 1 explícito, 2 implícito */
    if (h->tipo == 0 && h->ponderada) modo = 1;
    else if (h->tipo == 1 && h->bipred == 1) modo = 1;
    else if (h->tipo == 1 && h->bipred == 2 && bi) modo = 2;
    if (modo == 1 && !(bi ? (h->tem_peso[0][r0] || h->tem_peso[1][r1]) : (p0 ? h->tem_peso[0][r0] : h->tem_peso[1][r1]))) modo = 0;
    if (!modo) {
        for (int j = 0; j < hh; j++)
            for (int i = 0; i < w; i++)
                d[j * passo + i] = bi ? (uint8_t)((p0[j * 16 + i] + p1[j * 16 + i] + 1) >> 1) : (p0 ? p0 : p1)[j * 16 + i];
        return;
    }
    int lw = modo == 2 ? 5 : h->log2_peso[comp ? 1 : 0];
    int w0, w1, o0 = 0, o1 = 0;
    if (modo == 2) pesos_implicitos(h, r0, r1, &w0, &w1);
    else {
        int def = 1 << lw;
        w0 = h->tem_peso[0][r0] ? h->peso[0][r0][comp].w : def; o0 = h->tem_peso[0][r0] ? h->peso[0][r0][comp].o : 0;
        w1 = h->tem_peso[1][r1] ? h->peso[1][r1][comp].w : def; o1 = h->tem_peso[1][r1] ? h->peso[1][r1][comp].o : 0;
    }
    for (int j = 0; j < hh; j++)
        for (int i = 0; i < w; i++) {
            int v;
            if (bi) v = ((p0[j * 16 + i] * w0 + p1[j * 16 + i] * w1 + (1 << lw)) >> (lw + 1)) + ((o0 + o1 + 1) >> 1);
            else {
                int p = p0 ? p0[j * 16 + i] : p1[j * 16 + i], ww = p0 ? w0 : w1, oo = p0 ? o0 : o1;
                v = lw >= 1 ? ((p * ww + (1 << (lw - 1))) >> lw) + oo : p * ww + oo;
            }
            d[j * passo + i] = clip1(v);
        }
}

/* Faz a predição inter do MB inteiro a partir dos caches (refs e mvs das duas listas). */
static void reconstruir_inter(h264_t *h, ctx_t *c, mvc_t *k)
{
    quadro_t *q = h->atual;
    int x0 = c->mbx * 16, y0 = c->mby * 16;
    uint8_t t[2][16 * 16], tc[2][2][8 * 16];
    /* O caso mais comum (MB 16x16, pulado ou direto com um vetor só): o MB
     * inteiro tem a mesma referência e o mesmo vetor, e é previsto de uma
     * vez em vez de em quatro blocos 8x8, cada um buscando a sua margem. */
    bool tudo = true;
    for (int l = 0; l < 2 && tudo; l++)
        for (int j = 1; j < 5 && tudo; j++)
            for (int i = 1; i < 5; i++)
                if (k[l].ref[j][i] != k[l].ref[1][1] || k[l].mv[j][i][0] != k[l].mv[1][1][0] ||
                    k[l].mv[j][i][1] != k[l].mv[1][1][1]) { tudo = false; break; }
    for (int b8 = 0; b8 < (tudo ? 1 : 4); b8++) {
        int bx = (b8 & 1) * 2, by = (b8 >> 1) * 2;
        bool iguais = true;
        for (int l = 0; l < 2 && !tudo; l++)
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++)
                    if (k[l].ref[by + 1 + j][bx + 1 + i] != k[l].ref[by + 1][bx + 1] ||
                        k[l].mv[by + 1 + j][bx + 1 + i][0] != k[l].mv[by + 1][bx + 1][0] ||
                        k[l].mv[by + 1 + j][bx + 1 + i][1] != k[l].mv[by + 1][bx + 1][1]) iguais = false;
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++) {
                if (iguais && (i || j)) continue;
                int xx = bx + i, yy = by + j, tam = tudo ? 16 : iguais ? 8 : 4;
                int lx = x0 + xx * 4, ly = y0 + yy * 4, cx = lx / 2, cy = ly / 2, ct = tam / 2;
                int rr[2] = { -1, -1 };
                for (int l = 0; l < 2; l++) {
                    int ri = k[l].ref[yy + 1][xx + 1];
                    if (ri < 0 || ri > MAX_REF) continue;
                    quadro_t *ref = h->lista[l][ri];
                    if (!ref) ref = h->lista[l][0] ? h->lista[l][0] : q;
                    rr[l] = ri;
                    int mx = k[l].mv[yy + 1][xx + 1][0], my = k[l].mv[yy + 1][xx + 1][1];
                    mc_luma(h, ref, t[l], 16, lx, ly, tam, tam, mx, my);
                    mc_croma(h, ref->u, tc[l][0], 16, cx, cy, ct, ct, mx, my);
                    mc_croma(h, ref->v, tc[l][1], 16, cx, cy, ct, ct, mx, my);
                }
                if (rr[0] < 0 && rr[1] < 0) continue;
                combinar(h, q->y + ly * h->passo + lx, h->passo, rr[0] >= 0 ? t[0] : NULL, rr[1] >= 0 ? t[1] : NULL, tam, tam, 0, rr[0] < 0 ? 0 : rr[0], rr[1] < 0 ? 0 : rr[1]);
                for (int cp = 0; cp < 2; cp++)
                    combinar(h, (cp ? q->v : q->u) + cy * h->passo_c + cx, h->passo_c, rr[0] >= 0 ? tc[0][cp] : NULL, rr[1] >= 0 ? tc[1][cp] : NULL,
                             ct, ct, 1 + cp, rr[0] < 0 ? 0 : rr[0], rr[1] < 0 ? 0 : rr[1]);
            }
    }
}

/* Copia refs e mvs dos caches para o MB (para vizinhos, desbloqueio e quadros futuros). */
static void guardar_movimento(h264_t *h, mbinfo_t *m, mvc_t *k)
{
    for (int l = 0; l < 2; l++) {
        for (int i = 0; i < 4; i++) {
            int r = k[l].ref[(i >> 1) * 2 + 1][(i & 1) * 2 + 1];
            if (r < 0) r = -1;
            m->refidx[l][i] = (int8_t)r;
            quadro_t *rq = (r >= 0 && r <= MAX_REF) ? h->lista[l][r] : NULL;
            m->ref[l][i] = (int8_t)(rq ? rq->id & 127 : -1);
        }
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) {
                m->mv[l][y * 4 + x][0] = k[l].ref[y + 1][x + 1] >= 0 ? k[l].mv[y + 1][x + 1][0] : 0;
                m->mv[l][y * 4 + x][1] = k[l].ref[y + 1][x + 1] >= 0 ? k[l].mv[y + 1][x + 1][1] : 0;
                m->mvd[l][y * 4 + x][0] = k[l].mvd[y + 1][x + 1][0];
                m->mvd[l][y * 4 + x][1] = k[l].mvd[y + 1][x + 1][1];
            }
    }
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) m->dir[y * 4 + x] = k[0].dir[y + 1][x + 1];
}

/* --------------------------------------------------- resíduo e reconstrução */

static inline uint8_t *py_(h264_t *h, quadro_t *q, int mbx, int mby) { return q->y + mby * 16 * h->passo + mbx * 16; }

typedef struct {
    union {
        int lum[16][16];    /* por bloco raster, coeficientes na posição da matriz */
        int lum8[4][64];    /* com a transformada 8x8: por bloco 8x8, na posição y * 8 + x */
    };
    int dc[16];             /* DC da luma (16x16), na ordem de varredura */
    int cdc[2][4];
    int cro[2][4][16];
} coef_t;

/* Desfaz a quantização de um coeficiente (8.5.12.1 e 8.5.13.1): multiplica
 * pelo LevelScale da posição e por 2^(QP / 6), e divide por 2^base, com
 * arredondamento quando sobra divisão. A base é 4 nos blocos 4x4 e 6 nos 8x8
 * e no DC da luma 16x16; ela existe porque o LevelScale já traz o peso da
 * matriz de escala, que vale 16 quando "não muda nada". */
static inline int desq(int c, int ls, int q6, int base)
{
    if (q6 >= base) return c * ls * (1 << (q6 - base));
    return (c * ls + (1 << (base - 1 - q6))) >> (base - q6);
}

/* Predição intra + desquantização + IDCT. O MB inter já tem a predição no quadro.
 * As matrizes de escala (h->ls4, h->ls8) dependem de o MB ser intra ou inter
 * e, na croma, de ser Cb ou Cr. */
static void aplicar_residuo(h264_t *h, ctx_t *c, mbinfo_t *m, bool intra, int modo16, int modo_croma, int cbp, coef_t *k)
{
    quadro_t *q = h->atual;
    int passo = h->passo, pc = h->passo_c;
    int qp = h->qp, q6 = qp / 6, r6 = qp % 6;
    int (*lum)[16] = k->lum;
    const uint16_t *el = h->ls4[intra ? 0 : 3][r6];
    if (modo16 >= 0) {
        int cm[16], tmp[16];
        for (int i = 0; i < 16; i++) cm[zigzag[i]] = k->dc[i];
        for (int i = 0; i < 4; i++) {
            int *r = &cm[i * 4];
            int a = r[0] + r[1], bq = r[0] - r[1], cc = r[2] + r[3], dd = r[2] - r[3];
            tmp[i * 4 + 0] = a + cc; tmp[i * 4 + 1] = a - cc; tmp[i * 4 + 2] = bq - dd; tmp[i * 4 + 3] = bq + dd;
        }
        for (int i = 0; i < 4; i++) {
            int a = tmp[i] + tmp[4 + i], bq = tmp[i] - tmp[4 + i], cc = tmp[8 + i] + tmp[12 + i], dd = tmp[8 + i] - tmp[12 + i];
            int f[4] = { a + cc, a - cc, bq - dd, bq + dd };
            for (int kk = 0; kk < 4; kk++) lum[kk * 4 + i][0] = desq(f[kk], el[0], q6, 6);
        }
    }
    if (cbp & 0x30) {
        for (int comp = 0; comp < 2; comp++) {
            int qpc = m->qpc[comp], c6 = qpc / 6;
            const uint16_t *ec = h->ls4[(intra ? 1 : 4) + comp][qpc % 6];
            int a = k->cdc[comp][0], bq = k->cdc[comp][1], cc = k->cdc[comp][2], dd = k->cdc[comp][3];
            int f[4] = { a + bq + cc + dd, a - bq + cc - dd, a + bq - cc - dd, a - bq - cc + dd };
            for (int i = 0; i < 4; i++) {
                k->cro[comp][i][0] = (f[i] * ec[0] * (1 << c6)) >> 5;      /* DC da croma 4:2:0 (8.5.11.2) */
                for (int j = 1; j < 16; j++)
                    if (k->cro[comp][i][j]) k->cro[comp][i][j] = desq(k->cro[comp][i][j], ec[j], c6, 4);
            }
        }
    }
    if (m->t8) {
        const uint16_t *e8 = h->ls8[intra ? 0 : 1][r6];
        for (int b8 = 0; b8 < 4; b8++) {
            if (!(cbp & (1 << b8))) continue;
            int *l8 = k->lum8[b8];
            for (int j = 0; j < 64; j++) if (l8[j]) l8[j] = desq(l8[j], e8[j], q6, 6);
        }
    } else
        for (int blk = 0; blk < 16; blk++)
            for (int j = modo16 >= 0 ? 1 : 0; j < 16 && m->nz[blk]; j++)
                if (lum[blk][j]) lum[blk][j] = desq(lum[blk][j], el[j], q6, 4);
    uint8_t *Y = py_(h, q, c->mbx, c->mby);
    CONTA(0);
    if (m->t8) CONTA(intra ? 1 : 2);
    if (m->t8) {
        /* transformada 8x8: quatro blocos; no MB intra cada um é previsto e
         * reconstruído antes do seguinte, que usa as amostras dele */
        for (int b8 = 0; b8 < 4; b8++) {
            int bx = (b8 & 1) * 2, by = (b8 >> 1) * 2;
            if (intra) { pred8x8(h, c, b8, m->modo4[by * 4 + bx]); CONTA(7 + m->modo4[by * 4 + bx]); }
            if (cbp & (1 << b8)) idct8_somar(Y + by * 4 * passo + bx * 4, passo, k->lum8[b8]);
        }
    } else if (intra && modo16 < 0) {
        for (int i = 0; i < 16; i++) {
            int bx = blk_x[i], by = blk_y[i];
            int t[8], l[4], lt;
            bool tt, tl;
            bordas_luma(h, c, bx, by, t, l, &lt, &tt, &tl);
            uint8_t *d = Y + by * 4 * passo + bx * 4;
            pred4x4(d, passo, m->modo4[by * 4 + bx], t, l, lt, tt, tl);
            if (m->nz[by * 4 + bx]) idct4_somar(d, passo, lum[by * 4 + bx]);
        }
    } else {
        if (intra) {
            uint8_t t[16], l[16];
            int lt = 128;
            bool ta = c->b != NULL, la = c->a != NULL;
            for (int i = 0; i < 16; i++) {
                t[i] = ta ? Y[-passo + i] : 128;
                l[i] = la ? Y[i * passo - 1] : 128;
            }
            if (c->d) lt = Y[-passo - 1];
            if (modo16 == 0) for (int j = 0; j < 16; j++) memcpy(Y + j * passo, t, 16);
            else if (modo16 == 1) for (int j = 0; j < 16; j++) memset(Y + j * passo, l[j], 16);
            else if (modo16 == 2) {
                int s = 0, v;
                for (int i = 0; i < 16; i++) s += (ta ? t[i] : 0) + (la ? l[i] : 0);
                v = ta && la ? (s + 16) >> 5 : (ta || la) ? (s + 8) >> 4 : 128;
                for (int j = 0; j < 16; j++) memset(Y + j * passo, v, 16);
            } else pred_plano(Y, passo, 16, t, l, lt);
        }
        for (int i = 0; i < 16; i++) {
            int bx = i & 3, by = i >> 2;
            bool tem = modo16 >= 0 ? (m->nz[i] || lum[i][0]) : m->nz[i] != 0;
            if (tem) idct4_somar(Y + by * 4 * passo + bx * 4, passo, lum[i]);
        }
    }
    for (int comp = 0; comp < 2; comp++) {
        uint8_t *C = (comp ? q->v : q->u) + c->mby * 8 * pc + c->mbx * 8;
        if (intra) {
            uint8_t t[8], l[8];
            int lt = 128;
            bool ta = c->b != NULL, la = c->a != NULL;
            for (int i = 0; i < 8; i++) { t[i] = ta ? C[-pc + i] : 128; l[i] = la ? C[i * pc - 1] : 128; }
            if (c->d) lt = C[-pc - 1];
            if (modo_croma == 0) {
                for (int bl = 0; bl < 4; bl++) {
                    int xo = (bl & 1) * 4, yo = (bl >> 1) * 4, st = 0, sl = 0, v;
                    for (int i = 0; i < 4; i++) { st += t[xo + i]; sl += l[yo + i]; }
                    if ((xo == 0 && yo == 0) || (xo && yo)) v = ta && la ? (st + sl + 4) >> 3 : la ? (sl + 2) >> 2 : ta ? (st + 2) >> 2 : 128;
                    else if (xo) v = ta ? (st + 2) >> 2 : la ? (sl + 2) >> 2 : 128;
                    else v = la ? (sl + 2) >> 2 : ta ? (st + 2) >> 2 : 128;
                    for (int j = 0; j < 4; j++) memset(C + (yo + j) * pc + xo, v, 4);
                }
            } else if (modo_croma == 1) for (int j = 0; j < 8; j++) memset(C + j * pc, l[j], 8);
            else if (modo_croma == 2) for (int j = 0; j < 8; j++) memcpy(C + j * pc, t, 8);
            else pred_plano(C, pc, 8, t, l, lt);
        }
        if (cbp & 0x30)
            for (int i = 0; i < 4; i++)
                idct4_somar(C + (i >> 1) * 4 * pc + (i & 1) * 4, pc, k->cro[comp][i]);
    }
}

static void iniciar_mb(h264_t *h, mbinfo_t *m)
{
    memset(m->nz, 0, sizeof m->nz);
    m->fatia = h->fatia_atual;
    m->intra = m->i4x4 = m->i16 = m->skip = m->direto = m->t8 = 0;
    m->qpd = 0; m->cbp = 0; m->modo_croma = 0; m->dc = 0;
    for (int i = 0; i < 16; i++) { m->modo4[i] = 2; m->dir[i] = 0; }
    memset(m->mvd, 0, sizeof m->mvd);
}

/* QP da croma de um MB cuja luma tem QP "qp" (8.5.8 e tabela 8-15): o Cb
 * usa o ajuste chroma_qp_index_offset do PPS e o Cr, no High, pode ter outro
 * (second_chroma_qp_index_offset). */
static void qp_da_croma(h264_t *h, mbinfo_t *m, int qp)
{
    m->qpc[0] = (int8_t)qp_croma[clip(qp + h->croma_qp_off, 0, 51)];
    m->qpc[1] = (int8_t)qp_croma[clip(qp + h->croma_qp_off2, 0, 51)];
}

/* Modo intra mais provável do bloco 4x4 em (bx, by) (8.3.1.1 e 8.3.2.1): o
 * menor entre o do vizinho da esquerda e o do de cima; se falta um dos dois,
 * DC (2); vizinho que não é intra 4x4 nem 8x8 conta como DC. Serve também
 * para os blocos 8x8: o modo de um bloco 8x8 fica guardado nos seus quatro
 * 4x4, e a norma manda olhar justamente o 4x4 vizinho da primeira linha (à
 * esquerda) ou da primeira coluna (acima) do bloco 8x8. */
static int prever_modo(ctx_t *c, mbinfo_t *m, int bx, int by)
{
    int ma, mb;
    bool oka, okb;
    if (bx > 0) { ma = m->modo4[by * 4 + bx - 1]; oka = true; }
    else if (c->a) { ma = c->a->i4x4 ? c->a->modo4[by * 4 + 3] : 2; oka = true; }
    else { ma = 2; oka = false; }
    if (by > 0) { mb = m->modo4[(by - 1) * 4 + bx]; okb = true; }
    else if (c->b) { mb = c->b->i4x4 ? c->b->modo4[12 + bx] : 2; okb = true; }
    else { mb = 2; okb = false; }
    return (oka && okb) ? (ma < mb ? ma : mb) : 2;
}

static void gravar_modo(mbinfo_t *m, int bx, int by, int modo)
{
    m->modo4[by * 4 + bx] = (int8_t)modo;
    if (m->t8) m->modo4[by * 4 + bx + 1] = m->modo4[by * 4 + bx + 4] = m->modo4[by * 4 + bx + 5] = (int8_t)modo;
}

static void sem_movimento(mbinfo_t *m)
{
    for (int l = 0; l < 2; l++) for (int i = 0; i < 4; i++) { m->refidx[l][i] = -1; m->ref[l][i] = -1; }
    memset(m->mv, 0, sizeof m->mv);
}

static void pcm(h264_t *h, ctx_t *c, mbinfo_t *m, bits_t *b)
{
    quadro_t *q = h->atual;
    int passo = h->passo, pc = h->passo_c;
    b->pos = (b->pos + 7) & ~7u;
    uint8_t *y = py_(h, q, c->mbx, c->mby);
    for (int j = 0; j < 16; j++) for (int i = 0; i < 16; i++) y[j * passo + i] = (uint8_t)ler_bits(b, 8);
    for (int comp = 0; comp < 2; comp++) {
        uint8_t *d = (comp ? q->v : q->u) + c->mby * 8 * pc + c->mbx * 8;
        for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) d[j * pc + i] = (uint8_t)ler_bits(b, 8);
    }
    m->intra = 2;
    memset(m->nz, 16, sizeof m->nz);
    m->qp = 0; qp_da_croma(h, m, 0);
    m->cbp = 0x2F; m->dc = 7;
    sem_movimento(m);
}

/* --------------------------------------------------- MB por CAVLC (I e P) */

static int getenv_dbg_mb = -1, dbg_poc = -1;
static int decodificar_mb(h264_t *h, bits_t *b, ctx_t *c, int tipo_mb, bool skip)
{
    mbinfo_t *m = c->m;
    iniciar_mb(h, m);
    mvc_t k[2];
    bool inter = !skip && h->tipo == 0 && tipo_mb < 5;
    if (h->tipo == 0 && !skip && tipo_mb >= 5) tipo_mb -= 5;
    bool intra = !skip && !inter;
    int cbp = 0, modo16 = -1, modo_croma = 0;
    /* Um MB inter só pode usar a transformada 8x8 se nenhuma partição dele
     * for menor que 8x8 (7.3.5): senão um bloco 8x8 de resíduo cobriria
     * pedaços previstos com vetores diferentes. */
    bool pode8 = true;
    if (intra && tipo_mb == 25) { pcm(h, c, m, b); return 0; }
    if (intra) {
        m->intra = 1;
        if (tipo_mb == 0) {
            m->i4x4 = 1;
            /* I_NxN: com a transformada 8x8, a predição também é 8x8 (7.3.5) */
            if (h->t8_modo) m->t8 = (int8_t)ler_bit(b);
            for (int i = 0; i < 16; i += m->t8 ? 4 : 1) {    /* 16 modos 4x4 ou 4 modos 8x8 */
                int bx = blk_x[i], by = blk_y[i];
                int pred = prever_modo(c, m, bx, by);
                int modo;
                if (ler_bit(b)) modo = pred;
                else { int r = (int)ler_bits(b, 3); modo = r < pred ? r : r + 1; }
                gravar_modo(m, bx, by, modo);
            }
        } else {
            m->i16 = 1;
            modo16 = (tipo_mb - 1) % 4;
            cbp = (((tipo_mb - 1) / 4) % 3) << 4;
            if (tipo_mb >= 13) cbp |= 15;
        }
        modo_croma = (int)ue(b);
        sem_movimento(m);
    } else {
        mvc_preencher(c, &k[0], 0);
        mvc_preencher(c, &k[1], 1);
        for (int y = 1; y < 5; y++) for (int x = 1; x < 5; x++) k[1].ref[y][x] = REF_INTRA;
        int nref = h->num_ref[0];
        if (skip) {
            int mx, my;
            m->skip = 1;
            mv_skip_p(&k[0], &mx, &my);
            mvc_escrever(&k[0], 0, 0, 4, 4, 0, mx, my);
        } else if (tipo_mb < 3) {
            int np = tipo_mb == 0 ? 1 : 2;
            int ri[2] = { 0, 0 };
            for (int p = 0; p < np; p++)
                if (nref > 1) ri[p] = nref == 2 ? !ler_bit(b) : (int)ue(b);
            for (int p = 0; p < np; p++) {
                int bx, by, w, hh, forma;
                if (tipo_mb == 0) { bx = 0; by = 0; w = 4; hh = 4; forma = 0; }
                else if (tipo_mb == 1) { bx = 0; by = p * 2; w = 4; hh = 2; forma = 1 + p; }
                else { bx = p * 2; by = 0; w = 2; hh = 4; forma = 3 + p; }
                int mx, my;
                prever_mv(&k[0], bx, by, w, ri[p], forma, &mx, &my);
                mx += se(b); my += se(b);
                mvc_escrever(&k[0], bx, by, w, hh, ri[p], mx, my);
            }
        } else {
            int sub[4], ri[4] = { 0, 0, 0, 0 };
            for (int i = 0; i < 4; i++) { sub[i] = (int)ue(b) & 3; if (sub[i]) pode8 = false; }
            if (nref > 1 && tipo_mb != 4)
                for (int i = 0; i < 4; i++) ri[i] = nref == 2 ? !ler_bit(b) : (int)ue(b);
            for (int i = 0; i < 4; i++) {
                int bx0 = (i & 1) * 2, by0 = (i >> 1) * 2;
                int n = sub[i] == 0 ? 1 : sub[i] == 3 ? 4 : 2;
                for (int s = 0; s < n; s++) {
                    int bx = bx0, by = by0, w = 2, hh = 2;
                    if (sub[i] == 1) { hh = 1; by += s; }
                    else if (sub[i] == 2) { w = 1; bx += s; }
                    else if (sub[i] == 3) { w = 1; hh = 1; bx += s & 1; by += s >> 1; }
                    int mx, my;
                    prever_mv(&k[0], bx, by, w, ri[i], 0, &mx, &my);
                    mx += se(b); my += se(b);
                    mvc_escrever(&k[0], bx, by, w, hh, ri[i], mx, my);
                }
            }
        }
        guardar_movimento(h, m, k);
        reconstruir_inter(h, c, k);
    }
    if (!skip && modo16 < 0) {
        uint32_t cod = ue(b);
        if (cod > 47) return -1;
        cbp = intra ? cbp_intra[cod] : cbp_inter[cod];
        if (!intra && (cbp & 15) && h->t8_modo && pode8) m->t8 = (int8_t)ler_bit(b);
    }
    if (cbp || modo16 >= 0) {
        int d = se(b);
        if (d < -26 || d > 25) return -1;
        h->qp = (h->qp + d + 52) % 52;
    }
    m->qp = (int8_t)h->qp;
    qp_da_croma(h, m, h->qp);
    m->cbp = (int8_t)cbp; m->modo_croma = (int8_t)modo_croma;
    if (skip) return 0;
    coef_t kf;
    memset(&kf, 0, sizeof kf);
    if (modo16 >= 0) {
        int16_t dc[16];
        memset(dc, 0, sizeof dc);
        if (residuo(b, nc_luma(c, 0, 0), dc, 0, 16) < 0) return -1;
        for (int i = 0; i < 16; i++) kf.dc[i] = dc[i];
        if (cbp & 15)
            for (int i = 0; i < 16; i++) {
                int bx = blk_x[i], by = blk_y[i];
                int16_t t[16];
                memset(t, 0, sizeof t);
                int n = residuo(b, nc_luma(c, bx, by), t, 0, 15);
                if (n < 0) return -1;
                m->nz[by * 4 + bx] = (uint8_t)n;
                for (int j = 0; j < 15; j++) kf.lum[by * 4 + bx][zigzag[j + 1]] = t[j];
            }
    } else {
        for (int i = 0; i < 16; i++) {
            if (!(cbp & (1 << (i >> 2)))) continue;
            int bx = blk_x[i], by = blk_y[i];
            int16_t t[16];
            memset(t, 0, sizeof t);
            int n = residuo(b, nc_luma(c, bx, by), t, 0, 16);
            if (n < 0) return -1;
            m->nz[by * 4 + bx] = (uint8_t)n;
            /* No CAVLC um bloco 8x8 vem como quatro blocos de 16 coeficientes
             * entrelaçados (7.3.5.3.2): o bloco i4 leva as posições 4*j + i4
             * da varredura 8x8. Assim as tabelas do CAVLC, feitas para 16
             * coeficientes, servem sem mudança, e cada 4x4 guarda a sua
             * contagem para o nC dos vizinhos. */
            if (m->t8) for (int j = 0; j < 16; j++) kf.lum8[i >> 2][zigzag8[4 * j + (i & 3)]] = t[j];
            else for (int j = 0; j < 16; j++) kf.lum[by * 4 + bx][zigzag[j]] = t[j];
        }
    }
    if (cbp & 0x30) {
        for (int comp = 0; comp < 2; comp++) {
            int16_t cdc[4] = { 0, 0, 0, 0 };
            if (residuo(b, -1, cdc, 0, 4) < 0) return -1;
            for (int i = 0; i < 4; i++) kf.cdc[comp][i] = cdc[i];
        }
        if (cbp & 0x20)
            for (int comp = 0; comp < 2; comp++)
                for (int i = 0; i < 4; i++) {
                    int16_t t[16];
                    memset(t, 0, sizeof t);
                    int n = residuo(b, nc_croma(c, comp, i & 1, i >> 1), t, 0, 15);
                    if (n < 0) return -1;
                    m->nz[16 + comp * 4 + i] = (uint8_t)n;
                    for (int j = 0; j < 15; j++) kf.cro[comp][i][zigzag[j + 1]] = t[j];
                }
    }
    aplicar_residuo(h, c, m, intra, modo16, modo_croma, cbp, &kf);
    return 0;
}

/* -------------------------------------------------------- desbloqueio */

static void filtrar_borda(uint8_t *p, int passo_borda, int passo_amostra, int n, const int *bs,
                          int qpm, int ia_off, int ib_off, bool croma)
{
    int ia = clip(qpm + ia_off, 0, 51), ib = clip(qpm + ib_off, 0, 51);
    int alfa = alfa_t[ia], beta = beta_t[ib];
    if (!alfa || !beta) return;
    for (int i = 0; i < n; i++) {
        int s = bs[croma ? i / 2 : i / 4];
        if (!s) continue;
        uint8_t *q = p + i * passo_amostra;
        int p0 = q[-passo_borda], p1 = q[-2 * passo_borda], q0 = q[0], q1 = q[passo_borda];
        if (absi(p0 - q0) >= alfa || absi(p1 - p0) >= beta || absi(q1 - q0) >= beta) continue;
        if (croma) {
            if (s < 4) {
                int tc = tc0_t[ia][s - 1] + 1;
                int d = clip((((q0 - p0) << 2) + (p1 - q1) + 4) >> 3, -tc, tc);
                q[-passo_borda] = clip1(p0 + d);
                q[0] = clip1(q0 - d);
            } else {
                q[-passo_borda] = (uint8_t)((2 * p1 + p0 + q1 + 2) >> 2);
                q[0] = (uint8_t)((2 * q1 + q0 + p1 + 2) >> 2);
            }
            continue;
        }
        int p2 = q[-3 * passo_borda], q2 = q[2 * passo_borda];
        int ap = absi(p2 - p0), aq = absi(q2 - q0);
        if (s < 4) {
            int c0 = tc0_t[ia][s - 1];
            int tc = c0 + (ap < beta) + (aq < beta);
            int d = clip((((q0 - p0) << 2) + (p1 - q1) + 4) >> 3, -tc, tc);
            q[-passo_borda] = clip1(p0 + d);
            q[0] = clip1(q0 - d);
            if (ap < beta) q[-2 * passo_borda] = (uint8_t)(p1 + clip((p2 + ((p0 + q0 + 1) >> 1) - (p1 << 1)) >> 1, -c0, c0));
            if (aq < beta) q[passo_borda] = (uint8_t)(q1 + clip((q2 + ((p0 + q0 + 1) >> 1) - (q1 << 1)) >> 1, -c0, c0));
        } else {
            int p3 = q[-4 * passo_borda], q3 = q[3 * passo_borda];
            bool forte = absi(p0 - q0) < ((alfa >> 2) + 2);
            if (ap < beta && forte) {
                q[-passo_borda] = (uint8_t)((p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1 + 4) >> 3);
                q[-2 * passo_borda] = (uint8_t)((p2 + p1 + p0 + q0 + 2) >> 2);
                q[-3 * passo_borda] = (uint8_t)((2 * p3 + 3 * p2 + p1 + p0 + q0 + 4) >> 3);
            } else q[-passo_borda] = (uint8_t)((2 * p1 + p0 + q1 + 2) >> 2);
            if (aq < beta && forte) {
                q[0] = (uint8_t)((p1 + 2 * p0 + 2 * q0 + 2 * q1 + q2 + 4) >> 3);
                q[passo_borda] = (uint8_t)((p0 + q0 + q1 + q2 + 2) >> 2);
                q[2 * passo_borda] = (uint8_t)((2 * q3 + 3 * q2 + q1 + q0 + p0 + 4) >> 3);
            } else q[0] = (uint8_t)((2 * q1 + q0 + p1 + 2) >> 2);
        }
    }
}
/* ------------------------------------------------------------------ CABAC */

typedef struct {
    bits_t *b;
    uint32_t faixa, desl;
    uint8_t st[460];        /* estado*2 + MPS */
} cabac_t;

static void cabac_iniciar(h264_t *h, cabac_t *k, bits_t *b)
{
    k->b = b;
    const int8_t (*tab)[2] = h->tipo == 2 ? cabac_init_I : h->cabac_init == 0 ? cabac_init_PB0 : h->cabac_init == 1 ? cabac_init_PB1 : cabac_init_PB2;
    int qp = clip(h->qp, 0, 51);
    for (int i = 0; i < 460; i++) {
        int pre = clip(((tab[i][0] * qp) >> 4) + tab[i][1], 1, 126);
        k->st[i] = (uint8_t)(pre <= 63 ? (63 - pre) << 1 : ((pre - 64) << 1) | 1);
    }
    k->faixa = 510;
    k->desl = ler_bits(b, 9);
}

static inline int cab(cabac_t *k, int ctx)
{
    uint8_t s = k->st[ctx];
    int estado = s >> 1, mps = s & 1;
    uint32_t lps = cabac_lps[estado][(k->faixa >> 6) & 3];
    int bin;
    k->faixa -= lps;
    if (k->desl >= k->faixa) {
        bin = !mps;
        k->desl -= k->faixa;
        k->faixa = lps;
        if (!estado) mps = !mps;
        estado = cabac_trans_lps[estado];
    } else {
        bin = mps;
        if (estado < 62) estado++;
    }
    k->st[ctx] = (uint8_t)(estado << 1 | mps);
    while (k->faixa < 256) { k->faixa <<= 1; k->desl = (k->desl << 1) | (uint32_t)ler_bit(k->b); }
    return bin;
}

static inline int cab_bypass(cabac_t *k)
{
    k->desl = (k->desl << 1) | (uint32_t)ler_bit(k->b);
    if (k->desl >= k->faixa) { k->desl -= k->faixa; return 1; }
    return 0;
}

static inline int cab_fim(cabac_t *k)
{
    k->faixa -= 2;
    if (k->desl >= k->faixa) return 1;
    while (k->faixa < 256) { k->faixa <<= 1; k->desl = (k->desl << 1) | (uint32_t)ler_bit(k->b); }
    return 0;
}

/* tipo do MB intra (I: base 3; sufixo em P: 17, em B: 32) -> 0 I_NxN, 1..24 I_16x16, 25 I_PCM */
static int cab_intra(cabac_t *k, int base, bool fatia_i, ctx_t *c)
{
    if (fatia_i) {
        int inc = (c->a && !c->a->i4x4) + (c->b && !c->b->i4x4);
        if (!cab(k, base + inc)) return 0;
        base += 2;
    } else if (!cab(k, base)) return 0;
    if (cab_fim(k)) return 25;
    int t = 1;
    t += 12 * cab(k, base + 1);
    if (cab(k, base + 2)) t += 4 + 4 * cab(k, base + 2 + fatia_i);
    t += 2 * cab(k, base + 3 + fatia_i);
    t += cab(k, base + 3 + 2 * fatia_i);
    return t;
}

static int cab_mvd(cabac_t *k, int base, int soma)
{
    int inc = soma < 3 ? 0 : soma > 32 ? 2 : 1;
    if (!cab(k, base + inc)) return 0;
    int v = 1, ctx = base + 3;
    while (v < 9 && cab(k, ctx)) { if (v < 4) ctx++; v++; }
    if (v >= 9) {
        int e = 3;
        while (cab_bypass(k)) { v += 1 << e; e++; if (e > 24) break; }
        while (e--) v += cab_bypass(k) << e;
    }
    return cab_bypass(k) ? -v : v;
}

static const uint8_t cbf_off[5] = { 0, 4, 8, 12, 16 };
static const uint8_t sig_off[5] = { 0, 15, 29, 44, 47 };
static const uint8_t lev_off[5] = { 0, 10, 20, 30, 39 };

/* Bloco residual; coef na ordem de varredura. Devolve quantos não nulos, ou 0 se coded_block_flag = 0.
 *
 * A categoria 5 é o bloco 8x8 de luma do High (ctxBlockCat 5, 9.3.3.1.3):
 * em 4:2:0 ele não tem coded_block_flag (o bit do coded_block_pattern já diz
 * que há coeficientes); os contextos dele ficam em outra faixa (tabela 9-34:
 * 402 para "é significativo", 417 para "é o último", 426 para os valores); e
 * como 63 posições dividem 15 e 9 contextos, a tabela 9-43 (cabac_sig8 e
 * cabac_ult8) diz qual contexto cada posição usa. Os valores seguem a mesma
 * regra das outras categorias. */
static int cab_residuo(cabac_t *k, int cat, int inc_cbf, int *coef, int max)
{
    int b_sig, b_ult, b_niv;
    if (cat == 5) { b_sig = 402; b_ult = 417; b_niv = 426; }
    else {
        if (!cab(k, 85 + cbf_off[cat] + inc_cbf)) return 0;
        b_sig = 105 + sig_off[cat]; b_ult = 166 + sig_off[cat]; b_niv = 227 + lev_off[cat];
    }
    uint8_t sig[64];
    int ns = 0, i;
    for (i = 0; i < max - 1; i++) {
        int cs = cat == 5 ? cabac_sig8[i] : cat == 3 ? (i < 2 ? i : 2) : i;
        if (cab(k, b_sig + cs)) {
            sig[ns++] = (uint8_t)i;
            if (cab(k, b_ult + (cat == 5 ? cabac_ult8[i] : cs))) break;
        }
    }
    if (i == max - 1) sig[ns++] = (uint8_t)(max - 1);
    int gt1 = 0, eq1 = 0;
    for (int j = ns - 1; j >= 0; j--) {
        int ctx = b_niv + (gt1 ? 0 : (1 + eq1 < 4 ? 1 + eq1 : 4));
        int v;
        if (!cab(k, ctx)) { v = 1; eq1++; }
        else {
            int c2 = b_niv + 5 + (gt1 < 4 - (cat == 3) ? gt1 : 4 - (cat == 3));
            v = 2;
            while (v < 15 && cab(k, c2)) v++;
            if (v >= 15) {
                int e = 0;
                while (cab_bypass(k)) { e++; if (e > 20) break; }
                int x = 1;
                while (e--) x = (x << 1) | cab_bypass(k);
                v = 14 + x;
            }
            gt1++;
        }
        coef[sig[j]] = cab_bypass(k) ? -v : v;
    }
    return ns;
}

/* coded_block_flag dos vizinhos */
static int cbf_luma(ctx_t *c, int bx, int by, bool intra)
{
    mbinfo_t *m = c->m;
    if (bx < 0) { m = c->a; bx += 4; }
    else if (by < 0) { m = c->b; by += 4; }
    if (!m) return intra;
    if (m->intra == 2) return 1;
    return m->nz[by * 4 + bx] != 0;
}
static int cbf_croma_ac(ctx_t *c, int comp, int bx, int by, bool intra)
{
    mbinfo_t *m = c->m;
    if (bx < 0) { m = c->a; bx += 2; }
    else if (by < 0) { m = c->b; by += 2; }
    if (!m) return intra;
    if (m->intra == 2) return 1;
    if ((m->cbp >> 4) != 2) return 0;
    return m->nz[16 + comp * 4 + by * 2 + bx] != 0;
}
static int cbf_dc(mbinfo_t *n, int bit, bool intra, bool luma)
{
    if (!n) return intra;
    if (n->intra == 2) return 1;
    if (luma && !n->i16) return 0;
    if (!luma && !(n->cbp & 0x30)) return 0;
    return (n->dc >> bit) & 1;
}

static const uint8_t b_parte[23][2] = {   /* pred por partição: 1 L0, 2 L1, 3 Bi */
    {0,0},{1,0},{2,0},{3,0},{1,1},{1,1},{2,2},{2,2},{1,2},{1,2},{2,1},{2,1},{1,3},{1,3},{2,3},{2,3},{3,1},{3,1},{3,2},{3,2},{3,3},{3,3},{0,0} };
static const uint8_t b_sub[13][2] = {      /* pred, forma (0 8x8, 1 8x4, 2 4x8, 3 4x4) */
    {0,0},{1,0},{2,0},{3,0},{1,1},{1,2},{2,1},{2,2},{3,1},{3,2},{1,3},{2,3},{3,3} };

static int cab_ref(cabac_t *k, mvc_t *kk, int bx, int by)
{
    int ra = kk->ref[by + 1][bx], rb = kk->ref[by][bx + 1];
    int inc = (ra > 0 && !kk->dir[by + 1][bx]) + 2 * (rb > 0 && !kk->dir[by][bx + 1]);
    int r = 0;
    while (cab(k, 54 + inc)) { r++; inc = (inc >> 2) + 4; if (r > 31) break; }
    return r;
}

/* lê mvd de uma partição e grava mv + |mvd| no cache */
static void cab_mv(cabac_t *k, mvc_t *kk, int l, int bx, int by, int w, int hh, int forma, int ref)
{
    (void)l;
    int px, py;
    prever_mv(kk, bx, by, w, ref, forma, &px, &py);
    int sx = kk->mvd[by + 1][bx][0] + kk->mvd[by][bx + 1][0];
    int sy = kk->mvd[by + 1][bx][1] + kk->mvd[by][bx + 1][1];
    int dx = cab_mvd(k, 40, sx), dy = cab_mvd(k, 47, sy);
    mvc_escrever(kk, bx, by, w, hh, ref, px + dx, py + dy);
    int ax = absi(dx), ay = absi(dy);
    for (int y = by; y < by + hh; y++)
        for (int x = bx; x < bx + w; x++) { kk->mvd[y + 1][x + 1][0] = (uint8_t)(ax > 255 ? 255 : ax); kk->mvd[y + 1][x + 1][1] = (uint8_t)(ay > 255 ? 255 : ay); }
}

static int qpd_ant;   /* o MB anterior teve mb_qp_delta != 0 */

static int decodificar_mb_cabac(h264_t *h, cabac_t *k, ctx_t *c, bits_t *b)
{
    mbinfo_t *m = c->m;
    iniciar_mb(h, m);
    mvc_t kk[2];
    int fatia = h->tipo;   /* 0 P, 1 B, 2 I */
    bool skip = false;
    if (fatia != 2) {
        int base = fatia == 0 ? 11 : 24;
        int inc = (c->a && !c->a->skip) + (c->b && !c->b->skip);
        skip = cab(k, base + inc);
    }
    int tipo = 0;
    bool intra = false;
    if (!skip) {
        if (fatia == 2) { tipo = cab_intra(k, 3, true, c); intra = true; }
        else if (fatia == 0) {
            if (!cab(k, 14)) {
                if (!cab(k, 15)) tipo = 3 * cab(k, 16);
                else tipo = 2 - cab(k, 17);
            } else { tipo = cab_intra(k, 17, false, c); intra = true; }
        } else {
            int inc = (c->a && !(c->a->skip || c->a->direto)) + (c->b && !(c->b->skip || c->b->direto));
            if (!cab(k, 27 + inc)) tipo = 0;
            else if (!cab(k, 27 + 3)) tipo = 1 + cab(k, 27 + 5);
            else {
                int bits = cab(k, 27 + 4) << 3;
                bits |= cab(k, 27 + 5) << 2;
                bits |= cab(k, 27 + 5) << 1;
                bits |= cab(k, 27 + 5);
                if (bits < 8) tipo = bits + 3;
                else if (bits == 13) { tipo = cab_intra(k, 32, false, c); intra = true; }
                else if (bits == 14) tipo = 11;
                else if (bits == 15) tipo = 22;
                else { bits = (bits << 1) | cab(k, 27 + 5); tipo = bits - 4; }
            }
        }
    }
    int cbp = 0, modo16 = -1, modo_croma = 0;
    /* Um MB inter só pode usar a transformada 8x8 se nenhuma partição dele
     * for menor que 8x8 (7.3.5): senão um bloco 8x8 de resíduo cobriria
     * pedaços previstos com vetores diferentes. */
    bool pode8 = true;
    if (intra && getenv_dbg_mb == -3 && h->poc == dbg_poc) DBG("%sI", c->mbx ? "" : "\n");
    if (intra && tipo == 25) {
        /* I_PCM: os bytes começam alinhados depois do decodificador aritmético */
        pcm(h, c, m, b);
        cabac_iniciar(h, k, b);
        qpd_ant = 0;
        return 0;
    }
    if (intra) {
        m->intra = 1;
        if (tipo == 0) {
            m->i4x4 = 1;
            /* transform_size_8x8_flag (7.3.5): no I_NxN vem logo depois do
             * tipo, porque decide se os modos a seguir são 16 (4x4) ou 4
             * (8x8). O contexto (9.3.3.1.1.10) conta quantos dos vizinhos da
             * esquerda e de cima usaram a transformada 8x8. */
            if (h->t8_modo) m->t8 = (int8_t)cab(k, 399 + (c->a && c->a->t8) + (c->b && c->b->t8));
            for (int i = 0; i < 16; i += m->t8 ? 4 : 1) {    /* 16 modos 4x4 ou 4 modos 8x8 */
                int bx = blk_x[i], by = blk_y[i];
                int pred = prever_modo(c, m, bx, by);
                int modo;
                if (cab(k, 68)) modo = pred;
                else { int r = cab(k, 69); r |= cab(k, 69) << 1; r |= cab(k, 69) << 2; modo = r < pred ? r : r + 1; }
                gravar_modo(m, bx, by, modo);
            }
        } else {
            m->i16 = 1;
            modo16 = (tipo - 1) % 4;
            cbp = (((tipo - 1) / 4) % 3) << 4;
            if (tipo >= 13) cbp |= 15;
        }
        {
            int inc = (c->a && c->a->intra == 1 && c->a->modo_croma) + (c->b && c->b->intra == 1 && c->b->modo_croma);
            if (!cab(k, 64 + inc)) modo_croma = 0;
            else if (!cab(k, 67)) modo_croma = 1;
            else if (!cab(k, 67)) modo_croma = 2;
            else modo_croma = 3;
        }
        sem_movimento(m);
    } else {
        mvc_preencher(c, &kk[0], 0);
        mvc_preencher(c, &kk[1], 1);
        if (fatia == 0) for (int y = 1; y < 5; y++) for (int x = 1; x < 5; x++) kk[1].ref[y][x] = REF_INTRA;
        if (skip) {
            m->skip = 1;
            if (fatia == 0) { int mx, my; mv_skip_p(&kk[0], &mx, &my); mvc_escrever(&kk[0], 0, 0, 4, 4, 0, mx, my); }
            else { m->direto = 1; prever_direto(h, c, kk, 0xFFFF); for (int y = 1; y < 5; y++) for (int x = 1; x < 5; x++) kk[0].dir[y][x] = 1; }
        } else if (fatia == 0) {
            int nref = h->num_ref[0];
            if (tipo < 3) {
                int np = tipo == 0 ? 1 : 2, ri[2] = { 0, 0 };
                int pb[2][4] = { { 0, 0, 4, 4 }, { 0, 0, 0, 0 } };
                for (int p = 0; p < np; p++) {
                    int bx, by, w, hh;
                    if (tipo == 0) { bx = 0; by = 0; w = 4; hh = 4; }
                    else if (tipo == 1) { bx = 0; by = p * 2; w = 4; hh = 2; }
                    else { bx = p * 2; by = 0; w = 2; hh = 4; }
                    pb[p][0] = bx; pb[p][1] = by; pb[p][2] = w; pb[p][3] = hh;
                    if (nref > 1) ri[p] = cab_ref(k, &kk[0], bx, by);
                    mvc_escrever(&kk[0], bx, by, w, hh, ri[p], 0, 0);
                }
                for (int p = 0; p < np; p++) {
                    int forma = tipo == 0 ? 0 : tipo == 1 ? 1 + p : 3 + p;
                    /* o vetor do vizinho dentro do MB ainda não existe: limpa a ref antes de prever */
                    mvc_escrever(&kk[0], pb[p][0], pb[p][1], pb[p][2], pb[p][3], REF_NADA, 0, 0);
                    cab_mv(k, &kk[0], 0, pb[p][0], pb[p][1], pb[p][2], pb[p][3], forma, ri[p]);
                }
            } else {
                int sub[4], ri[4] = { 0, 0, 0, 0 };
                for (int i = 0; i < 4; i++) {
                    if (cab(k, 21)) sub[i] = 0;
                    else if (!cab(k, 22)) sub[i] = 1;
                    else sub[i] = cab(k, 23) ? 2 : 3;
                }
                for (int i = 0; i < 4; i++) {
                    if (sub[i]) pode8 = false;      /* partição 8x4, 4x8 ou 4x4 */
                    if (nref > 1 && tipo != 4) ri[i] = cab_ref(k, &kk[0], (i & 1) * 2, (i >> 1) * 2);
                    mvc_escrever(&kk[0], (i & 1) * 2, (i >> 1) * 2, 2, 2, ri[i], 0, 0);
                }
                for (int i = 0; i < 4; i++) mvc_escrever(&kk[0], (i & 1) * 2, (i >> 1) * 2, 2, 2, REF_NADA, 0, 0);
                for (int i = 0; i < 4; i++) {
                    int bx0 = (i & 1) * 2, by0 = (i >> 1) * 2;
                    int n = sub[i] == 0 ? 1 : sub[i] == 3 ? 4 : 2;
                    for (int s = 0; s < n; s++) {
                        int bx = bx0, by = by0, w = 2, hh = 2;
                        if (sub[i] == 1) { hh = 1; by += s; }
                        else if (sub[i] == 2) { w = 1; bx += s; }
                        else if (sub[i] == 3) { w = 1; hh = 1; bx += s & 1; by += s >> 1; }
                        cab_mv(k, &kk[0], 0, bx, by, w, hh, 0, ri[i]);
                    }
                }
            }
        } else {
            /* fatia B */
            if (tipo == 0) {
                m->direto = 1;
                /* na predição direta os vetores podem variar a cada 4x4, a não
                 * ser que o SPS garanta que são um por 8x8 (direct_8x8_inference) */
                pode8 = h->direto_8x8 != 0;
                prever_direto(h, c, kk, 0xFFFF);
                for (int y = 1; y < 5; y++) for (int x = 1; x < 5; x++) kk[0].dir[y][x] = 1;
            } else if (tipo < 22) {
                int np = tipo <= 3 ? 1 : 2, ri[2][2] = { { -1, -1 }, { -1, -1 } };
                int pb[2][4];
                for (int p = 0; p < np; p++) {
                    if (tipo <= 3) { pb[p][0] = 0; pb[p][1] = 0; pb[p][2] = 4; pb[p][3] = 4; }
                    else if (tipo % 2 == 0) { pb[p][0] = 0; pb[p][1] = p * 2; pb[p][2] = 4; pb[p][3] = 2; }
                    else { pb[p][0] = p * 2; pb[p][1] = 0; pb[p][2] = 2; pb[p][3] = 4; }
                }
                for (int l = 0; l < 2; l++)
                    for (int p = 0; p < np; p++) {
                        int pred = b_parte[tipo][p];
                        if (!(pred & (1 << l))) { mvc_escrever(&kk[l], pb[p][0], pb[p][1], pb[p][2], pb[p][3], REF_INTRA, 0, 0); continue; }
                        ri[l][p] = h->num_ref[l] > 1 ? cab_ref(k, &kk[l], pb[p][0], pb[p][1]) : 0;
                        mvc_escrever(&kk[l], pb[p][0], pb[p][1], pb[p][2], pb[p][3], ri[l][p], 0, 0);
                    }
                for (int l = 0; l < 2; l++)
                    for (int p = 0; p < np; p++) if (ri[l][p] >= 0) mvc_escrever(&kk[l], pb[p][0], pb[p][1], pb[p][2], pb[p][3], REF_NADA, 0, 0);
                for (int l = 0; l < 2; l++)
                    for (int p = 0; p < np; p++) {
                        if (ri[l][p] < 0) continue;
                        int forma = tipo <= 3 ? 0 : tipo % 2 == 0 ? 1 + p : 3 + p;
                        cab_mv(k, &kk[l], l, pb[p][0], pb[p][1], pb[p][2], pb[p][3], forma, ri[l][p]);
                    }
            } else {
                int sub[4], ri[2][4];
                for (int i = 0; i < 4; i++) {
                    int t;
                    if (!cab(k, 36)) t = 0;
                    else if (!cab(k, 37)) t = 1 + cab(k, 39);
                    else {
                        t = 3;
                        if (cab(k, 38)) {
                            if (cab(k, 39)) { t = 11 + cab(k, 39); goto achou; }
                            t += 4;
                        }
                        t += 2 * cab(k, 39);
                        t += cab(k, 39);
                    }
                achou:
                    sub[i] = t;
                }
                /* diretos primeiro (usam só os vizinhos de fora, como no 16x16) */
                unsigned dirs = 0;
                for (int i = 0; i < 4; i++) if (!sub[i]) dirs |= 0x33u << ((i & 1) * 2 + (i >> 1) * 8);
                for (int i = 0; i < 4; i++)
                    if (sub[i] ? b_sub[sub[i]][1] != 0 : !h->direto_8x8) pode8 = false;
                mvc_t salvo[2]; salvo[0] = kk[0]; salvo[1] = kk[1];
                if (dirs) {
                    prever_direto(h, c, kk, dirs);
                    for (int b2 = 0; b2 < 16; b2++) if (dirs & (1u << b2)) kk[0].dir[(b2 >> 2) + 1][(b2 & 3) + 1] = kk[1].dir[(b2 >> 2) + 1][(b2 & 3) + 1] = 1;
                }
                (void)salvo;
                for (int l = 0; l < 2; l++)
                    for (int i = 0; i < 4; i++) {
                        ri[l][i] = -1;
                        if (!sub[i]) continue;
                        int bx = (i & 1) * 2, by = (i >> 1) * 2;
                        if (!(b_sub[sub[i]][0] & (1 << l))) { mvc_escrever(&kk[l], bx, by, 2, 2, REF_INTRA, 0, 0); continue; }
                        ri[l][i] = h->num_ref[l] > 1 ? cab_ref(k, &kk[l], bx, by) : 0;
                        mvc_escrever(&kk[l], bx, by, 2, 2, ri[l][i], 0, 0);
                    }
                for (int l = 0; l < 2; l++)
                    for (int i = 0; i < 4; i++) if (ri[l][i] >= 0) mvc_escrever(&kk[l], (i & 1) * 2, (i >> 1) * 2, 2, 2, REF_NADA, 0, 0);
                for (int l = 0; l < 2; l++)
                    for (int i = 0; i < 4; i++) {
                        if (ri[l][i] < 0) continue;
                        int bx0 = (i & 1) * 2, by0 = (i >> 1) * 2, f = b_sub[sub[i]][1];
                        int n = f == 0 ? 1 : f == 3 ? 4 : 2;
                        for (int s = 0; s < n; s++) {
                            int bx = bx0, by = by0, w = 2, hh = 2;
                            if (f == 1) { hh = 1; by += s; }
                            else if (f == 2) { w = 1; bx += s; }
                            else if (f == 3) { w = 1; hh = 1; bx += s & 1; by += s >> 1; }
                            cab_mv(k, &kk[l], l, bx, by, w, hh, 0, ri[l][i]);
                        }
                    }
            }
        }
        guardar_movimento(h, m, kk);
        if (getenv_dbg_mb == -3 && h->poc == dbg_poc) DBG("%s%s", c->mbx ? "" : "\n", skip ? "d" : tipo == 0 ? "D" : tipo == 1 ? "<" : tipo == 2 ? ">" : tipo == 3 ? "B" : tipo == 22 ? "X" : fatia == 0 ? "P" : "p");
        if (getenv_dbg_mb == c->idx) {
            DBG("POC %d MB %d tipo %d skip %d fatia %d:", h->poc, c->idx, tipo, skip, fatia);
            for (int l = 0; l < 2; l++) for (int i = 0; i < 16; i += 1) DBG(" L%d[%d,%d r%d]", l, m->mv[l][i][0], m->mv[l][i][1], m->refidx[l][(i >> 3) * 2 + ((i & 3) >> 1)]);
            DBG("\n");
        }
        reconstruir_inter(h, c, kk);
    }
    if (skip) {
        m->qp = (int8_t)h->qp;
        qp_da_croma(h, m, h->qp);
        qpd_ant = 0;
        return 0;
    }
    /* coded_block_pattern */
    if (modo16 < 0) {
        int ca = c->a ? (c->a->intra == 2 ? 0x2F : c->a->cbp) : 0x0F, cb = c->b ? (c->b->intra == 2 ? 0x2F : c->b->cbp) : 0x0F;
        int l = 0;
        l |= cab(k, 73 + !(ca & 2) + 2 * !(cb & 4));
        l |= cab(k, 73 + !(l & 1) + 2 * !(cb & 8)) << 1;
        l |= cab(k, 73 + !(ca & 8) + 2 * !(l & 1)) << 2;
        l |= cab(k, 73 + !(l & 4) + 2 * !(l & 2)) << 3;
        int cca = c->a ? (c->a->intra == 2 ? 2 : (c->a->cbp >> 4) & 3) : 0, ccb = c->b ? (c->b->intra == 2 ? 2 : (c->b->cbp >> 4) & 3) : 0;
        int cr = 0;
        if (cab(k, 77 + (cca > 0) + 2 * (ccb > 0))) cr = 1 + cab(k, 77 + 4 + (cca == 2) + 2 * (ccb == 2));
        cbp = l | cr << 4;
        /* nos MBs inter o transform_size_8x8_flag vem depois do
         * coded_block_pattern, e só se houver resíduo de luma (7.3.5) */
        if (!intra && (cbp & 15) && h->t8_modo && pode8)
            m->t8 = (int8_t)cab(k, 399 + (c->a && c->a->t8) + (c->b && c->b->t8));
    }
    m->cbp = (int8_t)cbp; m->modo_croma = (int8_t)modo_croma;
    if (cbp || modo16 >= 0) {
        int v = 0;
        if (cab(k, 60 + qpd_ant)) {
            v = 1;
            int ctx = 62;
            while (cab(k, ctx)) { ctx = 63; v++; if (v > 104) break; }
        }
        int d = (v & 1) ? (v + 1) >> 1 : -(v >> 1);
        h->qp = ((h->qp + d) % 52 + 52) % 52;
        qpd_ant = d != 0;
        m->qpd = (int8_t)(d != 0);
    } else qpd_ant = 0;
    m->qp = (int8_t)h->qp;
    qp_da_croma(h, m, h->qp);
    coef_t kf;
    memset(&kf, 0, sizeof kf);
    int t[16];
    if (modo16 >= 0) {
        memset(t, 0, sizeof t);
        int inc = cbf_dc(c->a, 0, true, true) + 2 * cbf_dc(c->b, 0, true, true);
        if (cab_residuo(k, 0, inc, t, 16)) m->dc |= 1;
        for (int i = 0; i < 16; i++) kf.dc[i] = t[i];
        if (cbp & 15)
            for (int i = 0; i < 16; i++) {
                int bx = blk_x[i], by = blk_y[i];
                memset(t, 0, sizeof t);
                int inc2 = cbf_luma(c, bx - 1, by, true) + 2 * cbf_luma(c, bx, by - 1, true);
                m->nz[by * 4 + bx] = (uint8_t)cab_residuo(k, 1, inc2, t, 15);
                for (int j = 0; j < 15; j++) kf.lum[by * 4 + bx][zigzag[j + 1]] = t[j];
            }
    } else if (m->t8) {
        /* quatro blocos 8x8 de 64 coeficientes. Os quatro 4x4 de cada um
         * ficam marcados com a contagem dele: é o que os vizinhos (o
         * coded_block_flag deles, 9.3.3.1.1.9) e o filtro de desbloqueio
         * devem enxergar. */
        int t8[64];
        for (int b8 = 0; b8 < 4; b8++) {
            if (!(cbp & (1 << b8))) continue;
            memset(t8, 0, sizeof t8);
            int n = cab_residuo(k, 5, 0, t8, 64);
            for (int j = 0; j < 64; j++) kf.lum8[b8][zigzag8[j]] = t8[j];
            int b0 = (b8 >> 1) * 8 + (b8 & 1) * 2;
            m->nz[b0] = m->nz[b0 + 1] = m->nz[b0 + 4] = m->nz[b0 + 5] = (uint8_t)n;
        }
    } else {
        for (int i = 0; i < 16; i++) {
            if (!(cbp & (1 << (i >> 2)))) continue;
            int bx = blk_x[i], by = blk_y[i];
            memset(t, 0, sizeof t);
            int inc = cbf_luma(c, bx - 1, by, intra) + 2 * cbf_luma(c, bx, by - 1, intra);
            m->nz[by * 4 + bx] = (uint8_t)cab_residuo(k, 2, inc, t, 16);
            for (int j = 0; j < 16; j++) kf.lum[by * 4 + bx][zigzag[j]] = t[j];
        }
    }
    if (cbp & 0x30) {
        for (int comp = 0; comp < 2; comp++) {
            memset(t, 0, sizeof t);
            int inc = cbf_dc(c->a, 1 + comp, intra, false) + 2 * cbf_dc(c->b, 1 + comp, intra, false);
            if (cab_residuo(k, 3, inc, t, 4)) m->dc |= (uint8_t)(2 << comp);
            for (int i = 0; i < 4; i++) kf.cdc[comp][i] = t[i];
        }
        if (cbp & 0x20)
            for (int comp = 0; comp < 2; comp++)
                for (int i = 0; i < 4; i++) {
                    int bx = i & 1, by = i >> 1;
                    memset(t, 0, sizeof t);
                    int inc = cbf_croma_ac(c, comp, bx - 1, by, intra) + 2 * cbf_croma_ac(c, comp, bx, by - 1, intra);
                    m->nz[16 + comp * 4 + i] = (uint8_t)cab_residuo(k, 4, inc, t, 15);
                    for (int j = 0; j < 15; j++) kf.cro[comp][i][zigzag[j + 1]] = t[j];
                }
    }
    aplicar_residuo(h, c, m, intra, modo16, modo_croma, cbp, &kf);
    return 0;
}

static int mvdif(const int16_t *a, const int16_t *b) { return absi(a[0] - b[0]) >= 4 || absi(a[1] - b[1]) >= 4; }

/* O bloco de transformada que contém o 4x4 "b" tem coeficientes? Com a
 * transformada 8x8 a pergunta vale para o bloco 8x8 inteiro (8.7.2.1): no
 * CAVLC os coeficientes dele estão repartidos pelas contagens dos quatro 4x4. */
static inline bool tem_coef(const mbinfo_t *m, int b)
{
    if (!m->t8) return m->nz[b] != 0;
    int b0 = b & 10;    /* o 4x4 de cima à esquerda do 8x8: x e y sem o bit de baixo */
    return m->nz[b0] || m->nz[b0 + 1] || m->nz[b0 + 4] || m->nz[b0 + 5];
}

static int calc_bs(mbinfo_t *p, int bp, mbinfo_t *q, int bq, bool borda_mb)
{
    if (p->intra || q->intra) return borda_mb ? 4 : 3;
    if (tem_coef(p, bp) || tem_coef(q, bq)) return 2;
    int b8p = (bp >> 3) * 2 + ((bp & 3) >> 1), b8q = (bq >> 3) * 2 + ((bq & 3) >> 1);
    int p0 = p->ref[0][b8p], p1 = p->ref[1][b8p], q0 = q->ref[0][b8q], q1 = q->ref[1][b8q];
    int np = (p0 >= 0) + (p1 >= 0), nq = (q0 >= 0) + (q1 >= 0);
    if (np != nq) return 1;
    if (np == 1) {
        int lp = p0 >= 0 ? 0 : 1, lq = q0 >= 0 ? 0 : 1;
        if (p->ref[lp][b8p] != q->ref[lq][b8q]) return 1;
        return mvdif(p->mv[lp][bp], q->mv[lq][bq]);
    }
    if (!((p0 == q0 && p1 == q1) || (p0 == q1 && p1 == q0))) return 1;
    if (p0 != p1) {
        if (p0 == q0) return mvdif(p->mv[0][bp], q->mv[0][bq]) || mvdif(p->mv[1][bp], q->mv[1][bq]);
        return mvdif(p->mv[0][bp], q->mv[1][bq]) || mvdif(p->mv[1][bp], q->mv[0][bq]);
    }
    return (mvdif(p->mv[0][bp], q->mv[0][bq]) || mvdif(p->mv[1][bp], q->mv[1][bq])) &&
           (mvdif(p->mv[0][bp], q->mv[1][bq]) || mvdif(p->mv[1][bp], q->mv[0][bq]));
}

typedef struct { int modo, alfa, beta; } desbloq_t;

static void desbloquear(h264_t *h, const desbloq_t *dbf)
{
    quadro_t *qd = h->atual;
    for (int my = 0; my < h->alt_mb; my++)
        for (int mx = 0; mx < h->larg_mb; mx++) {
            mbinfo_t *m = &h->mb[my * h->larg_mb + mx];
            if (m->fatia < 0) continue;
            const desbloq_t *f = &dbf[m->fatia & 255];
            if (f->modo == 1) continue;
            for (int dir = 0; dir < 2; dir++)
                for (int e = 0; e < 4; e++) {
                    mbinfo_t *p = m;
                    /* com a transformada 8x8 as bordas 1 e 3 caem no meio de
                     * um bloco: ali não há degrau de transformada para alisar (8.7) */
                    if (m->t8 && (e & 1)) continue;
                    if (e == 0) {
                        if (dir == 0 && mx == 0) continue;
                        if (dir == 1 && my == 0) continue;
                        p = dir == 0 ? m - 1 : m - h->larg_mb;
                        if (p->fatia < 0) continue;
                        if (f->modo == 2 && p->fatia != m->fatia) continue;
                    }
                    int bs[4];
                    bool algum = false;
                    for (int i = 0; i < 4; i++) {
                        int bq = dir == 0 ? i * 4 + e : e * 4 + i;
                        int bp = e ? (dir == 0 ? bq - 1 : bq - 4) : (dir == 0 ? i * 4 + 3 : 12 + i);
                        bs[i] = calc_bs(p, bp, m, bq, e == 0);
                        if (bs[i]) algum = true;
                    }
                    if (!algum) continue;
                    int qpl = (p->qp + m->qp + 1) >> 1;
                    uint8_t *Y = qd->y + my * 16 * h->passo + mx * 16;
                    if (dir == 0) filtrar_borda(Y + e * 4, 1, h->passo, 16, bs, qpl, f->alfa, f->beta, false);
                    else filtrar_borda(Y + e * 4 * h->passo, h->passo, 1, 16, bs, qpl, f->alfa, f->beta, false);
                    if (e & 1) continue;
                    for (int comp = 0; comp < 2; comp++) {
                        int qpc = (p->qpc[comp] + m->qpc[comp] + 1) >> 1;
                        uint8_t *C = (comp ? qd->v : qd->u) + my * 8 * h->passo_c + mx * 8;
                        if (dir == 0) filtrar_borda(C + e * 2, 1, h->passo_c, 8, bs, qpc, f->alfa, f->beta, true);
                        else filtrar_borda(C + e * 2 * h->passo_c, h->passo_c, 1, 8, bs, qpc, f->alfa, f->beta, true);
                    }
                }
        }
}

/* ------------------------------------------------------------- fatias */

static desbloq_t dbf_fatias[256];

static void aplicar_mmco(h264_t *h)
{
    int maxf = 1 << h->log2_frame_num;
    for (int i = 0; i < h->n_mmco; i++) {
        int op = h->mmco[i][0], v = h->mmco[i][1];
        if (op == 1) {
            int num = h->atual->frame_num - v;
            for (int j = 0; j < N_QUADROS; j++) {
                quadro_t *q = &h->q[j];
                if (!q->ref || q == h->atual) continue;
                int pn = q->frame_num > h->atual->frame_num ? q->frame_num - maxf : q->frame_num;
                if (pn == num) q->ref = false;
            }
        } else if (op == 5) {
            for (int j = 0; j < N_QUADROS; j++) if (&h->q[j] != h->atual) h->q[j].ref = false;
            h->geracao++;
            h->atual->ordem = h->geracao * 2000000;
            h->poc_msb_ant = 0; h->poc_lsb_ant = 0;
        }
    }
}

static void fechar_quadro(h264_t *h)
{
    if (!h->quadro_aberto) return;
    desbloquear(h, dbf_fatias);
    if (h->nal_ref) {
        if (h->adaptativo) aplicar_mmco(h);
        h->atual->ref = true;
        if (!h->adaptativo) janela_deslizante(h);
    }
    h->atual->saida = true;
    h->quadro_aberto = false;
}

static mbinfo_t *vizinho(h264_t *h, int x, int y);

static void ler_pesos(h264_t *h, bits_t *b)
{
    h->log2_peso[0] = (int)ue(b);
    h->log2_peso[1] = (int)ue(b);
    for (int l = 0; l < (h->tipo == 1 ? 2 : 1); l++)
        for (int i = 0; i < h->num_ref[l]; i++) {
            h->tem_peso[l][i] = false;
            h->peso[l][i][0].w = (int16_t)(1 << h->log2_peso[0]); h->peso[l][i][0].o = 0;
            for (int cp = 1; cp < 3; cp++) { h->peso[l][i][cp].w = (int16_t)(1 << h->log2_peso[1]); h->peso[l][i][cp].o = 0; }
            if (ler_bit(b)) { h->peso[l][i][0].w = (int16_t)se(b); h->peso[l][i][0].o = (int16_t)se(b); h->tem_peso[l][i] = true; }
            if (ler_bit(b)) {
                for (int cp = 1; cp < 3; cp++) { h->peso[l][i][cp].w = (int16_t)se(b); h->peso[l][i][cp].o = (int16_t)se(b); }
                h->tem_peso[l][i] = true;
            }
        }
}

static int ler_fatia(h264_t *h, bits_t *b, int tipo_nal, int nal_ref)
{
    if (!h->sps_ok || !h->pps_ok) return H264_SEM_SUPORTE;
    int primeiro = (int)ue(b);
    int t = (int)ue(b) % 5;
    int tipo = t == 0 ? 0 : t == 1 ? 1 : t == 2 ? 2 : -1;
    if (tipo < 0) return H264_SEM_SUPORTE;
    ue(b);
    int frame_num = (int)ler_bits(b, h->log2_frame_num);
    bool idr = tipo_nal == 5;
    h->tipo = tipo;
    h->nal_ref = nal_ref;
    h->idr = idr;
    if (tipo == 1) h->viu_b = true;
    if (idr) ue(b);
    int lsb = 0, delta_inf = 0;
    if (h->poc_tipo == 0) {
        lsb = (int)ler_bits(b, h->log2_poc_lsb);
        if (h->poc_inferior) delta_inf = se(b);
    } else if (h->poc_tipo == 1 && !h->delta_poc_zero) {
        se(b);
        if (h->poc_inferior) se(b);
    }
    if (primeiro == 0) {
        fechar_quadro(h);
        if (idr) { for (int i = 0; i < N_QUADROS; i++) h->q[i].ref = false; h->geracao++; }
        h->frame_num = frame_num;
        calc_poc(h, lsb, delta_inf);
        quadro_t *livre = NULL;
        for (int i = 0; i < N_QUADROS; i++)
            if (!h->q[i].ref && !h->q[i].saida && &h->q[i] != h->atual) { livre = &h->q[i]; break; }
        if (!livre)   /* fila de saída cheia: descarta o mais antigo */
            for (int i = 0; i < N_QUADROS; i++) if (!h->q[i].ref && &h->q[i] != h->atual) { livre = &h->q[i]; break; }
        if (!livre) livre = &h->q[0];
        h->atual = livre;
        h->mb = livre->mb;
        livre->frame_num = frame_num;
        livre->poc = h->poc;
        livre->ordem = h->geracao * 2000000 + h->poc;
        livre->id = ++h->prox_id;
        livre->ref = false;
        livre->saida = false;
        h->quadro_aberto = true;
        h->fatia_atual = 0;
        for (int i = 0; i < h->larg_mb * h->alt_mb; i++) h->mb[i].fatia = -1;
    } else if (!h->quadro_aberto) return H264_ERRO;
    else h->fatia_atual++;
    h->frame_num = frame_num;
    if (h->redundante) ue(b);
    if (tipo == 1) h->direto_espacial = ler_bit(b);
    h->num_ref[0] = h->ref_padrao[0];
    h->num_ref[1] = h->ref_padrao[1];
    if (tipo != 2) {
        if (ler_bit(b)) {
            h->num_ref[0] = (int)ue(b) + 1;
            if (tipo == 1) h->num_ref[1] = (int)ue(b) + 1;
        }
        if (h->num_ref[0] > MAX_REF || h->num_ref[1] > MAX_REF) return H264_ERRO;
        montar_listas(h);
        modificar_lista(h, b, 0);
        if (tipo == 1) modificar_lista(h, b, 1);
        completar_listas(h);
        if (!h->lista[0][0]) {            /* começou sem referência: usa cinza */
            for (int l = 0; l < 2; l++) for (int i = 0; i <= MAX_REF; i++) h->lista[l][i] = h->atual;
            memset(h->atual->y, 128, h->larg * h->alt * 3 / 2);
        }
        if (tipo == 1 && !h->lista[1][0]) for (int i = 0; i <= MAX_REF; i++) h->lista[1][i] = h->lista[0][0];
        if (getenv_dbg_mb == -2 && primeiro == 0) {
            DBG("POC %d tipo %d ref %d num %d/%d L0:", h->poc, tipo, nal_ref, h->num_ref[0], h->num_ref[1]);
            for (int i = 0; i < h->num_ref[0]; i++) DBG(" %d", h->lista[0][i] ? h->lista[0][i]->poc : -99);
            DBG(" L1:");
            for (int i = 0; i < h->num_ref[1] && tipo == 1; i++) DBG(" %d", h->lista[1][i] ? h->lista[1][i]->poc : -99);
            DBG(" refs:");
            for (int i = 0; i < N_QUADROS; i++) if (h->q[i].ref) DBG(" %d", h->q[i].poc);
            DBG("\n");
        }
        memset(h->tem_peso, 0, sizeof h->tem_peso);
        if ((h->ponderada && tipo == 0) || (h->bipred == 1 && tipo == 1)) ler_pesos(h, b);
    }
    if (nal_ref) {
        if (idr) { ler_bit(b); ler_bit(b); if (primeiro == 0) h->adaptativo = false; }
        else {
            bool ad = ler_bit(b);
            int n = 0;
            if (ad)
                for (;;) {
                    int op = (int)ue(b);
                    if (op == 0) break;
                    if (op > 6) return H264_ERRO;
                    int v = 0;
                    if (op == 1 || op == 3) v = (int)ue(b) + 1;
                    if (op == 2) ue(b);
                    if (op == 3 || op == 6) ue(b);
                    if (op == 4) ue(b);
                    if (n < 32) { h->mmco[n][0] = op; h->mmco[n][1] = v; n++; }
                }
            if (primeiro == 0) { h->adaptativo = ad; h->n_mmco = n; }
        }
    } else if (primeiro == 0) h->adaptativo = false;
    if (h->cabac && tipo != 2) h->cabac_init = (int)ue(b);
    h->qp = h->qp_inicial + se(b);
    desbloq_t *f = &dbf_fatias[h->fatia_atual & 255];
    f->modo = 0; f->alfa = 0; f->beta = 0;
    if (h->ctrl_desbloq) {
        f->modo = (int)ue(b);
        if (f->modo != 1) { f->alfa = se(b) * 2; f->beta = se(b) * 2; }
    }
    /* dados da fatia */
    int total = h->larg_mb * h->alt_mb, mb = primeiro;
    CONTA(h->cabac ? 3 : 4);
    if (tipo == 1) CONTA(6);
    if (h->cabac) {
        if (tipo == 1 && !h->cabac) return H264_SEM_SUPORTE;
        b->pos = (b->pos + 7) & ~7u;
        cabac_t k;
        cabac_iniciar(h, &k, b);
        qpd_ant = 0;
        while (mb < total) {
            ctx_t c = { mb % h->larg_mb, mb / h->larg_mb, mb, &h->mb[mb], 0, 0, 0, 0 };
            c.a = vizinho(h, c.mbx - 1, c.mby); c.b = vizinho(h, c.mbx, c.mby - 1);
            c.c = vizinho(h, c.mbx + 1, c.mby - 1); c.d = vizinho(h, c.mbx - 1, c.mby - 1);
            h->mb[mb].fatia = h->fatia_atual;
            if (decodificar_mb_cabac(h, &k, &c, b) < 0) return H264_ERRO;
            mb++;
            if (cab_fim(&k)) break;
            if (b->pos > b->fim + 16) return H264_ERRO;
        }
        /* o decodificador aritmético anda até 9 bits à frente do que já usou */
        if ((int)b->fim - (int)b->pos < -1 || (int)b->fim - (int)b->pos > 7) CONTA(16);
        return 0;
    }
    if (tipo == 1) return H264_SEM_SUPORTE;   /* B com CAVLC: não implementado */
    while (mb < total) {
        if (tipo == 0) {
            int corrida = (int)ue(b);
            while (corrida-- > 0 && mb < total) {
                ctx_t c = { mb % h->larg_mb, mb / h->larg_mb, mb, &h->mb[mb], 0, 0, 0, 0 };
                c.a = vizinho(h, c.mbx - 1, c.mby); c.b = vizinho(h, c.mbx, c.mby - 1);
                c.c = vizinho(h, c.mbx + 1, c.mby - 1); c.d = vizinho(h, c.mbx - 1, c.mby - 1);
                h->mb[mb].fatia = h->fatia_atual;
                decodificar_mb(h, b, &c, 0, true);
                mb++;
            }
            if (mb >= total || !mais_dados(b)) break;
        }
        ctx_t c = { mb % h->larg_mb, mb / h->larg_mb, mb, &h->mb[mb], 0, 0, 0, 0 };
        c.a = vizinho(h, c.mbx - 1, c.mby); c.b = vizinho(h, c.mbx, c.mby - 1);
        c.c = vizinho(h, c.mbx + 1, c.mby - 1); c.d = vizinho(h, c.mbx - 1, c.mby - 1);
        h->mb[mb].fatia = h->fatia_atual;
        int tipo_mb = (int)ue(b);
        if (tipo_mb > (tipo == 0 ? 30 : 25)) return H264_ERRO;
        if (decodificar_mb(h, b, &c, tipo_mb, false) < 0) { DBG("mb %d tipo %d fatia %d pos %u/%u\n", mb, tipo_mb, tipo, b->pos, b->fim); return H264_ERRO; }
        mb++;
        if (!mais_dados(b)) break;
        if (b->pos > b->fim) return H264_ERRO;
    }
    return 0;
}

/* ---------------------------------------------------------- NAL / API */

int h264_amostra(h264_t *h, const uint8_t *d, int n, int tam_len)
{
    int pos = 0, r = 0;
    while (pos + tam_len <= n) {
        uint32_t l = 0;
        for (int i = 0; i < tam_len; i++) l = (l << 8) | d[pos + i];
        pos += tam_len;
        if (l > (uint32_t)(n - pos)) return H264_ERRO;
        int e = h264_nal(h, d + pos, (int)l);
        if (e < 0) r = e;
        pos += (int)l;
    }
    fechar_quadro(h);
    return r;
}

/* Próximo quadro na ordem de exibição. Com quadros B, segura alguns até saber a ordem. */
bool h264_imagem(h264_t *h, h264_imagem_t *im)
{
    int pend = 0;
    quadro_t *q = NULL;
    for (int i = 0; i < N_QUADROS; i++)
        if (h->q[i].saida) { pend++; if (!q || h->q[i].ordem < q->ordem) q = &h->q[i]; }
    int atraso = h->viu_b || h->perfil != 66 ? h->reordenar : 0;
    if (!q || (pend <= atraso && !h->esvaziar)) return false;
    q->saida = false;
    int cx = h->corte[0], cy = h->corte[2];
    im->larg = h->larg - h->corte[0] - h->corte[1];
    im->alt = h->alt - h->corte[2] - h->corte[3];
    im->passo_y = h->passo;
    im->passo_c = h->passo_c;
    im->y = q->y + cy * h->passo + cx;
    im->u = q->u + (cy / 2) * h->passo_c + cx / 2;
    im->v = q->v + (cy / 2) * h->passo_c + cx / 2;
    im->erros = h->erros;
    return true;
}

void h264_esvaziar(h264_t *h) { fechar_quadro(h); h->esvaziar = true; }

const char *h264_motivo(h264_t *h) { return h ? h->motivo : NULL; }

/* ---------------------------------------------------------- NAL / API */

int h264_nal(h264_t *h, const uint8_t *d, int n)
{
    if (n < 1) return 0;
    if (n + 8 > h->rbsp_cap) {
        if (h->rbsp) libera(h->rbsp);
        h->rbsp_cap = n + 64 + n / 4;
        h->rbsp = aloca(h->rbsp_cap);
        if (!h->rbsp) { h->rbsp_cap = 0; return H264_SEM_MEMORIA; }
    }
    int k = 0;
    for (int i = 1; i < n; i++) {   /* tira os bytes de prevenção 00 00 03 */
        if (i + 2 < n && d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 3) {
            h->rbsp[k++] = 0; h->rbsp[k++] = 0; i += 2;
            continue;
        }
        h->rbsp[k++] = d[i];
    }
    memset(h->rbsp + k, 0, 8);
    while (k > 0 && !h->rbsp[k - 1]) k--;
    if (!k) return 0;
    bits_t b = { h->rbsp, 0, (uint32_t)k * 8 - 1 - (uint32_t)__builtin_ctz(h->rbsp[k - 1]) };
    int tipo = d[0] & 31, ref = (d[0] >> 5) & 3;
    switch (tipo) {
    case 7: return ler_sps(h, &b);
    case 8: return ler_pps(h, &b);
    case 1: case 5: {
        int r = ler_fatia(h, &b, tipo, ref);
        if (r < 0) h->erros++;
        return r;
    }
    default: return 0;
    }
}

int h264_config(h264_t *h, const uint8_t *avcc, int n, int *tam_len)
{
    if (n < 7) return H264_ERRO;
    *tam_len = (avcc[4] & 3) + 1;
    int pos = 5, qtd = avcc[pos++] & 31;
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < qtd; i++) {
            if (pos + 2 > n) return H264_ERRO;
            int l = (avcc[pos] << 8) | avcc[pos + 1];
            pos += 2;
            if (pos + l > n) return H264_ERRO;
            int e = h264_nal(h, avcc + pos, l);
            if (e < 0) return e;
            pos += l;
        }
        if (t == 0) { if (pos >= n) return H264_ERRO; qtd = avcc[pos++]; }
    }
    return 0;
}

#ifdef H264_HOST
int h264_dbg_mb(int m) { getenv_dbg_mb = m; if (getenv("POC")) dbg_poc = atoi(getenv("POC")); return 0; }
#endif
