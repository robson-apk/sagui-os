/* navegador.h - o Cipó, navegador do SaguiOS (fase 6 da 2.0; H82 da 2.1).
 *
 * Arquivos:
 *   png.c, jpeg.c   imagens da 2.0; imagens.c junta GIF e WebP
 *   html.c          leitura do HTML em árvore (2.0)
 *   motor/, fontes/, js/, graficos/   o motor do Cipó (pull request #6), cipo.h
 *   rede.c          pedidos com cookies, por cima do HTTP da libsagui
 *   navegador.c     janela, barra de endereço, histórico, favoritos */
#ifndef SAGUI_NAVEGADOR_H
#define SAGUI_NAVEGADOR_H
#include "sagui.h"
#include "gui.h"
#include "http.h"
#include "compactar.h"   /* DEFLATE, zlib e gzip: na libsagui desde a H93 */
#include "cipo.h"
#include "motor/motor.h"

typedef struct {
    uint32_t  larg, alt;
    uint32_t *px;                 /* 0xAARRGGBB */
} imagem_t;

imagem_t *png_ler(const uint8_t *d, uint32_t n, char *erro, int max_erro);
imagem_t *jpeg_ler(const uint8_t *d, uint32_t n, char *erro, int max_erro);
imagem_t *imagem_ler(const uint8_t *d, uint32_t n, char *erro, int max_erro);   /* escolhe pelo começo do arquivo */

/* ---- HTML (html.c) ---- */
enum {
    TAG_OUTRA, TAG_HTML, TAG_HEAD, TAG_BODY, TAG_TITLE, TAG_META, TAG_LINK, TAG_STYLE, TAG_SCRIPT, TAG_NOSCRIPT,
    TAG_BASE, TAG_P, TAG_DIV, TAG_SPAN, TAG_A, TAG_B, TAG_STRONG, TAG_I, TAG_EM, TAG_U, TAG_BR, TAG_HR, TAG_IMG,
    TAG_H1, TAG_H2, TAG_H3, TAG_H4, TAG_H5, TAG_H6, TAG_UL, TAG_OL, TAG_LI, TAG_DL, TAG_DT, TAG_DD,
    TAG_BLOCKQUOTE, TAG_PRE, TAG_CODE, TAG_TABLE, TAG_CAPTION, TAG_THEAD, TAG_TBODY, TAG_TFOOT, TAG_TR, TAG_TD,
    TAG_TH, TAG_FORM, TAG_INPUT, TAG_BUTTON, TAG_SELECT, TAG_OPTION, TAG_TEXTAREA, TAG_LABEL, TAG_NAV,
    TAG_HEADER, TAG_FOOTER, TAG_SECTION, TAG_ARTICLE, TAG_ASIDE, TAG_MAIN, TAG_FIGURE, TAG_FIGCAPTION, TAG_SUP,
    TAG_SUB, TAG_SMALL, TAG_CENTER, TAG_FONT, TAG_TT, TAG_KBD, TAG_SAMP, TAG_CITE, TAG_ABBR, TAG_IFRAME, TAG_SVG,
    TAG_MATH, TAG_TEMPLATE, TAG_WBR, TAG_AREA, TAG_COL, TAG_EMBED, TAG_SOURCE, TAG_PARAM, TAG_TRACK, TAG_DETAILS,
    TAG_SUMMARY, TAG_ADDRESS, TAG_VIDEO, TAG_AUDIO, TAG_CANVAS, TAG_OBJECT, TAG_S, TAG_STRIKE, TAG_DEL, TAG_INS,
    TAG_MARK, TAG_Q, TAG_VAR, TAG_BIG, TAG_COLGROUP, TAG_FIELDSET, TAG_LEGEND, TAG_PICTURE, TAG_TIME,
    TAG_QTD
};
#define NO_ELEMENTO 1
#define NO_TEXTO    2

typedef struct no {
    uint8_t     tipo, tag;
    uint32_t    tam;              /* texto: quantos bytes */
    char       *texto;            /* texto (Latin-1), ou o nome de uma tag desconhecida */
    char       *atributos;        /* "nome\0valor\0...\0" */
    struct no  *pai, *filho, *ultimo, *prox;
} no_t;

typedef struct {
    no_t  *raiz;
    char   titulo[128];
    char   base[256];             /* <base href> */
    bool   utf8, charset_definido;
    int    qtd_nos;
    void  *arena;
} documento_t;

/* charset: 1 UTF-8, 0 Latin-1, -1 não sabe (procura o <meta>). */
documento_t *html_ler(const char *d, uint32_t n, int charset);
documento_t *html_texto_puro(const char *d, uint32_t n, int charset);
void         html_liberar(documento_t *doc);
const char  *html_atributo(const no_t *x, const char *nome);
const char  *html_nome_tag(int tag);
void        *arena_aloca(documento_t *doc, uint32_t n);

/* ---- pedidos (rede.c) ---- */
typedef struct {
    uint8_t *d;                   /* o corpo (aloca), com um zero depois */
    uint32_t n, cap, max;
    int      situacao;            /* 200, 404... */
    char     tipo[64];            /* Content-Type */
    char     final[512];          /* endereço depois dos redirecionamentos */
    char     erro[160];
} rede_resposta_t;
/* Busca um endereço (http, https ou arquivo do SaguiOS). cab recebe os
 * cabeçalhos da resposta (pode ser NULL). Devolve a situação HTTP ou -1. */
int rede_buscar(const char *url, rede_resposta_t *r, unsigned max, char *cab, int max_cab);

/* ---- endereços (enderecos.c) ---- */
#define MAX_URL_NAV 1024
bool endereco_tem_esquema(const char *u);
void endereco_resolver(const char *base, const char *rel, char *out, int max);
void endereco_codificar(const char *s, char *out, int max);     /* para formulários: espaço vira + */
void endereco_para_rede(const char *u, char *out, int max);     /* acentos e espaços viram %XX; tira o #fragmento */
#endif
