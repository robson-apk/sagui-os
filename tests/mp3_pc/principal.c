/* Programa do teste H86, para o PC de build (não vai para o SaguiOS):
 * decodifica um MP3 com userland/tocador/mp3.c e grava o som (16 bits,
 * estéreo intercalado), para tests/h86_mp3.py comparar com um decodificador
 * de referência.
 *
 *   mp3_pc entrada.mp3 saida.pcm [pular_para_ms]
 *
 * Imprime: taxa, canais, duração em ms e quantos quadros de som saíram. */
#include <stdio.h>
#include <stdlib.h>
#include "mp3.h"

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "uso: mp3_pc entrada.mp3 saida.pcm [ms]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *dados = malloc((size_t)tam + 1);
    if (fread(dados, 1, (size_t)tam, f) != (size_t)tam) { perror("ler"); return 2; }
    fclose(f);
    mp3_t *m = mp3_abrir(dados, (unsigned)tam);
    if (!m) { printf("nao e mp3\n"); return 1; }
    if (argc > 3) mp3_ir_para_ms(m, (unsigned)atoi(argv[3]));
    FILE *s = fopen(argv[2], "wb");
    if (!s) { perror(argv[2]); return 2; }
    static short bloco[2 * 1000];
    long total = 0;
    int n;
    while ((n = mp3_decodificar(m, bloco, 1000)) > 0) {
        fwrite(bloco, 4, (size_t)n, s);
        total += n;
    }
    fclose(s);
    printf("%u %u %u %ld\n", mp3_taxa(m), mp3_canais(m), mp3_duracao_ms(m), total);
    mp3_fechar(m);
    free(dados);
    return 0;
}
