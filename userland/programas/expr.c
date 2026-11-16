/* expr (expressao) - calcula uma expressão dada em pedaços na linha de
 * comando (H91; a ideia veio do fork SaguiOS-Extended, do Alejandro
 * Anuncibay, e o programa foi reescrito sobre a libsagui atual).
 *
 *   expr 3 + 4            7
 *   expr 7 / 2            3 (só inteiros)
 *   expr 2 "<" 10         1 (verdadeiro; "<" e ">" entre aspas, senão o
 *                         shell entende como redirecionamento)
 *   expr abc = abc        1
 *   expr sagui : "sa.*"   5 (quantas letras do começo casam com a expressão)
 *   expr length sagui     5;  expr substr sagui 2 3   agu;
 *   expr index sagui gu   3 (primeira posição de qualquer uma das letras)
 *
 * Cada número, texto e operador é um argumento separado. Da prioridade
 * menor para a maior: |  &  = != < <= > >=  + -  * / %  :  (e parênteses).
 * a | b dá a se a não for vazio nem 0, senão b; a & b dá a se nenhum dos
 * dois for vazio nem 0, senão 0. As comparações são de números se os dois
 * lados forem números, senão de textos.
 *
 * Segue a descrição do expr na norma POSIX (IEEE Std 1003.1); length,
 * substr, index e match são extras comuns nos sistemas Unix. Código de
 * saída: 0 se o resultado não é vazio nem 0, 1 se é, 2 se a expressão é
 * inválida. A expressão de ":" usa o motor de expressao_simples.h (o mesmo
 * do sed): ela é casada só a partir do começo do texto, e não há grupos. */
#include "sagui.h"
#include "expressao_simples.h"

typedef struct { char t[256]; } valor_t;

static char **arg;
static int qtd, pos;
static bool invalida;

static int falha(const char *msg)
{
    if (!invalida) imprimir_em(ERROS, "expr: %s\n", msg);
    invalida = true;
    return 0;
}

static valor_t texto(const char *s) { valor_t v; copia_texto(v.t, s, sizeof v.t); return v; }
static valor_t numero(int n) { valor_t v; formatar_texto(v.t, sizeof v.t, "%d", n); return v; }

static bool eh_numero(const valor_t *v, int *n) { return converter_numero(v->t, n); }
static bool nulo(const valor_t *v) { int n; return !v->t[0] || (eh_numero(v, &n) && n == 0); }

static bool vem(const char *op)
{
    if (pos < qtd && compara_texto(arg[pos], op) == 0) { pos++; return true; }
    return false;
}

static valor_t ou(void);

static valor_t primario(void)
{
    if (pos >= qtd) { falha("falta um operando"); return texto(""); }
    if (vem("(")) {
        valor_t v = ou();
        if (!vem(")")) falha("falta o )");
        return v;
    }
    /* As palavras extras só valem como operador se vierem seguidas dos
     * operandos; "expr length" sozinho é o texto "length". */
    const char *a = arg[pos];
    if (compara_texto(a, "length") == 0 && pos + 1 < qtd) {
        pos++;
        valor_t v = primario();
        return numero((int)tam_texto(v.t));
    }
    if (compara_texto(a, "substr") == 0 && pos + 3 < qtd) {
        pos++;
        valor_t s = primario(), i = primario(), n = primario();
        int ini, tam, len = (int)tam_texto(s.t);
        if (!eh_numero(&i, &ini) || !eh_numero(&n, &tam)) { falha("substr precisa de números"); return texto(""); }
        if (ini < 1 || ini > len || tam < 1) return texto("");
        if (tam > len - ini + 1) tam = len - ini + 1;
        s.t[ini - 1 + tam] = 0;
        return texto(s.t + ini - 1);
    }
    if (compara_texto(a, "index") == 0 && pos + 2 < qtd) {
        pos++;
        valor_t s = primario(), letras = primario();
        for (int k = 0; s.t[k]; k++)
            if (acha_char(letras.t, s.t[k])) return numero(k + 1);
        return numero(0);
    }
    if (compara_texto(a, "match") == 0 && pos + 2 < qtd) {
        pos++;
        valor_t s = primario(), r = primario();
        expressao_t e;
        if (!montar_expressao(&e, r.t, (int)tam_texto(r.t), false)) { falha("expressão inválida"); return texto(""); }
        const char *fim = casar_aqui(&e, 0, s.t, s.t + tam_texto(s.t));
        return numero(fim ? (int)(fim - s.t) : 0);
    }
    pos++;
    return texto(a);
}

/* texto : expressão - quantas letras do começo casam. */
static valor_t casamento(void)
{
    valor_t v = primario();
    while (vem(":")) {
        valor_t r = primario();
        expressao_t e;
        if (!montar_expressao(&e, r.t, (int)tam_texto(r.t), false)) { falha("expressão inválida"); return texto(""); }
        const char *fim = casar_aqui(&e, 0, v.t, v.t + tam_texto(v.t));
        v = numero(fim ? (int)(fim - v.t) : 0);
    }
    return v;
}

/* Conta com dois números; a conta é feita em 64 bits para perceber quando
 * o resultado não cabe nos 32 bits de um int. */
static valor_t conta(valor_t a, char op, valor_t b)
{
    int x, y;
    if (!eh_numero(&a, &x) || !eh_numero(&b, &y)) { falha("conta com algo que não é número"); return texto(""); }
    int64_t r;
    if (op == '+') r = (int64_t)x + y;
    else if (op == '-') r = (int64_t)x - y;
    else if (op == '*') r = (int64_t)x * y;
    else if (y == 0) { falha("divisão por zero"); return texto(""); }
    else if (y == -1) r = op == '/' ? -(int64_t)x : 0;   /* o menor int dividido por -1 não cabe */
    else r = op == '/' ? x / y : x % y;
    if (r > 0x7FFFFFFF || r < -0x7FFFFFFF - 1) { falha("número grande demais"); return texto(""); }
    return numero((int)r);
}

static valor_t multiplicacao(void)
{
    valor_t v = casamento();
    for (;;) {
        char op = vem("*") ? '*' : vem("/") ? '/' : vem("%") ? '%' : 0;
        if (!op) return v;
        v = conta(v, op, casamento());
    }
}

static valor_t soma(void)
{
    valor_t v = multiplicacao();
    for (;;) {
        char op = vem("+") ? '+' : vem("-") ? '-' : 0;
        if (!op) return v;
        v = conta(v, op, multiplicacao());
    }
}

static valor_t comparacao(void)
{
    static const char *ops[] = { "=", "!=", "<", "<=", ">", ">=" };
    valor_t v = soma();
    for (;;) {
        int k = 0;
        while (k < 6 && !vem(ops[k])) k++;
        if (k == 6) return v;
        valor_t w = soma();
        int x, y, c;
        if (eh_numero(&v, &x) && eh_numero(&w, &y)) c = x < y ? -1 : x > y;
        else c = compara_texto(v.t, w.t);
        bool r = k == 0 ? c == 0 : k == 1 ? c != 0 : k == 2 ? c < 0 : k == 3 ? c <= 0 : k == 4 ? c > 0 : c >= 0;
        v = numero(r);
    }
}

static valor_t e_logico(void)
{
    valor_t v = comparacao();
    while (vem("&")) {
        valor_t w = comparacao();
        if (nulo(&v) || nulo(&w)) v = numero(0);
    }
    return v;
}

static valor_t ou(void)
{
    valor_t v = e_logico();
    while (vem("|")) {
        valor_t w = e_logico();
        if (nulo(&v)) v = nulo(&w) ? numero(0) : w;
    }
    return v;
}

int principal(int argc, char **argv)
{
    if (argc < 2) {
        imprimir_em(ERROS, "uso: expr EXPRESSÃO   (ex.: expr 3 + 4; cada parte é um argumento)\n");
        return 2;
    }
    arg = argv + 1;
    qtd = argc - 1;
    valor_t v = ou();
    if (!invalida && pos < qtd) falha("sobrou algo no fim da expressão");
    if (invalida) return 2;
    imprimir("%s\n", v.t);
    return nulo(&v) ? 1 : 0;
}
