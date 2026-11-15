/* energia - a janela do botão de energia da barra de cima (H36).
 * Desligar e Reiniciar gravam antes o que estiver pendente nos pendrives
 * (o kernel faz isso); se houver um pendrive montado, a janela avisa. */
#include "sagui.h"
#include "gui.h"
#include "icones.h"

#define L 384
#define A 150

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    jan_t *j = jan_abrir("Energia", L, A);
    if (!j) return 1;
    g_retangulo(j, 0, 0, L, A, COR_FUNDO);
    /* H88: ícone, pergunta e botões com ícones; sem o vão vazio de antes. */
    g_icone(j, 20, 18, icone_energia, 20);
    g_texto(j, 50, 20, "O que você quer fazer?", COR_TEXTO);
    info_disco_t d;
    bool montado = false;
    for (int i = 0; listar_discos(i, &d) == 1; i++)
        if (d.montado) montado = true;
    if (montado) {
        g_texto(j, 50, 46, "O pendrive em /usb vai ser gravado", COR_TEXTO_FRACO);
        g_texto(j, 50, 64, "e liberado antes.", COR_TEXTO_FRACO);
    }
    botao_t desl = { 20, 98, 112, 32, "Desligar", false, icone_energia };
    botao_t rein = { 140, 98, 120, 32, "Reiniciar", false, icone_restaurar };
    botao_t canc = { 268, 98, 96, 32, "Cancelar", false };
    botao_desenhar(j, &desl);
    botao_desenhar(j, &rein);
    botao_desenhar(j, &canc);
    jan_mostrar(j);
    registrar("energia: janela aberta");
    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR || botao_tratar(j, &canc, &e) || (e.tipo == EV_TECLA && e.tecla == TECLA_ESC)) break;
        if (botao_tratar(j, &desl, &e)) { registrar("energia: desligar"); desligar(); }
        if (botao_tratar(j, &rein, &e)) { registrar("energia: reiniciar"); reiniciar(); }
    }
    jan_fechar(j);
    return 0;
}
