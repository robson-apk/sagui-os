/* xml.h - um leitor de XML pequeno, para os projetos JClic (H92).
 *
 * Lê o texto todo de uma vez e monta uma árvore: cada elemento com o nome,
 * os atributos, o texto direto e os filhos. Entende o que os arquivos do
 * JClic usam (Extensible Markup Language 1.0, do W3C): declaração <?xml?>,
 * comentários, CDATA, atributos com aspas simples ou duplas e as entidades
 * &amp; &lt; &gt; &quot; &apos; e &#N; / &#xN;. Não há DTD nem espaços de
 * nomes. O texto sai em Latin-1 (a fonte do SaguiOS): o UTF-8 de fora é
 * convertido, e o que não cabe vira "?".
 *
 * Com TESTE_HOSPEDEIRO compila com a libc (tests/jogos/teste_jclic.c). */
#ifndef JCLIC_XML_H
#define JCLIC_XML_H
#ifdef TESTE_HOSPEDEIRO
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#define aloca malloc
#define aloca_zerado(n) calloc(1, n)
#define libera free
#define formatar_texto snprintf
#define compara_texto strcmp
#define tam_texto strlen
#define copia_texto(d, s, n) (snprintf((d), (n), "%s", (s)), (d))
#define texto_para_int atoi
#define acha_char strchr
#else
#include "sagui.h"
#endif

typedef struct xno {
    char        *nome;
    char       **atr;          /* nome, valor, nome, valor... */
    int          n_atr;
    char        *texto;        /* o texto direto do elemento (sem o dos filhos), ou "" */
    struct xno  *filho, *prox;
} xno_t;

/* Monta a árvore a partir de "texto" (que é modificado e passa a ser
 * usado pela árvore: não o libere antes dela; precisa de um byte livre
 * depois dos n). NULL = erro, explicado em "erro". */
xno_t      *xml_ler(char *texto, int n, char *erro, int max_erro);
void        xml_liberar(xno_t *no);
xno_t      *xml_filho(const xno_t *no, const char *nome);   /* o primeiro filho com esse nome */
xno_t      *xml_proximo(const xno_t *no, const char *nome); /* o próximo irmão com esse nome */
const char *xml_atr(const xno_t *no, const char *nome);     /* NULL se não tem */
int         xml_int(const xno_t *no, const char *nome, int padrao);
bool        xml_bool(const xno_t *no, const char *nome, bool padrao);
uint32_t    xml_cor(const xno_t *no, const char *nome, uint32_t padrao);   /* "0xRRGGBB" ou "#RRGGBB" */

#endif
