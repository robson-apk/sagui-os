/* compactar.c - CRC-32, Adler-32, DEFLATE (nos dois sentidos), zlib e gzip
 * (H93).
 *
 * De onde veio o conhecimento:
 *   - RFC 1951 (DEFLATE 1.3): o formato dos blocos, os códigos de Huffman
 *     canônicos (seção 3.2.2), as tabelas de tamanhos e distâncias (3.2.5),
 *     os códigos fixos (3.2.6), o cabeçalho dos códigos dinâmicos (3.2.7) e a
 *     ideia geral do compactador com tabela de hash de 3 bytes (seção 4);
 *   - RFC 1950 (zlib) e RFC 1952 (gzip), inclusive o CRC-32 da ISO 3309;
 *   - D. A. Huffman, "A Method for the Construction of Minimum-Redundancy
 *     Codes" (1952), para montar os códigos ótimos.
 *
 * O descompactador nasceu no navegador (userland/navegador/descompactar.c,
 * H59) e veio para cá para servir a todos (gzip, tar, zip, PNG, fontes),
 * agora conferindo mais coisas (bloco guardado com NLEN, códigos
 * incompletos), contando os bytes usados e podendo alocar a saída. O
 * compactador foi escrito para o SaguiOS a partir da RFC 1951; o formato
 * não obriga a nenhum jeito de procurar repetições, então as escolhas
 * (tamanho das cadeias, quando esperar um byte, quando trocar de bloco)
 * são nossas e estão explicadas onde aparecem. */
#include "sagui.h"
#include "compactar.h"

/* ================================================================ somas */

/* CRC-32 (ISO 3309, RFC 1952 seção 8): o resto da divisão dos bits por um
 * polinômio de grau 32. Os bits entram do menos significativo para o mais,
 * por isso o polinômio aparece "espelhado" (0xEDB88320). A tabela guarda o
 * efeito de cada byte possível, para processar 8 bits de uma vez. */
static uint32_t tabela_crc[256];
static bool tabela_crc_pronta;

uint32_t crc32_somar(uint32_t crc, const void *dados, size_t n)
{
    if (!tabela_crc_pronta) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tabela_crc[i] = c;
        }
        tabela_crc_pronta = true;
    }
    const uint8_t *p = dados;
    crc ^= 0xFFFFFFFFu;   /* o CRC começa com todos os bits em 1 e termina invertido */
    while (n--) crc = tabela_crc[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* Adler-32 (RFC 1950 seção 8.2): duas somas módulo 65521 (o maior primo
 * abaixo de 2^16). Subtraímos o módulo assim que passa dele: assim nada
 * estoura e não precisamos dividir. */
uint32_t adler32_somar(uint32_t adler, const void *dados, size_t n)
{
    const uint8_t *p = dados;
    uint32_t a = adler & 0xFFFF, b = adler >> 16;
    while (n--) {
        a += *p++;
        if (a >= 65521) a -= 65521;
        b += a;
        if (b >= 65521) b -= 65521;
    }
    return b << 16 | a;
}

/* ================================================================ tabelas da RFC 1951 (3.2.5) */

/* Símbolos 257 a 285: o tamanho da cópia é a base mais tantos bits extras. */
static const uint16_t base_tam[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t extra_tam[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                       3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
/* Códigos de distância 0 a 29. */
static const uint16_t base_dist[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                        8193, 12289, 16385, 24577 };
static const uint8_t extra_dist[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
/* A ordem em que os tamanhos do "código dos tamanhos" aparecem (3.2.7). */
static const uint8_t ordem_cl[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* Tamanhos dos códigos fixos (3.2.6). */
static uint8_t tam_fixo(int s) { return (uint8_t)(s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8); }

/* ================================================================ descompactar (inflate) */

typedef struct {
    const uint8_t *d;
    uint32_t n, pos;
    uint32_t bits, qtd;          /* bits já lidos e ainda não usados (os de baixo primeiro) */
    uint8_t *saida;
    uint32_t max, feito;
    bool crescer;                /* a saída veio de aloca e pode aumentar */
    int erro;                    /* 0, ou um COMP_ERRO_* */
} leitor_t;

typedef struct {
    uint16_t quantos[16];        /* quantos códigos de cada tamanho */
    uint16_t simbolos[320];      /* símbolos em ordem de código */
} huffman_t;

static uint32_t ler_bits(leitor_t *l, int n)
{
    while (l->qtd < (uint32_t)n) {
        if (l->pos >= l->n) { if (!l->erro) l->erro = COMP_ERRO_TRUNCADO; return 0; }
        l->bits |= (uint32_t)l->d[l->pos++] << l->qtd;
        l->qtd += 8;
    }
    uint32_t v = l->bits & ((1u << n) - 1);
    l->bits >>= n;
    l->qtd -= (uint32_t)n;
    return v;
}

/* Monta a tabela de decodificação a partir dos tamanhos de cada símbolo.
 * Confere a desigualdade de Kraft: códigos demais para os tamanhos dados
 * não formam um código de prefixo e o fluxo está errado. Códigos de menos
 * (incompletos) só são aceitos com um único código (a RFC permite um só
 * código de distância); fora isso, um bit que não leva a símbolo nenhum
 * também é erro, pego na decodificação. Devolve false se for inválido. */
static bool montar_huffman(huffman_t *h, const uint8_t *tamanhos, int n)
{
    uint16_t inicio[16];
    memset(h->quantos, 0, sizeof h->quantos);
    for (int i = 0; i < n; i++) h->quantos[tamanhos[i]]++;
    h->quantos[0] = 0;
    int sobra = 1;
    for (int t = 1; t < 16; t++) {
        sobra = sobra * 2 - h->quantos[t];
        if (sobra < 0) return false;
    }
    inicio[1] = 0;
    for (int t = 1; t < 15; t++) inicio[t + 1] = (uint16_t)(inicio[t] + h->quantos[t]);
    for (int i = 0; i < n; i++)
        if (tamanhos[i]) h->simbolos[inicio[tamanhos[i]]++] = (uint16_t)i;
    return true;
}

/* Lê um símbolo: o código cresce um bit por vez (o primeiro bit lido é o
 * mais significativo do código); os códigos de cada tamanho são números
 * consecutivos, então basta ver se o valor caiu na faixa daquele tamanho. */
static int decodificar(leitor_t *l, const huffman_t *h)
{
    int codigo = 0, primeiro = 0, indice = 0;
    for (int t = 1; t < 16; t++) {
        codigo |= (int)ler_bits(l, 1);
        if (l->erro) return -1;
        int n = h->quantos[t];
        if (codigo - primeiro < n) return h->simbolos[indice + (codigo - primeiro)];
        indice += n;
        primeiro = (primeiro + n) << 1;
        codigo <<= 1;
    }
    l->erro = COMP_ERRO_DADOS;   /* nenhum código com esses bits */
    return -1;
}

/* Garante espaço para mais 'extra' bytes na saída (aumentando, se pode). */
static bool garantir(leitor_t *l, uint32_t extra)
{
    if (l->feito + extra <= l->max) return true;
    if (!l->crescer) { l->erro = COMP_ERRO_CHEIO; return false; }
    uint32_t novo = l->max < 4096 ? 4096 : l->max;
    while (novo < l->feito + extra) {
        if (novo >= COMP_LIMITE) { l->erro = COMP_ERRO_CHEIO; return false; }
        novo = novo > COMP_LIMITE / 2 ? COMP_LIMITE : novo * 2;
    }
    uint8_t *p = realoca(l->saida, novo);
    if (!p) { l->erro = COMP_ERRO_MEMORIA; return false; }
    l->saida = p;
    l->max = novo;
    return true;
}

static void bloco_codificado(leitor_t *l, const huffman_t *lit, const huffman_t *dist)
{
    for (;;) {
        int s = decodificar(l, lit);
        if (s < 0) return;
        if (s < 256) {
            if (!garantir(l, 1)) return;
            l->saida[l->feito++] = (uint8_t)s;
            continue;
        }
        if (s == 256) return;   /* fim do bloco */
        s -= 257;
        if (s >= 29) { l->erro = COMP_ERRO_DADOS; return; }
        uint32_t tam = base_tam[s] + ler_bits(l, extra_tam[s]);
        int d = decodificar(l, dist);
        if (d < 0) return;
        if (d >= 30) { l->erro = COMP_ERRO_DADOS; return; }
        uint32_t distancia = base_dist[d] + ler_bits(l, extra_dist[d]);
        if (l->erro) return;
        if (distancia > l->feito) { l->erro = COMP_ERRO_DADOS; return; }
        if (!garantir(l, tam)) return;
        /* Byte a byte de propósito: a cópia pode alcançar o que ela mesma
         * acabou de escrever (distância menor que o tamanho repete um trecho). */
        uint8_t *o = l->saida + l->feito;
        for (uint32_t i = 0; i < tam; i++) o[i] = o[(int32_t)i - (int32_t)distancia];
        l->feito += tam;
    }
}

static void bloco_proprio(leitor_t *l)
{
    int nlit = (int)ler_bits(l, 5) + 257, ndist = (int)ler_bits(l, 5) + 1, ncod = (int)ler_bits(l, 4) + 4;
    if (l->erro) return;
    if (nlit > 286 || ndist > 30) { l->erro = COMP_ERRO_DADOS; return; }
    uint8_t tam_cod[19];
    memset(tam_cod, 0, sizeof tam_cod);
    for (int i = 0; i < ncod; i++) tam_cod[ordem_cl[i]] = (uint8_t)ler_bits(l, 3);
    huffman_t cod, lit, dist;
    if (!montar_huffman(&cod, tam_cod, 19)) { l->erro = COMP_ERRO_DADOS; return; }
    uint8_t tams[320];
    int k = 0;
    while (k < nlit + ndist && !l->erro) {
        int s = decodificar(l, &cod);
        if (s < 0) return;
        if (s < 16) { tams[k++] = (uint8_t)s; continue; }
        int repetir, valor = 0;
        if (s == 16) {
            if (k == 0) { l->erro = COMP_ERRO_DADOS; return; }
            valor = tams[k - 1];
            repetir = 3 + (int)ler_bits(l, 2);
        } else if (s == 17) repetir = 3 + (int)ler_bits(l, 3);
        else repetir = 11 + (int)ler_bits(l, 7);
        if (k + repetir > nlit + ndist) { l->erro = COMP_ERRO_DADOS; return; }
        while (repetir--) tams[k++] = (uint8_t)valor;
    }
    if (l->erro) return;
    if (tams[256] == 0) { l->erro = COMP_ERRO_DADOS; return; }   /* sem o símbolo de fim de bloco */
    if (!montar_huffman(&lit, tams, nlit) || !montar_huffman(&dist, tams + nlit, ndist)) {
        l->erro = COMP_ERRO_DADOS;
        return;
    }
    bloco_codificado(l, &lit, &dist);
}

static int inflar_leitor(leitor_t *l, uint32_t *usados)
{
    static huffman_t fixo_lit, fixo_dist;
    static bool fixos = false;
    if (!fixos) {
        uint8_t t[288];
        for (int i = 0; i < 288; i++) t[i] = tam_fixo(i);
        montar_huffman(&fixo_lit, t, 288);
        for (int i = 0; i < 30; i++) t[i] = 5;
        montar_huffman(&fixo_dist, t, 30);
        fixos = true;
    }
    int ultimo;
    do {
        ultimo = (int)ler_bits(l, 1);
        int tipo = (int)ler_bits(l, 2);
        if (l->erro) break;
        if (tipo == 0) {   /* guardado: alinha no byte, LEN, NLEN e os bytes */
            l->bits = 0; l->qtd = 0;
            if (l->pos + 4 > l->n) { l->erro = COMP_ERRO_TRUNCADO; break; }
            uint32_t tam = (uint32_t)(l->d[l->pos] | (l->d[l->pos + 1] << 8));
            uint32_t ntam = (uint32_t)(l->d[l->pos + 2] | (l->d[l->pos + 3] << 8));
            l->pos += 4;
            if ((tam ^ 0xFFFF) != ntam) { l->erro = COMP_ERRO_DADOS; break; }
            if (l->pos + tam > l->n) { l->erro = COMP_ERRO_TRUNCADO; break; }
            if (!garantir(l, tam)) break;
            memcpy(l->saida + l->feito, l->d + l->pos, tam);
            l->feito += tam;
            l->pos += tam;
        } else if (tipo == 1) bloco_codificado(l, &fixo_lit, &fixo_dist);
        else if (tipo == 2) bloco_proprio(l);
        else l->erro = COMP_ERRO_DADOS;
        if (l->erro) break;
    } while (!ultimo);
    if (l->erro) return l->erro;
    /* Os bits que sobraram no último byte não são de mais ninguém: o
     * próximo dado (rodapé do gzip, por exemplo) começa no byte seguinte. */
    if (usados) *usados = l->pos;
    return (int)l->feito;
}

int inflar_usados(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max, uint32_t *usados)
{
    leitor_t l = { dados, n, 0, 0, 0, saida, max, 0, false, 0 };
    return inflar_leitor(&l, usados);
}

int inflar(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max)
{
    return inflar_usados(dados, n, saida, max, NULL);
}

int inflar_alocando(const uint8_t *dados, uint32_t n, uint8_t **saida, uint32_t dica, uint32_t *usados)
{
    uint32_t cap = dica ? dica : n * 3;
    if (cap < 4096) cap = 4096;
    if (cap > COMP_LIMITE) cap = COMP_LIMITE;
    uint8_t *s = aloca(cap);
    if (!s) return COMP_ERRO_MEMORIA;
    leitor_t l = { dados, n, 0, 0, 0, s, cap, 0, true, 0 };
    int r = inflar_leitor(&l, usados);
    if (r < 0) { libera(l.saida); *saida = NULL; return r; }
    *saida = l.saida;
    return r;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void poe32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* zlib (RFC 1950): CMF e FLG na frente (método 8 = deflate; os dois juntos,
 * como número de 16 bits, são múltiplos de 31) e o Adler-32 no fim, em
 * ordem "big-endian". */
int inflar_zlib(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max)
{
    if (n < 6 || (dados[0] & 0x0F) != 8 || ((dados[0] << 8) | dados[1]) % 31 != 0 || (dados[1] & 0x20))
        return COMP_ERRO_DADOS;
    uint32_t usados = 0;
    int r = inflar_usados(dados + 2, n - 2, saida, max, &usados);
    if (r < 0) return r;
    if (2 + usados + 4 <= n) {   /* algumas fontes cortam o Adler-32; sem ele não dá para conferir */
        const uint8_t *a = dados + 2 + usados;
        uint32_t esperado = (uint32_t)a[0] << 24 | (uint32_t)a[1] << 16 | (uint32_t)a[2] << 8 | a[3];
        if (esperado != adler32_somar(1, saida, (size_t)r)) return COMP_ERRO_CRC;
    }
    return r;
}

/* ================================================================ gzip (RFC 1952) */

bool eh_gzip(const uint8_t *dados, uint32_t n)
{
    return n >= 18 && dados[0] == 0x1F && dados[1] == 0x8B;
}

/* Pula o cabeçalho de um membro gzip. Devolve onde começam os dados
 * DEFLATE, ou um erro. nome (opcional) recebe o FNAME. */
static int cabecalho_gzip(const uint8_t *d, uint32_t n, char *nome, int max_nome)
{
    if (n < 18) return COMP_ERRO_TRUNCADO;
    if (d[0] != 0x1F || d[1] != 0x8B) return COMP_ERRO_DADOS;
    if (d[2] != 8) return COMP_ERRO_METODO;   /* CM: só o 8 (deflate) existe */
    uint8_t flg = d[3];
    if (flg & 0xE0) return COMP_ERRO_DADOS;   /* bits reservados têm que ser 0 */
    uint32_t p = 10;
    if (flg & 4) {   /* FEXTRA: XLEN e os dados extras */
        if (p + 2 > n) return COMP_ERRO_TRUNCADO;
        p += 2u + (uint32_t)(d[p] | (d[p + 1] << 8));
    }
    if (flg & 8) {   /* FNAME: o nome original, terminado em zero (ISO 8859-1) */
        uint32_t ini = p;
        while (p < n && d[p]) p++;
        if (p >= n) return COMP_ERRO_TRUNCADO;
        if (nome && max_nome > 0) {
            int k = 0;
            for (uint32_t i = ini; i < p && k < max_nome - 1; i++) nome[k++] = (char)d[i];
            nome[k] = 0;
        }
        p++;
    }
    if (flg & 16) {   /* FCOMMENT */
        while (p < n && d[p]) p++;
        p++;
    }
    if (flg & 2) p += 2;   /* FHCRC: CRC-16 do cabeçalho (não conferimos) */
    if (p + 8 > n) return COMP_ERRO_TRUNCADO;
    return (int)p;
}

int inflar_gzip(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max)
{
    int p = cabecalho_gzip(dados, n, NULL, 0);
    if (p < 0) return p;
    return inflar(dados + p, n - (uint32_t)p - 8, saida, max);
}

int gzip_descompactar(const uint8_t *dados, uint32_t n, uint8_t **saida, char *nome, int max_nome)
{
    uint8_t *tudo = NULL;
    uint32_t total = 0, pos = 0;
    int membros = 0;
    if (nome && max_nome > 0) nome[0] = 0;
    *saida = NULL;
    while (pos < n) {
        /* Depois do último membro, alguns programas deixam zeros de enchimento. */
        if (membros && dados[pos] == 0) {
            uint32_t k = pos;
            while (k < n && dados[k] == 0) k++;
            if (k == n) break;
        }
        int p = cabecalho_gzip(dados + pos, n - pos, membros ? NULL : nome, max_nome);
        if (p < 0) { libera(tudo); return p; }
        /* ISIZE (o tamanho original módulo 2^32) serve de palpite para a saída. */
        uint32_t dica = n - pos >= 4 && membros == 0 ? le32(dados + n - 4) : 0;
        if (dica > COMP_LIMITE) dica = 0;
        uint8_t *parte;
        uint32_t usados = 0;
        int r = inflar_alocando(dados + pos + p, n - pos - (uint32_t)p, &parte, dica + 16, &usados);
        if (r < 0) { libera(tudo); return r; }
        uint32_t fim = pos + (uint32_t)p + usados;
        if (fim + 8 > n) { libera(parte); libera(tudo); return COMP_ERRO_TRUNCADO; }
        if (le32(dados + fim) != crc32_somar(0, parte, (size_t)r) || le32(dados + fim + 4) != (uint32_t)r) {
            libera(parte); libera(tudo);
            return COMP_ERRO_CRC;
        }
        if (!tudo) { tudo = parte; total = (uint32_t)r; }
        else {
            if (total + (uint32_t)r > COMP_LIMITE) { libera(parte); libera(tudo); return COMP_ERRO_CHEIO; }
            uint8_t *t = realoca(tudo, total + (uint32_t)r + 1);
            if (!t) { libera(parte); libera(tudo); return COMP_ERRO_MEMORIA; }
            tudo = t;
            memcpy(tudo + total, parte, (size_t)r);
            total += (uint32_t)r;
            libera(parte);
        }
        pos = fim + 8;
        membros++;
    }
    if (!membros) return COMP_ERRO_TRUNCADO;
    *saida = tudo;
    return (int)total;
}

int gzip_compactar(const uint8_t *dados, uint32_t n, const char *nome, uint32_t mtime, int nivel,
                   uint8_t **saida)
{
    uint32_t tn = nome ? (uint32_t)tam_texto(nome) + 1 : 0;
    uint32_t cap = 10 + tn + deflar_limite(n) + 8;
    uint8_t *s = aloca(cap);
    if (!s) return COMP_ERRO_MEMORIA;
    s[0] = 0x1F; s[1] = 0x8B;   /* ID1, ID2 */
    s[2] = 8;                   /* CM = deflate */
    s[3] = nome ? 8 : 0;        /* FLG: FNAME se tiver nome */
    poe32(s + 4, mtime);
    s[8] = (uint8_t)(nivel >= 9 ? 2 : nivel == 1 ? 4 : 0);   /* XFL: 2 = mais compacto, 4 = mais rápido */
    s[9] = 255;                 /* OS: 255 = "desconhecido" (o SaguiOS não está na lista) */
    if (nome) memcpy(s + 10, nome, tn);
    uint32_t k = 10 + tn;
    int r = deflar(dados, n, s + k, cap - k - 8, nivel);
    if (r < 0) { libera(s); return r; }
    k += (uint32_t)r;
    poe32(s + k, crc32_somar(0, dados, n));
    poe32(s + k + 4, n);
    *saida = s;
    return (int)(k + 8);
}

/* ================================================================ compactar (deflate) */

/* Escreve bits no fluxo, do menos significativo para o mais (RFC 1951,
 * 3.1.1). Os códigos de Huffman são guardados já invertidos (ver
 * codigos_canonicos), para que o primeiro bit do código saia primeiro. */
typedef struct {
    uint8_t *s;
    uint32_t pos, max;
    uint32_t acc;
    int nb;
    bool cheio;
} escritor_t;

static void por_bits(escritor_t *w, uint32_t v, int n)
{
    w->acc |= v << w->nb;   /* n <= 16 e nb < 8: cabe nos 32 bits */
    w->nb += n;
    while (w->nb >= 8) {
        if (w->pos < w->max) w->s[w->pos++] = (uint8_t)w->acc;
        else w->cheio = true;
        w->acc >>= 8;
        w->nb -= 8;
    }
}

static void alinhar(escritor_t *w)
{
    if (w->nb) por_bits(w, 0, 8 - w->nb);
}

/* Tamanhos de código ótimos (Huffman, 1952), limitados a 'limite' bits.
 *
 * Montagem: as folhas (símbolos usados) em ordem crescente de frequência e
 * os nós internos numa segunda fila; cada nó novo pesa a soma dos dois mais
 * leves e, como os pesos criados nunca diminuem, a fila dos nós já sai
 * ordenada. A profundidade de cada folha é o tamanho do código dela.
 *
 * O DEFLATE limita os códigos a 15 bits (7 no código dos tamanhos). Se a
 * árvore passar disso, cortamos os longos no limite e consertamos a soma
 * de Kraft (soma de 2^-tamanho, que num código de prefixo completo é
 * exatamente 1, aqui contada em unidades de 2^-limite): primeiro
 * alongamos os códigos mais longos abaixo do limite (cada um devolve um
 * pouco de espaço), escolhendo os menos usados, até caber; depois, se
 * sobrou espaço, encurtamos os mais usados até a soma fechar em 1, porque
 * muitos descompactadores recusam códigos incompletos. */
static void tamanhos_huffman(const uint32_t *freq, int n, int limite, uint8_t *tam)
{
    int folhas[288], nf = 0;
    uint32_t peso[288];
    int pai_no[288], pai_folha[288];
    uint8_t prof[288];
    for (int i = 0; i < n; i++) {
        tam[i] = 0;
        if (freq[i]) folhas[nf++] = i;
    }
    if (nf == 0) return;
    if (nf == 1) { tam[folhas[0]] = 1; return; }
    for (int i = 1; i < nf; i++) {   /* inserção: estável e n é pequeno */
        int s = folhas[i], j = i;
        while (j > 0 && freq[folhas[j - 1]] > freq[s]) { folhas[j] = folhas[j - 1]; j--; }
        folhas[j] = s;
    }
    int fi = 0, ni = 0, nn = 0;
    while ((nf - fi) + (nn - ni) > 1) {
        uint32_t soma = 0;
        for (int k = 0; k < 2; k++) {
            if (fi < nf && (ni >= nn || freq[folhas[fi]] <= peso[ni])) {
                soma += freq[folhas[fi]];
                pai_folha[fi++] = nn;
            } else {
                soma += peso[ni];
                pai_no[ni++] = nn;
            }
        }
        peso[nn++] = soma;
    }
    prof[nn - 1] = 0;   /* a raiz é o último nó criado */
    for (int k = nn - 2; k >= 0; k--) prof[k] = (uint8_t)(prof[pai_no[k]] + 1);
    int maior = 0;
    for (int i = 0; i < nf; i++) {
        int t = prof[pai_folha[i]] + 1;
        if (t > maior) maior = t;
        tam[folhas[i]] = (uint8_t)(t > limite ? limite : t);
    }
    if (maior <= limite) return;
    uint32_t cap = 1u << limite, kraft = 0;
    for (int i = 0; i < nf; i++) kraft += 1u << (limite - tam[folhas[i]]);
    while (kraft > cap) {
        int melhor = -1;
        for (int i = 0; i < nf; i++) {   /* em ordem crescente de uso */
            int s = folhas[i];
            if (tam[s] < limite && (melhor < 0 || tam[s] > tam[melhor])) melhor = s;
        }
        kraft -= 1u << (limite - tam[melhor] - 1);
        tam[melhor]++;
    }
    while (kraft < cap) {
        int melhor = -1;
        for (int i = nf - 1; i >= 0; i--) {   /* em ordem decrescente de uso */
            int s = folhas[i];
            if (tam[s] > 1 && (1u << (limite - tam[s])) <= cap - kraft && (melhor < 0 || tam[s] > tam[melhor]))
                melhor = s;
        }
        if (melhor < 0) break;   /* não acontece: a sobra é múltipla do menor passo */
        kraft += 1u << (limite - tam[melhor]);
        tam[melhor]--;
    }
}

/* Códigos canônicos (RFC 1951, 3.2.2): contar quantos há de cada tamanho,
 * achar o primeiro código de cada tamanho e numerar em ordem de símbolo.
 * Guardamos o código com os bits invertidos, pronto para por_bits. */
static void codigos_canonicos(const uint8_t *tam, int n, uint16_t *cod)
{
    uint16_t quantos[16] = { 0 }, prox[16];
    for (int i = 0; i < n; i++) quantos[tam[i]]++;
    quantos[0] = 0;
    uint16_t c = 0;
    for (int t = 1; t < 16; t++) { c = (uint16_t)((c + quantos[t - 1]) << 1); prox[t] = c; }
    for (int i = 0; i < n; i++) {
        cod[i] = 0;
        if (!tam[i]) continue;
        uint16_t v = prox[tam[i]]++, r = 0;
        for (int b = 0; b < tam[i]; b++) { r = (uint16_t)(r << 1 | (v & 1)); v >>= 1; }
        cod[i] = r;
    }
}

#define JANELA       32768          /* a maior distância do DEFLATE */
#define TAM_HASH     (1 << 15)
#define MAX_SIMBOLOS 16384          /* símbolos guardados antes de fechar um bloco */

typedef struct {
    int32_t cabeca[TAM_HASH];       /* a posição mais recente com cada hash (-1: nenhuma) */
    int32_t anterior[JANELA];       /* a posição anterior com o mesmo hash */
    uint16_t lit[MAX_SIMBOLOS];     /* byte, ou o tamanho da cópia */
    uint16_t dist[MAX_SIMBOLOS];    /* 0 = literal; senão a distância */
    uint8_t cod_tam[259];           /* tamanho da cópia -> índice em base_tam */
} compactador_t;

static int codigo_dist(uint32_t d)
{
    int c = 0;
    while (c < 29 && base_dist[c + 1] <= d) c++;
    return c;
}

/* Um bloco com os símbolos guardados. Calcula em bits o custo das três
 * formas que a RFC oferece (guardado, códigos fixos, códigos próprios) e
 * usa a menor: dados aleatórios vão guardados e não crescem quase nada. */
static void emitir_bloco(escritor_t *w, compactador_t *c, int nsim, const uint8_t *cru, uint32_t ncru, bool ultimo)
{
    uint32_t flit[286], fdist[30];
    memset(flit, 0, sizeof flit);
    memset(fdist, 0, sizeof fdist);
    uint32_t extras = 0;
    for (int i = 0; i < nsim; i++) {
        if (!c->dist[i]) { flit[c->lit[i]]++; continue; }
        int ct = c->cod_tam[c->lit[i]], cd = codigo_dist(c->dist[i]);
        flit[257 + ct]++;
        fdist[cd]++;
        extras += extra_tam[ct] + extra_dist[cd];
    }
    flit[256] = 1;
    /* Pelo menos dois códigos de distância: com um só, alguns leitores
     * reclamam de código incompleto; com dois, a árvore fica completa. */
    int usados_dist = 0;
    for (int i = 0; i < 30; i++) if (fdist[i]) usados_dist++;
    for (int i = 0; usados_dist < 2; i++) if (!fdist[i]) { fdist[i] = 1; usados_dist++; }

    uint8_t tlit[286], tdist[30];
    tamanhos_huffman(flit, 286, 15, tlit);
    tamanhos_huffman(fdist, 30, 15, tdist);
    int nlit = 286, ndist = 30;
    while (nlit > 257 && !tlit[nlit - 1]) nlit--;
    while (ndist > 1 && !tdist[ndist - 1]) ndist--;

    /* Os tamanhos de literais e distâncias em sequência, compactados com
     * repetições: 16 = repete o anterior 3-6 vezes, 17 = 3-10 zeros,
     * 18 = 11-138 zeros (3.2.7). */
    uint8_t todos[316], rle[316], rle_extra[316];
    int ntodos = nlit + ndist, nrle = 0;
    memcpy(todos, tlit, (size_t)nlit);
    memcpy(todos + nlit, tdist, (size_t)ndist);
    for (int i = 0; i < ntodos;) {
        int v = todos[i], r = 1;
        while (i + r < ntodos && todos[i + r] == v) r++;
        i += r;
        if (v == 0) {
            while (r >= 11) { int k = r > 138 ? 138 : r; rle[nrle] = 18; rle_extra[nrle++] = (uint8_t)(k - 11); r -= k; }
            if (r >= 3) { rle[nrle] = 17; rle_extra[nrle++] = (uint8_t)(r - 3); r = 0; }
        } else {
            rle[nrle] = (uint8_t)v; rle_extra[nrle++] = 0; r--;
            while (r >= 3) { int k = r > 6 ? 6 : r; rle[nrle] = 16; rle_extra[nrle++] = (uint8_t)(k - 3); r -= k; }
        }
        while (r-- > 0) { rle[nrle] = (uint8_t)v; rle_extra[nrle++] = 0; }
    }
    uint32_t fcl[19];
    memset(fcl, 0, sizeof fcl);
    for (int i = 0; i < nrle; i++) fcl[rle[i]]++;
    uint8_t tcl[19];
    tamanhos_huffman(fcl, 19, 7, tcl);
    int ncl = 19;
    while (ncl > 4 && !tcl[ordem_cl[ncl - 1]]) ncl--;

    uint32_t custo_prop = 3 + 14 + 3u * (uint32_t)ncl + extras;
    for (int i = 0; i < nrle; i++) custo_prop += tcl[rle[i]] + (rle[i] == 16 ? 2 : rle[i] == 17 ? 3 : rle[i] == 18 ? 7 : 0);
    uint32_t custo_fixo = 3 + extras;
    for (int i = 0; i < 286; i++) { custo_prop += flit[i] * tlit[i]; custo_fixo += flit[i] * tam_fixo(i); }
    for (int i = 0; i < 30; i++) { custo_prop += fdist[i] * tdist[i]; custo_fixo += fdist[i] * 5; }
    /* (As distâncias "inventadas" acima contam 1 uso a mais: errar para
     * cima um ou dois bits não muda a escolha de forma que importe.) */
    uint32_t pedacos = ncru / 65535 + 1;
    uint32_t custo_guardado = ncru * 8 + pedacos * (3 + 7 + 32);

    if (custo_guardado <= custo_prop && custo_guardado <= custo_fixo) {
        uint32_t resto = ncru;
        do {
            uint32_t m = resto > 65535 ? 65535 : resto;
            resto -= m;
            por_bits(w, ultimo && !resto, 1);
            por_bits(w, 0, 2);
            alinhar(w);
            por_bits(w, m, 16);
            por_bits(w, m ^ 0xFFFF, 16);
            if (w->pos + m <= w->max) { memcpy(w->s + w->pos, cru, m); w->pos += m; }
            else w->cheio = true;
            cru += m;
        } while (resto);
        return;
    }
    uint16_t clit[288], cdist[30];
    uint8_t *ulit = tlit, *udist = tdist;
    uint8_t fixo_lit[288], fixo_dist[30];
    por_bits(w, ultimo, 1);
    if (custo_fixo <= custo_prop) {
        por_bits(w, 1, 2);
        for (int i = 0; i < 288; i++) fixo_lit[i] = tam_fixo(i);
        for (int i = 0; i < 30; i++) fixo_dist[i] = 5;
        codigos_canonicos(fixo_lit, 288, clit);
        codigos_canonicos(fixo_dist, 30, cdist);
        ulit = fixo_lit;
        udist = fixo_dist;
    } else {
        por_bits(w, 2, 2);
        por_bits(w, (uint32_t)(nlit - 257), 5);
        por_bits(w, (uint32_t)(ndist - 1), 5);
        por_bits(w, (uint32_t)(ncl - 4), 4);
        for (int i = 0; i < ncl; i++) por_bits(w, tcl[ordem_cl[i]], 3);
        uint16_t ccl[19];
        codigos_canonicos(tcl, 19, ccl);
        for (int i = 0; i < nrle; i++) {
            int s = rle[i];
            por_bits(w, ccl[s], tcl[s]);
            if (s == 16) por_bits(w, rle_extra[i], 2);
            else if (s == 17) por_bits(w, rle_extra[i], 3);
            else if (s == 18) por_bits(w, rle_extra[i], 7);
        }
        codigos_canonicos(tlit, 286, clit);
        codigos_canonicos(tdist, 30, cdist);
    }
    for (int i = 0; i < nsim; i++) {
        if (!c->dist[i]) { por_bits(w, clit[c->lit[i]], ulit[c->lit[i]]); continue; }
        int ct = c->cod_tam[c->lit[i]], cd = codigo_dist(c->dist[i]);
        por_bits(w, clit[257 + ct], ulit[257 + ct]);
        if (extra_tam[ct]) por_bits(w, c->lit[i] - base_tam[ct], extra_tam[ct]);
        por_bits(w, cdist[cd], udist[cd]);
        if (extra_dist[cd]) por_bits(w, c->dist[i] - base_dist[cd], extra_dist[cd]);
    }
    por_bits(w, clit[256], ulit[256]);
}

/* O hash de 3 bytes: mistura os bytes com deslocamentos diferentes para
 * que sequências parecidas caiam em lugares diferentes da tabela. */
static uint32_t hash3(const uint8_t *p)
{
    return ((uint32_t)p[0] << 10 ^ (uint32_t)p[1] << 5 ^ p[2]) & (TAM_HASH - 1);
}

static void inserir(compactador_t *c, const uint8_t *d, uint32_t n, uint32_t pos)
{
    if (pos + 3 > n) return;
    uint32_t h = hash3(d + pos);
    c->anterior[pos & (JANELA - 1)] = c->cabeca[h];
    c->cabeca[h] = (int32_t)pos;
}

/* A repetição mais longa do que começa em pos, entre as posições
 * anteriores com o mesmo hash (até 'cadeia' tentativas, de trás para a
 * frente, as mais próximas primeiro). Devolve o tamanho (0 se < 3). */
static int achar_repeticao(compactador_t *c, const uint8_t *d, uint32_t n, uint32_t pos, int cadeia, uint32_t *dist)
{
    uint32_t max = n - pos > 258 ? 258 : n - pos;
    if (max < 3) return 0;
    uint32_t melhor = 0;
    int32_t cand = c->cabeca[hash3(d + pos)];
    while (cand >= 0 && pos - (uint32_t)cand <= JANELA && cadeia-- > 0) {
        const uint8_t *a = d + cand, *b = d + pos;
        /* Olhar primeiro o byte que faria a repetição ficar maior que a
         * melhor até agora descarta depressa a maioria dos candidatos. */
        if (a[melhor] == b[melhor]) {
            uint32_t k = 0;
            while (k < max && a[k] == b[k]) k++;
            if (k > melhor) {
                melhor = k;
                *dist = pos - (uint32_t)cand;
                if (k == max) break;
            }
        }
        int32_t prox = c->anterior[cand & (JANELA - 1)];
        if (prox >= cand) break;   /* entrada já reaproveitada por uma posição mais nova */
        cand = prox;
    }
    return melhor >= 3 ? (int)melhor : 0;
}

uint32_t deflar_limite(uint32_t n)
{
    /* No pior caso tudo vai guardado: 5 bytes de cabeçalho por pedaço de
     * até 65535 bytes e um por bloco para alinhar, com folga. */
    return n + n / 1024 + 128;
}

int deflar(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max, int nivel)
{
    escritor_t w = { saida, 0, max, 0, 0, false };
    if (nivel < 0 || nivel > 9) nivel = 6;
    if (n == 0) {   /* um bloco final com códigos fixos e só o fim de bloco */
        por_bits(&w, 1, 1);
        por_bits(&w, 1, 2);
        por_bits(&w, 0, 7);
        alinhar(&w);
        return w.cheio ? COMP_ERRO_CHEIO : (int)w.pos;
    }
    if (nivel == 0) {
        for (uint32_t p = 0; p < n;) {
            uint32_t m = n - p > 65535 ? 65535 : n - p;
            por_bits(&w, p + m >= n, 1);
            por_bits(&w, 0, 2);
            alinhar(&w);
            por_bits(&w, m, 16);
            por_bits(&w, m ^ 0xFFFF, 16);
            if (w.pos + m <= w.max) { memcpy(w.s + w.pos, dados + p, m); w.pos += m; }
            else w.cheio = true;
            p += m;
        }
        return w.cheio ? COMP_ERRO_CHEIO : (int)w.pos;
    }
    compactador_t *c = aloca(sizeof *c);
    if (!c) return COMP_ERRO_MEMORIA;
    for (int i = 0; i < TAM_HASH; i++) c->cabeca[i] = -1;
    for (int t = 3, k = 0; t <= 258; t++) {
        while (k < 28 && base_tam[k + 1] <= t) k++;
        c->cod_tam[t] = (uint8_t)k;
    }
    /* O nível escolhe quantos candidatos olhar (dobra a cada nível) e, do 4
     * em diante, a "espera de um byte": antes de aceitar uma repetição,
     * ver se a que começa no byte seguinte é maior; se for, o byte atual
     * vai como literal e ficamos com a maior (a RFC 1951, seção 4, cita a
     * ideia). Repetições já compridas não valem a espera. */
    int cadeia = 2 << nivel;                 /* 4, 8, ..., 1024 */
    uint32_t espera = nivel >= 4 ? 16u * (uint32_t)nivel : 0;
    int nsim = 0;
    uint32_t pos = 0, ini_bloco = 0, dist = 0;
    int tam = achar_repeticao(c, dados, n, 0, cadeia, &dist);
    while (pos < n) {
        if (tam && (uint32_t)tam < espera && pos + 1 < n) {
            inserir(c, dados, n, pos);
            uint32_t d2 = 0;
            int t2 = achar_repeticao(c, dados, n, pos + 1, cadeia, &d2);
            if (t2 > tam) {
                c->lit[nsim] = dados[pos]; c->dist[nsim++] = 0;
                pos++;
                tam = t2; dist = d2;
                if (nsim >= MAX_SIMBOLOS) {
                    emitir_bloco(&w, c, nsim, dados + ini_bloco, pos - ini_bloco, false);
                    nsim = 0; ini_bloco = pos;
                }
                continue;   /* pos já tem a sua repetição em tam/dist */
            }
            c->lit[nsim] = (uint16_t)tam; c->dist[nsim++] = (uint16_t)dist;
            for (int i = 1; i < tam; i++) inserir(c, dados, n, pos + (uint32_t)i);
            pos += (uint32_t)tam;
        } else if (tam) {
            c->lit[nsim] = (uint16_t)tam; c->dist[nsim++] = (uint16_t)dist;
            for (int i = 0; i < tam; i++) inserir(c, dados, n, pos + (uint32_t)i);
            pos += (uint32_t)tam;
        } else {
            inserir(c, dados, n, pos);
            c->lit[nsim] = dados[pos]; c->dist[nsim++] = 0;
            pos++;
        }
        if (nsim >= MAX_SIMBOLOS && pos < n) {
            emitir_bloco(&w, c, nsim, dados + ini_bloco, pos - ini_bloco, false);
            nsim = 0; ini_bloco = pos;
        }
        tam = pos < n ? achar_repeticao(c, dados, n, pos, cadeia, &dist) : 0;
    }
    emitir_bloco(&w, c, nsim, dados + ini_bloco, n - ini_bloco, true);
    alinhar(&w);
    libera(c);
    return w.cheio ? COMP_ERRO_CHEIO : (int)w.pos;
}
