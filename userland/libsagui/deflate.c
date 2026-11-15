/* deflate.c - compressão DEFLATE (RFC 1951), zlib (RFC 1950), CRC-32 e
 * Adler-32 (H87). Usada pela captura de tela (PNG) e pelo zip.
 *
 * Como funciona: o LZ77 procura, para cada posição, a sequência mais longa
 * que já apareceu nos últimos 32 KB. Uma tabela de dispersão dos 3 primeiros
 * bytes leva à última posição com o mesmo começo, e "anterior" encadeia as
 * posições mais velhas (até CADEIA_MAX tentativas). O que acha vira um par
 * tamanho/distância; o resto sai como bytes literais. Tudo é escrito num
 * bloco só com os códigos de Huffman fixos da RFC 1951 (seção 3.2.6): sem
 * tabela própria, mais simples, e bom o bastante para telas e textos. */
#include "compactar.h"

#define JANELA     32768
#define DISPERSAO  (1 << 15)
#define CADEIA_MAX 48
#define TAM_MIN    3
#define TAM_MAX    258

/* ---------------------------------------------------------------- somas */

uint32_t crc32(uint32_t crc, const void *dados, uint32_t n)
{
    static uint32_t tabela[256];
    static bool pronta;
    if (!pronta) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tabela[i] = c;
        }
        pronta = true;
    }
    const uint8_t *p = dados;
    crc = ~crc;
    for (uint32_t i = 0; i < n; i++) crc = tabela[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

uint32_t adler32(uint32_t soma, const void *dados, uint32_t n)
{
    const uint8_t *p = dados;
    uint32_t a = soma & 0xFFFF, b = soma >> 16;
    while (n > 0) {
        uint32_t bloco = n < 5552 ? n : 5552;   /* sem estourar 32 bits antes do módulo */
        n -= bloco;
        while (bloco--) { a += *p++; b += a; }
        a %= 65521; b %= 65521;
    }
    return b << 16 | a;
}

/* ---------------------------------------------------------------- bits */

typedef struct {
    uint8_t *saida;
    uint32_t max, pos;
    uint32_t bits, qtd;
    bool cheio;
} escritor_t;

static void por_bits(escritor_t *e, uint32_t v, int n)
{
    e->bits |= v << e->qtd;
    e->qtd += (uint32_t)n;
    while (e->qtd >= 8) {
        if (e->pos >= e->max) { e->cheio = true; e->qtd = 0; e->bits = 0; return; }
        e->saida[e->pos++] = (uint8_t)e->bits;
        e->bits >>= 8;
        e->qtd -= 8;
    }
}

/* Os códigos de Huffman vão do bit mais significativo para o menos. */
static void por_codigo(escritor_t *e, uint32_t codigo, int n)
{
    uint32_t r = 0;
    for (int i = 0; i < n; i++) { r = r << 1 | (codigo & 1); codigo >>= 1; }
    por_bits(e, r, n);
}

static void simbolo(escritor_t *e, int s)
{
    if (s < 144) por_codigo(e, 0x30u + (uint32_t)s, 8);
    else if (s < 256) por_codigo(e, 0x190u + (uint32_t)(s - 144), 9);
    else if (s < 280) por_codigo(e, (uint32_t)(s - 256), 7);
    else por_codigo(e, 0xC0u + (uint32_t)(s - 280), 8);
}

static const uint16_t TAM_BASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t TAM_EXTRA[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                       3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t DIST_BASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                        8193, 12289, 16385, 24577 };
static const uint8_t DIST_EXTRA[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void par(escritor_t *e, int tam, int dist)
{
    int t = 28;
    while (TAM_BASE[t] > tam) t--;
    simbolo(e, 257 + t);
    if (TAM_EXTRA[t]) por_bits(e, (uint32_t)(tam - TAM_BASE[t]), TAM_EXTRA[t]);
    int d = 29;
    while (DIST_BASE[d] > dist) d--;
    por_codigo(e, (uint32_t)d, 5);
    if (DIST_EXTRA[d]) por_bits(e, (uint32_t)(dist - DIST_BASE[d]), DIST_EXTRA[d]);
}

static uint32_t dispersar(const uint8_t *p)
{
    return ((uint32_t)p[0] << 10 ^ (uint32_t)p[1] << 5 ^ p[2]) & (DISPERSAO - 1);
}

/* ---------------------------------------------------------------- deflate */

int deflar(const uint8_t *d, uint32_t n, uint8_t *saida, uint32_t max)
{
    int32_t *cabeca = aloca(DISPERSAO * sizeof *cabeca);
    int32_t *anterior = aloca(JANELA * sizeof *anterior);
    if (!cabeca || !anterior) { libera(cabeca); libera(anterior); return -1; }
    for (int i = 0; i < DISPERSAO; i++) cabeca[i] = -1;

    escritor_t e = { saida, max, 0, 0, 0, false };
    por_bits(&e, 1, 1);   /* último bloco */
    por_bits(&e, 1, 2);   /* códigos fixos */
    uint32_t i = 0;
    while (i < n && !e.cheio) {
        int melhor = 0, dist = 0;
        if (i + TAM_MIN <= n) {
            uint32_t h = dispersar(d + i);
            int32_t cand = cabeca[h];
            int limite = n - i < TAM_MAX ? (int)(n - i) : TAM_MAX;
            for (int tentativas = 0; cand >= 0 && i - (uint32_t)cand <= JANELA && tentativas < CADEIA_MAX; tentativas++) {
                const uint8_t *a = d + cand, *b = d + i;
                if (a[melhor] == b[melhor]) {
                    int t = 0;
                    while (t < limite && a[t] == b[t]) t++;
                    if (t > melhor) { melhor = t; dist = (int)(i - (uint32_t)cand); if (t == limite) break; }
                }
                int32_t prox = anterior[cand & (JANELA - 1)];
                if (prox >= cand) break;
                cand = prox;
            }
        }
        uint32_t passo = 1;
        if (melhor >= TAM_MIN) { par(&e, melhor, dist); passo = (uint32_t)melhor; }
        else simbolo(&e, d[i]);
        for (uint32_t k = 0; k < passo; k++, i++) {   /* põe cada posição nas tabelas */
            if (i + TAM_MIN <= n) {
                uint32_t h = dispersar(d + i);
                anterior[i & (JANELA - 1)] = cabeca[h];
                cabeca[h] = (int32_t)i;
            }
        }
    }
    simbolo(&e, 256);
    if (e.qtd > 0 && !e.cheio) por_bits(&e, 0, 8 - (int)e.qtd);   /* completa o último byte */
    libera(cabeca);
    libera(anterior);
    return e.cheio ? -1 : (int)e.pos;
}

int deflar_zlib(const uint8_t *d, uint32_t n, uint8_t *saida, uint32_t max)
{
    if (max < 6) return -1;
    saida[0] = 0x78;   /* deflate, janela de 32 KB */
    saida[1] = 0x01;   /* 0x7801 é múltiplo de 31; sem dicionário, nível "rápido" */
    int r = deflar(d, n, saida + 2, max - 6);
    if (r < 0) return -1;
    uint32_t s = adler32(1, d, n), k = 2 + (uint32_t)r;
    saida[k] = (uint8_t)(s >> 24); saida[k + 1] = (uint8_t)(s >> 16);
    saida[k + 2] = (uint8_t)(s >> 8); saida[k + 3] = (uint8_t)s;
    return (int)k + 4;
}
