/* regras.h - as regras do Caça-Bananas (H92), sem nada de tela.
 *
 * O campo é um bananal dividido em casas. Algumas escondem uma banana-bomba;
 * as outras, quando abertas, mostram quantas bombas há nas 8 vizinhas. Ganha
 * quem abre todas as casas sem bomba. As regras ficam separadas da janela
 * para poderem ser testadas no computador de desenvolvimento (com o gcc de
 * lá, tests/jogos/teste_cacabananas.c): com TESTE_HOSPEDEIRO, o cabeçalho
 * usa a libc em vez da libsagui. */
#ifndef CACABANANAS_REGRAS_H
#define CACABANANAS_REGRAS_H
#ifdef TESTE_HOSPEDEIRO
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#else
#include "sagui.h"
#endif

#define CAMPO_MAX_COLS   30
#define CAMPO_MAX_LINHAS 20
#define CAMPO_MAX (CAMPO_MAX_COLS * CAMPO_MAX_LINHAS)

/* Cada casa é um byte com estas marcas. */
#define C_BOMBA    1
#define C_ABERTA   2
#define C_BANDEIRA 4
#define C_DUVIDA   8
#define C_ERRADA   16     /* bandeira numa casa sem bomba (mostrada ao perder) */
#define C_ESTOUROU 32     /* a bomba que foi aberta */

enum { CAMPO_ESPERA, CAMPO_JOGANDO, CAMPO_PERDEU, CAMPO_GANHOU };

typedef struct {
    int      cols, linhas, bombas;
    uint8_t  casa[CAMPO_MAX];
    int      estado;
    int      abertas, bandeiras;
    uint32_t sorteio;         /* estado do gerador de números (xorshift) */
} campo_t;

void campo_novo(campo_t *c, int cols, int linhas, int bombas, uint32_t semente);
int  campo_vizinhas(const campo_t *c, int x, int y, uint8_t marca);   /* quantas das 8 vizinhas têm a marca */
void campo_abrir(campo_t *c, int x, int y);       /* o primeiro clique sorteia as bombas */
void campo_marcar(campo_t *c, int x, int y, bool com_duvida);   /* nada -> bandeira -> "?" -> nada */
void campo_acorde(campo_t *c, int x, int y);      /* número com as bandeiras certas: abre as vizinhas */
#define CAMPO_CASA(c, x, y) ((c)->casa[(y) * (c)->cols + (x)])

#endif
