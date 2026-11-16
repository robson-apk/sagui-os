/* motor.h - as regras das atividades JClic (H92), sem desenho.
 *
 * Toda atividade vira um ou dois painéis de caixas (A e B), ou uma grade de
 * letras (caça-palavras e cruzadinha). A janela só desenha o estado daqui e
 * repassa os cliques e as letras digitadas; o teste no computador de
 * desenvolvimento (tests/jogos/teste_jclic.c) resolve as atividades pelas
 * mesmas funções. */
#ifndef JCLIC_MOTOR_H
#define JCLIC_MOTOR_H
#include "projeto.h"

#define CAIXAS_MAX 400

enum { CX_NORMAL, CX_VAZIA, CX_ACERTO, CX_VIRADA };
enum { SOM_NADA, SOM_ACERTO, SOM_ERRO, SOM_FIM };

typedef struct {
    int             x, y, w, h;
    const grupo_t  *g;        /* de onde vem o conteúdo */
    int             cel;      /* célula do grupo mostrada (-1 = nenhuma) */
    int             orig;     /* a identidade da peça: o par, o lugar certo... */
    int             est;
} bloco_t;

typedef struct {
    bool    existe;
    int     n, cols, linhas, cw, ch;
    int     x, y, w, h;
    bloco_t c[CAIXAS_MAX];
} painel_t;

typedef struct {
    const atividade_t *a;
    painel_t  p[2];
    int       sel_p, sel_i;            /* caixa escolhida, esperando a segunda */
    int       acertos, tentativas, alvo;
    bool      resolvida;
    int       som;                     /* o último SOM_*, para a janela tocar */
    int       mem_a, mem_b;            /* memória: as duas cartas viradas para cima */
    unsigned  esconder_em;
    int       buraco;                  /* quebra-cabeça de buraco: onde está o buraco */
    /* grade de letras */
    int       gx, gy, gcw;             /* posição e lado das casas */
    char      letra[GRADE_MAX][GRADE_MAX + 1];
    uint8_t   marcada[GRADE_MAX][GRADE_MAX];   /* caça-palavras: casa de palavra achada */
    bool      achou[PALAVRAS_MAX];
    int       ini_x, ini_y;            /* caça-palavras: primeira casa escolhida (-1 = nenhuma) */
    char      escrito[GRADE_MAX][GRADE_MAX + 1];   /* cruzadinha: o que foi digitado */
    int       numero[GRADE_MAX][GRADE_MAX];
    int       foco_x, foco_y, direcao; /* cruzadinha: casa com o cursor; 0 horizontal, 1 vertical */
    uint32_t  sorteio;
} motor_t;

extern motor_t M;

void motor_iniciar(const atividade_t *a, uint32_t semente);
void motor_dispor(int x, int y, int w, int h);      /* a área da janela para o tabuleiro */
bool motor_caixa_em(int x, int y, int *p, int *i);
void motor_clique(int p, int i);
bool motor_casa_em(int x, int y, int *gx, int *gy);
void motor_clique_casa(int gx, int gy);
void motor_letra(int c);                            /* cruzadinha: letra (ou 8 = apagar) */
bool motor_tique(unsigned agora);                   /* true = algo mudou (memória escondeu as cartas) */
const char *motor_pista(void);                      /* cruzadinha: a pista da palavra com o cursor */
bool motor_eh_grade(void);

#endif
