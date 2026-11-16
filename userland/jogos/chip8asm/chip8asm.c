/* chip8asm - monta um programa de CHIP-8 escrito em texto (H92).
 *
 *     chip8asm fonte.asm [-o saida.ch8]     (sem -o: troca .asm por .ch8)
 *
 * A sintaxe está no comentário de montador.c; os exemplos de /jogos/chip8
 * (.asm) foram montados com este mesmo código na hora do build. */
#include "sagui.h"
#include "montador.h"

#define MAX_FONTE (256 * 1024)
#define MAX_ROM   (4096 - 0x200)

int principal(int argc, char **argv)
{
    const char *entrada = NULL, *saida = NULL;
    for (int k = 1; k < argc; k++) {
        if (compara_texto(argv[k], "-o") == 0 && k + 1 < argc) saida = argv[++k];
        else if (!entrada) entrada = argv[k];
        else entrada = NULL, k = argc;
    }
    if (!entrada) { imprimir_em(ERROS, "uso: chip8asm fonte.asm [-o saida.ch8]\n"); return 2; }
    char nome_saida[200];
    if (!saida) {
        copia_texto(nome_saida, entrada, sizeof nome_saida - 4);
        char *ponto = acha_ultimo_char(nome_saida, '.');
        if (ponto && !acha_char(ponto, '/')) *ponto = 0;
        junta_texto(nome_saida, ".ch8", sizeof nome_saida);
        saida = nome_saida;
    }
    int fd = abrir(entrada, ABRIR_LER);
    if (fd < 0) { imprimir_em(ERROS, "chip8asm: não consegui abrir %s\n", entrada); return 1; }
    char *fonte = aloca(MAX_FONTE);
    uint8_t *rom = aloca(MAX_ROM);
    if (!fonte || !rom) { fechar(fd); imprimir_em(ERROS, "chip8asm: sem memória\n"); return 1; }
    int n = 0, r;
    while (n < MAX_FONTE && (r = ler(fd, fonte + n, (unsigned)(MAX_FONTE - n))) > 0) n += r;
    fechar(fd);
    char erro[160];
    int tam = c8_montar(fonte, n, rom, MAX_ROM, erro, sizeof erro);
    if (tam < 0) { imprimir_em(ERROS, "%s:%s\n", entrada, erro + 5); return 1; }   /* "linha N: ..." -> "arq:N: ..." */
    fd = abrir(saida, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0 || escrever_tudo(fd, rom, (unsigned)tam) < 0) { imprimir_em(ERROS, "chip8asm: não consegui gravar %s\n", saida); return 1; }
    fechar(fd);
    imprimir("%s: %d bytes\n", saida, tam);
    return 0;
}
