/* od (octal) - mostra os bytes de um arquivo como números (H91; a ideia veio
 * do fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi
 * reescrito sobre a libsagui atual).
 *
 *   od [-A d|o|x|n] [-t TIPO | -b | -c | -d | -o | -x] [-j N] [-N N] [-v] [arquivo...]
 *
 *   -t TIPO  como mostrar: o (octal), x (hexadecimal), d (decimal com
 *            sinal) ou u (sem sinal), seguido do tamanho em bytes (1, 2 ou
 *            4; os de 2 e 4 são lidos com o byte baixo primeiro, como no
 *            x86); ou c (letras, com \n \t \0 e octal para o resto);
 *   -b = -t o1   -c = -t c   -d = -t u2   -o = -t o2 (o padrão)   -x = -t x2;
 *   -A       a base do endereço no começo da linha (n = sem endereço);
 *   -j N     pula N bytes;   -N N  mostra só N bytes;
 *   -v       mostra todas as linhas (sem -v, linhas repetidas viram "*").
 *
 * Segue a descrição do od na norma POSIX (IEEE Std 1003.1). Vários
 * arquivos são lidos como um só, um depois do outro. */
#include "sagui.h"

static char tipo = 'o', base_endereco = 'o';
static int tamanho = 2;

/* Escreve v na base, com "largura" casas (zeros à esquerda se zeros). O
 * imprimir da libsagui não tem octal, por isso esta conta própria. */
static int numero(char *dst, uint32_t v, unsigned base, int largura, bool zeros)
{
    char d[16];
    int n = 0;
    do { d[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    int k = 0;
    while (largura-- > n) dst[k++] = zeros ? '0' : ' ';
    while (n) dst[k++] = d[--n];
    dst[k] = 0;
    return k;
}

static void endereco(uint32_t a)
{
    char t[16];
    if (base_endereco == 'n') return;
    if (base_endereco == 'x') numero(t, a, 16, 6, true);
    else if (base_endereco == 'd') numero(t, a, 10, 7, true);
    else numero(t, a, 8, 7, true);
    escrever_texto(SAIDA, t);
}

/* Uma linha: o endereço e até 16 bytes no formato escolhido. */
static void linha(uint32_t ender, const uint8_t *b, int n)
{
    char t[256];
    int k = 0;
    endereco(ender);
    for (int i = 0; i < n; i += tipo == 'c' ? 1 : tamanho) {
        if (tipo == 'c') {
            static const char escapes[] = "\0\a\b\t\n\v\f\r";
            static const char letras[] = "0abtnvfr";
            uint8_t c = b[i];
            const char *e = c < 14 ? acha_char(escapes + 1, c) : NULL;
            if (c == 0) e = escapes;
            if (e) { t[k++] = ' '; t[k++] = ' '; t[k++] = '\\'; t[k++] = letras[e - escapes]; }
            else if (c >= 32 && c < 127) { t[k++] = ' '; t[k++] = ' '; t[k++] = ' '; t[k++] = (char)c; }
            else { t[k++] = ' '; k += numero(t + k, c, 8, 3, true); }
            continue;
        }
        /* O valor: 1, 2 ou 4 bytes, o byte baixo primeiro (faltando bytes
         * no fim do arquivo, valem zero). */
        uint32_t v = 0;
        for (int j = tamanho - 1; j >= 0; j--) v = v << 8 | (i + j < n ? b[i + j] : 0);
        static const int larg_o[5] = { 0, 3, 6, 0, 11 }, larg_x[5] = { 0, 2, 4, 0, 8 };
        static const int larg_u[5] = { 0, 3, 5, 0, 10 }, larg_d[5] = { 0, 4, 6, 0, 11 };
        t[k++] = ' ';
        if (tipo == 'o') k += numero(t + k, v, 8, larg_o[tamanho], true);
        else if (tipo == 'x') k += numero(t + k, v, 16, larg_x[tamanho], true);
        else if (tipo == 'u') k += numero(t + k, v, 10, larg_u[tamanho], false);
        else {
            /* Decimal com sinal: o bit mais alto do tamanho é o sinal. */
            int32_t s = tamanho == 1 ? (int8_t)v : tamanho == 2 ? (int16_t)v : (int32_t)v;
            char d[16];
            int m = numero(d + 1, (uint32_t)(s < 0 ? -s : s), 10, 0, false);
            if (s < 0) { d[0] = '-'; m++; } else memmove(d, d + 1, (size_t)m + 1);
            for (int p = m; p < larg_d[tamanho]; p++) t[k++] = ' ';
            memcpy(t + k, d, (size_t)m);
            k += m;
        }
    }
    t[k++] = '\n';
    escrever_tudo(SAIDA, t, (unsigned)k);
}

static bool ler_tipo(const char *s)
{
    if (s[0] == 'c' && !s[1]) { tipo = 'c'; return true; }
    if (!acha_char("oxdu", s[0]) || !s[0]) return false;
    tipo = s[0];
    tamanho = s[1] ? s[1] - '0' : (tipo == 'o' || tipo == 'x' ? 2 : 4);
    return (tamanho == 1 || tamanho == 2 || tamanho == 4) && (!s[1] || !s[2]);
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: od [-A d|o|x|n] [-t TIPO | -b | -c | -d | -o | -x] [-j N] [-N N] [-v] [arquivo...]\n");
    return 1;
}

int principal(int argc, char **argv)
{
    int pular = 0, limite = -1;
    bool todas = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        for (const char *o = argv[i] + 1; *o; o++) {
            if (*o == 'A' || *o == 't' || *o == 'j' || *o == 'N') {
                const char *v = o[1] ? o + 1 : (i + 1 < argc ? argv[++i] : NULL);
                bool ok = v != NULL;
                if (ok && *o == 'A') { base_endereco = v[0]; ok = acha_char("doxn", v[0]) && v[0] && !v[1]; }
                else if (ok && *o == 't') ok = ler_tipo(v);
                else if (ok && *o == 'j') ok = converter_numero(v, &pular) && pular >= 0;
                else if (ok) ok = converter_numero(v, &limite) && limite >= 0;
                if (!ok) { imprimir_em(ERROS, "od: valor inválido para -%c\n", *o); return 1; }
                break;
            }
            if (*o == 'b') { tipo = 'o'; tamanho = 1; }
            else if (*o == 'c') tipo = 'c';
            else if (*o == 'd') { tipo = 'u'; tamanho = 2; }
            else if (*o == 'o') { tipo = 'o'; tamanho = 2; }
            else if (*o == 'x') { tipo = 'x'; tamanho = 2; }
            else if (*o == 'v') todas = true;
            else { imprimir_em(ERROS, "od: opção desconhecida -%c\n", *o); return uso(); }
        }
    }
    /* Junta os arquivos num bloco só (o od trata tudo como um fluxo). */
    uint8_t *tudo = NULL;
    unsigned total = 0;
    int erros = 0;
    for (int k = i; k < (i < argc ? argc : i + 1); k++) {
        int fd = abrir_para_ler("od", i < argc ? argv[k] : "-");
        if (fd < 0) { erros++; continue; }
        unsigned n;
        char *b = ler_ate_o_fim(fd, &n);
        if (fd != ENTRADA) fechar(fd);
        uint8_t *junto = b ? realoca(tudo, total + n + 1) : NULL;
        if (!junto) { imprimir_em(ERROS, "od: memória insuficiente\n"); return 1; }
        tudo = junto;
        memcpy(tudo + total, b, n);
        total += n;
        libera(b);
    }
    unsigned ini = (unsigned)pular < total ? (unsigned)pular : total, fim = total;
    if (limite >= 0 && ini + (unsigned)limite < fim) fim = ini + (unsigned)limite;
    bool repetindo = false;
    for (unsigned p = ini; p < fim; p += 16) {
        int n = fim - p < 16 ? (int)(fim - p) : 16;
        /* Linha igual à anterior (e completa): um "*" no lugar de todas. */
        if (!todas && p > ini && n == 16 && memcmp(tudo + p, tudo + p - 16, 16) == 0) {
            if (!repetindo) escrever_texto(SAIDA, "*\n");
            repetindo = true;
            continue;
        }
        repetindo = false;
        linha(p, tudo + p, n);
    }
    if (base_endereco != 'n') { endereco(fim); escrever_texto(SAIDA, "\n"); }
    return erros ? 1 : 0;
}
