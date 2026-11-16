/* xxd (hexa) - mostra os bytes de um arquivo em hexadecimal, ao lado das
 * letras; com -r, faz o caminho de volta (H91; a ideia veio do fork
 * SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi reescrito
 * sobre a libsagui atual).
 *
 *   xxd [-c N] [-g N] [-l N] [-s N] [-u] [arquivo]
 *   xxd -p [arquivo]          só o hexadecimal, sem endereço nem letras
 *   xxd -r [-p] [arquivo]     lê um "dump" (ou só hexadecimal, com -p) e
 *                             escreve os bytes de volta
 *
 *   -c N  bytes por linha (16; 30 com -p);   -g N  bytes por grupo (2);
 *   -l N  mostra só N bytes;   -s N  começa no byte N;   -u  letras A-F.
 *
 * Cada linha tem o endereço (8 casas hexadecimais), os bytes em grupos e,
 * no fim, as letras: o que não for letra ASCII visível vira ".". Na volta
 * (-r), o endereço de cada linha é ignorado e os bytes saem na ordem em que
 * aparecem. */
#include "sagui.h"

static const char *digitos = "0123456789abcdef";

static void dump(const uint8_t *b, unsigned n, unsigned inicio, int colunas, int grupo, bool simples)
{
    static char t[1100];   /* até 256 bytes por linha: endereço, 3 letras por byte e as letras */
    for (unsigned p = 0; p < n; p += (unsigned)colunas) {
        int k = 0, qtd = n - p < (unsigned)colunas ? (int)(n - p) : colunas;
        if (!simples) {
            uint32_t a = inicio + p;
            for (int s = 28; s >= 0; s -= 4) t[k++] = digitos[a >> s & 15];
            t[k++] = ':';
            t[k++] = ' ';
        }
        for (int i = 0; i < (simples ? qtd : colunas); i++) {
            if (i < qtd) { t[k++] = digitos[b[p + i] >> 4]; t[k++] = digitos[b[p + i] & 15]; }
            else { t[k++] = ' '; t[k++] = ' '; }
            if (!simples && grupo && (i + 1) % grupo == 0) t[k++] = ' ';
        }
        if (!simples) {
            if (!grupo) t[k++] = ' ';
            t[k++] = ' ';
            for (int i = 0; i < qtd; i++) {
                uint8_t c = b[p + i];
                t[k++] = c >= 32 && c < 127 ? (char)c : '.';
            }
        }
        t[k++] = '\n';
        escrever_tudo(SAIDA, t, (unsigned)k);
    }
}

static int valor_hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* -r: junta pares de dígitos hexadecimais em bytes. No formato normal,
 * cada linha só vale do ":" até dois espaços seguidos (onde começam as
 * letras, que não podem ser lidas como números). */
static void reverter(const char *b, unsigned n, bool simples)
{
    static uint8_t saida[4096];
    unsigned s = 0;
    for (unsigned p = 0; p < n;) {
        unsigned f = p;
        while (f < n && b[f] != '\n') f++;
        unsigned i = p;
        if (!simples) {
            while (i < f && b[i] != ':') i++;
            i = i < f ? i + 1 : f;
        }
        int alto = -1;
        for (; i < f; i++) {
            if (!simples && b[i] == ' ' && i + 1 < f && b[i + 1] == ' ' && alto < 0) break;
            int v = valor_hex((unsigned char)b[i]);
            if (v < 0) continue;
            if (alto < 0) { alto = v; continue; }
            saida[s++] = (uint8_t)(alto << 4 | v);
            alto = -1;
            if (s == sizeof saida) { escrever_tudo(SAIDA, saida, s); s = 0; }
        }
        p = f + 1;
    }
    escrever_tudo(SAIDA, saida, s);
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: xxd [-c N] [-g N] [-l N] [-s N] [-u] [-p] [-r] [arquivo]\n");
    return 1;
}

int principal(int argc, char **argv)
{
    int colunas = 0, grupo = 2, limite = -1, inicio = 0;
    bool simples = false, volta = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        for (const char *o = argv[i] + 1; *o; o++) {
            if (acha_char("cgls", *o)) {
                const char *v = o[1] ? o + 1 : (i + 1 < argc ? argv[++i] : NULL);
                int x;
                if (!v || !converter_numero(v, &x) || x < 0 || (*o == 'c' && (x < 1 || x > 256))) {
                    imprimir_em(ERROS, "xxd: valor inválido para -%c\n", *o);
                    return 1;
                }
                if (*o == 'c') colunas = x;
                else if (*o == 'g') grupo = x;
                else if (*o == 'l') limite = x;
                else inicio = x;
                break;
            }
            if (*o == 'p') simples = true;
            else if (*o == 'r') volta = true;
            else if (*o == 'u') digitos = "0123456789ABCDEF";
            else { imprimir_em(ERROS, "xxd: opção desconhecida -%c\n", *o); return uso(); }
        }
    }
    if (argc - i > 1) return uso();
    int fd = abrir_para_ler("xxd", i < argc ? argv[i] : "-");
    if (fd < 0) return 1;
    unsigned n;
    char *b = ler_ate_o_fim(fd, &n);
    if (fd != ENTRADA) fechar(fd);
    if (!b) { imprimir_em(ERROS, "xxd: memória insuficiente\n"); return 1; }
    if (volta) { reverter(b, n, simples); return 0; }
    if (!colunas) colunas = simples ? 30 : 16;
    unsigned ini = (unsigned)inicio < n ? (unsigned)inicio : n, qtd = n - ini;
    if (limite >= 0 && (unsigned)limite < qtd) qtd = (unsigned)limite;
    dump((const uint8_t *)b + ini, qtd, ini, colunas, grupo, simples);
    return 0;
}
