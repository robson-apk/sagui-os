/* tr (trocar) - troca, apaga ou junta letras da entrada (H91; a ideia veio
 * do fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi
 * reescrito sobre a libsagui atual).
 *
 *   tr [-c] [-s] CONJUNTO1 CONJUNTO2    troca cada letra do 1 pela do 2
 *   tr [-c] -d [-s] CONJUNTO1 [CONJ2]    apaga as letras do 1
 *   tr -s CONJUNTO1                      junta repetições seguidas
 *
 *   -c  usa o complemento do conjunto 1 (todas as letras que NÃO estão nele);
 *   -d  apaga as letras do conjunto 1;
 *   -s  depois de trocar ou apagar, junta numa só as repetições seguidas das
 *       letras do último conjunto dado ("tr -s ' '" tira espaços dobrados).
 *
 * Um conjunto é uma lista de letras, com intervalos ("a-z"), escapes (\n,
 * \t, \\, \NNN em octal) e classes: [:alpha:] [:digit:] [:alnum:] [:lower:]
 * [:upper:] [:space:] [:blank:] [:punct:]. Se o conjunto 2 for mais curto,
 * a última letra dele se repete. Segue a descrição do tr na norma POSIX
 * (IEEE Std 1003.1).
 *
 * O texto do SaguiOS é Latin-1, então as letras acentuadas também são
 * letras: [:lower:] e [:upper:] têm as mesmas 56 letras na mesma ordem (a-z
 * e à-þ; A-Z e À-Þ, sem ÷ e ×), e "tr [:lower:] [:upper:]" troca "maçã"
 * por "MAÇÃ". ß e ÿ não têm maiúscula no Latin-1 e ficam de fora. */
#include "sagui.h"

#define MAX_CONJUNTO 1024

/* Acrescenta as letras de uma classe; devolve false se o nome não existe. */
static bool classe(const char *nome, unsigned char *c, int *n)
{
    for (int x = 1; x < 256; x++) {
        bool minus = (x >= 'a' && x <= 'z') || (x >= 0xE0 && x <= 0xFE && x != 0xF7);
        bool maius = (x >= 'A' && x <= 'Z') || (x >= 0xC0 && x <= 0xDE && x != 0xD7);
        bool digito = x >= '0' && x <= '9';
        bool letra = minus || maius || x == 0xDF || x == 0xFF;
        bool sim;
        if (compara_texto(nome, "alpha") == 0) sim = letra;
        else if (compara_texto(nome, "digit") == 0) sim = digito;
        else if (compara_texto(nome, "alnum") == 0) sim = letra || digito;
        else if (compara_texto(nome, "lower") == 0) sim = minus;
        else if (compara_texto(nome, "upper") == 0) sim = maius;
        else if (compara_texto(nome, "space") == 0) sim = x == ' ' || (x >= 9 && x <= 13);
        else if (compara_texto(nome, "blank") == 0) sim = x == ' ' || x == '\t';
        else if (compara_texto(nome, "punct") == 0) sim = x > ' ' && x < 127 && !digito && !letra;
        else return false;
        if (sim && *n < MAX_CONJUNTO) c[(*n)++] = (unsigned char)x;
    }
    return true;
}

/* Lê uma letra do conjunto, tratando os escapes. */
static unsigned char letra(const char **p)
{
    const char *s = *p;
    unsigned char c = (unsigned char)*s++;
    if (c == '\\' && *s) {
        c = (unsigned char)*s++;
        if (c == 'n') c = '\n';
        else if (c == 't') c = '\t';
        else if (c == 'r') c = '\r';
        else if (c >= '0' && c <= '7') {      /* \NNN: até três dígitos octais */
            unsigned v = c - '0';
            for (int k = 0; k < 2 && *s >= '0' && *s <= '7'; k++) v = v * 8 + (unsigned)(*s++ - '0');
            c = (unsigned char)v;
        }
    }
    *p = s;
    return c;
}

/* Expande um conjunto escrito ("a-z0-9") na lista das letras. */
static int expandir(const char *s, unsigned char *c)
{
    int n = 0;
    while (*s && n < MAX_CONJUNTO) {
        if (s[0] == '[' && s[1] == ':') {
            const char *f = s + 2;
            while (*f && !(f[0] == ':' && f[1] == ']')) f++;
            char nome[16];
            int t = (int)(f - (s + 2));
            if (*f && t < (int)sizeof nome) {
                memcpy(nome, s + 2, (size_t)t);
                nome[t] = 0;
                if (!classe(nome, c, &n)) { imprimir_em(ERROS, "tr: classe desconhecida: %s\n", nome); return -1; }
                s = f + 2;
                continue;
            }
        }
        unsigned char a = letra(&s);
        if (*s == '-' && s[1]) {               /* intervalo a-z */
            s++;
            unsigned char b = letra(&s);
            if (b < a) { imprimir_em(ERROS, "tr: intervalo ao contrário: %c-%c\n", a, b); return -1; }
            for (unsigned x = a; x <= b && n < MAX_CONJUNTO; x++) c[n++] = (unsigned char)x;
        } else {
            c[n++] = a;
        }
    }
    return n;
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: tr [-cds] CONJUNTO1 [CONJUNTO2]   (lê a entrada)\n");
    return 1;
}

int principal(int argc, char **argv)
{
    bool complemento = false, apagar = false, juntar = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        for (const char *o = argv[i] + 1; *o; o++) {
            if (*o == 'c' || *o == 'C') complemento = true;
            else if (*o == 'd') apagar = true;
            else if (*o == 's') juntar = true;
            else { imprimir_em(ERROS, "tr: opção desconhecida -%c\n", *o); return uso(); }
        }
    }
    int sobram = argc - i;
    bool trocar = !apagar && sobram == 2;
    if (sobram < 1 || sobram > 2 || (!apagar && !juntar && sobram != 2) || (apagar && !juntar && sobram != 1))
        return uso();

    static unsigned char c1[MAX_CONJUNTO], c2[MAX_CONJUNTO];
    int n1 = expandir(argv[i], c1), n2 = sobram == 2 ? expandir(argv[i + 1], c2) : 0;
    if (n1 < 0 || n2 < 0) return 1;

    /* Monta as tabelas de 256 posições: o que fazer com cada byte. */
    bool no1[256] = { 0 }, juntavel[256] = { 0 };
    unsigned char troca[256];
    for (int x = 0; x < 256; x++) troca[x] = (unsigned char)x;
    for (int k = 0; k < n1; k++) no1[c1[k]] = true;
    if (complemento) {
        /* O complemento, em ordem crescente, faz o papel do conjunto 1. */
        n1 = 0;
        for (int x = 0; x < 256; x++) if (!no1[x]) c1[n1++] = (unsigned char)x;
        for (int x = 0; x < 256; x++) no1[x] = !no1[x];
    }
    if (trocar) {
        if (n2 == 0) { imprimir_em(ERROS, "tr: o conjunto 2 está vazio\n"); return 1; }
        for (int k = 0; k < n1; k++) troca[c1[k]] = c2[k < n2 ? k : n2 - 1];
    }
    /* -s vale para o último conjunto: o 2, se houver; senão, o 1. */
    if (juntar) {
        if (sobram == 2) for (int k = 0; k < n2; k++) juntavel[c2[k]] = true;
        else for (int x = 0; x < 256; x++) juntavel[x] = no1[x];
    }

    char buf[1025];
    static char saida[1024];
    size_t pendente = 0;
    int n, anterior = -1;
    while ((n = ler(ENTRADA, buf + pendente, (unsigned)(sizeof buf - 1 - pendente))) > 0) {
        size_t total = pendente + (size_t)n, sobra;
        size_t m = utf8_para_latin1(buf, total, &sobra);
        unsigned s = 0;
        for (size_t k = 0; k < m - sobra; k++) {
            unsigned char x = (unsigned char)buf[k];
            if (apagar && no1[x]) continue;
            x = troca[x];
            if (juntar && juntavel[x] && anterior == x) continue;
            anterior = x;
            saida[s++] = (char)x;
        }
        if (escrever_tudo(SAIDA, saida, s) < 0) return 1;
        if (sobra) buf[0] = buf[m - 1];
        pendente = sobra;
    }
    if (pendente) escrever_tudo(SAIDA, buf, (unsigned)pendente);
    return 0;
}
