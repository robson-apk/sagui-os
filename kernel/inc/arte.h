/* arte.h - desenhos em pixel art (letras = cores da paleta). */
#ifndef SAGUI_ARTE_H
#define SAGUI_ARTE_H
#include "tipos.h"

typedef struct {
    const char *const *linhas;
    int largura, altura;
} desenho_t;

extern const desenho_t arte_sagui, arte_icone_arquivos, arte_icone_terminal, arte_icone_editor,
                       arte_icone_calculadora, arte_icone_banana, arte_icone_sagui3d, arte_icone_sobre, arte_icone_pendrive;
extern const char *const arte_botao_energia[16];
extern const char *const arte_icone_rede[16];
extern const desenho_t arte_icone_configuracoes, arte_icone_instalado, arte_icone_navegador;
bool arte_cor(char letra, uint32_t *cor);

/* Ícones ARGB gerados por tools/mkarte.py (H81): 64x64 na área de trabalho
 * e 40x40 (sufixo _40) na doca; 20x20 (sufixo _20) no menu (H88). */
#define ICONE_LADO   64
#define ICONE_PEQUENO 40
#define ARTE_PX(nome) extern const uint32_t arte_px_##nome[], arte_px_##nome##_40[], arte_px_##nome##_20[];
ARTE_PX(arquivos) ARTE_PX(terminal) ARTE_PX(editor) ARTE_PX(calculadora) ARTE_PX(banana) ARTE_PX(sagui3d)
ARTE_PX(sobre) ARTE_PX(pendrive) ARTE_PX(configuracoes) ARTE_PX(cipo) ARTE_PX(instalado)
ARTE_PX(monitor) ARTE_PX(visualizador) ARTE_PX(musica) ARTE_PX(energia)   /* H88 */
#undef ARTE_PX

#endif
