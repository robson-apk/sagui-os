/* bc (calcular) - calculadora de linha de comando, com números de muitos
 * dígitos (H91; a ideia veio do fork SaguiOS-Extended, do Alejandro
 * Anuncibay, e o programa foi reescrito sobre a libsagui atual).
 *
 *   echo "2^100" | bc              1267650600228229401496703205376
 *   echo "scale=3; 10/3" | bc      3.333
 *   bc -e "sqrt(2)" -l             1.41421356237309504880
 *   bc                             (digite contas, uma por linha; quit sai)
 *   bc contas.txt                  (as contas de um arquivo)
 *
 * Tem + - * / % ^ (potência inteira), parênteses, sqrt(x), comparações
 * (== != < <= > >=, que dão 1 ou 0), variáveis (x=5; depois x*2) e
 * "scale", o número de casas depois do ponto nas divisões (0 no começo,
 * como no bc dos sistemas Unix; -l começa com 20). Várias contas numa
 * linha vão separadas por ";". É o pedaço do bc da norma POSIX (IEEE Std
 * 1003.1) que se usa como calculadora: sem funções próprias, laços, ibase
 * e obase. Um número menor que 1 sai com o zero na frente (0.5, não .5).
 *
 * Os números são guardados como uma lista de dígitos decimais (até 200)
 * mais quantos deles ficam depois do ponto: 12.5 é "125" com 1 casa. As
 * contas são as da escola, dígito a dígito: somar e subtrair da direita
 * para a esquerda com "vai um", multiplicar cada dígito por cada dígito,
 * e dividir pela divisão longa (quantas vezes o divisor cabe, um dígito
 * do quociente por vez). Assim não há erro de arredondamento binário:
 * 0.1 + 0.2 dá exatamente 0.3. */
#include "sagui.h"

#define MAX_DIGITOS 200
#define MAX_VARIAVEIS 32

typedef struct {
    bool    neg;
    int     casas;                /* quantos dos dígitos ficam depois do ponto */
    int     n;                    /* quantos dígitos há */
    uint8_t d[MAX_DIGITOS];       /* d[0] é o mais significativo */
} num_t;

static int escala;
static bool erro;                 /* houve erro na conta atual */
static int erros;                 /* contas com erro, para o código de saída */
static const char *p;             /* onde a leitura da linha está */

static void falhar(const char *msg)
{
    if (!erro) { imprimir_em(ERROS, "bc: %s\n", msg); erros++; }
    erro = true;
}

static void zerar(num_t *a) { a->neg = false; a->casas = 0; a->n = 1; a->d[0] = 0; }

static void de_inteiro(num_t *a, int v)
{
    char t[16];
    formatar_texto(t, sizeof t, "%d", v < 0 ? -v : v);
    a->neg = v < 0;
    a->casas = 0;
    a->n = (int)tam_texto(t);
    for (int i = 0; i < a->n; i++) a->d[i] = (uint8_t)(t[i] - '0');
}

/* Tira os zeros da esquerda (deixando ao menos um dígito antes do ponto)
 * e o sinal do zero. */
static void ajeitar(num_t *a)
{
    int z = 0;
    while (z < a->n - a->casas - 1 && a->d[z] == 0) z++;
    if (z) { memmove(a->d, a->d + z, (size_t)(a->n - z)); a->n -= z; }
    if (a->n == a->casas) {                       /* ".5" vira "0.5" */
        if (a->n == MAX_DIGITOS) { a->n--; a->casas--; }
        memmove(a->d + 1, a->d, (size_t)a->n);
        a->d[0] = 0;
        a->n++;
    }
    bool tudo_zero = true;
    for (int i = 0; i < a->n; i++) if (a->d[i]) { tudo_zero = false; break; }
    if (tudo_zero) a->neg = false;
}

/* Muda o número de casas: acrescenta zeros ou corta (sem arredondar, como
 * o bc). */
static bool mudar_casas(num_t *a, int c)
{
    if (c > a->casas) {
        int mais = c - a->casas;
        if (a->n + mais > MAX_DIGITOS) { falhar("número grande demais"); return false; }
        memset(a->d + a->n, 0, (size_t)mais);
        a->n += mais;
    } else {
        a->n -= a->casas - c;
    }
    a->casas = c;
    ajeitar(a);
    return true;
}

/* Compara os valores absolutos: -1, 0 ou 1. */
static int comparar_abs(num_t a, num_t b)
{
    int c = a.casas > b.casas ? a.casas : b.casas;
    if (!mudar_casas(&a, c) || !mudar_casas(&b, c)) return 0;
    if (a.n != b.n) return a.n < b.n ? -1 : 1;
    for (int i = 0; i < a.n; i++) if (a.d[i] != b.d[i]) return a.d[i] < b.d[i] ? -1 : 1;
    return 0;
}

static int comparar(const num_t *a, const num_t *b)
{
    if (a->neg != b->neg) return a->neg ? -1 : 1;
    int c = comparar_abs(*a, *b);
    return a->neg ? -c : c;
}

/* a + b (ou a - b com subtrair). Com as casas igualadas, soma ou subtrai
 * os valores absolutos conforme os sinais, da direita para a esquerda. */
static num_t somar(num_t a, num_t b, bool subtrair)
{
    if (subtrair) b.neg = !b.neg;
    int c = a.casas > b.casas ? a.casas : b.casas;
    num_t r;
    zerar(&r);
    if (!mudar_casas(&a, c) || !mudar_casas(&b, c)) return r;
    if (a.neg != b.neg && comparar_abs(a, b) < 0) { num_t t = a; a = b; b = t; }   /* |a| >= |b| */
    int n = (a.n > b.n ? a.n : b.n) + 1;
    if (n > MAX_DIGITOS) { falhar("número grande demais"); return r; }
    int vai = 0;
    for (int i = 1; i <= n; i++) {
        int x = i <= a.n ? a.d[a.n - i] : 0, y = i <= b.n ? b.d[b.n - i] : 0;
        int v = a.neg == b.neg ? x + y + vai : x - y - vai;
        vai = 0;
        if (v >= 10) { v -= 10; vai = 1; }
        if (v < 0) { v += 10; vai = 1; }
        r.d[n - i] = (uint8_t)v;
    }
    r.n = n;
    r.casas = c;
    r.neg = a.neg;
    ajeitar(&r);
    return r;
}

/* a * b com todas as casas (a soma das casas dos dois). */
static num_t multiplicar_exato(const num_t *a, const num_t *b)
{
    num_t r;
    zerar(&r);
    int n = a->n + b->n;
    if (n > MAX_DIGITOS) { falhar("número grande demais"); return r; }
    static int soma[2 * MAX_DIGITOS];
    memset(soma, 0, sizeof soma);
    for (int i = a->n - 1; i >= 0; i--)
        for (int j = b->n - 1; j >= 0; j--)
            soma[i + j + 1] += a->d[i] * b->d[j];
    for (int k = n - 1; k > 0; k--) { soma[k - 1] += soma[k] / 10; soma[k] %= 10; }
    for (int k = 0; k < n; k++) r.d[k] = (uint8_t)soma[k];
    r.n = n;
    r.casas = a->casas + b->casas;
    r.neg = a->neg != b->neg;
    ajeitar(&r);
    return r;
}

/* Como no bc: o produto fica com as casas dos dois, mas no máximo
 * max(scale, casas de a, casas de b). */
static num_t multiplicar(const num_t *a, const num_t *b)
{
    num_t r = multiplicar_exato(a, b);
    int c = escala;
    if (a->casas > c) c = a->casas;
    if (b->casas > c) c = b->casas;
    if (r.casas > c) mudar_casas(&r, c);
    return r;
}

/* a / b com "casas" dígitos depois do ponto: a divisão longa de dois
 * inteiros, depois de deslocar o ponto dos dois para o mesmo lugar. */
static num_t dividir(num_t a, num_t b, int casas)
{
    num_t q, resto;
    zerar(&q);
    zerar(&resto);
    if (comparar_abs(b, q) == 0) { falhar("divisão por zero"); return q; }
    bool neg = a.neg != b.neg;
    a.neg = b.neg = false;
    /* Dividendo e divisor viram inteiros: a * 10^(casas + casas de b) e
     * b * 10^(casas de a), contando só os dígitos. */
    int desloc = casas + b.casas - a.casas;
    int na = a.n + (desloc > 0 ? desloc : 0);
    if (na > MAX_DIGITOS || b.n - (desloc < 0 ? desloc : 0) > MAX_DIGITOS) { falhar("número grande demais"); return q; }
    if (desloc > 0) memset(a.d + a.n, 0, (size_t)desloc);
    a.n = na;
    a.casas = 0;
    if (desloc < 0) { memset(b.d + b.n, 0, (size_t)-desloc); b.n -= desloc; }
    b.casas = 0;
    ajeitar(&b);
    q.n = 0;
    for (int i = 0; i < a.n; i++) {
        /* resto = resto * 10 + próximo dígito */
        if (resto.n == 1 && resto.d[0] == 0) resto.d[0] = a.d[i];
        else if (resto.n < MAX_DIGITOS) resto.d[resto.n++] = a.d[i];
        int dig = 0;
        while (comparar_abs(resto, b) >= 0) { resto = somar(resto, b, true); dig++; }
        q.d[q.n++] = (uint8_t)dig;
    }
    if (q.n == 0) q.d[q.n++] = 0;
    q.casas = casas;
    if (q.n < q.casas + 1) {                      /* zeros que faltam na frente */
        int falta = q.casas + 1 - q.n;
        memmove(q.d + falta, q.d, (size_t)q.n);
        memset(q.d, 0, (size_t)falta);
        q.n += falta;
    }
    q.neg = neg;
    ajeitar(&q);
    return q;
}

static num_t potencia(const num_t *a, const num_t *e)
{
    num_t r;
    de_inteiro(&r, 1);
    /* O expoente precisa ser inteiro (só zeros depois do ponto). */
    for (int i = e->n - e->casas; i < e->n; i++) if (e->d[i]) { falhar("o expoente precisa ser inteiro"); return r; }
    int k = 0;
    for (int i = 0; i < e->n - e->casas; i++) {
        k = k * 10 + e->d[i];
        if (k > 100000) { falhar("expoente grande demais"); return r; }
    }
    for (int i = 0; i < k && !erro; i++) r = multiplicar_exato(&r, a);
    int c = escala > a->casas ? escala : a->casas;
    if (a->casas * k < c) c = a->casas * k;
    if (r.casas > c) mudar_casas(&r, c);
    if (e->neg) {
        num_t um;
        de_inteiro(&um, 1);
        r = dividir(um, r, escala);
    }
    return r;
}

/* Raiz quadrada pelo método de Newton: x = (x + a/x) / 2 até parar de
 * mudar, com duas casas a mais que o pedido para o corte não atrapalhar. */
static num_t raiz(const num_t *a)
{
    num_t x, dois, ant;
    int c = escala > a->casas ? escala : a->casas;
    if (a->neg) { falhar("raiz de número negativo"); zerar(&x); return x; }
    zerar(&x);
    if (comparar_abs(*a, x) == 0) return x;
    de_inteiro(&dois, 2);
    x = *a;
    num_t um;
    de_inteiro(&um, 1);
    if (comparar_abs(x, um) < 0) x = um;          /* para a < 1, começar de 1 converge */
    for (int i = 0; i < 400 && !erro; i++) {
        ant = x;
        x = dividir(somar(x, dividir(*a, x, c + 2), false), dois, c + 2);
        if (comparar(&x, &ant) == 0) break;
    }
    mudar_casas(&x, c);
    return x;
}

static void escrever_numero(const num_t *a)
{
    char t[MAX_DIGITOS + 4];
    int k = 0;
    bool zero = true;
    for (int i = 0; i < a->n; i++) if (a->d[i]) zero = false;
    if (zero) { escrever_texto(SAIDA, "0\n"); return; }   /* o zero sai sempre "0", como no bc */
    if (a->neg) t[k++] = '-';
    for (int i = 0; i < a->n; i++) {
        if (i == a->n - a->casas) t[k++] = '.';
        t[k++] = (char)('0' + a->d[i]);
    }
    t[k++] = '\n';
    escrever_tudo(SAIDA, t, (unsigned)k);
}

/* ------------------------------------------------------------------ leitura */

static char nomes[MAX_VARIAVEIS][16];
static num_t valores[MAX_VARIAVEIS];
static int qtd_var;

static num_t *variavel(const char *nome)
{
    for (int i = 0; i < qtd_var; i++) if (compara_texto(nomes[i], nome) == 0) return &valores[i];
    if (qtd_var == MAX_VARIAVEIS) { falhar("variáveis demais"); return NULL; }
    copia_texto(nomes[qtd_var], nome, sizeof nomes[0]);
    zerar(&valores[qtd_var]);
    return &valores[qtd_var++];
}

static void pular_espacos(void) { while (*p == ' ' || *p == '\t') p++; }

static bool vem(const char *s)
{
    pular_espacos();
    size_t n = tam_texto(s);
    if (compara_texto_n(p, s, n) == 0) { p += n; return true; }
    return false;
}

static bool eh_letra(char c) { return (c >= 'a' && c <= 'z') || c == '_'; }

static int ler_nome(char *nome, int max)
{
    pular_espacos();
    int n = 0;
    while ((eh_letra(*p) || (n && *p >= '0' && *p <= '9')) && n < max - 1) nome[n++] = *p++;
    nome[n] = 0;
    return n;
}

static num_t expressao(void);

static num_t primario(void)
{
    num_t r;
    zerar(&r);
    pular_espacos();
    if (vem("(")) {
        r = expressao();
        if (!vem(")")) falhar("falta o )");
        return r;
    }
    if ((*p >= '0' && *p <= '9') || *p == '.') {
        r.n = 0;
        r.casas = 0;
        bool ponto = false;
        while ((*p >= '0' && *p <= '9') || (*p == '.' && !ponto)) {
            if (*p == '.') { ponto = true; p++; continue; }
            if (r.n == MAX_DIGITOS) { falhar("número grande demais"); return r; }
            r.d[r.n++] = (uint8_t)(*p++ - '0');
            if (ponto) r.casas++;
        }
        if (r.n == 0) r.d[r.n++] = 0;
        ajeitar(&r);
        return r;
    }
    char nome[16];
    if (ler_nome(nome, sizeof nome)) {
        if (compara_texto(nome, "sqrt") == 0 && vem("(")) {
            num_t a = expressao();
            if (!vem(")")) falhar("falta o )");
            return raiz(&a);
        }
        if (compara_texto(nome, "scale") == 0) { de_inteiro(&r, escala); return r; }
        num_t *v = variavel(nome);
        return v ? *v : r;
    }
    falhar("conta inválida");
    return r;
}

/* Como no bc, o menos na frente vale antes da potência: -2^2 é 4. */
static num_t unario(void)
{
    if (vem("-")) { num_t a = unario(); a.neg = !a.neg; ajeitar(&a); return a; }
    if (vem("+")) return unario();
    return primario();
}

static num_t pot(void)
{
    num_t a = unario();
    if (vem("^")) { num_t e = pot(); return potencia(&a, &e); }
    return a;
}

static num_t termo(void)
{
    num_t a = pot();
    for (;;) {
        if (vem("*")) { num_t b = pot(); a = multiplicar(&a, &b); }
        else if (vem("/")) a = dividir(a, pot(), escala);
        else if (vem("%")) {
            /* Resto como no bc: a - (a / b) * b, com a divisão em "scale" casas. */
            num_t b = pot(), q = dividir(a, b, escala), qb = multiplicar_exato(&q, &b);
            a = somar(a, qb, true);
        } else return a;
    }
}

static num_t soma(void)
{
    num_t a = termo();
    for (;;) {
        if (vem("+")) a = somar(a, termo(), false);
        else if (vem("-")) a = somar(a, termo(), true);
        else return a;
    }
}

static num_t expressao(void)
{
    static const char *ops[] = { "==", "!=", "<=", ">=", "<", ">" };
    num_t a = soma();
    for (int k = 0; k < 6; k++) {
        if (!vem(ops[k])) continue;
        num_t b = soma();
        int c = comparar(&a, &b);
        bool v = k == 0 ? c == 0 : k == 1 ? c != 0 : k == 2 ? c <= 0 : k == 3 ? c >= 0 : k == 4 ? c < 0 : c > 0;
        de_inteiro(&a, v);
        break;
    }
    return a;
}

/* Uma linha: contas separadas por ";". Devolve false em "quit". */
static bool linha(char *texto)
{
    char *comentario = acha_char(texto, '#');
    if (comentario) *comentario = 0;
    p = texto;
    for (;;) {
        erro = false;
        pular_espacos();
        if (!*p) return true;
        if (*p == ';') { p++; continue; }
        const char *ini = p;
        char nome[16];
        if (ler_nome(nome, sizeof nome) && compara_texto(nome, "quit") == 0) return false;
        /* Atribuição "nome = conta" (mas não "nome == conta"). */
        pular_espacos();
        if (nome[0] && *p == '=' && p[1] != '=') {
            p++;
            num_t v = expressao();
            if (!erro && compara_texto(nome, "scale") == 0) {
                if (v.neg || v.n - v.casas > 2) falhar("scale vai de 0 a 99");
                else {
                    int s = 0;
                    for (int i = 0; i < v.n - v.casas; i++) s = s * 10 + v.d[i];
                    escala = s;
                }
            } else if (!erro) {
                num_t *dest = variavel(nome);
                if (dest) *dest = v;
            }
        } else {
            p = ini;
            num_t v = expressao();
            pular_espacos();
            if (!erro && *p && *p != ';') falhar("conta inválida");
            if (!erro) escrever_numero(&v);
        }
        if (erro)
            while (*p && *p != ';') p++;   /* pula para a próxima conta */
    }
}

/* Roda as linhas de um texto inteiro (de um arquivo ou de -e). */
static bool texto(char *t)
{
    while (t && *t) {
        char *f = acha_char(t, '\n');
        if (f) *f = 0;
        if (!linha(t)) return false;
        t = f ? f + 1 : NULL;
    }
    return true;
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: bc [-l] [-e conta]... [arquivo...]\n");
    return 1;
}

int principal(int argc, char **argv)
{
    static char contas[2048];
    bool tem_contas = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        for (const char *o = argv[i] + 1; *o; o++) {
            if (*o == 'e') {
                const char *v = o[1] ? o + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!v) return uso();
                junta_texto(contas, v, sizeof contas);
                junta_texto(contas, "\n", sizeof contas);
                tem_contas = true;
                break;
            }
            if (*o == 'l') escala = 20;          /* o -l do bc também liga a biblioteca de matemática */
            else if (*o != 'q') { imprimir_em(ERROS, "bc: opção desconhecida -%c\n", *o); return uso(); }
        }
    }
    if (tem_contas) {
        texto(contas);
        return erros ? 1 : 0;
    }
    if (i < argc) {
        for (; i < argc; i++) {
            int fd = abrir_para_ler("bc", argv[i]);
            if (fd < 0) { erros++; continue; }
            char *b = ler_ate_o_fim(fd, NULL);
            if (fd != ENTRADA) fechar(fd);
            if (!b) { imprimir_em(ERROS, "bc: memória insuficiente\n"); return 1; }
            bool continua = texto(b);
            libera(b);
            if (!continua) break;
        }
        return erros ? 1 : 0;
    }
    /* Da entrada, uma linha por vez: assim o bc responde a cada conta
     * que chega do terminal, sem esperar o fim da entrada. */
    static char l[1024];
    while (ler_linha(ENTRADA, l, sizeof l) >= 0)
        if (!linha(l)) break;
    return erros ? 1 : 0;
}
