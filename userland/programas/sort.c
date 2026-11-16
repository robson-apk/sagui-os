/* sort (ordenar) - ordena as linhas de arquivos ou da entrada (H91; a ideia
 * veio do fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi
 * reescrito sobre a libsagui atual).
 *
 *   sort [-rnufbc] [-k N[,M][nrf]] [-t C] [-o arquivo] [arquivo...]
 *
 *   -r  ordem inversa                -n  compara como números (10 depois de 9)
 *   -u  tira as linhas repetidas     -f  ignora maiúsculas (também É e é)
 *   -b  ignora espaços no começo     -c  só confere se já está em ordem
 *   -k  ordena pelo campo N (até o campo M, ou até o fim da linha); as
 *       letras n, r e f no fim valem só para esse campo ("-k 2n");
 *   -t  separador dos campos (sem -t, os campos são separados por espaços);
 *   -o  grava o resultado num arquivo (pode ser o mesmo que foi lido).
 *
 * Segue a descrição do sort na norma POSIX (IEEE Std 1003.1). As letras são
 * comparadas pelo número do byte em Latin-1 (sem regras de idioma).
 *
 * A ordenação é por intercalação ("merge sort"): divide a lista ao meio,
 * ordena cada metade e junta as duas pegando sempre a menor da frente. É
 * estável (linhas iguais ficam na ordem em que vieram) e nunca fica lenta,
 * mesmo com a entrada já ordenada ou ao contrário. */
#include "sagui.h"

static bool inverso, numerico, unicos, ignorar_caixa, pular_brancos, so_conferir;
static int campo_ini, campo_fim, separador = -1;

/* Acha o começo do campo k (1 = o primeiro) numa linha. Sem separador, um
 * campo começa depois de uma sequência de espaços, e os espaços da frente
 * contam como parte dele (como no sort clássico; -b os ignora). */
static const char *campo(const char *s, int k)
{
    for (int i = 1; i < k && *s; i++) {
        if (separador >= 0) {
            while (*s && (unsigned char)*s != separador) s++;
            if (*s) s++;
        } else {
            while (*s == ' ' || *s == '\t') s++;
            while (*s && *s != ' ' && *s != '\t') s++;
        }
    }
    return s;
}

/* Onde a chave termina: no fim do campo M, ou no fim da linha. */
static const char *fim_chave(const char *ini_linha)
{
    if (!campo_fim) return ini_linha + tam_texto(ini_linha);
    const char *s = campo(ini_linha, campo_fim);
    if (separador >= 0) while (*s && (unsigned char)*s != separador) s++;
    else {
        while (*s == ' ' || *s == '\t') s++;
        while (*s && *s != ' ' && *s != '\t') s++;
    }
    return s;
}

/* Lê o número do começo de um texto, em milésimos, para "-n": aceita
 * espaços antes, sinal, parte inteira e até três casas depois do ponto ou
 * da vírgula. Um texto sem número vale zero, como no sort clássico. */
static int64_t valor(const char *s, const char *fim)
{
    while (s < fim && (*s == ' ' || *s == '\t')) s++;
    bool neg = false;
    if (s < fim && (*s == '-' || *s == '+')) neg = *s++ == '-';
    int64_t v = 0;
    while (s < fim && *s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    v *= 1000;
    if (s < fim && (*s == '.' || *s == ',')) {
        s++;
        for (int casa = 100; casa >= 1; casa /= 10)
            if (s < fim && *s >= '0' && *s <= '9') v += (*s++ - '0') * casa;
    }
    return neg ? -v : v;
}

static int comparar_texto(const char *a, const char *fa, const char *b, const char *fb)
{
    if (pular_brancos) {
        while (a < fa && (*a == ' ' || *a == '\t')) a++;
        while (b < fb && (*b == ' ' || *b == '\t')) b++;
    }
    for (;; a++, b++) {
        int x = a < fa ? (unsigned char)*a : -1, y = b < fb ? (unsigned char)*b : -1;
        if (ignorar_caixa) { if (x >= 0) x = minuscula_latin1(x); if (y >= 0) y = minuscula_latin1(y); }
        if (x != y) return x < y ? -1 : 1;
        if (x < 0) return 0;
    }
}

/* Compara duas linhas pela chave pedida. Se as chaves empatam, a linha
 * inteira desempata (com -u não: aí chaves iguais são "a mesma linha"). */
static int comparar(const char *la, const char *lb)
{
    const char *a = la, *b = lb, *fa, *fb;
    if (campo_ini > 1) { a = campo(la, campo_ini); b = campo(lb, campo_ini); }
    fa = fim_chave(la);
    fb = fim_chave(lb);
    if (fa < a) fa = a;
    if (fb < b) fb = b;
    int r;
    if (numerico) {
        int64_t x = valor(a, fa), y = valor(b, fb);
        r = x < y ? -1 : x > y ? 1 : 0;
    } else {
        r = comparar_texto(a, fa, b, fb);
    }
    if (r == 0 && !unicos && (campo_ini > 1 || numerico || ignorar_caixa || pular_brancos))
        r = comparar_texto(la, la + tam_texto(la), lb, lb + tam_texto(lb));
    return inverso ? -r : r;
}

/* Ordena v[0..n) por intercalação, usando tmp (do mesmo tamanho). */
static void ordenar(char **v, char **tmp, int n)
{
    if (n < 2) return;
    int meio = n / 2;
    ordenar(v, tmp, meio);
    ordenar(v + meio, tmp, n - meio);
    int i = 0, j = meio, k = 0;
    while (i < meio && j < n) tmp[k++] = comparar(v[j], v[i]) < 0 ? v[j++] : v[i++];
    while (i < meio) tmp[k++] = v[i++];
    while (j < n) tmp[k++] = v[j++];
    memcpy(v, tmp, (size_t)n * sizeof *v);
}

/* "-k 2", "-k 2,3", "-k 2n", "-k 2,2nr". */
static bool ler_chave(const char *s)
{
    if (*s < '1' || *s > '9') return false;
    campo_ini = 0;
    while (*s >= '0' && *s <= '9') campo_ini = campo_ini * 10 + (*s++ - '0');
    if (*s == ',') {
        s++;
        campo_fim = 0;
        while (*s >= '0' && *s <= '9') campo_fim = campo_fim * 10 + (*s++ - '0');
    }
    for (; *s; s++) {
        if (*s == 'n') numerico = true;
        else if (*s == 'r') inverso = true;
        else if (*s == 'f') ignorar_caixa = true;
        else if (*s == 'b') pular_brancos = true;
        else return false;
    }
    return true;
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: sort [-rnufbc] [-k N[,M]] [-t C] [-o arquivo] [arquivo...]\n");
    return 2;
}

int principal(int argc, char **argv)
{
    const char *saida = NULL;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        for (const char *o = argv[i] + 1; *o; o++) {
            if (*o == 'k' || *o == 't' || *o == 'o') {
                const char *v = o[1] ? o + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!v) return uso();
                if (*o == 'k' && !ler_chave(v)) { imprimir_em(ERROS, "sort: chave inválida: %s\n", v); return 2; }
                if (*o == 't') separador = (unsigned char)v[0];
                if (*o == 'o') saida = v;
                break;
            }
            if (*o == 'r') inverso = true;
            else if (*o == 'n') numerico = true;
            else if (*o == 'u') unicos = true;
            else if (*o == 'f') ignorar_caixa = true;
            else if (*o == 'b') pular_brancos = true;
            else if (*o == 'c') so_conferir = true;
            else { imprimir_em(ERROS, "sort: opção desconhecida -%c\n", *o); return uso(); }
        }
    }

    /* Junta tudo num texto só: cada arquivo termina com '\n', mesmo que a
     * última linha dele não tenha. */
    unsigned total = 0, cap = 4096;
    char *tudo = aloca(cap);
    int erros = 0, arquivos = argc - i;
    for (int k = 0; tudo && k < (arquivos ? arquivos : 1); k++) {
        int fd = abrir_para_ler("sort", arquivos ? argv[i + k] : "-");
        if (fd < 0) { erros++; continue; }
        unsigned n;
        char *b = ler_ate_o_fim(fd, &n);
        if (fd != ENTRADA) fechar(fd);
        if (!b) { libera(tudo); tudo = NULL; break; }
        n = (unsigned)utf8_para_latin1(b, n, NULL);
        if (total + n + 2 > cap) {
            while (total + n + 2 > cap) cap *= 2;
            char *novo = realoca(tudo, cap);
            if (!novo) { libera(tudo); tudo = NULL; libera(b); break; }
            tudo = novo;
        }
        memcpy(tudo + total, b, n);
        total += n;
        if (n && b[n - 1] != '\n') tudo[total++] = '\n';
        libera(b);
    }
    if (!tudo) { imprimir_em(ERROS, "sort: memória insuficiente\n"); return 2; }

    /* Corta em linhas: cada '\n' vira o fim de um texto. */
    int qtd = 0;
    for (unsigned p = 0; p < total; p++) if (tudo[p] == '\n') qtd++;
    char **linhas = aloca((size_t)(qtd + 1) * sizeof *linhas);
    char **tmp = aloca((size_t)(qtd + 1) * sizeof *tmp);
    if (!linhas || !tmp) { imprimir_em(ERROS, "sort: memória insuficiente\n"); return 2; }
    char *ini = tudo;
    int k = 0;
    for (unsigned p = 0; p < total; p++)
        if (tudo[p] == '\n') { tudo[p] = 0; linhas[k++] = ini; ini = tudo + p + 1; }

    if (so_conferir) {
        for (k = 1; k < qtd; k++) {
            int r = comparar(linhas[k - 1], linhas[k]);
            if (r > 0 || (unicos && r == 0)) {
                imprimir_em(ERROS, "sort: fora de ordem na linha %d: %s\n", k + 1, linhas[k]);
                return 1;
            }
        }
        return 0;
    }

    ordenar(linhas, tmp, qtd);
    int fd = SAIDA;
    if (saida) {
        fd = abrir(saida, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
        if (fd < 0) { imprimir_em(ERROS, "sort: %s: %s\n", saida, nome_erro(fd)); return 2; }
    }
    for (k = 0; k < qtd; k++) {
        if (unicos && k > 0 && comparar(linhas[k - 1], linhas[k]) == 0) continue;
        if (escrever_texto(fd, linhas[k]) < 0 || escrever_texto(fd, "\n") < 0) break;
    }
    if (fd != SAIDA) fechar(fd);
    return erros ? 2 : 0;
}
