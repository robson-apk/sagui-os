/* Programa do teste H88, para o PC de build (não vai para o SaguiOS):
 * decodifica a trilha de vídeo de um MP4 com userland/video/mp4.c e h264.c
 * (os mesmos arquivos do /bin/video, compilados com -DH264_HOST) e diz o
 * que saiu, para tests/h88_video_alto.py conferir.
 *
 *   video_pc entrada.mp4 [prefixo [i j k ...]]
 *   video_pc --conferir
 *
 * Com um MP4, imprime uma linha por quadro, na ordem de exibição:
 *     q <índice> <resumo> <diferença>
 * onde "resumo" é um FNV-1a de 32 bits dos três planos já cortados (serve
 * para saber se duas execuções deram exatamente a mesma imagem) e
 * "diferença" é a média, vezes 100, de |luma - luma do quadro anterior|
 * (num vídeo de verdade, quadros vizinhos são parecidos). No fim:
 *     fim <largura> <altura> <amostras no MP4> <quadros que saíram> <erros> <ms>
 *     alto <17 contagens: veja h264_contagem no começo de h264.c>
 * Com "prefixo", grava os quadros de índices i, j, k... em
 * <prefixo>-<índice>.yuv (I420: Y, depois U, depois V).
 *
 * Com --conferir, testa as peças novas do perfil High uma a uma, com contas
 * que não dependem de nenhum vídeo (uma linha "ok ..." ou "ERRO ..." por
 * conferência). Para alcançar as funções internas, este arquivo inclui o
 * h264.c inteiro em vez de ligar com ele. */
#include <time.h>
#include <math.h>
#include "../../userland/video/h264.c"
#include "mp4.h"

/* ------------------------------------------------------------ conferências */

static int falhas;
static void conferir(bool ok, const char *o_que)
{
    printf("%s %s\n", ok ? "ok" : "ERRO", o_que);
    if (!ok) falhas++;
}

/* A imagem que um único coeficiente 8x8 (ou 4x4) produz: a "função de base". */
static void base8(int pos, double *img)
{
    int c[64] = { 0 };
    c[pos] = 1 << 16;       /* grande, para os arredondamentos não pesarem */
    for (int i = 0; i < 8; i++) idct8_passada(c + i * 8, 1);
    for (int i = 0; i < 8; i++) idct8_passada(c + i, 8);
    for (int i = 0; i < 64; i++) img[i] = c[i];
}

static double escalar(const double *a, const double *b, int n)
{
    double s = 0;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

static void conferir_transformada(void)
{
    /* 1. O ziguezague 8x8 passa uma vez por cada posição, sempre para uma
     *    casa vizinha, e começa como o da tabela 8-13 da norma. */
    bool visto[64] = { false }, ok = true;
    vlc_iniciar();
    for (int k = 0; k < 64; k++) {
        if (visto[zigzag8[k]]) ok = false;
        visto[zigzag8[k]] = true;
        if (k && (abs((zigzag8[k] & 7) - (zigzag8[k - 1] & 7)) > 1 || abs((zigzag8[k] >> 3) - (zigzag8[k - 1] >> 3)) > 1)) ok = false;
    }
    static const uint8_t comeco[12] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25 };
    conferir(ok && !memcmp(zigzag8, comeco, 12) && zigzag8[63] == 63, "ziguezague 8x8: uma permutacao por diagonais");

    /* 2. As 64 funções de base da transformada inversa 8x8 são ortogonais
     *    entre si: é o que faz dela uma transformada. Um sinal trocado ou
     *    um termo fora do lugar em idct8_passada quebra isso. */
    static double b[64][64];
    double pior = 0;
    for (int p = 0; p < 64; p++) base8(p, b[p]);
    for (int p = 0; p < 64; p++)
        for (int q = p + 1; q < 64; q++) {
            double v = fabs(escalar(b[p], b[q], 64)) / sqrt(escalar(b[p], b[p], 64) * escalar(b[q], b[q], 64));
            if (v > pior) pior = v;
        }
    conferir(pior < 1e-3, "transformada 8x8: as 64 funcoes de base sao ortogonais");

    /* 3. As funções de base têm tamanhos diferentes, e é o normAdjust8x8
     *    (tabela norma8, pela classe da posição) que compensa: para um mesmo
     *    QP % 6, tamanho vezes normAdjust tem de dar quase o mesmo em todas
     *    as posições (os números da norma são inteiros arredondados: a folga
     *    é de 5%; uma classe trocada erraria por 20% ou mais), e de uma
     *    linha para a seguinte tudo cresce perto de 2^(1/6) (folga de 10%). */
    double menor = 1e30, maior = 0, pior_linha = 0;
    for (int m = 0; m < 6; m++) {
        double mn = 1e30, mx = 0;
        for (int p = 0; p < 64; p++) {
            double v = sqrt(escalar(b[p], b[p], 64)) * norma8[m][classe8(p & 7, p >> 3)] / pow(2.0, m / 6.0);
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        if (mx / mn > pior_linha) pior_linha = mx / mn;
        if (mn < menor) menor = mn;
        if (mx > maior) maior = mx;
    }
    conferir(pior_linha < 1.05 && maior / menor < 1.10, "normAdjust8x8: compensa o tamanho de cada funcao de base");

    /* 4. Sem matrizes de escala (peso 16 em tudo), a desquantização nova dá
     *    exatamente a conta que o decodificador fazia no Baseline e no Main. */
    ok = true;
    for (int qp = 0; qp < 52; qp++)
        for (int cl = 0; cl < 3; cl++)
            for (int c = -3000; c <= 3000; c += 7)
                if (desq(c, 16 * dq_v[qp % 6][cl], qp / 6, 4) != c * dq_v[qp % 6][cl] * (1 << (qp / 6))) ok = false;
    conferir(ok, "desquantizacao 4x4 com matriz plana: igual a de antes");

    /* 5. Um bloco 8x8 só com o coeficiente DC vira um bloco liso. */
    int c[64] = { 0 };
    uint8_t d[64];
    memset(d, 100, sizeof d);
    c[0] = 640;                     /* (640 + 32) >> 6 = 10 em toda parte */
    idct8_somar(d, 8, c);
    ok = true;
    for (int i = 0; i < 64; i++) if (d[i] != 110) ok = false;
    conferir(ok, "transformada 8x8: so o DC da um bloco liso");
}

static void conferir_listas(void)
{
    /* As listas de escala padrão crescem com a frequência (a não ser por um
     * vai-e-vem no começo das 8x8) e as intra começam e terminam nos mesmos
     * pesos. Não prova que os números são os da norma: só pega um número
     * trocado de forma grosseira. */
    bool ok = true;
    for (int i = 1; i < 16; i++) if (escala4_intra[i] < escala4_intra[i - 1] || escala4_inter[i] < escala4_inter[i - 1]) ok = false;
    for (int i = 5; i < 64; i++) if (escala8_intra[i] < escala8_intra[i - 1] || escala8_inter[i] < escala8_inter[i - 1]) ok = false;
    conferir(ok && escala4_intra[0] == escala8_intra[0] && escala4_intra[15] == escala8_intra[63],
             "listas de escala padrao: crescentes");

    /* scaling_list: três diferenças e um "repete até o fim". As diferenças
     * são se(v): +2 = "00100", -2 = "00101", -1 = "011", -7 = "0001111".
     * A lista começa em 8: 10, 8, 7, e o -7 leva a zero (repete o 7). */
    static const uint8_t fluxo[8] = { 0x21, 0x58, 0xF0, 0, 0, 0, 0, 0 };   /* 00100 00101 011 0001111 */
    bits_t b = { fluxo, 0, 64 };
    uint8_t l[16];
    bool veio = ler_lista_escala(&b, l, 16);
    ok = veio && l[0] == 10 && l[1] == 8 && l[2] == 7;
    for (int i = 3; i < 16; i++) if (l[i] != 7) ok = false;
    conferir(ok, "scaling_list: diferencas e repeticao do ultimo valor");
    /* a primeira diferença levando a zero pede a lista padrão: -8 = "000010001" */
    static const uint8_t fluxo3[8] = { 0x08, 0x80, 0, 0, 0, 0, 0, 0 };
    bits_t b3 = { fluxo3, 0, 64 };
    conferir(!ler_lista_escala(&b3, l, 16), "scaling_list: zero no primeiro valor pede a lista padrao");

    /* Regras da tabela 7-2: nenhum bit ligado. Regra A: padrões. Regra B: as do SPS. */
    static const uint8_t zeros[8] = { 0 };
    static uint8_t a[8][64], sps[8][64], r[8][64];
    bits_t bz = { zeros, 0, 64 };
    ler_matrizes(&bz, a, 8, NULL);
    ok = !memcmp(a[0], escala4_intra, 16) && !memcmp(a[2], escala4_intra, 16) && !memcmp(a[3], escala4_inter, 16) &&
         !memcmp(a[5], escala4_inter, 16) && !memcmp(a[6], escala8_intra, 64) && !memcmp(a[7], escala8_inter, 64);
    conferir(ok, "matrizes de escala: regra A (listas padrao)");
    for (int i = 0; i < 8; i++) memset(sps[i], 20 + i, 64);
    bz.pos = 0;
    ler_matrizes(&bz, r, 8, sps);
    conferir(r[0][0] == 20 && r[1][5] == 20 && r[2][15] == 20 && r[3][0] == 23 && r[5][9] == 23 && r[6][63] == 26 && r[7][0] == 27,
             "matrizes de escala: regra B (as do SPS)");
}

static void conferir_intra8(void)
{
    /* Um MB no meio de um quadro de 48x48, com todos os vizinhos. */
    h264_t *h = h264_novo();
    static uint8_t plano[48 * 48];
    static mbinfo_t viz;
    h->passo = 48;
    h->q[0].y = plano;
    h->atual = &h->q[0];
    ctx_t c = { 1, 1, 0, &viz, &viz, &viz, &viz, &viz };
    /* 1. Vizinhança lisa: qualquer direção prevê o mesmo valor. */
    bool ok = true;
    for (int b8 = 0; b8 < 4; b8++)
        for (int modo = 0; modo < 9; modo++) {
            uint8_t *bloco = plano + (16 + (b8 >> 1) * 8) * 48 + 16 + (b8 & 1) * 8;
            memset(plano, 77, sizeof plano);
            for (int y = 0; y < 8; y++) memset(bloco + y * 48, 0, 8);
            pred8x8(h, &c, b8, modo);
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) if (bloco[y * 48 + x] != 77) ok = false;
        }
    conferir(ok, "intra 8x8: vizinhanca lisa da bloco liso nos 9 modos");
    /* 2. Vertical copia a linha de cima filtrada; horizontal, a coluna da
     *    esquerda. Com um degrau na vizinha, o filtro [1 2 1]/4 o suaviza. */
    memset(plano, 40, sizeof plano);
    for (int x = 20; x < 48; x++) plano[15 * 48 + x] = 80;      /* linha de cima: 40 40 40 40 80 80 ... */
    pred8x8(h, &c, 0, 0);
    ok = true;
    static const uint8_t esperado[8] = { 40, 40, 40, 50, 70, 80, 80, 80 };
    for (int y = 0; y < 8; y++) if (memcmp(plano + (16 + y) * 48 + 16, esperado, 8)) ok = false;
    conferir(ok, "intra 8x8 vertical: a linha de cima passa pelo filtro 1-2-1");
    memset(plano, 40, sizeof plano);
    for (int y = 20; y < 48; y++) plano[y * 48 + 15] = 80;
    pred8x8(h, &c, 0, 1);
    ok = true;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) if (plano[(16 + y) * 48 + 16 + x] != esperado[y]) ok = false;
    conferir(ok, "intra 8x8 horizontal: a coluna da esquerda passa pelo filtro 1-2-1");
    /* 3. As diagonais levam uma rampa adiante: com a linha de cima subindo 4
     *    por amostra, "para baixo e esquerda" dá uma rampa em x + y. */
    memset(plano, 0, sizeof plano);
    for (int x = 0; x < 48; x++) plano[15 * 48 + x] = (uint8_t)(4 * x);
    pred8x8(h, &c, 0, 3);
    ok = true;
    for (int y = 0; y < 7; y++) for (int x = 0; x < 7; x++) if (plano[(16 + y) * 48 + 16 + x] != 4 * (16 + x + y + 1)) ok = false;
    conferir(ok, "intra 8x8 diagonal: uma rampa na linha de cima continua em diagonal");
    h->q[0].y = NULL;       /* não é do decodificador: não deixa o h264_fim liberar */
    h264_fim(h);
}

/* ---------------------------------------------------------------- vídeo */

static uint8_t *anterior;
static int saidos;

static uint32_t fnv(uint32_t h, const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static void quadro(const h264_imagem_t *im, const char *prefixo, int argc, char **argv)
{
    uint32_t r = 2166136261u;
    long dif = 0;
    if (!anterior) anterior = calloc(1, (size_t)im->larg * im->alt);
    for (int y = 0; y < im->alt; y++) {
        const uint8_t *l = im->y + y * im->passo_y;
        r = fnv(r, l, im->larg);
        for (int x = 0; x < im->larg; x++) dif += abs(l[x] - anterior[y * im->larg + x]);
        memcpy(anterior + y * im->larg, l, (size_t)im->larg);
    }
    for (int y = 0; y < im->alt / 2; y++) r = fnv(r, im->u + y * im->passo_c, im->larg / 2);
    for (int y = 0; y < im->alt / 2; y++) r = fnv(r, im->v + y * im->passo_c, im->larg / 2);
    printf("q %d %08x %ld\n", saidos, r, dif * 100 / ((long)im->larg * im->alt));
    for (int a = 3; prefixo && a < argc; a++) {
        if (atoi(argv[a]) != saidos) continue;
        char nome[512];
        snprintf(nome, sizeof nome, "%s-%d.yuv", prefixo, saidos);
        FILE *f = fopen(nome, "wb");
        if (!f) { perror(nome); exit(2); }
        for (int y = 0; y < im->alt; y++) fwrite(im->y + y * im->passo_y, 1, (size_t)im->larg, f);
        for (int y = 0; y < im->alt / 2; y++) fwrite(im->u + y * im->passo_c, 1, (size_t)im->larg / 2, f);
        for (int y = 0; y < im->alt / 2; y++) fwrite(im->v + y * im->passo_c, 1, (size_t)im->larg / 2, f);
        fclose(f);
    }
    saidos++;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "uso: video_pc entrada.mp4 [prefixo [indices...]] | video_pc --conferir\n"); return 2; }
    if (!strcmp(argv[1], "--conferir")) {
        conferir_transformada();
        conferir_listas();
        conferir_intra8();
        return falhas ? 1 : 0;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *dados = malloc((size_t)tam + 16);
    if (fread(dados, 1, (size_t)tam, f) != (size_t)tam) { perror("ler"); return 2; }
    fclose(f);
    mp4_t m;
    if (mp4_ler(dados, (uint32_t)tam, &m) != 0 || !m.n_video) { printf("sem trilha de video\n"); return 1; }
    h264_t *h = h264_novo();
    int tam_len = 4;
    int rc = h264_config(h, m.avcc, (int)m.avcc_n, &tam_len);
    if (rc < 0) { printf("recusado %d %s\n", rc, h264_motivo(h) ? h264_motivo(h) : "-"); return 1; }
    const char *prefixo = argc > 2 ? argv[2] : NULL;
    h264_imagem_t im;
    int larg = 0, alt = 0, erros = 0;
    clock_t ini = clock();
    for (int i = 0; i < m.n_video; i++) {
        mp4_amostra_t *s = &m.video[i];
        if ((uint64_t)s->pos + s->tam > (uint64_t)tam) { printf("amostra %d fora do arquivo\n", i); return 1; }
        h264_amostra(h, dados + s->pos, (int)s->tam, tam_len);
        while (h264_imagem(h, &im)) { larg = im.larg; alt = im.alt; erros = im.erros; quadro(&im, prefixo, argc, argv); }
    }
    h264_esvaziar(h);
    while (h264_imagem(h, &im)) { larg = im.larg; alt = im.alt; erros = im.erros; quadro(&im, prefixo, argc, argv); }
    printf("fim %d %d %d %d %d %ld\n", larg, alt, m.n_video, saidos, erros, (long)((clock() - ini) * 1000 / CLOCKS_PER_SEC));
    printf("alto");
    for (int i = 0; i < 17; i++) printf(" %ld", h264_contagem[i]);
    printf("\n");
    h264_fim(h);
    return 0;
}
