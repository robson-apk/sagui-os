/* uniq (unicas) - tira as linhas repetidas seguidas (use depois do sort).
 * uniq [-c] [-d] [arquivo]: -c conta as repetições, -d só as repetidas. */
#include "sagui.h"

#define LINHA_MAX 1024

static char atual[LINHA_MAX], anterior[LINHA_MAX];
static bool contar, so_repetidas;

static void soltar(int vezes)
{
    if (vezes == 0 || (so_repetidas && vezes < 2)) return;
    if (contar) imprimir("%7d %s\n", vezes, anterior);
    else imprimir("%s\n", anterior);
}

int principal(int argc, char **argv)
{
    const char *nome = NULL;
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "-c") == 0) contar = true;
        else if (compara_texto(argv[i], "-d") == 0) so_repetidas = true;
        else if (!nome) nome = argv[i];
        else { imprimir_em(ERROS, "uso: uniq [-c] [-d] [arquivo]\n"); return 1; }
    }
    int fd = ENTRADA;
    if (nome) {
        fd = abrir(nome, ABRIR_LER);
        if (fd < 0) { imprimir_em(ERROS, "uniq: %s: %s\n", nome, nome_erro(fd)); return 1; }
    }
    int vezes = 0, n;
    while ((n = ler_linha(fd, atual, sizeof atual)) >= 0) {
        if (n > 0 && atual[n - 1] == '\n') atual[n - 1] = 0;
        if (vezes > 0 && compara_texto(atual, anterior) == 0) { vezes++; continue; }
        soltar(vezes);
        copia_texto(anterior, atual, sizeof anterior);
        vezes = 1;
    }
    soltar(vezes);
    if (nome) fechar(fd);
    return 0;
}
