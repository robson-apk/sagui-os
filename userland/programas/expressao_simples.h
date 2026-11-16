/* expressao_simples.h - um motor pequeno de expressões regulares, usado
 * pelo sed e pelo expr (H91). Escrito aqui, a partir da descrição das
 * "expressões regulares básicas" da norma POSIX (IEEE Std 1003.1,
 * capítulo 9), num subconjunto: letras, . (qualquer letra), [abc] [a-z]
 * [^abc], * (repete), ^ e $ (começo e fim da linha) e \ para tirar o
 * significado especial. Com "estendida" (o -E do sed), ou com \+ e \?,
 * também + (uma ou mais vezes) e ? (talvez). Não há grupos \( \) nem \1.
 *
 * Como a expressão é casada: ela vira uma lista de "peças", cada uma com o
 * conjunto das letras que aceita (256 bits) e quantas vezes pode repetir.
 * Para casar a partir de um ponto, cada peça engole o máximo de letras que
 * puder e, se o resto não casar, devolve uma de cada vez e tenta de novo.
 *
 * Fica num cabeçalho (funções "static") porque só dois programas o usam;
 * cada um leva a sua cópia. */
#ifndef EXPRESSAO_SIMPLES_H
#define EXPRESSAO_SIMPLES_H
#include "sagui.h"

#define MAX_PECAS 64

enum { UMA, ZERO_OU_MAIS, UMA_OU_MAIS, TALVEZ };

typedef struct {
    uint8_t conj[32];   /* bit x ligado = a letra x serve */
    uint8_t quant;
} peca_t;

typedef struct {
    peca_t p[MAX_PECAS];
    int    n;
    bool   no_comeco, no_fim;
} expressao_t;

static bool estendida;   /* + e ? sem barra (o -E do sed) */

static void liga(peca_t *p, int c) { p->conj[(c & 0xFF) >> 3] |= (uint8_t)(1 << (c & 7)); }
static bool tem(const peca_t *p, int c) { return p->conj[(c & 0xFF) >> 3] >> (c & 7) & 1; }

/* Monta a expressão de "s" (que tem n letras). Devolve false se estiver
 * mal formada. Com sem_caixa, cada letra liga também a outra caixa. */
static bool montar_expressao(expressao_t *e, const char *s, int n, bool sem_caixa)
{
    const char *fim = s + n;
    memset(e, 0, sizeof *e);
    if (s < fim && *s == '^') { e->no_comeco = true; s++; }
    while (s < fim) {
        if (*s == '$' && s + 1 == fim) { e->no_fim = true; break; }
        /* Quantificadores: valem para a peça anterior. Um * no começo é
         * uma letra comum, como manda a norma. */
        int q = -1;
        if (*s == '*') q = ZERO_OU_MAIS;
        else if (estendida && *s == '+') q = UMA_OU_MAIS;
        else if (estendida && *s == '?') q = TALVEZ;
        else if (!estendida && *s == '\\' && s + 1 < fim && s[1] == '+') { q = UMA_OU_MAIS; s++; }
        else if (!estendida && *s == '\\' && s + 1 < fim && s[1] == '?') { q = TALVEZ; s++; }
        if (q >= 0 && e->n > 0 && e->p[e->n - 1].quant == UMA) { e->p[e->n - 1].quant = (uint8_t)q; s++; continue; }
        if (e->n == MAX_PECAS) return false;
        peca_t *p = &e->p[e->n++];
        if (*s == '.') {
            for (int c = 1; c < 256; c++) if (c != '\n') liga(p, c);
            s++;
        } else if (*s == '[') {
            s++;
            bool inverter = s < fim && *s == '^';
            if (inverter) s++;
            /* Um ] logo no começo é uma letra da lista. */
            for (bool primeiro = true; s < fim && (*s != ']' || primeiro); s++, primeiro = false) {
                int a = (unsigned char)*s, b = a;
                if (s + 2 < fim && s[1] == '-' && s[2] != ']') { b = (unsigned char)s[2]; s += 2; }
                for (int c = a; c <= b; c++) liga(p, c);
            }
            if (s >= fim) return false;
            s++;
            if (inverter) for (int k = 0; k < 32; k++) p->conj[k] = (uint8_t)~p->conj[k];
        } else {
            if (*s == '\\' && s + 1 < fim) {
                s++;
                if (*s == 'n') { liga(p, '\n'); s++; continue; }
                if (*s == 't') { liga(p, '\t'); s++; continue; }
            }
            liga(p, (unsigned char)*s++);
        }
        if (sem_caixa)
            for (int c = 0; c < 256; c++)
                if (tem(p, c)) { liga(p, minuscula_latin1(c)); if (minuscula_latin1(c ^ 0x20) == c) liga(p, c ^ 0x20); }
    }
    return true;
}

/* Tenta casar as peças k.. a partir de s; devolve onde terminou, ou NULL. */
static const char *casar_aqui(const expressao_t *e, int k, const char *s, const char *fim)
{
    if (k == e->n) return (!e->no_fim || s == fim) ? s : NULL;
    const peca_t *p = &e->p[k];
    int min = p->quant == UMA || p->quant == UMA_OU_MAIS ? 1 : 0;
    int max = p->quant == UMA || p->quant == TALVEZ ? 1 : 0x7FFFFFFF;
    int n = 0;
    while (n < max && s + n < fim && tem(p, (unsigned char)s[n])) n++;
    for (; n >= min; n--) {
        const char *r = casar_aqui(e, k + 1, s + n, fim);
        if (r) return r;
    }
    return NULL;
}

/* Procura a primeira ocorrência em [s, fim). "comeco" é o início da linha
 * (o ^ só casa ali). "unused": o expr não usa esta e o compilador não
 * deve reclamar. */
static bool __attribute__((unused))
procurar(const expressao_t *e, const char *comeco, const char *s, const char *fim,
         const char **ini, const char **fim_achado)
{
    for (; s <= fim; s++) {
        if (e->no_comeco && s != comeco) return false;
        const char *r = casar_aqui(e, 0, s, fim);
        if (r) { *ini = s; *fim_achado = r; return true; }
    }
    return false;
}

#endif
