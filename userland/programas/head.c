/* head (inicio) - mostra o começo de arquivos ou da entrada (H91; a ideia
 * veio do fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi
 * reescrito sobre a libsagui atual).
 *
 *   head [-n N | -N] [-c N] [-q | -v] [arquivo...]
 *
 *   -n N  as N primeiras linhas (o padrão é 10); "-5" é o mesmo que "-n 5";
 *   -c N  os N primeiros bytes;
 *   -q    nunca escreve o nome do arquivo; -v escreve sempre.
 * Com mais de um arquivo, cada um vem depois de "==> nome <==". Sem
 * arquivo (ou com "-"), lê a entrada: "ls bin | head -3".
 *
 * O comportamento segue a descrição do head na norma POSIX (IEEE Std
 * 1003.1). O programa para de ler assim que tem o que precisa: num pipe,
 * quem escreve recebe "pipe quebrado" e também termina. */
#include "sagui.h"

static int quantidade = 10;
static bool por_bytes;

/* Copia o começo de um arquivo já aberto. No modo de linhas, o texto UTF-8
 * vira Latin-1 como no cat (D027); no de bytes, os bytes vão como estão. */
static int comeco(int fd)
{
    char buf[1025];
    size_t pendente = 0;
    int falta = quantidade, n;
    while (falta > 0 && (n = ler(fd, buf + pendente, (unsigned)(sizeof buf - 1 - pendente))) > 0) {
        size_t total = pendente + (size_t)n, sobra = 0, m = total, ate;
        if (!por_bytes) m = utf8_para_latin1(buf, total, &sobra);
        if (por_bytes) {
            ate = m < (size_t)falta ? m : (size_t)falta;
            falta -= (int)ate;
        } else {
            for (ate = 0; ate < m - sobra && falta > 0; ate++)
                if (buf[ate] == '\n') falta--;
        }
        if (escrever_tudo(SAIDA, buf, (unsigned)ate) < 0) return 1;
        if (sobra) buf[0] = buf[m - 1];
        pendente = sobra;
    }
    return 0;
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: head [-n N | -N] [-c N] [-q | -v] [arquivo...]\n");
    return 1;
}

int principal(int argc, char **argv)
{
    bool calado = false, sempre = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *a = argv[i];
        if (compara_texto(a, "--") == 0) { i++; break; }
        if (a[1] >= '0' && a[1] <= '9') {          /* "-5", a forma antiga */
            if (!converter_numero(a + 1, &quantidade)) return uso();
            por_bytes = false;
            continue;
        }
        if (a[1] == 'n' || a[1] == 'c') {
            const char *v = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : NULL);
            if (!v || !converter_numero(v, &quantidade) || quantidade < 0) {
                imprimir_em(ERROS, "head: quantidade inválida: %s\n", v ? v : "(nada)");
                return 1;
            }
            por_bytes = a[1] == 'c';
            continue;
        }
        for (const char *o = a + 1; *o; o++) {
            if (*o == 'q') calado = true;
            else if (*o == 'v') sempre = true;
            else { imprimir_em(ERROS, "head: opção desconhecida -%c\n", *o); return uso(); }
        }
    }
    int arquivos = argc - i, erros = 0;
    if (arquivos == 0) return comeco(ENTRADA);
    for (int k = 0; i < argc; i++, k++) {
        int fd = abrir_para_ler("head", argv[i]);
        if (fd < 0) { erros++; continue; }
        if ((arquivos > 1 || sempre) && !calado)
            imprimir("%s==> %s <==\n", k ? "\n" : "", fd == ENTRADA ? "(entrada)" : argv[i]);
        int quebrou = comeco(fd);
        if (fd != ENTRADA) fechar(fd);
        if (quebrou) break;
    }
    return erros ? 1 : 0;
}
