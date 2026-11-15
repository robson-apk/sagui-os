/* vidro.h - vidro (desfoque + cor), sombra e cantos arredondados (H74). */
#ifndef SAGUI_VIDRO_H
#define SAGUI_VIDRO_H
#include "desenho.h"

#define VIDRO_RAIO     6      /* desfoque: média num quadrado de 13 x 13 */
#define VIDRO_ALT_MAX  72     /* vidro mais alto que isso não é desfocado */
#define VIDRO_SOMBRA   12     /* largura da sombra das janelas */
#define VIDRO_CANTO    8      /* raio dos cantos arredondados */

#define CANTOS_EM_CIMA  1
#define CANTOS_EMBAIXO  2

extern bool vidro_efeitos;    /* false = efeitos leves (sem desfoque) */

bool vidro_iniciar(int largura_tela);
/* Desfoca o que já está em s no retângulo e mistura com a tinta (k/256). */
void vidro_pintar(superficie_t *s, int x, int y, int l, int a, uint32_t tinta, int k, int cantos);
void vidro_sombra(superficie_t *s, int x, int y, int l, int a);
int  vidro_canto(int linha);  /* pixels inteiros de fora na ponta da linha (0 = a da ponta) */
/* Quanto do pixel (i, j) cai dentro do retângulo de cantos arredondados (0 a 256). */
int  vidro_cobertura(int i, int j, int x, int y, int l, int a, int cantos);
/* Cantos de baixo de uma janela: guarda o fundo antes, devolve depois (H88). */
void vidro_guardar_cantos(superficie_t *s, int x, int y, int l, int a);
void vidro_aplicar_cantos(superficie_t *s, int x, int y, int l, int a);

#endif
