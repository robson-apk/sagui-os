/* teste_compactar.c - roda o compactar.c da libsagui no computador de
 * desenvolvimento (com o sagui.h simulado do navegador), para o teste H93
 * comparar com o zlib do Python. Uso:
 *   teste_compactar deflar <nível> <entrada> <saída>
 *   teste_compactar inflar <entrada> <saída>
 *   teste_compactar gzip <nível> <entrada> <saída>
 *   teste_compactar gunzip <entrada> <saída>
 *   teste_compactar zlib <entrada> <saída>        (inflar_zlib)
 *   teste_compactar crc <entrada>                 (CRC-32 e Adler-32)
 * Saída 1 = erro (com o código impresso). */
#include "sagui.h"
#include "compactar.h"

static uint8_t *ler_tudo(const char *c, uint32_t *n)
{
    FILE *f = fopen(c, "rb");
    if (!f) { perror(c); exit(2); }
    fseek(f, 0, SEEK_END);
    long t = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)t + 1);
    if (fread(d, 1, (size_t)t, f) != (size_t)t) exit(2);
    fclose(f);
    *n = (uint32_t)t;
    return d;
}

static void gravar(const char *c, const uint8_t *d, int n)
{
    FILE *f = fopen(c, "wb");
    fwrite(d, 1, (size_t)n, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    uint32_t n;
    if (!strcmp(argv[1], "deflar") || !strcmp(argv[1], "gzip")) {
        if (argc < 5) return 2;
        uint8_t *e = ler_tudo(argv[3], &n);
        int nivel = atoi(argv[2]), r;
        uint8_t *s;
        if (argv[1][0] == 'd') {
            uint32_t cap = deflar_limite(n);
            s = malloc(cap);
            r = deflar(e, n, s, cap, nivel);
        } else r = gzip_compactar(e, n, "nome.txt", 1234567890u, nivel, &s);
        if (r < 0) { printf("erro %d\n", r); return 1; }
        gravar(argv[4], s, r);
        free(s);
        free(e);
        return 0;
    }
    if (!strcmp(argv[1], "crc")) {
        uint8_t *e = ler_tudo(argv[2], &n);
        uint32_t m = n / 3;   /* em dois pedaços, para conferir a soma continuada */
        printf("%08x %08x\n", crc32_somar(crc32_somar(0, e, m), e + m, n - m),
               adler32_somar(adler32_somar(1, e, m), e + m, n - m));
        free(e);
        return 0;
    }
    if (argc < 4) return 2;
    uint8_t *e = ler_tudo(argv[2], &n);
    uint8_t *s = NULL;
    int r;
    if (!strcmp(argv[1], "inflar")) {
        uint32_t usados = 0;
        r = inflar_alocando(e, n, &s, 0, &usados);
        if (r >= 0 && usados != n) { printf("usados %u de %u\n", usados, n); return 1; }
    } else if (!strcmp(argv[1], "gunzip")) {
        char nome[64];
        r = gzip_descompactar(e, n, &s, nome, sizeof nome);
        if (r >= 0) fprintf(stderr, "nome: %s\n", nome);
    } else if (!strcmp(argv[1], "zlib")) {
        s = malloc(64u << 20);
        r = inflar_zlib(e, n, s, 64u << 20);
    } else return 2;
    if (r < 0) { printf("erro %d\n", r); free(s); free(e); return 1; }
    gravar(argv[3], s, r);
    free(s);
    free(e);
    return 0;
}
