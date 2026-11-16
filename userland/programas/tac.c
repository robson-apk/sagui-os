/* tac (de-tras) - escreve as linhas na ordem inversa, da última para a
 * primeira (H91; a ideia veio do fork SaguiOS-Extended, do Alejandro
 * Anuncibay, e o programa foi reescrito sobre a libsagui atual).
 *
 *   tac [arquivo...]
 *
 * O nome é "cat" ao contrário. Com vários arquivos, cada um é invertido
 * por inteiro, na ordem em que foram dados. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    int erros = 0;
    for (int k = 1; k < (argc > 1 ? argc : 2); k++) {
        int fd = abrir_para_ler("tac", argc > 1 ? argv[k] : "-");
        if (fd < 0) { erros++; continue; }
        unsigned n;
        char *b = ler_ate_o_fim(fd, &n);
        if (fd != ENTRADA) fechar(fd);
        if (!b) { imprimir_em(ERROS, "tac: memória insuficiente\n"); return 1; }
        n = (unsigned)utf8_para_latin1(b, n, NULL);
        /* Anda de trás para a frente: cada linha começa logo depois de um
         * '\n' (ou no começo do texto). A última linha, se não tiver '\n',
         * ganha um, para não grudar na seguinte. */
        unsigned fim = n;
        while (fim > 0) {
            unsigned ini = fim - 1;              /* fim-1 é o '\n' da linha (ou a última letra) */
            while (ini > 0 && b[ini - 1] != '\n') ini--;
            escrever_tudo(SAIDA, b + ini, fim - ini);
            if (b[fim - 1] != '\n') escrever_tudo(SAIDA, "\n", 1);
            fim = ini;
        }
        libera(b);
    }
    return erros ? 1 : 0;
}
