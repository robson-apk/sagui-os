/* teste_chip8.c - testa o núcleo do CHIP-8 e o montador (H92) no computador
 * de desenvolvimento, com os mesmos userland/jogos/chip8/nucleo.c e
 * userland/jogos/chip8asm/montador.c do SaguiOS.
 *
 *     teste_chip8 [rom.asm...]
 * Confere instruções isoladas (escritas com o montador), depois roda cada
 * ROM de exemplo por alguns segundos de máquina e diz o que viu. */
#include <stdio.h>
#include <stdlib.h>
#include "nucleo.h"
#include "montador.h"

static int falhas;

static void conferir(int cond, const char *msg)
{
    printf("%s: %s\n", cond ? "ok" : "FALHOU", msg);
    if (!cond) falhas++;
}

static c8_t maq;
static uint8_t rom[4096];

/* Monta o texto e roda até "n" instruções no modo dado. */
static int rodar(const char *fonte, int modo, int n)
{
    char erro[160];
    int tam = c8_montar(fonte, (int)strlen(fonte), rom, sizeof rom - 0x200, erro, sizeof erro);
    if (tam < 0) { printf("montador: %s\n", erro); return -1; }
    c8_iniciar(&maq, modo, 1);
    c8_carregar(&maq, rom, tam);
    maq.a.espera_quadro = false;
    return c8_executar(&maq, n);
}

static int pixels(void)
{
    int n = 0;
    for (int k = 0; k < maq.larg * maq.alt; k++) n += maq.tela[k] & 1;
    return n;
}

static int ler_arquivo(const char *caminho, char *buf, int max)
{
    FILE *f = fopen(caminho, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)max, f);
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    char erro[160];
    /* 1. O montador: códigos conhecidos da referência técnica. */
    const char *teste = "inicio: cls\n ld v3, 0x2A\n add v3, v4\n drw v1, v2, 5\n ld i, dados\n jp inicio\n"
                        "dados: db %#.#.#.#., 0b11, 'A'\n";
    int tam = c8_montar(teste, (int)strlen(teste), rom, sizeof rom, erro, sizeof erro);
    static const uint8_t esperado[] = { 0x00, 0xE0, 0x63, 0x2A, 0x83, 0x44, 0xD1, 0x25, 0xA2, 0x0C, 0x12, 0x00, 0xAA, 0x03, 0x41 };
    conferir(tam == (int)sizeof esperado && memcmp(rom, esperado, sizeof esperado) == 0,
             "o montador gera os códigos da referência (cls, ld, add, drw, ld i, jp, db)");
    conferir(c8_montar(" ld v0, nada\n", 12, rom, sizeof rom, erro, sizeof erro) < 0 && strstr(erro, "linha 1") &&
             strstr(erro, "nada"), "nome não definido vira erro com o número da linha");
    conferir(c8_montar(" pula v0\n", 9, rom, sizeof rom, erro, sizeof erro) < 0, "instrução desconhecida vira erro");

    /* 2. Aritmética e VF. */
    rodar("ld v0, 200\n ld v1, 100\n add v0, v1\n", C8_CHIP8, 3);
    conferir(maq.v[0] == 44 && maq.v[15] == 1, "8XY4: 200 + 100 = 44 com vai-um em VF");
    rodar("ld v0, 5\n ld v1, 9\n sub v0, v1\n ld v2, 9\n ld v3, 5\n sub v2, v3\n", C8_CHIP8, 6);
    conferir(maq.v[0] == 252 && maq.v[2] == 4 && maq.v[15] == 1, "8XY5: 5 - 9 = 252 (VF 0) e 9 - 5 = 4 (VF 1)");
    rodar("ld v1, 0x81\n shr v0, v1\n", C8_CHIP8, 2);
    conferir(maq.v[0] == 0x40 && maq.v[15] == 1, "8XY6 no CHIP-8 desloca VY (COSMAC)");
    rodar("ld v0, 0x81\n ld v1, 0\n shr v0, v1\n", C8_SCHIP, 3);
    conferir(maq.v[0] == 0x40 && maq.v[15] == 1, "8XY6 no SUPER-CHIP desloca o próprio VX");
    rodar("ld vf, 1\n ld v0, 3\n or v0, v0\n", C8_CHIP8, 3);
    conferir(maq.v[15] == 0, "8XY1 zera VF no CHIP-8");

    /* 3. Saltos, sub-rotinas, BCD e memória. */
    rodar("ld v0, 7\n se v0, 7\n ld v1, 1\n sne v0, 7\n ld v2, 1\n call sub\n ld v4, 1\n jp fim\n"
          "sub: ld v3, 1\n ret\nfim: jp fim\n", C8_CHIP8, 20);
    conferir(maq.v[1] == 0 && maq.v[2] == 1 && maq.v[3] == 1 && maq.v[4] == 1 && maq.sp == 0,
             "SE, SNE, CALL e RET");
    rodar("ld v0, 254\n ld i, 0x300\n ld b, v0\n ld v2, [i]\n", C8_CHIP8, 4);
    conferir(maq.v[0] == 2 && maq.v[1] == 5 && maq.v[2] == 4 && maq.i == 0x303, "FX33 (BCD de 254) e FX65; I avança no CHIP-8");

    /* 4. Desenho e colisão. */
    rodar("ld i, s\n ld v0, 62\n ld v1, 0\n drw v0, v1, 1\n drw v0, v1, 1\n jp fim\nfim: jp fim\ns: db 0xFF\n", C8_CHIP8, 4);
    conferir(pixels() == 2 && maq.v[15] == 0, "o desenho é cortado na borda direita (2 pixels de 8)");
    c8_executar(&maq, 1);
    conferir(pixels() == 0 && maq.v[15] == 1, "desenhar de novo apaga e liga VF (colisão)");
    rodar("ld v0, 0xA\n ld f, v0\n ld v1, 0\n drw v1, v1, 5\n", C8_CHIP8, 4);
    conferir(pixels() == 14, "FX29: o \"A\" da fonte pequena (14 pixels)");
    rodar("high\n ld v0, 120\n ld v1, 60\n ld i, s\n drw v0, v1, 1\n jp fim\nfim: jp fim\ns: db 0xFF\n", C8_SCHIP, 5);
    conferir(maq.larg == 128 && pixels() == 8, "SUPER-CHIP: 00FF liga a tela de 128x64");

    /* 5. Teclas e temporizadores. */
    rodar("ld v0, 5\n skp v0\n ld v1, 1\n ld v2, 30\n ld dt, v2\n", C8_CHIP8, 5);
    conferir(maq.v[1] == 1 && maq.dt == 30, "EX9E sem tecla não pula; FX15 acerta o temporizador");
    for (int k = 0; k < 10; k++) c8_quadro(&maq);
    conferir(maq.dt == 20, "o temporizador desce 1 por quadro");
    c8_iniciar(&maq, C8_CHIP8, 1);
    tam = c8_montar("ld v0, k\n", 9, rom, sizeof rom, erro, sizeof erro);
    c8_carregar(&maq, rom, tam);
    c8_executar(&maq, 5);
    maq.teclas = 1 << 7;
    c8_executar(&maq, 5);
    maq.teclas = 0;
    c8_executar(&maq, 5);
    conferir(maq.v[0] == 7, "FX0A espera a tecla e a recebe quando ela é solta");
    rodar("dw 0x5121\n", C8_CHIP8, 1);
    conferir(maq.estado == C8_ERRO, "instrução inválida para a máquina com erro");

    /* 6. As ROMs de exemplo: 10 s de máquina (600 quadros) cada. */
    for (int a = 1; a < argc; a++) {
        static char fonte[64 * 1024];
        int n = ler_arquivo(argv[a], fonte, sizeof fonte);
        tam = n < 0 ? -1 : c8_montar(fonte, n, rom, 4096 - 0x200, erro, sizeof erro);
        char msg[300];
        snprintf(msg, sizeof msg, "%s monta (%d bytes)%s%s", argv[a], tam, tam < 0 ? ": " : "", tam < 0 ? erro : "");
        conferir(tam > 0, msg);
        if (tam <= 0) continue;
        c8_iniciar(&maq, C8_CHIP8, 1234);
        c8_carregar(&maq, rom, tam);
        uint32_t somas[4];
        int bips = 0;
        for (int q = 0; q < 600; q++) {
            maq.teclas = (q / 40) % 2 ? 1 << 5 : 1 << 8;     /* sobe e desce (para o Rebate) */
            c8_executar(&maq, c8_instrucoes_por_quadro(C8_CHIP8));
            c8_quadro(&maq);
            if (c8_freq_som(&maq)) bips++;
            if (q % 150 == 149) somas[q / 150] = c8_soma_tela(&maq);
        }
        int diferentes = somas[0] != somas[1] && somas[1] != somas[2] && somas[2] != somas[3];
        snprintf(msg, sizeof msg, "%s roda 600 quadros sem erro, a tela muda e há bips (%u instruções, %d pixels)",
                 argv[a], maq.instrucoes, pixels());
        conferir(maq.estado == C8_RODANDO && diferentes && bips > 0 && pixels() > 4, msg);
    }
    return falhas ? 1 : 0;
}
