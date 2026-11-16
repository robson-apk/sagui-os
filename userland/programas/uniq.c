/* uniq (unicos) - junta linhas repetidas seguidas (H91; a ideia veio do
 * fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi reescrito
 * sobre a libsagui atual).
 *
 *   uniq [-c] [-d | -u] [-i] [-f N] [entrada [saída]]
 *
 *   -c  escreve na frente de cada linha quantas vezes ela apareceu seguida;
 *   -d  só as linhas que se repetem;   -u  só as que não se repetem;
 *   -i  ignora maiúsculas (também nas letras acentuadas);
 *   -f N  ignora os N primeiros campos (palavras) ao comparar.
 *
 * Só linhas vizinhas são comparadas, por isso o uniq costuma vir depois do
 * sort: "sort nomes.txt | uniq -c". Segue a descrição do uniq na norma POSIX
 * (IEEE Std 1003.1), inclusive o segundo nome, que é o arquivo de saída. */
#include "sagui.h"

static bool contar, so_repetidas, so_unicas, ignorar_caixa;
static int pular_campos;

/* Começo da parte comparada: depois de N campos (espaços + palavra). */
static const char *chave(const char *s)
{
    for (int i = 0; i < pular_campos; i++) {
        while (*s == ' ' || *s == '\t') s++;
        while (*s && *s != ' ' && *s != '\t') s++;
    }
    return s;
}

static bool iguais(const char *a, const char *b)
{
    a = chave(a);
    b = chave(b);
    for (;; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (ignorar_caixa) { x = minuscula_latin1(x); y = minuscula_latin1(y); }
        if (x != y) return false;
        if (!x) return true;
    }
}

/* Escreve uma linha do grupo que acabou, se as opções deixarem. */
static void emitir(int fd, const char *linha, int vezes)
{
    if (so_repetidas && vezes < 2) return;
    if (so_unicas && vezes > 1) return;
    if (contar) imprimir_em(fd, "%7d ", vezes);
    escrever_texto(fd, linha);
    escrever_texto(fd, "\n");
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: uniq [-c] [-d | -u] [-i] [-f N] [entrada [saída]]\n");
    return 1;
}

int principal(int argc, char **argv)
{
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        for (const char *o = argv[i] + 1; *o; o++) {
            if (*o == 'f') {
                const char *v = o[1] ? o + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!v || !converter_numero(v, &pular_campos) || pular_campos < 0) {
                    imprimir_em(ERROS, "uniq: número de campos inválido\n");
                    return 1;
                }
                break;
            }
            if (*o == 'c') contar = true;
            else if (*o == 'd') so_repetidas = true;
            else if (*o == 'u') so_unicas = true;
            else if (*o == 'i') ignorar_caixa = true;
            else { imprimir_em(ERROS, "uniq: opção desconhecida -%c\n", *o); return uso(); }
        }
    }
    if (argc - i > 2) return uso();
    int fd = abrir_para_ler("uniq", i < argc ? argv[i] : "-");
    if (fd < 0) return 1;
    int saida = SAIDA;
    if (i + 1 < argc) {
        saida = abrir(argv[i + 1], ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
        if (saida < 0) { imprimir_em(ERROS, "uniq: %s: %s\n", argv[i + 1], nome_erro(saida)); return 1; }
    }
    unsigned n;
    char *b = ler_ate_o_fim(fd, &n);
    if (!b) { imprimir_em(ERROS, "uniq: memória insuficiente\n"); return 1; }
    n = (unsigned)utf8_para_latin1(b, n, NULL);
    b[n] = 0;

    /* Anda linha a linha guardando o começo do grupo atual e quantas vezes
     * ele apareceu; quando vem uma linha diferente, o grupo é escrito. */
    char *grupo = NULL;
    int vezes = 0;
    char *p = b, *fim = b + n;
    while (p < fim) {
        char *nl = acha_char(p, '\n');
        if (nl) *nl = 0;
        if (nl && nl > p && nl[-1] == '\r') nl[-1] = 0;
        if (grupo && iguais(grupo, p)) vezes++;
        else {
            if (grupo) emitir(saida, grupo, vezes);
            grupo = p;
            vezes = 1;
        }
        p = nl ? nl + 1 : fim;
    }
    if (grupo) emitir(saida, grupo, vezes);
    if (saida != SAIDA) fechar(saida);
    return 0;
}
