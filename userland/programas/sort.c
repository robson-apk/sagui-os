/* sort (ordenar) - ordena as linhas de um arquivo ou da entrada.
 * sort [-r] [-n] [-u] [arquivo]: -r ao contrário, -n pelo número do começo
 * da linha, -u tira as linhas repetidas. Ordenação estável (mesclagem). */
#include "sagui.h"

static bool numerico, inverso;

static int numero(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return texto_para_int(s);
}

static int comparar(const char *a, const char *b)
{
    int c;
    if (numerico) {
        int x = numero(a), y = numero(b);
        c = x < y ? -1 : x > y;
        if (c == 0) c = compara_texto(a, b);
    } else {
        c = compara_texto(a, b);
    }
    return inverso ? -c : c;
}

static void mesclar(char **v, char **aux, int n)
{
    if (n < 2) return;
    int m = n / 2;
    mesclar(v, aux, m);
    mesclar(v + m, aux, n - m);
    int i = 0, j = m, k = 0;
    while (i < m && j < n) aux[k++] = comparar(v[j], v[i]) < 0 ? v[j++] : v[i++];
    while (i < m) aux[k++] = v[i++];
    while (j < n) aux[k++] = v[j++];
    memcpy(v, aux, (size_t)n * sizeof *v);
}

int principal(int argc, char **argv)
{
    bool unicas = false;
    const char *nome = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1]) {
            for (const char *o = argv[i] + 1; *o; o++) {
                if (*o == 'r') inverso = true;
                else if (*o == 'n') numerico = true;
                else if (*o == 'u') unicas = true;
                else { imprimir_em(ERROS, "uso: sort [-r] [-n] [-u] [arquivo]\n"); return 1; }
            }
        } else if (!nome) nome = argv[i];
        else { imprimir_em(ERROS, "uso: sort [-r] [-n] [-u] [arquivo]\n"); return 1; }
    }
    int fd = ENTRADA;
    if (nome) {
        fd = abrir(nome, ABRIR_LER);
        if (fd < 0) { imprimir_em(ERROS, "sort: %s: %s\n", nome, nome_erro(fd)); return 1; }
    }
    unsigned cap = 4096, tam = 0;
    char *d = aloca(cap + 1);
    int r;
    while (d) {
        if (tam == cap) { d = realoca(d, cap * 2 + 1); cap *= 2; if (!d) break; }
        r = ler(fd, d + tam, cap - tam);
        if (r <= 0) break;
        tam += (unsigned)r;
    }
    if (nome) fechar(fd);
    if (!d) { imprimir_em(ERROS, "sort: sem memória\n"); return 1; }
    d[tam] = 0;
    int qtd = 0;
    for (unsigned k = 0; k < tam; k++) if (d[k] == '\n') qtd++;
    if (tam && d[tam - 1] != '\n') qtd++;
    char **v = aloca((size_t)(qtd + 1) * sizeof *v), **aux = aloca((size_t)(qtd + 1) * sizeof *v);
    if (!v || !aux) { imprimir_em(ERROS, "sort: sem memória\n"); return 1; }
    int n = 0;
    char *p = d;
    while (p < d + tam) {
        v[n++] = p;
        char *q = acha_char(p, '\n');
        if (!q) break;
        *q = 0;
        p = q + 1;
    }
    mesclar(v, aux, n);
    for (int i = 0; i < n; i++) {
        if (unicas && i > 0 && compara_texto(v[i], v[i - 1]) == 0) continue;
        escrever_texto(SAIDA, v[i]);
        escrever_tudo(SAIDA, "\n", 1);
    }
    return 0;
}
