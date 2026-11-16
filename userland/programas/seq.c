/* seq (sequencia) - escreve uma sequência de números (H91; a ideia veio do
 * fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi reescrito
 * sobre a libsagui atual).
 *
 *   seq [-w] [-s SEP] [PRIMEIRO [PASSO]] ÚLTIMO
 *
 *   seq 5          1 2 3 4 5 (um por linha)
 *   seq 2 5        2 3 4 5
 *   seq 10 -3 1    10 7 4 1
 *   seq 0 0.5 2    0.0 0.5 1.0 1.5 2.0
 *   -w             todos com a mesma largura, com zeros na frente (08 09 10);
 *   -s SEP         separa os números com SEP em vez da quebra de linha.
 *
 * Os números com casas decimais são guardados como inteiros "em
 * centésimos" (ou na casa que for preciso): 0.5 com uma casa vira 5. Assim
 * a conta é exata, sem os arredondamentos do ponto flutuante, e a saída
 * tem tantas casas quanto o argumento que tiver mais. */
#include "sagui.h"

static int casas;   /* o maior número de casas decimais dos argumentos */

/* Quantas casas decimais um argumento tem; -1 se não for número. */
static int contar_casas(const char *s)
{
    if (*s == '-' || *s == '+') s++;
    bool digito = false;
    while (*s >= '0' && *s <= '9') { s++; digito = true; }
    int c = 0;
    if (*s == '.' || *s == ',') {
        s++;
        while (*s >= '0' && *s <= '9') { s++; c++; digito = true; }
    }
    return (*s || !digito || c > 6) ? -1 : c;
}

/* Converte para inteiro na escala de "casas" decimais. */
static int64_t escalar(const char *s)
{
    bool neg = *s == '-';
    if (*s == '-' || *s == '+') s++;
    int64_t v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    int c = 0;
    if (*s == '.' || *s == ',') {
        s++;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); c++; }
    }
    for (; c < casas; c++) v *= 10;
    return neg ? -v : v;
}

/* Escreve v (na escala) em t; com largura > 0, completa com zeros. */
static void formatar(char *t, int64_t v, int largura)
{
    char d[32];
    int n = 0;
    bool neg = v < 0;
    uint32_t u = (uint32_t)(neg ? -v : v);   /* cabe: os limites são conferidos antes */
    do {
        if (n == casas && casas) d[n++] = '.';
        d[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u || n <= casas);
    while (n < largura - (neg ? 1 : 0)) d[n++] = '0';
    int k = 0;
    if (neg) t[k++] = '-';
    while (n) t[k++] = d[--n];
    t[k] = 0;
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: seq [-w] [-s SEP] [PRIMEIRO [PASSO]] ÚLTIMO\n");
    return 1;
}

int principal(int argc, char **argv)
{
    bool mesma_largura = false;
    const char *sep = "\n";
    int i = 1;
    /* Um "-" seguido de dígito é um número negativo, não uma opção. */
    for (; i < argc && argv[i][0] == '-' && argv[i][1] && !(argv[i][1] >= '0' && argv[i][1] <= '9'); i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        if (argv[i][1] == 's') {
            sep = argv[i][2] ? argv[i] + 2 : (i + 1 < argc ? argv[++i] : NULL);
            if (!sep) return uso();
        } else if (argv[i][1] == 'w' && !argv[i][2]) {
            mesma_largura = true;
        } else {
            imprimir_em(ERROS, "seq: opção desconhecida %s\n", argv[i]);
            return uso();
        }
    }
    int n = argc - i;
    if (n < 1 || n > 3) return uso();
    for (int k = i; k < argc; k++) {
        int c = contar_casas(argv[k]);
        if (c < 0) { imprimir_em(ERROS, "seq: não é um número: %s\n", argv[k]); return 1; }
        if (c > casas) casas = c;
    }
    int64_t primeiro = escalar("1"), passo = escalar("1"), ultimo;
    if (n == 1) ultimo = escalar(argv[i]);
    else {
        primeiro = escalar(argv[i]);
        if (n == 3) passo = escalar(argv[i + 1]);
        ultimo = escalar(argv[argc - 1]);
    }
    if (passo == 0) { imprimir_em(ERROS, "seq: o passo não pode ser zero\n"); return 1; }
    const int64_t limite = 2000000000;
    if (primeiro > limite || primeiro < -limite || ultimo > limite || ultimo < -limite) {
        imprimir_em(ERROS, "seq: número grande demais\n");
        return 1;
    }

    /* Com -w, a largura é a do maior número escrito (o primeiro ou o último). */
    int largura = 0;
    if (mesma_largura) {
        char a[32], b[32];
        formatar(a, primeiro, 0);
        formatar(b, ultimo, 0);
        largura = (int)tam_texto(a) > (int)tam_texto(b) ? (int)tam_texto(a) : (int)tam_texto(b);
    }
    char t[40];
    bool primeiro_escrito = true;
    for (int64_t v = primeiro; passo > 0 ? v <= ultimo : v >= ultimo; v += passo) {
        formatar(t, v, largura);
        if (!primeiro_escrito && escrever_texto(SAIDA, sep) < 0) return 1;
        if (escrever_texto(SAIDA, t) < 0) return 1;
        primeiro_escrito = false;
    }
    if (!primeiro_escrito) escrever_texto(SAIDA, "\n");
    return 0;
}
