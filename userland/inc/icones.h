/* icones.h - ícones de 20x20 para os programas (H88).
 *
 * São gerados no build por tools/mkarte.py --programas, com os mesmos
 * desenhos dos ícones do kernel (área de trabalho, doca, menu) mais os da
 * interface. Cada um é um vetor ARGB de 20 x 20; desenhe com g_icone (gui.h).
 * Só os ícones que o programa usa entram nele (--gc-sections). */
#ifndef SAGUI_ICONES_H
#define SAGUI_ICONES_H
#include <stdint.h>

#define ICONE_LADO_PROG 20
#define ICONE(n) extern const uint32_t icone_##n[ICONE_LADO_PROG * ICONE_LADO_PROG];
/* programas */
ICONE(arquivos) ICONE(terminal) ICONE(editor) ICONE(calculadora) ICONE(banana) ICONE(sagui3d)
ICONE(sobre) ICONE(pendrive) ICONE(configuracoes) ICONE(cipo) ICONE(instalado)
ICONE(monitor) ICONE(visualizador) ICONE(musica) ICONE(energia)
/* interface */
ICONE(relogio) ICONE(aparencia) ICONE(som) ICONE(rede) ICONE(programas) ICONE(info) ICONE(ia) ICONE(teclado)
ICONE(lixeira) ICONE(voltar) ICONE(nova_pasta) ICONE(apagar) ICONE(restaurar)
ICONE(tocar) ICONE(pausar) ICONE(anterior) ICONE(proxima) ICONE(teste)
#undef ICONE

#endif
