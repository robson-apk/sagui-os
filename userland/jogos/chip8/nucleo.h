/* nucleo.h - a máquina CHIP-8 (com SUPER-CHIP 1.1 e XO-CHIP), sem tela (H92).
 *
 * O núcleo não desenha, não toca e não lê o teclado: o programa entrega as
 * 16 teclas, manda executar as instruções de um quadro e chama c8_quadro 60
 * vezes por segundo; depois lê c->tela (um byte por pixel, um bit por
 * plano) quando c->tela_mudou. Assim o mesmo código serve à janela do
 * SaguiOS e ao teste no computador de desenvolvimento (TESTE_HOSPEDEIRO). */
#ifndef CHIP8_NUCLEO_H
#define CHIP8_NUCLEO_H
#ifdef TESTE_HOSPEDEIRO
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#define formatar_texto snprintf
#define compara_texto strcmp
#define tam_texto strlen
#else
#include "sagui.h"
#endif

#define C8_LARG_MAX 128
#define C8_ALT_MAX  64
#define C8_INICIO   0x200     /* onde o programa é carregado */
#define C8_FONTE    0x000     /* fonte pequena 4x5: 16 x 5 bytes */
#define C8_FONTE_G  0x050     /* fonte grande 8x10 (SUPER-CHIP): 16 x 10 bytes */

enum { C8_CHIP8, C8_SCHIP, C8_XOCHIP, C8_QTD_MODOS };
enum { C8_RODANDO, C8_TERMINOU, C8_ERRO };

/* O que mudou de uma máquina para outra. Cada modo tem os seus valores
 * (c8_ajustes_padrao). */
typedef struct {
    bool desloca_vy;      /* 8XY6/8XYE deslocam VY (COSMAC, XO-CHIP) ou o próprio VX (SUPER-CHIP) */
    bool avanca_i;        /* FX55/FX65 deixam I depois do último registro (COSMAC, XO-CHIP) */
    bool salto_vx;        /* BNNN soma VX em vez de V0 (SUPER-CHIP) */
    bool corta_borda;     /* o desenho é cortado na borda (senão dá a volta, XO-CHIP) */
    bool zera_vf;         /* 8XY1/8XY2/8XY3 zeram VF (COSMAC) */
    bool espera_quadro;   /* DXYN termina o quadro (o COSMAC esperava o retraço da tela) */
} c8_ajustes_t;

typedef struct {
    int          modo;
    c8_ajustes_t a;
    uint8_t      mem[65536];
    uint32_t     mascara;          /* 0xFFF (4 KB) ou 0xFFFF (XO-CHIP, 64 KB) */
    uint8_t      v[16];
    uint16_t     i, pc;
    uint16_t     pilha[16];
    int          sp;
    uint8_t      dt, st;           /* temporizadores de espera e de som */
    bool         alta;             /* 128x64 (00FF) */
    int          larg, alt;
    uint8_t      tela[C8_LARG_MAX * C8_ALT_MAX];
    uint8_t      planos;           /* planos em que se desenha (FN01); 1 fora do XO-CHIP */
    uint8_t      guardados[16];    /* FX75/FX85 */
    uint8_t      padrao[16];       /* som do XO-CHIP (F002) */
    bool         tem_padrao;
    uint8_t      tom;              /* FX3A */
    uint16_t     teclas;           /* bit k = tecla k apertada agora */
    int          espera_reg;       /* FX0A: registro que recebe a tecla; -1 = não espera */
    uint16_t     espera_vistas;
    bool         quadro_feito;     /* já desenhou neste quadro (com espera_quadro) */
    int          estado;
    char         erro[80];
    bool         tela_mudou;
    uint32_t     sorteio;
    uint32_t     instrucoes;
} c8_t;

void        c8_ajustes_padrao(int modo, c8_ajustes_t *a);
void        c8_iniciar(c8_t *c, int modo, uint32_t semente);
bool        c8_carregar(c8_t *c, const uint8_t *rom, int n);   /* false = não cabe */
int         c8_executar(c8_t *c, int n);   /* até n instruções; devolve quantas rodaram */
void        c8_quadro(c8_t *c);            /* 1/60 s: temporizadores */
uint32_t    c8_freq_som(const c8_t *c);    /* Hz para o alto-falante agora (0 = silêncio) */
uint32_t    c8_soma_tela(const c8_t *c);   /* resumo da tela (FNV-1a), para os testes */
int         c8_rom_max(int modo);
int         c8_instrucoes_por_quadro(int modo);
const char *c8_nome_modo(int modo);
int         c8_modo_por_extensao(const char *arquivo);   /* .ch8 .sc8 .xo8; C8_CHIP8 se outra */

#endif
