/* nl (numerar) - numera as linhas de arquivos ou da entrada (H91; a ideia
 * veio do fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi
 * reescrito sobre a libsagui atual).
 *
 *   nl [-b a|t] [-n ln|rn|rz] [-w N] [-s SEP] [-v N] [-i N] [arquivo...]
 *
 *   -b t  numera só as linhas com texto (o padrão); -b a numera todas;
 *   -n    rn: número alinhado à direita (o padrão); ln: à esquerda;
 *         rz: à direita, com zeros na frente;
 *   -w N  largura do número (6);    -s SEP  o que vem depois dele (Tab);
 *   -v N  primeiro número (1);      -i N    de quanto em quanto (1).
 *
 * Segue a descrição do nl na norma POSIX (IEEE Std 1003.1), sem as
 * divisões de página (cabeçalho, corpo e rodapé), que quase ninguém usa.
 * Com vários arquivos, a numeração continua de um para o outro. */
#include "sagui.h"

static bool todas;
static char formato = 'r';     /* 'r' direita, 'l' esquerda, 'z' zeros */
static int largura = 6, numero = 1, incremento = 1;
static const char *sep = "\t";

static void numerar(const char *l, unsigned n)
{
    if (n == 0 && !todas) {
        /* Linha vazia sem número: o espaço do número fica em branco. */
        int brancos = largura + (int)tam_texto(sep);
        while (brancos-- > 0) escrever_tudo(SAIDA, " ", 1);
    } else {
        if (formato == 'l') imprimir("%-*d%s", largura, numero, sep);
        else if (formato == 'z') imprimir("%0*d%s", largura, numero, sep);
        else imprimir("%*d%s", largura, numero, sep);
        numero += incremento;
    }
    escrever_tudo(SAIDA, l, n);
    escrever_tudo(SAIDA, "\n", 1);
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: nl [-b a|t] [-n ln|rn|rz] [-w N] [-s SEP] [-v N] [-i N] [arquivo...]\n");
    return 1;
}

int principal(int argc, char **argv)
{
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        char o = argv[i][1];
        const char *v = argv[i][2] ? argv[i] + 2 : (i + 1 < argc ? argv[++i] : NULL);
        if (!v) return uso();
        bool ok = true;
        if (o == 'b') { ok = (v[0] == 'a' || v[0] == 't') && !v[1]; todas = v[0] == 'a'; }
        else if (o == 'n') {
            if (compara_texto(v, "ln") == 0) formato = 'l';
            else if (compara_texto(v, "rn") == 0) formato = 'r';
            else if (compara_texto(v, "rz") == 0) formato = 'z';
            else ok = false;
        }
        else if (o == 'w') ok = converter_numero(v, &largura) && largura > 0 && largura < 30;
        else if (o == 's') sep = v;
        else if (o == 'v') ok = converter_numero(v, &numero);
        else if (o == 'i') ok = converter_numero(v, &incremento);
        else { imprimir_em(ERROS, "nl: opção desconhecida -%c\n", o); return uso(); }
        if (!ok) { imprimir_em(ERROS, "nl: valor inválido para -%c: %s\n", o, v); return 1; }
    }
    int arquivos = argc - i, erros = 0;
    for (int k = 0; k < (arquivos ? arquivos : 1); k++) {
        int fd = abrir_para_ler("nl", arquivos ? argv[i + k] : "-");
        if (fd < 0) { erros++; continue; }
        unsigned n;
        char *b = ler_ate_o_fim(fd, &n);
        if (fd != ENTRADA) fechar(fd);
        if (!b) { imprimir_em(ERROS, "nl: memória insuficiente\n"); return 1; }
        n = (unsigned)utf8_para_latin1(b, n, NULL);
        for (unsigned p = 0; p < n;) {
            unsigned f = p;
            while (f < n && b[f] != '\n') f++;
            unsigned t = f;
            if (t > p && b[t - 1] == '\r') t--;
            numerar(b + p, t - p);
            p = f + 1;
        }
        libera(b);
    }
    return erros ? 1 : 0;
}
