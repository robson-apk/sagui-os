/* tail (fim) - mostra o fim de arquivos ou da entrada (H91; a ideia veio do
 * fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi reescrito
 * sobre a libsagui atual).
 *
 *   tail [-n N | -n +N | -N] [-c N | -c +N] [-f] [-q | -v] [arquivo...]
 *
 *   -n N   as N últimas linhas (o padrão é 10); com +N, da linha N em diante;
 *   -c N   os N últimos bytes; com +N, do byte N em diante;
 *   -f     depois do fim, continua esperando o arquivo crescer e mostra o
 *          que for acrescentado (Ctrl+C encerra);
 *   -q/-v  nunca/sempre escreve "==> nome <==" antes de cada arquivo.
 *
 * Segue a descrição do tail na norma POSIX (IEEE Std 1003.1). Para achar o
 * fim de um pipe é preciso ler tudo, então o arquivo inteiro vai para a
 * memória e a conta é feita de trás para a frente. */
#include "sagui.h"

static int quantidade = 10;
static bool por_bytes, do_comeco, seguir;

/* Escreve o pedaço pedido de um texto já na memória. */
static void mostrar_fim(const char *b, unsigned n)
{
    unsigned ini = 0;
    if (por_bytes) {
        if (do_comeco) ini = quantidade > 0 ? (unsigned)quantidade - 1 : 0;
        else ini = (unsigned)quantidade < n ? n - (unsigned)quantidade : 0;
        if (ini > n) ini = n;
    } else if (do_comeco) {
        /* Pula as N-1 primeiras linhas. */
        int pular = quantidade - 1;
        while (ini < n && pular > 0)
            if (b[ini++] == '\n') pular--;
    } else {
        /* De trás para a frente: a última linha pode não ter '\n' no fim
         * (ainda conta como linha); por isso o último '\n' é ignorado. */
        int achadas = 0;
        unsigned p = n;
        if (p > 0 && b[p - 1] == '\n') p--;
        while (p > 0) {
            if (b[p - 1] == '\n' && ++achadas == quantidade) break;
            p--;
        }
        ini = quantidade == 0 ? n : p;
    }
    escrever_tudo(SAIDA, b + ini, n - ini);
}

/* tail -f: o arquivo foi lido até o fim; a cada meio segundo, o que
 * aparecer depois é mostrado. O programa só termina com Ctrl+C. */
static void acompanhar(int fd)
{
    char buf[512];
    for (;;) {
        int r = ler(fd, buf, sizeof buf);
        if (r > 0) { if (escrever_tudo(SAIDA, buf, (unsigned)r) < 0) return; continue; }
        dormir(500);
    }
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: tail [-n [+]N | -N] [-c [+]N] [-f] [-q | -v] [arquivo...]\n");
    return 1;
}

/* Lê o número de -n ou -c; um "+" na frente quer dizer "a partir de". */
static bool ler_quantidade(const char *v)
{
    do_comeco = v[0] == '+';
    return converter_numero(v, &quantidade) && quantidade >= 0;
}

int principal(int argc, char **argv)
{
    bool calado = false, sempre = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *a = argv[i];
        if (compara_texto(a, "--") == 0) { i++; break; }
        if (a[1] >= '0' && a[1] <= '9') {
            if (!ler_quantidade(a + 1)) return uso();
            por_bytes = false;
            continue;
        }
        if (a[1] == 'n' || a[1] == 'c') {
            const char *v = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : NULL);
            if (!v || !ler_quantidade(v)) {
                imprimir_em(ERROS, "tail: quantidade inválida: %s\n", v ? v : "(nada)");
                return 1;
            }
            por_bytes = a[1] == 'c';
            continue;
        }
        for (const char *o = a + 1; *o; o++) {
            if (*o == 'q') calado = true;
            else if (*o == 'v') sempre = true;
            else if (*o == 'f') seguir = true;
            else { imprimir_em(ERROS, "tail: opção desconhecida -%c\n", *o); return uso(); }
        }
    }
    int arquivos = argc - i, erros = 0, ultimo = -1;
    for (int k = 0; k < (arquivos ? arquivos : 1); k++) {
        const char *nome = arquivos ? argv[i + k] : "-";
        int fd = abrir_para_ler("tail", nome);
        if (fd < 0) { erros++; continue; }
        if ((arquivos > 1 || sempre) && !calado)
            imprimir("%s==> %s <==\n", k ? "\n" : "", fd == ENTRADA ? "(entrada)" : nome);
        unsigned n;
        char *b = ler_ate_o_fim(fd, &n);
        if (!b) { imprimir_em(ERROS, "tail: memória insuficiente\n"); return 1; }
        if (!por_bytes) n = (unsigned)utf8_para_latin1(b, n, NULL);
        mostrar_fim(b, n);
        libera(b);
        if (ultimo >= 0 && ultimo != ENTRADA) fechar(ultimo);
        ultimo = fd;
    }
    /* Como no tail clássico, -f acompanha o último arquivo; num pipe não
     * há o que esperar (o pipe já acabou). */
    if (seguir && ultimo > 0 && ultimo != ENTRADA) acompanhar(ultimo);
    if (ultimo > 0) fechar(ultimo);
    return erros ? 1 : 0;
}
