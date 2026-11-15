/* du (ocupa) - quanto cada pasta ocupa, somando os arquivos de dentro.
 * du [-s] [pasta...]: mostra cada subpasta e o total; -s só o total.
 * Tamanhos em KB, arredondados para cima. */
#include "sagui.h"

#define CAMINHO_MAX 256

static bool so_total;

static unsigned kb(unsigned bytes) { return (bytes + 1023) / 1024; }

static unsigned medir(const char *pasta, int nivel)
{
    entrada_pasta_t e;
    char caminho[CAMINHO_MAX];
    unsigned total = 0;
    int i = 0, r;
    while ((r = listar(pasta, i++, &e)) == 1) {
        copia_texto(caminho, pasta, sizeof caminho);
        size_t n = tam_texto(caminho);
        if (n == 0 || caminho[n - 1] != '/') junta_texto(caminho, "/", sizeof caminho);
        junta_texto(caminho, e.nome, sizeof caminho);
        if (e.tipo == TIPO_PASTA) {
            unsigned sub = nivel < 32 ? medir(caminho, nivel + 1) : 0;
            if (!so_total) imprimir("%7u KB  %s\n", kb(sub), caminho);
            total += sub;
        } else {
            total += e.tamanho;
        }
    }
    if (r < 0 && r != ERRO_NAO_E_PASTA) imprimir_em(ERROS, "du: %s: %s\n", pasta, nome_erro(r));
    return total;
}

static int um(const char *alvo)
{
    info_arquivo_t inf;
    int r = info_arquivo(alvo, &inf);
    if (r < 0) { imprimir_em(ERROS, "du: %s: %s\n", alvo, nome_erro(r)); return 1; }
    unsigned total = inf.tipo == TIPO_PASTA ? medir(alvo, 0) : inf.tamanho;
    imprimir("%7u KB  %s\n", kb(total), alvo);
    return 0;
}

int principal(int argc, char **argv)
{
    int alvos = 0, erros = 0;
    for (int i = 1; i < argc; i++)
        if (compara_texto(argv[i], "-s") == 0) so_total = true;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        alvos++;
        erros += um(argv[i]);
    }
    if (!alvos) erros += um(".");
    return erros ? 1 : 0;
}
