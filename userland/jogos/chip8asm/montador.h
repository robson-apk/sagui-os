/* montador.h - o montador de CHIP-8 do chip8asm (H92).
 *
 * Transforma o texto de um programa (os mnemônicos da referência técnica do
 * CHIP-8, com os do SUPER-CHIP) nos bytes da ROM, que começa no endereço
 * 0x200. O mesmo arquivo compila para o SaguiOS e para o computador de
 * desenvolvimento (TESTE_HOSPEDEIRO): o build monta as ROMs de exemplo de
 * /jogos/chip8 com ele, e os testes conferem o resultado. */
#ifndef CHIP8_MONTADOR_H
#define CHIP8_MONTADOR_H
#ifdef TESTE_HOSPEDEIRO
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#define formatar_texto snprintf
#else
#include "sagui.h"
#endif

/* Monta "fonte" (n bytes). Devolve o tamanho da ROM gravada em "saida" (no
 * máximo "max" bytes, a partir do endereço 0x200) ou -1, com a mensagem
 * "linha N: ..." em "erro". */
int c8_montar(const char *fonte, int n, uint8_t *saida, int max, char *erro, int max_erro);

#endif
