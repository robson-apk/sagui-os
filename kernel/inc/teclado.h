/* teclado.h - teclado PS/2 com layout ABNT2 ou espanhol (teclado.layout, H90).
 *
 * Cada tecla vira um código: de 1 a 255 é um caractere Latin-1 (com Ctrl+letra
 * virando o código de controle 1-26, como Ctrl+C = 3); de 0x100 em diante são
 * teclas sem caractere (setas, Home, F1...). */
#ifndef SAGUI_TECLADO_H
#define SAGUI_TECLADO_H
#include "tipos.h"

enum {
    TECLA_CIMA = 0x100, TECLA_BAIXO, TECLA_ESQUERDA, TECLA_DIREITA,
    TECLA_HOME, TECLA_END, TECLA_PGUP, TECLA_PGDN, TECLA_INSERT, TECLA_DELETE,
    TECLA_F1, TECLA_F2, TECLA_F3, TECLA_F4, TECLA_F5, TECLA_F6,
    TECLA_F7, TECLA_F8, TECLA_F9, TECLA_F10, TECLA_F11, TECLA_F12,
    TECLA_ESC,
    TECLA_SUPER,      /* tecla Windows solta sem outra tecla */
    TECLA_IMPRIMIR,   /* Print Screen (H87) */
};

#define MOD_SHIFT 1
#define MOD_CTRL  2
#define MOD_ALT   4
#define MOD_ALTGR 8
#define MOD_CAPS  16
#define MOD_SUPER 32   /* tecla Windows segurada (H95) */

void     teclado_iniciar(void);
int      teclado_ler(void);      /* espera uma tecla */
int      teclado_tentar(void);   /* -1 se não há tecla no buffer */
uint32_t teclado_modificadores(void);
const void *teclado_canal(void);
void     teclado_seguradas(uint8_t mapa[32]);   /* um bit por código de varredura (128+ = estendida) */

#endif
