/* sagui.h de mentira, só para o teste H86 compilar userland/tocador/mp3.c no
 * PC de build: o decodificador só precisa de aloca e libera, que aqui são o
 * malloc e o free do PC. (Dentro do SaguiOS vale o userland/inc/sagui.h.) */
#ifndef SAGUI_H_DO_TESTE
#define SAGUI_H_DO_TESTE
#include <stdlib.h>
#define aloca(n) malloc(n)
#define libera(p) free(p)
#endif
