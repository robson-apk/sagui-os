/* md5sum (md5) - calcula o resumo MD5 de arquivos, ou confere uma lista
 * de resumos (H91; a ideia veio do fork SaguiOS-Extended, do Alejandro
 * Anuncibay, e o programa foi reescrito sobre a libsagui atual).
 *
 *   md5sum [arquivo...]       um resumo por arquivo: "resumo  nome"
 *   md5sum -c lista.md5       confere cada linha "resumo  nome" da lista
 *
 * O MD5 vem da RFC 1321: o arquivo é completado até um múltiplo de 64
 * bytes (um bit 1, zeros e o tamanho em bits em 8 bytes, o byte baixo
 * primeiro) e cada bloco de 64 bytes passa por 64 passos que misturam os
 * quatro números de 32 bits do estado. O MD5 não serve mais para
 * segurança (dá para fabricar dois arquivos com o mesmo resumo), mas
 * continua bom para conferir se uma cópia chegou inteira. Os outros
 * resumos do SaguiOS (SHA-2) ficam em userland/cripto. */
#include "sagui.h"

typedef struct {
    uint32_t estado[4];
    uint8_t  bloco[64];
    uint32_t usados;      /* bytes esperando no bloco */
    uint64_t total;       /* bytes recebidos */
} md5_t;

/* Tabela T da seção 3.4 da RFC 1321: T[i] é a parte inteira de
 * 4294967296 * |seno(i + 1)|, com o ângulo em radianos. */
static const uint32_t T[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

/* Quanto cada passo gira, rodada por rodada (seção 3.4: os números que
 * aparecem nos quatro grupos de 16 operações). */
static const uint8_t GIRO[4][4] = { { 7, 12, 17, 22 }, { 5, 9, 14, 20 }, { 4, 11, 16, 23 }, { 6, 10, 15, 21 } };

static uint32_t girar(uint32_t x, int n) { return x << n | x >> (32 - n); }

static void md5_bloco(md5_t *m, const uint8_t *b)
{
    uint32_t x[16];
    for (int i = 0; i < 16; i++)   /* palavras de 32 bits, o byte baixo primeiro */
        x[i] = (uint32_t)b[4 * i] | (uint32_t)b[4 * i + 1] << 8 | (uint32_t)b[4 * i + 2] << 16 | (uint32_t)b[4 * i + 3] << 24;
    uint32_t a = m->estado[0], bb = m->estado[1], c = m->estado[2], d = m->estado[3];
    for (int i = 0; i < 64; i++) {
        int rodada = i / 16, k;
        uint32_t f;
        /* As quatro funções F, G, H e I e a ordem em que cada rodada lê as
         * 16 palavras do bloco (k), como na seção 3.4. */
        if (rodada == 0)      { f = (bb & c) | (~bb & d); k = i; }
        else if (rodada == 1) { f = (bb & d) | (c & ~d);  k = (1 + 5 * i) % 16; }
        else if (rodada == 2) { f = bb ^ c ^ d;            k = (5 + 3 * i) % 16; }
        else                  { f = c ^ (bb | ~d);         k = (7 * i) % 16; }
        uint32_t novo = bb + girar(a + f + x[k] + T[i], GIRO[rodada][i % 4]);
        a = d; d = c; c = bb; bb = novo;
    }
    m->estado[0] += a; m->estado[1] += bb; m->estado[2] += c; m->estado[3] += d;
}

static void md5_iniciar(md5_t *m)
{
    /* Valores iniciais da seção 3.3 (01 23 45 67..., o byte baixo primeiro). */
    m->estado[0] = 0x67452301; m->estado[1] = 0xefcdab89; m->estado[2] = 0x98badcfe; m->estado[3] = 0x10325476;
    m->usados = 0;
    m->total = 0;
}

static void md5_somar(md5_t *m, const uint8_t *p, unsigned n)
{
    m->total += n;
    while (n) {
        unsigned cabe = 64 - m->usados, k = n < cabe ? n : cabe;
        memcpy(m->bloco + m->usados, p, k);
        m->usados += k; p += k; n -= k;
        if (m->usados == 64) { md5_bloco(m, m->bloco); m->usados = 0; }
    }
}

static void md5_terminar(md5_t *m, char hex[33])
{
    uint64_t bits = m->total * 8;
    uint8_t um = 0x80, zero = 0, tam[8];
    md5_somar(m, &um, 1);
    while (m->usados != 56) md5_somar(m, &zero, 1);
    for (int i = 0; i < 8; i++) tam[i] = (uint8_t)(bits >> (8 * i));
    md5_somar(m, tam, 8);
    for (int i = 0; i < 16; i++) {
        uint8_t byte = (uint8_t)(m->estado[i / 4] >> (8 * (i % 4)));
        hex[2 * i] = "0123456789abcdef"[byte >> 4];
        hex[2 * i + 1] = "0123456789abcdef"[byte & 15];
    }
    hex[32] = 0;
}

/* Resumo de um arquivo (ou da entrada, com "-"); false se não deu para ler. */
static bool resumir(const char *nome, char hex[33])
{
    int fd = abrir_para_ler("md5sum", nome);
    if (fd < 0) return false;
    md5_t m;
    md5_iniciar(&m);
    static uint8_t buf[4096];
    int n;
    while ((n = ler(fd, buf, sizeof buf)) > 0) md5_somar(&m, buf, (unsigned)n);
    if (fd != ENTRADA) fechar(fd);
    md5_terminar(&m, hex);
    return true;
}

/* -c: cada linha da lista tem 32 letras hexadecimais, dois espaços (ou
 * espaço e "*") e o nome. */
static int conferir(const char *lista)
{
    int fd = abrir_para_ler("md5sum", lista);
    if (fd < 0) return 1;
    unsigned n;
    char *b = ler_ate_o_fim(fd, &n);
    if (fd != ENTRADA) fechar(fd);
    if (!b) return 1;
    int ruins = 0, mal_formadas = 0;
    for (char *l = b; l < b + n;) {
        char *f = acha_char(l, '\n');
        if (f) { *f = 0; if (f > l && f[-1] == '\r') f[-1] = 0; }
        if (*l) {
            if (tam_texto(l) < 35 || l[32] != ' ' || (l[33] != ' ' && l[33] != '*')) mal_formadas++;
            else {
                char hex[33], esperado[33];
                for (int i = 0; i < 32; i++) esperado[i] = (char)minuscula_latin1(l[i]);
                esperado[32] = 0;
                if (!resumir(l + 34, hex)) { imprimir("%s: NÃO DEU PARA LER\n", l + 34); ruins++; }
                else if (compara_texto(hex, esperado) == 0) imprimir("%s: OK\n", l + 34);
                else { imprimir("%s: FALHOU\n", l + 34); ruins++; }
            }
        }
        l = f ? f + 1 : b + n;
    }
    if (mal_formadas) imprimir_em(ERROS, "md5sum: %d linha(s) fora do formato \"resumo  nome\"\n", mal_formadas);
    if (ruins) imprimir_em(ERROS, "md5sum: %d arquivo(s) não conferem\n", ruins);
    return ruins || mal_formadas ? 1 : 0;
}

int principal(int argc, char **argv)
{
    int i = 1;
    bool checar = false;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        if (compara_texto(argv[i], "-c") == 0) checar = true;
        else { imprimir_em(ERROS, "uso: md5sum [arquivo...]  ou  md5sum -c lista\n"); return 1; }
    }
    if (checar) return conferir(i < argc ? argv[i] : "-");
    int erros = 0;
    for (int k = i; k < (i < argc ? argc : i + 1); k++) {
        const char *nome = i < argc ? argv[k] : "-";
        char hex[33];
        if (!resumir(nome, hex)) { erros++; continue; }
        imprimir("%s  %s\n", hex, nome);
    }
    return erros ? 1 : 0;
}
