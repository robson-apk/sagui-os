/* hexdump (hexa) - mostra os bytes de um arquivo em hexadecimal e em texto,
 * 16 por linha, como o "hexdump -C". hexdump [-n bytes] [arquivo]. */
#include "sagui.h"

static const char HEXA[] = "0123456789abcdef";

static unsigned char bloco[4096];
static int bloco_n, bloco_pos;

static int proximo(int fd)
{
    if (bloco_pos == bloco_n) {
        bloco_n = ler(fd, bloco, sizeof bloco);
        bloco_pos = 0;
        if (bloco_n <= 0) { bloco_n = 0; return -1; }
    }
    return bloco[bloco_pos++];
}

int principal(int argc, char **argv)
{
    const char *nome = NULL;
    int limite = -1;
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "-n") == 0 && i + 1 < argc) limite = texto_para_int(argv[++i]);
        else if (compara_texto(argv[i], "-C") == 0) continue;
        else if (!nome) nome = argv[i];
        else { imprimir_em(ERROS, "uso: hexdump [-n bytes] [arquivo]\n"); return 1; }
    }
    int fd = ENTRADA;
    if (nome) {
        fd = abrir(nome, ABRIR_LER);
        if (fd < 0) { imprimir_em(ERROS, "hexdump: %s: %s\n", nome, nome_erro(fd)); return 1; }
    }
    unsigned char b[16];
    unsigned pos = 0;
    char linha[96];
    for (;;) {
        int n = 0;
        while (n < 16) {   /* junta 16 bytes: um pipe pode entregar aos pedaços */
            if (limite >= 0 && pos + (unsigned)n >= (unsigned)limite) break;
            int c = proximo(fd);
            if (c < 0) break;
            b[n++] = (unsigned char)c;
        }
        if (n == 0) break;
        int k = formatar_texto(linha, sizeof linha, "%08x  ", pos);
        for (int i = 0; i < 16; i++) {
            if (i < n) { linha[k++] = HEXA[b[i] >> 4]; linha[k++] = HEXA[b[i] & 15]; }
            else { linha[k++] = ' '; linha[k++] = ' '; }
            linha[k++] = ' ';
            if (i == 7) linha[k++] = ' ';
        }
        linha[k++] = ' '; linha[k++] = '|';
        for (int i = 0; i < n; i++) linha[k++] = (b[i] >= 32 && b[i] < 127) ? (char)b[i] : '.';
        linha[k++] = '|'; linha[k++] = '\n';
        escrever_tudo(SAIDA, linha, (unsigned)k);
        pos += (unsigned)n;
        if (n < 16) break;
    }
    imprimir("%08x\n", pos);
    if (nome) fechar(fd);
    return 0;
}
