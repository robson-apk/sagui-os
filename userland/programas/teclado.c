/* teclado - mostra ou troca o layout do teclado (H90).
 *   teclado          diz qual layout está valendo
 *   teclado es       espanhol (ñ, ç, ¡, ¿; acentos com teclas mortas)
 *   teclado abnt2    português do Brasil (o padrão)
 *
 * O layout é a configuração "teclado.layout" (H37): este programa só a grava,
 * e o driver do teclado percebe a mudança na próxima tecla apertada. Assim a
 * escolha fica guardada no pendrive como as outras configurações.
 *
 * Ideia e layout espanhol vindos do fork SaguiOS-Extended de Alejandro
 * Anuncibay, reescritos sobre as configurações do SaguiOS atual. */
#include "sagui.h"

static const char *descricao(const char *l)
{
    return compara_texto(l, "es") == 0 ? "es (espanhol)" : "abnt2 (português do Brasil)";
}

int principal(int argc, char **argv)
{
    char atual[16];
    if (config_ler("teclado.layout", atual, sizeof atual) < 0) copia_texto(atual, "abnt2", sizeof atual);
    if (argc == 1) {
        imprimir("Layout do teclado: %s\n", descricao(atual));
        return 0;
    }
    if (argc != 2 || (compara_texto(argv[1], "es") != 0 && compara_texto(argv[1], "abnt2") != 0)) {
        imprimir_em(ERROS, "uso: teclado [es | abnt2]\n"
                           "  abnt2  português do Brasil (ç; ~ ^ ´ ` ¨ como teclas mortas)\n"
                           "  es     espanhol (ñ, ç, ¡, ¿; ´ ` ^ ¨ e AltGr+4 = ~ como teclas mortas)\n");
        return 1;
    }
    int r = config_gravar("teclado.layout", argv[1]);
    if (r < 0) { imprimir_em(ERROS, "teclado: %s\n", nome_erro(r)); return 1; }
    imprimir("Layout do teclado: %s%s\n", descricao(argv[1]), r == 1 ? "" : "  (sem pendrive: vale até desligar)");
    return 0;
}
