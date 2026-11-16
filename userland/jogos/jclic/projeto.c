/* projeto.c - lê um projeto JClic do XML (H92). Ver projeto.h.
 *
 * Trazido do visor de JClic do fork SaguiOS-Extended de Alejandro
 * Anuncibay e reescrito sobre o main atual (código nosso: nada do JClic
 * nem do JClic.js). */
#include "projeto.h"

static bool contem(const char *s, const char *p)
{
    int n = (int)tam_texto(p);
    for (; *s; s++) if (!memcmp(s, p, (size_t)n)) return true;
    return false;
}

static int tipo_da_classe(const char *c)
{
    if (contem(c, "InformationScreen")) return AT_INFO;
    if (contem(c, "DoublePuzzle")) return AT_PUZZLE_DUPLO;
    if (contem(c, "ExchangePuzzle")) return AT_PUZZLE_TROCA;
    if (contem(c, "HolePuzzle")) return AT_PUZZLE_BURACO;
    if (contem(c, "ComplexAssociation")) return AT_ASSOCIACAO_COMPLEXA;
    if (contem(c, "SimpleAssociation")) return AT_ASSOCIACAO;
    if (contem(c, "MemoryGame")) return AT_MEMORIA;
    if (contem(c, "Explore")) return AT_EXPLORAR;
    if (contem(c, "Identify")) return AT_IDENTIFICAR;
    if (contem(c, "WordSearch")) return AT_CACA_PALAVRAS;
    if (contem(c, "CrossWord")) return AT_CRUZADINHA;
    return AT_OUTRA;
}

const char *nome_do_tipo(int t)
{
    static const char *const nomes[] = { "tela de informação", "quebra-cabeça duplo", "quebra-cabeça de troca",
        "quebra-cabeça de buraco", "associação", "associação complexa", "jogo da memória", "explorar", "identificar",
        "caça-palavras", "cruzadinha", "tipo que este visor não joga" };
    return t >= 0 && t <= AT_OUTRA ? nomes[t] : nomes[AT_OUTRA];
}

int projeto_ler(projeto_t *p, char *xml, int n, char *erro, int max_erro)
{
    memset(p, 0, sizeof *p);
    p->xml = xml;
    p->raiz = xml_ler(xml, n, erro, max_erro);
    if (!p->raiz) return -1;
    if (compara_texto(p->raiz->nome, "JClicProject")) { formatar_texto(erro, (size_t)max_erro, "não é um projeto JClic"); return -1; }
    xno_t *t = xml_filho(xml_filho(p->raiz, "settings"), "title");
    copia_texto(p->titulo, t && t->texto[0] ? t->texto : "Projeto JClic", sizeof p->titulo);
    xno_t *ativs = xml_filho(p->raiz, "activities");
    /* A sequência diz a ordem; sem ela, valem as atividades na ordem do arquivo. */
    for (xno_t *i = xml_filho(xml_filho(p->raiz, "sequence"), "item"); i && p->n < 128; i = xml_proximo(i, "item")) {
        const char *nome = xml_atr(i, "activity");
        if (!nome) nome = xml_atr(i, "name");
        if (!nome) continue;
        for (xno_t *a = xml_filho(ativs, "activity"); a; a = xml_proximo(a, "activity")) {
            const char *na = xml_atr(a, "name");
            if (na && !compara_texto(na, nome)) { p->ativ[p->n++] = a; break; }
        }
    }
    if (!p->n)
        for (xno_t *a = xml_filho(ativs, "activity"); a && p->n < 128; a = xml_proximo(a, "activity")) p->ativ[p->n++] = a;
    if (!p->n) { formatar_texto(erro, (size_t)max_erro, "o projeto não tem atividades"); return -1; }
    return 0;
}

void projeto_liberar(projeto_t *p)
{
    xml_liberar(p->raiz);
    libera(p->xml);
    memset(p, 0, sizeof *p);
}

/* Os parágrafos (<p>) de uma célula, um por linha. Sem <p>, o texto direto. */
static char *paragrafos(const xno_t *no)
{
    int t = 1;
    for (xno_t *p = xml_filho(no, "p"); p; p = xml_proximo(p, "p")) t += (int)tam_texto(p->texto) + 1;
    if (t == 1) t += (int)tam_texto(no ? no->texto : "");
    char *s = aloca((size_t)t);
    if (!s) return NULL;
    s[0] = 0;
    int k = 0;
    for (xno_t *p = xml_filho(no, "p"); p; p = xml_proximo(p, "p")) {
        if (k) s[k++] = '\n';
        int l = (int)tam_texto(p->texto);
        memcpy(s + k, p->texto, (size_t)l);
        k += l;
    }
    if (!k && no) { int l = (int)tam_texto(no->texto); memcpy(s, no->texto, (size_t)l); k = l; }
    s[k] = 0;
    return s;
}

static void estilo(const xno_t *est, uint32_t *texto, uint32_t *fundo)
{
    xno_t *c = xml_filho(est, "color");
    if (!c) return;
    *texto = xml_cor(c, "foreground", *texto);
    *fundo = xml_cor(c, "background", *fundo);
}

static void grupo_ler(const xno_t *no, grupo_t *g)
{
    memset(g, 0, sizeof *g);
    g->existe = true;
    g->cols = xml_int(no, "cols", xml_int(no, "columns", 0));
    g->linhas = xml_int(no, "rows", 0);
    xno_t *forma = xml_filho(no, "shaper");
    if (forma) {
        if (!g->cols) g->cols = xml_int(forma, "cols", 0);
        if (!g->linhas) g->linhas = xml_int(forma, "rows", 0);
    }
    g->cw = xml_int(no, "cellWidth", 0);
    g->ch = xml_int(no, "cellHeight", 0);
    g->borda = xml_bool(no, "border", true);
    g->imagem = xml_atr(no, "image");
    if (g->imagem && !g->imagem[0]) g->imagem = NULL;
    g->cor_texto = 0x000000;
    g->cor_fundo = 0xFFFFFF;
    estilo(xml_filho(no, "style"), &g->cor_texto, &g->cor_fundo);
    int n = 0;
    for (xno_t *c = xml_filho(no, "cell"); c; c = xml_proximo(c, "cell")) n++;
    if (!n && g->imagem && g->cols > 0 && g->linhas > 0) n = g->cols * g->linhas;
    /* A grade: o que faltar sai das células (uma linha só, se nada foi dito). */
    if (g->cols <= 0 && g->linhas <= 0) { g->cols = n > 0 ? n : 1; g->linhas = 1; }
    else if (g->cols <= 0) g->cols = (n + g->linhas - 1) / g->linhas;
    else if (g->linhas <= 0) g->linhas = (n + g->cols - 1) / g->cols;
    if (n < g->cols * g->linhas) n = g->cols * g->linhas;
    if (n > 400) n = 400;
    g->c = aloca_zerado((size_t)n * sizeof(celula_t));
    if (!g->c) { g->n = 0; return; }
    g->n = n;
    int k = 0;
    for (xno_t *c = xml_filho(no, "cell"); c && k < n; c = xml_proximo(c, "cell"), k++) {
        celula_t *e = &g->c[k];
        e->id = xml_int(c, "id", -1);
        e->texto = paragrafos(c);
        e->imagem = xml_atr(c, "image");
        if (e->imagem && !e->imagem[0]) e->imagem = NULL;
        e->cor_texto = g->cor_texto;
        e->cor_fundo = g->cor_fundo;
        estilo(xml_filho(c, "style"), &e->cor_texto, &e->cor_fundo);
    }
    for (; k < n; k++) { g->c[k].id = -1; g->c[k].cor_texto = g->cor_texto; g->c[k].cor_fundo = g->cor_fundo; }
    if (g->cw <= 0) g->cw = 100;
    if (g->ch <= 0) g->ch = 80;
}

static void grupo_liberar(grupo_t *g)
{
    for (int k = 0; k < g->n; k++) libera(g->c[k].texto);
    libera(g->c);
    memset(g, 0, sizeof *g);
}

static int maiuscula(int c)
{
    c &= 255;
    return (c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7) ? c - 32 : c;
}

bool projeto_atividade(const projeto_t *p, int k, atividade_t *a)
{
    memset(a, 0, sizeof *a);
    if (k < 0 || k >= p->n) return false;
    xno_t *no = p->ativ[k];
    a->nome = xml_atr(no, "name") ? xml_atr(no, "name") : "";
    a->classe = xml_atr(no, "class") ? xml_atr(no, "class") : "";
    a->tipo = tipo_da_classe(a->classe);
    copia_texto(a->disposicao, "AB", sizeof a->disposicao);
    a->cor_fundo = 0xF4F4F0;
    xno_t *cont = xml_filho(xml_filho(no, "settings"), "container");
    if (cont) a->cor_fundo = xml_cor(cont, "bgColor", a->cor_fundo);
    for (xno_t *c = xml_filho(xml_filho(no, "messages"), "cell"); c; c = xml_proximo(c, "cell")) {
        const char *t = xml_atr(c, "type");
        if (!t) continue;
        if (!compara_texto(t, "initial") && !a->msg_inicial) a->msg_inicial = paragrafos(c);
        else if (!compara_texto(t, "final") && !a->msg_final) a->msg_final = paragrafos(c);
    }
    for (xno_t *c = xml_filho(no, "cells"); c; c = xml_proximo(c, "cells")) {
        const char *id = xml_atr(c, "id") ? xml_atr(c, "id") : "primary";
        grupo_t *g = !compara_texto(id, "primary") ? &a->a : !compara_texto(id, "secondary") ? &a->b
                   : !compara_texto(id, "acrossClues") ? &a->pistas_h : !compara_texto(id, "downClues") ? &a->pistas_v : NULL;
        if (g && !g->existe) grupo_ler(c, g);
    }
    xno_t *emb = xml_filho(no, "scramble");
    if (emb) { a->embaralhar_a = xml_bool(emb, "primary", false); a->embaralhar_b = xml_bool(emb, "secondary", false); }
    xno_t *disp = xml_filho(no, "layout");
    if (disp && xml_atr(disp, "position")) copia_texto(a->disposicao, xml_atr(disp, "position"), sizeof a->disposicao);
    xno_t *tg = xml_filho(no, "textGrid");
    if (tg) {
        int l = 0;
        for (xno_t *r = xml_filho(xml_filho(tg, "text"), "row"); r && l < GRADE_MAX; r = xml_proximo(r, "row"), l++) {
            int n = 0;
            for (const char *s = r->texto; *s && n < GRADE_MAX; s++) a->grade[l][n++] = (char)maiuscula((uint8_t)*s);
            a->grade[l][n] = 0;
            if (n > a->gc) a->gc = n;
        }
        a->gl = l;
        for (int y = 0; y < a->gl; y++) {        /* linhas curtas: o resto é casa vazia */
            int n = (int)tam_texto(a->grade[y]);
            for (int x = n; x < a->gc; x++) a->grade[y][x] = '*';
            a->grade[y][a->gc] = 0;
        }
    }
    for (xno_t *c = xml_filho(xml_filho(no, "clues"), "clue"); c && a->n_palavras < PALAVRAS_MAX; c = xml_proximo(c, "clue")) {
        int n = 0;
        for (const char *s = c->texto; *s && n < GRADE_MAX; s++) if (*s != ' ') a->palavra[a->n_palavras][n++] = (char)maiuscula((uint8_t)*s);
        a->palavra[a->n_palavras][n] = 0;
        if (n) a->n_palavras++;
    }
    return true;
}

void atividade_liberar(atividade_t *a)
{
    grupo_liberar(&a->a);
    grupo_liberar(&a->b);
    grupo_liberar(&a->pistas_h);
    grupo_liberar(&a->pistas_v);
    libera(a->msg_inicial);
    libera(a->msg_final);
    memset(a, 0, sizeof *a);
}
