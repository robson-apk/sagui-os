/* tail (final) - mostra as últimas linhas de um arquivo ou da entrada.
 * tail [-n N] [arquivo]; o padrão são 10 linhas. Lê tudo e guarda só o que
 * pode sair: um anel com o começo das N últimas linhas. */
#include "sagui.h"

int principal(int argc, char **argv)
{
    int n = 10;
    const char *nome = NULL;
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "-n") == 0 && i + 1 < argc) n = texto_para_int(argv[++i]);
        else if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') n = texto_para_int(argv[i] + 1);
        else if (!nome) nome = argv[i];
        else { imprimir_em(ERROS, "uso: tail [-n N] [arquivo]\n"); return 1; }
    }
    if (n <= 0) return 0;
    int fd = ENTRADA;
    if (nome) {
        fd = abrir(nome, ABRIR_LER);
        if (fd < 0) { imprimir_em(ERROS, "tail: %s: %s\n", nome, nome_erro(fd)); return 1; }
    }
    unsigned cap = 4096, tam = 0;
    char *d = aloca(cap);
    if (!d) { imprimir_em(ERROS, "tail: sem memória\n"); return 1; }
    int r;
    for (;;) {
        if (tam == cap) {
            char *novo = realoca(d, cap * 2);
            if (!novo) { imprimir_em(ERROS, "tail: sem memória\n"); return 1; }
            d = novo; cap *= 2;
        }
        r = ler(fd, d + tam, cap - tam);
        if (r <= 0) break;
        tam += (unsigned)r;
    }
    if (nome) fechar(fd);
    /* Anda de trás para a frente contando quebras; a última linha pode não ter \n. */
    unsigned ini = tam;
    int vistas = 0;
    unsigned k = tam;
    if (k > 0 && d[k - 1] == '\n') k--;
    while (k > 0) {
        if (d[k - 1] == '\n' && ++vistas == n) break;
        k--;
    }
    ini = k;
    escrever_tudo(SAIDA, d + ini, tam - ini);
    return 0;
}
