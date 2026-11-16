/* chip8asm.c - o montador de CHIP-8 do SaguiOS rodando no computador de
 * desenvolvimento (H92). O build monta com ele as ROMs de exemplo de
 * rootfs/jogos/chip8 (os .asm); o montador é o mesmo arquivo do SaguiOS
 * (userland/jogos/chip8asm/montador.c), compilado com TESTE_HOSPEDEIRO.
 *
 *     chip8asm fonte.asm saida.ch8 */
#include <stdio.h>
#include <stdlib.h>
#include "montador.h"

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "uso: chip8asm fonte.asm saida.ch8\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    static char fonte[256 * 1024];
    int n = (int)fread(fonte, 1, sizeof fonte, f);
    fclose(f);
    static uint8_t rom[4096 - 0x200];
    char erro[160];
    int tam = c8_montar(fonte, n, rom, sizeof rom, erro, sizeof erro);
    if (tam < 0) { fprintf(stderr, "%s: %s\n", argv[1], erro); return 1; }
    f = fopen(argv[2], "wb");
    if (!f || fwrite(rom, 1, (size_t)tam, f) != (size_t)tam) { perror(argv[2]); return 1; }
    fclose(f);
    return 0;
}
