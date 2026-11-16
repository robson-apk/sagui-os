/* xml.c - o leitor de XML dos projetos JClic (H92). Ver xml.h.
 *
 * Escrito a partir da recomendação XML 1.0 do W3C. O texto é lido com um
 * ponteiro que anda para a frente: "<" abre uma marcação, o resto é texto.
 * Os nomes e valores ficam no próprio buffer (cada um termina num zero
 * escrito por cima do caractere seguinte), por isso o leitor não copia
 * quase nada. Uma pilha guarda os elementos abertos. */
#include "xml.h"

#define PROFUNDIDADE 64

static bool espaco(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static bool letra_nome(int c) { return c && !espaco(c) && c != '>' && c != '/' && c != '=' && c != '<' && c != '?'; }

/* Converte um código Unicode para Latin-1 (o que não cabe vira "?"). */
static int para_latin1(uint32_t u) { return u < 256 ? (int)u : u == 0x2019 || u == 0x2018 ? '\'' : u == 0x201C || u == 0x201D ? '"' : u == 0x2013 || u == 0x2014 ? '-' : '?'; }

/* Decodifica o texto de s até fim no próprio lugar: entidades e UTF-8.
 * Devolve o fim do texto decodificado (que é menor ou igual). */
static char *decodificar(char *s, char *fim)
{
    char *d = s;
    while (s < fim) {
        uint8_t c = (uint8_t)*s;
        if (c == '&') {
            char *pv = s;
            while (pv < fim && *pv != ';' && pv - s < 12) pv++;
            if (pv < fim && *pv == ';') {
                *pv = 0;
                const char *e = s + 1;
                int r = -1;
                if (!compara_texto(e, "amp")) r = '&';
                else if (!compara_texto(e, "lt")) r = '<';
                else if (!compara_texto(e, "gt")) r = '>';
                else if (!compara_texto(e, "quot")) r = '"';
                else if (!compara_texto(e, "apos")) r = '\'';
                else if (e[0] == '#') {
                    uint32_t u = 0;
                    if (e[1] == 'x' || e[1] == 'X')
                        for (const char *p = e + 2; *p; p++) {
                            int h = *p >= '0' && *p <= '9' ? *p - '0' : (*p | 32) >= 'a' && (*p | 32) <= 'f' ? (*p | 32) - 'a' + 10 : 0;
                            u = u * 16 + (uint32_t)h;
                        }
                    else for (const char *p = e + 1; *p >= '0' && *p <= '9'; p++) u = u * 10 + (uint32_t)(*p - '0');
                    r = para_latin1(u);
                }
                if (r >= 0) { *d++ = (char)r; s = pv + 1; continue; }
                *pv = ';';
            }
            *d++ = *s++;
        } else if (c >= 0xC0 && c < 0xF8) {
            /* UTF-8: 110xxxxx 10xxxxxx (2 bytes), 1110xxxx (3), 11110xxx (4). */
            int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            uint32_t u = c & (0x3F >> n);
            int k = 1;
            for (; k <= n && s + k < fim && ((uint8_t)s[k] & 0xC0) == 0x80; k++) u = u << 6 | ((uint8_t)s[k] & 0x3F);
            *d++ = (char)para_latin1(k == n + 1 ? u : '?');
            s += k;
        } else *d++ = *s++;
    }
    return d;
}

static xno_t *novo(xno_t *pai, xno_t **ultimo_filho)
{
    xno_t *n = aloca_zerado(sizeof(xno_t));
    if (!n) return NULL;
    n->texto = "";
    if (pai) {
        if (ultimo_filho[0]) ultimo_filho[0]->prox = n;
        else pai->filho = n;
        ultimo_filho[0] = n;
    }
    return n;
}

void xml_liberar(xno_t *no)
{
    while (no) {
        xno_t *prox = no->prox;
        xml_liberar(no->filho);
        libera(no->atr);
        libera(no);
        no = prox;
    }
}

static void falha(char *erro, int max, const char *msg, const char *base, const char *onde)
{
    int linha = 1;
    for (const char *p = base; p < onde; p++) if (*p == '\n') linha++;
    formatar_texto(erro, (size_t)max, "XML: %s (linha %d)", msg, linha);
}

xno_t *xml_ler(char *t, int n, char *erro, int max_erro)
{
    char *p = t, *fim = t + n;
    xno_t *pilha[PROFUNDIDADE], *ultimo[PROFUNDIDADE];
    int topo = -1;
    xno_t *raiz = NULL;
    erro[0] = 0;
    if (n >= 3 && (uint8_t)p[0] == 0xEF && (uint8_t)p[1] == 0xBB && (uint8_t)p[2] == 0xBF) p += 3;   /* marca de UTF-8 */
    bool marcacao = false;    /* o '<' em p foi trocado pelo zero do fim de um texto */
    while (p < fim) {
        if (!marcacao && *p != '<') {
            /* Texto: só interessa dentro de um elemento e se não for só espaço. */
            char *ini = p;
            while (p < fim && *p != '<') p++;
            if (topo < 0) continue;
            bool so_espaco = true;
            for (char *q = ini; q < p; q++) if (!espaco(*q)) { so_espaco = false; break; }
            if (so_espaco) continue;
            char *f = decodificar(ini, p);
            if (f == p) marcacao = p < fim;   /* o zero vai cair em cima do '<' */
            *f = 0;
            xno_t *e = pilha[topo];
            if (!e->texto[0]) e->texto = ini;
            continue;
        }
        marcacao = false;
        if (p + 4 <= fim && !memcmp(p + 1, "!--", 3)) {
            char *q = p + 4;
            while (q + 3 <= fim && memcmp(q, "-->", 3)) q++;
            p = q + 3;
            continue;
        }
        if (p + 9 <= fim && !memcmp(p + 1, "![CDATA[", 8)) {
            char *ini = p + 9, *q = ini;
            while (q + 3 <= fim && memcmp(q, "]]>", 3)) q++;
            if (topo >= 0 && !pilha[topo]->texto[0]) { char *f = decodificar(ini, q); *f = 0; pilha[topo]->texto = ini; }   /* (UTF-8 só) */
            p = q + 3;
            continue;
        }
        if (p[1] == '?' || p[1] == '!') {     /* <?xml ...?>, <!DOCTYPE ...> */
            while (p < fim && *p != '>') p++;
            p++;
            continue;
        }
        if (p[1] == '/') {                     /* </nome> */
            char *q = p + 2;
            while (q < fim && *q != '>') q++;
            if (topo < 0) { falha(erro, max_erro, "fechamento sem abertura", t, p); xml_liberar(raiz); return NULL; }
            topo--;
            p = q + 1;
            continue;
        }
        /* <nome atr="valor" ...> ou <nome .../> */
        char *nome = ++p;
        while (p < fim && letra_nome(*p)) p++;
        if (p >= fim) break;
        xno_t *e = novo(topo >= 0 ? pilha[topo] : NULL, topo >= 0 ? &ultimo[topo] : NULL);
        if (!e) { falha(erro, max_erro, "sem memória", t, p); xml_liberar(raiz); return NULL; }
        if (!raiz) raiz = e;
        else if (topo < 0) { xml_liberar(e); falha(erro, max_erro, "mais de um elemento raiz", t, p); xml_liberar(raiz); return NULL; }
        char *fim_nome = p;
        int cap = 0;
        bool vazio = false;
        for (;;) {
            while (p < fim && espaco(*p)) p++;
            if (p >= fim) break;
            if (*p == '/') { vazio = true; p++; continue; }
            if (*p == '>') { p++; break; }
            char *an = p;
            while (p < fim && letra_nome(*p)) p++;
            char *fim_an = p;
            while (p < fim && espaco(*p)) p++;
            if (p >= fim || *p != '=') { falha(erro, max_erro, "atributo sem valor", t, p); xml_liberar(raiz); return NULL; }
            p++;
            while (p < fim && espaco(*p)) p++;
            char aspa = *p;
            if (aspa != '"' && aspa != '\'') { falha(erro, max_erro, "valor sem aspas", t, p); xml_liberar(raiz); return NULL; }
            char *val = ++p;
            while (p < fim && *p != aspa) p++;
            char *fv = decodificar(val, p);
            *fv = 0;
            p++;
            *fim_an = 0;
            if (e->n_atr + 1 > cap) {           /* cap conta pares nome/valor */
                cap = cap ? cap * 2 : 4;
                char **novo_atr = aloca((size_t)cap * 2 * sizeof(char *));
                if (!novo_atr) { xml_liberar(raiz); return NULL; }
                if (e->atr) { memcpy(novo_atr, e->atr, (size_t)e->n_atr * 2 * sizeof(char *)); libera(e->atr); }
                e->atr = novo_atr;
            }
            e->atr[e->n_atr * 2] = an;
            e->atr[e->n_atr * 2 + 1] = val;
            e->n_atr++;
        }
        *fim_nome = 0;
        e->nome = nome;
        if (!vazio) {
            if (topo + 1 >= PROFUNDIDADE) { falha(erro, max_erro, "elementos aninhados demais", t, p); xml_liberar(raiz); return NULL; }
            pilha[++topo] = e;
            ultimo[topo] = NULL;
        }
    }
    if (!raiz) { falha(erro, max_erro, "nenhum elemento", t, p); return NULL; }
    return raiz;
}

xno_t *xml_filho(const xno_t *no, const char *nome)
{
    if (!no) return NULL;
    for (xno_t *f = no->filho; f; f = f->prox) if (!compara_texto(f->nome, nome)) return f;
    return NULL;
}

xno_t *xml_proximo(const xno_t *no, const char *nome)
{
    if (!no) return NULL;
    for (xno_t *f = no->prox; f; f = f->prox) if (!compara_texto(f->nome, nome)) return f;
    return NULL;
}

const char *xml_atr(const xno_t *no, const char *nome)
{
    if (!no) return NULL;
    for (int k = 0; k < no->n_atr; k++) if (!compara_texto(no->atr[2 * k], nome)) return no->atr[2 * k + 1];
    return NULL;
}

int xml_int(const xno_t *no, const char *nome, int padrao)
{
    const char *v = xml_atr(no, nome);
    return v && ((*v >= '0' && *v <= '9') || *v == '-') ? texto_para_int(v) : padrao;
}

bool xml_bool(const xno_t *no, const char *nome, bool padrao)
{
    const char *v = xml_atr(no, nome);
    if (!v) return padrao;
    return !compara_texto(v, "true") || !compara_texto(v, "1") || !compara_texto(v, "yes");
}

uint32_t xml_cor(const xno_t *no, const char *nome, uint32_t padrao)
{
    const char *v = xml_atr(no, nome);
    if (!v) return padrao;
    if (v[0] == '#') v++;
    else if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v += 2;
    uint32_t c = 0;
    int n = 0;
    for (; *v; v++, n++) {
        int h = *v >= '0' && *v <= '9' ? *v - '0' : (*v | 32) >= 'a' && (*v | 32) <= 'f' ? (*v | 32) - 'a' + 10 : -1;
        if (h < 0) return padrao;
        c = c << 4 | (uint32_t)h;
    }
    return n >= 6 ? c & 0xFFFFFF : padrao;   /* 0xAARRGGBB também aparece: fica o RGB */
}
