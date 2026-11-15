/* find (achar) - procura arquivos e pastas pelo nome, descendo nas pastas.
 * find [pasta] [-name padrão] [-type f|d]. O padrão aceita * e ?
 * (ponha entre aspas para o shell não mexer). */
#include "sagui.h"

#define CAMINHO_MAX 256

static const char *padrao;
static int tipo_pedido;   /* 0 qualquer, TIPO_ARQUIVO ou TIPO_PASTA */

/* Casa o nome com o padrão: * qualquer sequência, ? uma letra. */
static bool casa(const char *p, const char *s)
{
    const char *estrela = NULL, *volta = NULL;
    while (*s) {
        if (*p == '?' || *p == *s) { p++; s++; }
        else if (*p == '*') { estrela = p++; volta = s; }
        else if (estrela) { p = estrela + 1; s = ++volta; }
        else return false;
    }
    while (*p == '*') p++;
    return *p == 0;
}

static void juntar(char *dst, const char *pasta, const char *nome)
{
    copia_texto(dst, pasta, CAMINHO_MAX);
    size_t n = tam_texto(dst);
    if (n == 0 || dst[n - 1] != '/') junta_texto(dst, "/", CAMINHO_MAX);
    junta_texto(dst, nome, CAMINHO_MAX);
}

static bool mostrar(const char *caminho, const char *nome, unsigned tipo)
{
    if (padrao && !casa(padrao, nome)) return false;
    if (tipo_pedido == TIPO_PASTA && tipo != TIPO_PASTA) return false;
    if (tipo_pedido == TIPO_ARQUIVO && tipo == TIPO_PASTA) return false;
    imprimir("%s\n", caminho);
    return true;
}

static int descer(const char *pasta, int nivel)
{
    if (nivel > 32) return 0;
    entrada_pasta_t e;
    char caminho[CAMINHO_MAX];
    int i = 0, r, erros = 0;
    while ((r = listar(pasta, i++, &e)) == 1) {
        juntar(caminho, pasta, e.nome);
        mostrar(caminho, e.nome, e.tipo);
        if (e.tipo == TIPO_PASTA) erros += descer(caminho, nivel + 1);
    }
    if (r < 0) { imprimir_em(ERROS, "find: %s: %s\n", pasta, nome_erro(r)); return 1; }
    return erros;
}

int principal(int argc, char **argv)
{
    const char *inicio = NULL;
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "-name") == 0 && i + 1 < argc) padrao = argv[++i];
        else if (compara_texto(argv[i], "-type") == 0 && i + 1 < argc) {
            i++;
            if (argv[i][0] == 'f') tipo_pedido = TIPO_ARQUIVO;
            else if (argv[i][0] == 'd') tipo_pedido = TIPO_PASTA;
            else { imprimir_em(ERROS, "find: -type f ou -type d\n"); return 1; }
        } else if (!inicio && argv[i][0] != '-') inicio = argv[i];
        else { imprimir_em(ERROS, "uso: find [pasta] [-name padrão] [-type f|d]\n"); return 1; }
    }
    if (!inicio) inicio = ".";
    info_arquivo_t inf;
    int r = info_arquivo(inicio, &inf);
    if (r < 0) { imprimir_em(ERROS, "find: %s: %s\n", inicio, nome_erro(r)); return 1; }
    const char *base = acha_ultimo_char(inicio, '/');
    base = base && base[1] ? base + 1 : inicio;
    mostrar(inicio, base, inf.tipo);
    if (inf.tipo != TIPO_PASTA) return 0;
    return descer(inicio, 0) ? 1 : 0;
}
