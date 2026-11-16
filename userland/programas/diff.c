/* diff (diferencas) - compara dois arquivos linha a linha (H91; a ideia veio
 * do fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi
 * reescrito sobre a libsagui atual).
 *
 *   diff [-u] [-q] [-i] [-b] arquivo1 arquivo2      ("-" = a entrada)
 *
 *   (sem opção)  o formato clássico: "2c2", "< linha velha", "---",
 *                "> linha nova" (a = acrescentar, d = apagar, c = trocar);
 *   -u  o formato "unificado", com 3 linhas de contexto em volta de cada
 *       mudança, "-" para o que saiu e "+" para o que entrou;
 *   -q  só diz se os arquivos são diferentes;
 *   -i  ignora maiúsculas;   -b  ignora diferenças na quantidade de espaços.
 * Código de saída: 0 iguais, 1 diferentes, 2 erro (como na norma POSIX).
 *
 * Como funciona: o que as duas listas de linhas têm em comum, na mesma
 * ordem, é a "maior subsequência comum" (LCS). As linhas iguais do começo e
 * do fim são tiradas antes; para o miolo, uma tabela guarda, para cada par
 * de posições (i, j), o tamanho da LCS do resto dos dois arquivos a partir
 * dali. Depois, andando do começo, cada passo segue para onde a tabela diz
 * que se aproveita mais: linha igual (fica), linha do 1 (saiu) ou do 2
 * (entrou). A tabela cresce com o produto dos tamanhos, por isso há um
 * limite para o miolo diferente. */
#include "sagui.h"

#define MAX_CELULAS (4u * 1024 * 1024)

typedef struct {
    char   **l;      /* as linhas, já terminadas em zero */
    int      n;
} texto_t;

static bool ignorar_caixa, ignorar_espacos;

/* Lê um arquivo inteiro e corta em linhas. */
static bool carregar(const char *nome, texto_t *t)
{
    int fd = abrir_para_ler("diff", nome);
    if (fd < 0) return false;
    unsigned n;
    char *b = ler_ate_o_fim(fd, &n);
    if (fd != ENTRADA) fechar(fd);
    if (!b) { imprimir_em(ERROS, "diff: memória insuficiente\n"); return false; }
    n = (unsigned)utf8_para_latin1(b, n, NULL);
    b[n] = 0;
    int qtd = 0;
    for (unsigned p = 0; p < n; p++) if (b[p] == '\n') qtd++;
    if (n && b[n - 1] != '\n') qtd++;
    t->l = aloca((size_t)(qtd + 1) * sizeof(char *));
    if (!t->l) { imprimir_em(ERROS, "diff: memória insuficiente\n"); return false; }
    t->n = 0;
    for (char *p = b; p < b + n;) {
        char *f = acha_char(p, '\n');
        if (f) { *f = 0; if (f > p && f[-1] == '\r') f[-1] = 0; }
        t->l[t->n++] = p;
        p = f ? f + 1 : b + n;
    }
    return true;
}

static bool iguais(const char *a, const char *b)
{
    for (;;) {
        if (ignorar_espacos) {
            /* Qualquer sequência de espaços vale como um espaço só, e os
             * do fim da linha não contam. */
            bool ea = *a == ' ' || *a == '\t', eb = *b == ' ' || *b == '\t';
            while (*a == ' ' || *a == '\t') a++;
            while (*b == ' ' || *b == '\t') b++;
            if (ea != eb && *a && *b) return false;
        }
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (ignorar_caixa) { x = minuscula_latin1(x); y = minuscula_latin1(y); }
        if (x != y) return false;
        if (!x) return true;
        a++; b++;
    }
}

/* O roteiro das mudanças: uma letra por passo. ' ' = linha igual nos dois,
 * '-' = só no 1, '+' = só no 2. */
static char *roteiro;
static int passos;

static bool comparar(texto_t *a, texto_t *b)
{
    int ini = 0, fa = a->n, fb = b->n;
    while (ini < fa && ini < fb && iguais(a->l[ini], b->l[ini])) ini++;
    while (fa > ini && fb > ini && iguais(a->l[fa - 1], b->l[fb - 1])) { fa--; fb--; }
    int n = fa - ini, m = fb - ini;
    roteiro = aloca((size_t)(a->n + b->n + 1));
    if (!roteiro) return false;
    passos = 0;
    for (int k = 0; k < ini; k++) roteiro[passos++] = ' ';
    if ((unsigned)(n + 1) * (unsigned)(m + 1) > MAX_CELULAS) {
        imprimir_em(ERROS, "diff: arquivos diferentes demais para comparar (%d x %d linhas)\n", n, m);
        return false;
    }
    /* tab[i][j] = tamanho da LCS de a[ini+i..] e b[ini+j..]. */
    uint16_t *tab = aloca_zerado((size_t)(n + 1) * (size_t)(m + 1) * sizeof(uint16_t));
    if (!tab) { imprimir_em(ERROS, "diff: memória insuficiente\n"); return false; }
#define T(i, j) tab[(i) * (m + 1) + (j)]
    for (int i = n - 1; i >= 0; i--)
        for (int j = m - 1; j >= 0; j--)
            T(i, j) = iguais(a->l[ini + i], b->l[ini + j]) ? (uint16_t)(T(i + 1, j + 1) + 1)
                    : T(i + 1, j) >= T(i, j + 1) ? T(i + 1, j) : T(i, j + 1);
    int i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && iguais(a->l[ini + i], b->l[ini + j])) { roteiro[passos++] = ' '; i++; j++; }
        else if (j >= m || (i < n && T(i + 1, j) >= T(i, j + 1))) { roteiro[passos++] = '-'; i++; }
        else { roteiro[passos++] = '+'; j++; }
    }
#undef T
    libera(tab);
    for (int k = fa; k < a->n; k++) roteiro[passos++] = ' ';
    return true;
}

/* "3" ou "3,5", para o formato clássico. */
static void intervalo(int de, int ate)
{
    if (ate > de) imprimir("%d,%d", de, ate);
    else imprimir("%d", de);
}

static void classico(texto_t *a, texto_t *b)
{
    int i = 0, j = 0;   /* linhas já passadas em cada arquivo */
    for (int p = 0; p < passos;) {
        if (roteiro[p] == ' ') { i++; j++; p++; continue; }
        int sai = 0, entra = 0;
        while (p < passos && roteiro[p] != ' ') { if (roteiro[p] == '-') sai++; else entra++; p++; }
        if (sai && entra) { intervalo(i + 1, i + sai); imprimir("c"); intervalo(j + 1, j + entra); }
        else if (sai) { intervalo(i + 1, i + sai); imprimir("d%d", j); }
        else { imprimir("%da", i); intervalo(j + 1, j + entra); }
        imprimir("\n");
        for (int k = 0; k < sai; k++) imprimir("< %s\n", a->l[i + k]);
        if (sai && entra) imprimir("---\n");
        for (int k = 0; k < entra; k++) imprimir("> %s\n", b->l[j + k]);
        i += sai;
        j += entra;
    }
}

/* "-3,4" no cabeçalho do bloco unificado: com uma linha só, só o número;
 * sem nenhuma, o número da linha de antes e ",0". */
static void faixa(char sinal, int ini, int qtd)
{
    if (qtd == 1) imprimir("%c%d", sinal, ini);
    else imprimir("%c%d,%d", sinal, qtd ? ini : ini - 1, qtd);
}

static void unificado(texto_t *a, texto_t *b, const char *na, const char *nb)
{
    const int CONTEXTO = 3;
    imprimir("--- %s\n+++ %s\n", na, nb);
    int p = 0;
    while (p < passos) {
        while (p < passos && roteiro[p] == ' ') p++;
        if (p == passos) break;
        /* O bloco começa até 3 linhas antes da mudança e termina quando
         * aparecem mais de 6 linhas iguais seguidas (3 depois desta
         * mudança e 3 antes da próxima). */
        int ini = p - CONTEXTO < 0 ? 0 : p - CONTEXTO, fim = p;
        while (fim < passos) {
            if (roteiro[fim] != ' ') { fim++; continue; }
            int iguais_seguidas = 0;
            while (fim + iguais_seguidas < passos && roteiro[fim + iguais_seguidas] == ' ') iguais_seguidas++;
            if (fim + iguais_seguidas == passos || iguais_seguidas > 2 * CONTEXTO) {
                fim += iguais_seguidas < CONTEXTO ? iguais_seguidas : CONTEXTO;
                break;
            }
            fim += iguais_seguidas;
        }
        /* Em que linha de cada arquivo o bloco começa. */
        int la = 0, lb = 0;
        for (int k = 0; k < ini; k++) { if (roteiro[k] != '+') la++; if (roteiro[k] != '-') lb++; }
        int qa = 0, qb = 0;
        for (int k = ini; k < fim; k++) { if (roteiro[k] != '+') qa++; if (roteiro[k] != '-') qb++; }
        imprimir("@@ ");
        faixa('-', la + 1, qa);
        imprimir(" ");
        faixa('+', lb + 1, qb);
        imprimir(" @@\n");
        for (int k = ini; k < fim; k++) {
            if (roteiro[k] == ' ') { imprimir(" %s\n", a->l[la++]); lb++; }
            else if (roteiro[k] == '-') imprimir("-%s\n", a->l[la++]);
            else imprimir("+%s\n", b->l[lb++]);
        }
        p = fim;
    }
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: diff [-u] [-q] [-i] [-b] arquivo1 arquivo2\n");
    return 2;
}

int principal(int argc, char **argv)
{
    bool unif = false, so_dizer = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        for (const char *o = argv[i] + 1; *o; o++) {
            if (*o == 'u') unif = true;
            else if (*o == 'q') so_dizer = true;
            else if (*o == 'i') ignorar_caixa = true;
            else if (*o == 'b') ignorar_espacos = true;
            else { imprimir_em(ERROS, "diff: opção desconhecida -%c\n", *o); return uso(); }
        }
    }
    if (argc - i != 2) return uso();
    texto_t a, b;
    if (!carregar(argv[i], &a) || !carregar(argv[i + 1], &b)) return 2;
    if (!comparar(&a, &b)) return 2;
    bool diferentes = false;
    for (int p = 0; p < passos; p++) if (roteiro[p] != ' ') { diferentes = true; break; }
    if (!diferentes) return 0;
    if (so_dizer) imprimir("Os arquivos %s e %s são diferentes\n", argv[i], argv[i + 1]);
    else if (unif) unificado(&a, &b, argv[i], argv[i + 1]);
    else classico(&a, &b);
    return 1;
}
