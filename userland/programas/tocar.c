/* tocar arquivo.wav - toca um arquivo WAV pela placa de som (H42).
 *
 * Aceita WAV "PCM" de 8 ou 16 bits, mono ou estéreo, em qualquer taxa: o
 * programa converte para o que a placa usa (16 bits, estéreo, 48 kHz). A
 * conversão de taxa é por interpolação linear entre duas amostras vizinhas,
 * com a posição em ponto fixo (16 bits de fração). */
#include "sagui.h"

static unsigned le16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned le32(const unsigned char *p) { return le16(p) | (le16(p + 2) << 16); }

int principal(int argc, char **argv)
{
    if (argc < 2) { imprimir_em(ERROS, "uso: tocar arquivo.wav\n"); return 1; }
    int fd = abrir(argv[1], ABRIR_LER);
    if (fd < 0) { imprimir_em(ERROS, "tocar: %s: %s\n", argv[1], nome_erro(fd)); return 1; }
    info_arquivo_t inf;
    info_arquivo(argv[1], &inf);
    unsigned char *dados = aloca(inf.tamanho + 1);
    if (!dados) { imprimir_em(ERROS, "tocar: arquivo grande demais\n"); return 1; }
    /* Um ler() pode trazer menos que o pedido (o kernel entrega o arquivo em
     * pedaços): lê em laço até o fim. Antes era um ler() só, e no G470 a
     * música tocava só os primeiros segundos. */
    int n = 0;
    while ((unsigned)n < inf.tamanho) {
        int r = ler(fd, dados + n, inf.tamanho - (unsigned)n);
        if (r <= 0) break;
        n += r;
    }
    fechar(fd);
    if ((unsigned)n < inf.tamanho) registrar("tocar: li %d de %u bytes", n, inf.tamanho);
    if (n < 44 || compara_texto_n((char *)dados, "RIFF", 4) != 0 || compara_texto_n((char *)dados + 8, "WAVE", 4) != 0) {
        imprimir_em(ERROS, "tocar: %s não é um arquivo WAV\n", argv[1]);
        return 1;
    }
    /* Percorre os pedaços ("chunks"): "fmt " diz o formato, "data" tem as amostras. */
    unsigned formato = 0, canais = 0, taxa = 0, bits = 0, ini = 0, tam = 0;
    for (unsigned p = 12; p + 8 <= (unsigned)n;) {
        unsigned t = le32(dados + p + 4);
        if (compara_texto_n((char *)dados + p, "fmt ", 4) == 0 && t >= 16) {
            formato = le16(dados + p + 8); canais = le16(dados + p + 10);
            taxa = le32(dados + p + 12); bits = le16(dados + p + 22);
        } else if (compara_texto_n((char *)dados + p, "data", 4) == 0) {
            ini = p + 8;
            tam = t;
            if (ini + tam > (unsigned)n) tam = (unsigned)n - ini;
            break;
        }
        p += 8 + t + (t & 1);
    }
    if (formato != 1 || (bits != 8 && bits != 16) || canais < 1 || canais > 2 || taxa < 4000 || taxa > 96000 || !ini) {
        imprimir_em(ERROS, "tocar: formato não suportado (só PCM de 8 ou 16 bits, mono ou estéreo)\n");
        return 1;
    }
    unsigned bq = bits / 8 * canais, quadros = tam / bq;
    imprimir("Tocando %s: %u Hz, %u bits, %s, %u.%u s\n", argv[1], taxa, bits, canais == 2 ? "estéreo" : "mono",
             quadros / taxa, quadros % taxa * 10 / taxa);
    registrar("tocar: %s %u Hz %u bits %u canais", argv[1], taxa, bits, canais);
    int f = som_abrir();
    if (f < 0) { imprimir_em(ERROS, "tocar: sem placa de som HD Audio\n"); return 1; }
    /* Lê a amostra k de um canal, já em 16 bits com sinal. */
    #define AMOSTRA(k, c) (bits == 16 ? (short)le16(dados + ini + (k) * bq + (c) * 2) \
                                      : (short)(((int)dados[ini + (k) * bq + (c)] - 128) << 8))
    static short bloco[2 * 960];
    /* passo = taxa / 48000 em 16.16; 65536 / 48000 = 1024 / 750, que cabe em 32 bits. */
    /* A posição em 16.16 num inteiro de 64 bits: em 32 bits ela só ia até
     * 65536 amostras (1,5 s a 44,1 kHz) e dava a volta (G470: a música
     * parava). Somar e deslocar 64 bits não precisa da libgcc. */
    unsigned passo = taxa * 1024u / 750u;
    unsigned long long pos = 0;
    for (;;) {
        int q = 0;
        while (q < 960) {
            unsigned k = (unsigned)(pos >> 16), fr = (unsigned)pos & 0xFFFF;
            if (k + 1 >= quadros) break;
            for (unsigned c = 0; c < 2; c++) {
                unsigned origem = canais == 2 ? c : 0;
                int a = AMOSTRA(k, origem), b = AMOSTRA(k + 1, origem);
                bloco[2 * q + c] = (short)(a + (int)(((long long)(b - a) * fr) >> 16));
            }
            q++;
            pos += passo;
        }
        if (q == 0) break;
        int enviados = 0, parado = 0;
        while (enviados < q * 4) {
            int r = som_enviar(f, (char *)bloco + enviados, (unsigned)(q * 4 - enviados));
            if (r < 0) { som_fechar(f); return 1; }
            enviados += r;
            parado = r > 0 ? 0 : parado + 1;
            /* 3 segundos sem a placa aceitar nada: ela não está tocando (no G470
             * isso aconteceu). Desiste em vez de ficar parado para sempre. */
            if (parado > 300) {
                imprimir_em(ERROS, "tocar: a placa de som não está tocando (o áudio não anda). "
                                   "Para ajudar: registro HDA > /usb/som.txt\n");
                registrar("tocar: placa de som parada, desistindo");
                som_fechar(f);
                return 1;
            }
            if (enviados < q * 4) dormir(10);
        }
    }
    dormir(400);
    som_fechar(f);
    return 0;
}
