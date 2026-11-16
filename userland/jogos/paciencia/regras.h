/* regras.h - as regras da Paciência Banana (H92), sem nada de tela.
 *
 * É a paciência de dois baralhos e dez colunas (a "aranha", jogo de cartas
 * de domínio público): 104 cartas, 54 nas colunas (6 nas quatro primeiras, 5
 * nas outras, só a última de cada aberta) e 50 no monte, que dá 5 rodadas de
 * 10 cartas, uma em cada coluna. Pode-se pegar uma carta aberta ou uma
 * escadinha descendente do mesmo naipe e pô-la numa coluna vazia ou sobre
 * uma carta um ponto maior (de qualquer naipe). Uma sequência completa, do
 * rei ao ás do mesmo naipe, sai da mesa. Ganha quem tira as 8 sequências.
 * Os níveis usam 1, 2 ou 4 naipes (com 1 naipe, todas são de espadas).
 *
 * Com TESTE_HOSPEDEIRO, compila com a libc (tests/jogos/teste_paciencia.c). */
#ifndef PACIENCIA_REGRAS_H
#define PACIENCIA_REGRAS_H
#ifdef TESTE_HOSPEDEIRO
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#else
#include "sagui.h"
#endif

#define COLUNAS     10
#define MAX_PILHA   104
#define SEQUENCIAS  8

/* Uma carta num byte: bit 7 = aberta, bits 4-5 = naipe, bits 0-3 = valor (1 = ás ... 13 = rei). */
typedef uint8_t carta_t;
#define VALOR(c)  ((int)((c) & 15))
#define NAIPE(c)  ((int)(((c) >> 4) & 3))
#define ABERTA(c) (((c) & 0x80) != 0)
#define CARTA(naipe, valor, aberta) ((carta_t)(((aberta) ? 0x80 : 0) | ((naipe) << 4) | (valor)))

enum { ESPADAS, COPAS, PAUS, OUROS };

typedef struct {
    carta_t  col[COLUNAS][MAX_PILHA];
    uint8_t  n[COLUNAS];
    carta_t  monte[50];
    uint8_t  n_monte;
    uint8_t  feitas;                  /* sequências tiradas da mesa */
    uint8_t  naipe_feita[SEQUENCIAS];
    int      pontos, jogadas;
} mesa_t;

typedef struct { int de, i, para; } jogada_t;

void mesa_nova(mesa_t *m, int naipes, uint32_t semente);
int  mesa_inicio_escada(const mesa_t *m, int c);           /* primeira carta que pode ser pega na coluna */
bool mesa_pode_pegar(const mesa_t *m, int c, int i);
bool mesa_pode_soltar(const mesa_t *m, int c, int valor);
bool mesa_mover(mesa_t *m, int de, int i, int para);        /* false = jogada inválida (nada muda) */
enum { DAR_OK, DAR_SEM_CARTAS, DAR_COLUNA_VAZIA };
int  mesa_dar(mesa_t *m);                                   /* uma carta do monte em cada coluna */
int  mesa_melhor_destino(const mesa_t *m, int c, int i);    /* para o clique simples; -1 = nenhum */
bool mesa_dica(const mesa_t *m, int ordem, jogada_t *j);    /* a "ordem"-ésima melhor jogada (dá a volta) */
void mesa_quase_ganha(mesa_t *m);                           /* falta uma jogada para ganhar (teste da vitória) */
#define MESA_GANHOU(m) ((m)->feitas >= SEQUENCIAS)

#endif
