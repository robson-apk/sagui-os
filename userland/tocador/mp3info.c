/* mp3info [-t] arquivo.mp3 - mostra os dados de um MP3 e testa o decodificador (H86).
 *
 * Abre o arquivo com o decodificador de mp3.c, mostra a taxa, os canais e a
 * duração, decodifica o arquivo inteiro e diz quanto tempo isso levou (para
 * saber se a máquina dá conta de tocar em tempo real). Com -t, também toca o
 * som pela placa, que quer 16 bits, estéreo, 48 kHz: a conversão de taxa é
 * por interpolação linear entre duas amostras vizinhas, como no "tocar".
 */
#include "sagui.h"
#include "mp3.h"

#define BLOCO 576               /* quadros decodificados por vez */

static short entrada[2 * (BLOCO + 1)];      /* a posição 0 guarda o último quadro do bloco anterior */
static short saida[2 * (BLOCO * 6 + 8)];    /* 8 kHz -> 48 kHz multiplica por 6 */

/* Manda q quadros para a placa, esperando ela aceitar. -1 se ela parou. */
static int enviar(int fonte, const short *quadros, int q)
{
    int enviados = 0, parado = 0;
    while (enviados < q * 4) {
        int r = som_enviar(fonte, (const char *)quadros + enviados, (unsigned)(q * 4 - enviados));
        if (r < 0) return -1;
        enviados += r;
        parado = r > 0 ? 0 : parado + 1;
        if (parado > 300) return -1;        /* 3 segundos sem aceitar nada */
        if (enviados < q * 4) dormir(10);
    }
    return 0;
}

int principal(int argc, char **argv)
{
    int tocar = 0;
    const char *nome = 0;
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "-t") == 0) tocar = 1;
        else nome = argv[i];
    }
    if (!nome) { imprimir_em(ERROS, "uso: mp3info [-t] arquivo.mp3   (-t: também toca)\n"); return 1; }
    int fd = abrir(nome, ABRIR_LER);
    if (fd < 0) { imprimir_em(ERROS, "mp3info: %s: %s\n", nome, nome_erro(fd)); return 1; }
    info_arquivo_t inf;
    info_arquivo(nome, &inf);
    unsigned char *dados = aloca(inf.tamanho + 1);
    if (!dados) { fechar(fd); imprimir_em(ERROS, "mp3info: arquivo grande demais\n"); return 1; }
    /* Uma leitura pode devolver menos do que foi pedido: repete até o fim. */
    unsigned lidos = 0;
    while (lidos < inf.tamanho) {
        int n = ler(fd, dados + lidos, inf.tamanho - lidos);
        if (n <= 0) break;
        lidos += (unsigned)n;
    }
    fechar(fd);
    mp3_t *m = mp3_abrir(dados, lidos);
    if (!m) {
        imprimir_em(ERROS, "mp3info: %s não é um arquivo MP3\n", nome);
        libera(dados);
        return 1;
    }
    unsigned taxa = mp3_taxa(m), dur = mp3_duracao_ms(m);
    imprimir("%s: MP3, %u Hz, %s, %u.%u s\n", nome, taxa, mp3_canais(m) == 2 ? "estéreo" : "mono",
             dur / 1000, dur % 1000 / 100);

    int fonte = -1;
    if (tocar) {
        fonte = som_abrir();
        if (fonte < 0) { imprimir_em(ERROS, "mp3info: sem placa de som HD Audio\n"); tocar = 0; }
    }
    /* passo = taxa / 48000 em 16.16 (65536 / 48000 = 1024 / 750). */
    unsigned passo = taxa * 1024u / 750u, pos = 0, total = 0, inicio = tempo_ms();
    int erro = 0;
    entrada[0] = entrada[1] = 0;
    for (;;) {
        int n = mp3_decodificar(m, entrada + 2, BLOCO);
        if (n <= 0) break;
        total += (unsigned)n;
        if (!tocar) continue;
        int q = 0;
        while ((pos >> 16) < (unsigned)n) {
            unsigned k = pos >> 16, fr = pos & 0xFFFF;
            for (int c = 0; c < 2; c++) {
                int a = entrada[2 * k + c], b = entrada[2 * (k + 1) + c];
                saida[2 * q + c] = (short)(a + (int)(((long long)(b - a) * fr) >> 16));
            }
            q++;
            pos += passo;
        }
        pos -= (unsigned)n << 16;
        entrada[0] = entrada[2 * n];
        entrada[1] = entrada[2 * n + 1];
        if (enviar(fonte, saida, q) < 0) {
            imprimir_em(ERROS, "mp3info: a placa de som não está tocando\n");
            erro = 1;
            break;
        }
    }
    unsigned gasto = tempo_ms() - inicio;
    unsigned som_ms = total / taxa * 1000 + total % taxa * 1000 / taxa;
    imprimir("%s %u quadros (%u.%u s de som) em %u ms\n", tocar ? "Tocados" : "Decodificados", total,
             som_ms / 1000, som_ms % 1000 / 100, gasto);
    registrar("mp3info: %s %u Hz %u quadros em %u ms", nome, taxa, total, gasto);
    if (fonte >= 0) {
        dormir(400);            /* deixa a placa terminar o que está na fila */
        som_fechar(fonte);
    }
    mp3_fechar(m);
    libera(dados);
    return erro;
}
