/* rev (inverter) - escreve cada linha de trás para a frente (H91; a ideia
 * veio do fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi
 * reescrito sobre a libsagui atual).
 *
 *   rev [arquivo...]
 *
 * "sagui" vira "iugas". Como o texto do SaguiOS é Latin-1 (uma letra por
 * byte), inverter os bytes inverte as letras, acentuadas inclusive. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    int erros = 0;
    for (int k = 1; k < (argc > 1 ? argc : 2); k++) {
        int fd = abrir_para_ler("rev", argc > 1 ? argv[k] : "-");
        if (fd < 0) { erros++; continue; }
        unsigned n;
        char *b = ler_ate_o_fim(fd, &n);
        if (fd != ENTRADA) fechar(fd);
        if (!b) { imprimir_em(ERROS, "rev: memória insuficiente\n"); return 1; }
        n = (unsigned)utf8_para_latin1(b, n, NULL);
        /* Inverte cada linha no próprio buffer, trocando as pontas e
         * andando para o meio; o '\n' (e um '\r' antes dele) fica no lugar. */
        for (unsigned p = 0; p < n;) {
            unsigned f = p;
            while (f < n && b[f] != '\n') f++;
            unsigned t = f;
            if (t > p && b[t - 1] == '\r') t--;
            for (unsigned a = p, z = t; a + 1 < z; a++, z--) {
                char c = b[a];
                b[a] = b[z - 1];
                b[z - 1] = c;
            }
            p = f + 1;
        }
        if (escrever_tudo(SAIDA, b, n) < 0) erros++;
        libera(b);
    }
    return erros ? 1 : 0;
}
