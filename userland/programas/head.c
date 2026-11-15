/* head (inicio) - mostra as primeiras linhas de um arquivo ou da entrada.
 * head [-n N] [arquivo]; o padrão são 10 linhas. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    int n = 10;
    const char *nome = NULL;
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "-n") == 0 && i + 1 < argc) n = texto_para_int(argv[++i]);
        else if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') n = texto_para_int(argv[i] + 1);
        else if (!nome) nome = argv[i];
        else { imprimir_em(ERROS, "uso: head [-n N] [arquivo]\n"); return 1; }
    }
    int fd = ENTRADA;
    if (nome) {
        fd = abrir(nome, ABRIR_LER);
        if (fd < 0) { imprimir_em(ERROS, "head: %s: %s\n", nome, nome_erro(fd)); return 1; }
    }
    char buf[512];
    int r, linhas = 0;
    while (linhas < n && (r = ler(fd, buf, sizeof buf)) > 0) {
        int fim = 0;
        while (fim < r && linhas < n)
            if (buf[fim++] == '\n') linhas++;
        escrever_tudo(SAIDA, buf, (unsigned)fim);
    }
    if (nome) fechar(fd);
    return 0;
}
