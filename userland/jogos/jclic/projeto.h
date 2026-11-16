/* projeto.h - um projeto JClic lido do XML (H92).
 *
 * O JClic (de Francesc Busquets, do Departament d'Educació da Generalitat
 * de Catalunya, XTEC) guarda um projeto num arquivo .jclic em XML,
 * normalmente dentro de um ZIP (.jclic.zip) com as imagens. O que importa
 * aqui, como os arquivos do formato aberto se apresentam:
 *
 *   <JClicProject>
 *     <settings><title>...</title></settings>
 *     <activities>
 *       <activity class="@memory.MemoryGame" name="...">
 *         <messages><cell type="initial|final"><p>texto</p></cell></messages>
 *         <cells id="primary|secondary|acrossClues|downClues" rows cols cellWidth cellHeight image>
 *           <style><color foreground background/></style>
 *           <cell id image><p>texto</p></cell> ...
 *         </cells>
 *         <scramble primary secondary/>  <layout position="AB|BA|AUB|BUA"/>
 *         <textGrid rows columns><text><row>LETRAS</row>...</text></textGrid>
 *         <clues><clue>palavra</clue>...</clues>
 *       </activity>
 *     </activities>
 *     <sequence><item activity="..."/>...</sequence>
 *   </JClicProject>
 *
 * Só se lê a estrutura; as regras de cada tipo de atividade estão em
 * motor.c e o desenho em jclic.c. Todo o código é nosso. */
#ifndef JCLIC_PROJETO_H
#define JCLIC_PROJETO_H
#include "xml.h"

enum { AT_INFO, AT_PUZZLE_DUPLO, AT_PUZZLE_TROCA, AT_PUZZLE_BURACO, AT_ASSOCIACAO, AT_ASSOCIACAO_COMPLEXA,
       AT_MEMORIA, AT_EXPLORAR, AT_IDENTIFICAR, AT_CACA_PALAVRAS, AT_CRUZADINHA, AT_OUTRA };

#define GRADE_MAX 24
#define PALAVRAS_MAX 32

typedef struct {
    char       *texto;        /* parágrafos separados por '\n' (alocado) */
    const char *imagem;       /* nome do arquivo de imagem, ou NULL */
    int         id;           /* -1 = sem o atributo */
    uint32_t    cor_texto, cor_fundo;
} celula_t;

typedef struct {
    bool        existe;
    int         cols, linhas, cw, ch;
    const char *imagem;       /* imagem que é cortada em grade (quebra-cabeças) */
    bool        borda;
    uint32_t    cor_texto, cor_fundo;
    int         n;
    celula_t   *c;
} grupo_t;

typedef struct {
    const char *nome, *classe;
    int         tipo;
    grupo_t     a, b, pistas_h, pistas_v;
    char       *msg_inicial, *msg_final;
    bool        embaralhar_a, embaralhar_b;
    char        disposicao[8];          /* "AB", "BA", "AUB" ou "BUA" */
    uint32_t    cor_fundo;
    /* caça-palavras e cruzadinha */
    int         gl, gc;                 /* linhas e colunas da grade */
    char        grade[GRADE_MAX][GRADE_MAX + 1];
    int         n_palavras;
    char        palavra[PALAVRAS_MAX][GRADE_MAX + 1];
} atividade_t;

typedef struct {
    char       *xml;          /* o texto (a árvore aponta para ele) */
    xno_t      *raiz;
    char        titulo[96];
    int         n;            /* atividades na sequência */
    xno_t      *ativ[128];
} projeto_t;

/* Lê o XML (o buffer passa a ser do projeto, que o libera). 0 ou erro. */
int  projeto_ler(projeto_t *p, char *xml, int n, char *erro, int max_erro);
void projeto_liberar(projeto_t *p);
bool projeto_atividade(const projeto_t *p, int k, atividade_t *a);
void atividade_liberar(atividade_t *a);
const char *nome_do_tipo(int tipo);

#endif
