/* mp3.c - decodificador de MP3 (MPEG-1 e MPEG-2 Audio Layer III), do zero.
 *
 * De onde veio o conhecimento: a norma ISO/IEC 11172-3 ("MPEG-1 áudio",
 * seções 2.4.1 a 2.4.3 e os anexos A e B) e, para as taxas de 16, 22,05 e
 * 24 kHz, a ISO/IEC 13818-3 ("MPEG-2 áudio", seção 2.4). O quadro Xing/Info,
 * que diz quantos quadros o arquivo tem, e as etiquetas ID3 não são da ISO:
 * são convenções públicas dos programas de MP3. Nenhum decodificador
 * existente foi consultado ou copiado (D047); as tabelas dos anexos da norma
 * estão em mp3_tabelas.h, como dados.
 *
 * COMO O MP3 GUARDA O SOM (o caminho de volta é o que este arquivo faz)
 *
 * O codificador corta o som em "grânulos" de 576 amostras e transforma cada
 * um em 576 "linhas de frequência": em vez de dizer quanto o alto-falante se
 * mexe a cada instante, diz quanto há de cada tom (grave, médio, agudo)
 * naquele pedacinho de tempo. A vantagem é que o ouvido não percebe erros
 * pequenos num tom quando há outro tom forte por perto; então cada linha é
 * guardada com pouca precisão (um número inteiro pequeno), e os números
 * pequenos ganham códigos curtos (Huffman). Um quadro de MP3 tem 2 grânulos
 * (1 no MPEG-2), para 1 ou 2 canais.
 *
 * Para voltar ao som, cada quadro passa por estas etapas, nesta ordem:
 *
 *   1. cabeçalho do quadro (2.4.1.3): taxa, canais, tamanho;
 *   2. informação lateral (2.4.1.7): como os dados do quadro estão arrumados;
 *   3. dados principais e a "reserva de bits" (2.4.2.7);
 *   4. fatores de escala: o volume de cada faixa de frequências;
 *   5. Huffman: os 576 números inteiros de cada grânulo;
 *   6. requantização (2.4.3.4): de inteiro para o valor de verdade;
 *   7. reordenação dos blocos curtos;
 *   8. estéreo conjunto: "meio/lado" (MS) e "intensidade";
 *   9. redução de "alias" entre as sub-bandas vizinhas;
 *  10. IMDCT com janela e soma sobreposta: de 18 linhas de frequência de
 *      cada uma das 32 sub-bandas para 18 amostras no tempo;
 *  11. inversão de frequência das sub-bandas ímpares;
 *  12. banco de filtros de síntese (2.4.3.2 e figura A.2): junta as 32
 *      sub-bandas em 32 amostras de som, 18 vezes por grânulo.
 *
 * As contas são em ponto flutuante (D042). Não há libc: o cosseno, a raiz
 * quadrada e a raiz cúbica usados para montar as tabelas estão aqui mesmo.
 */
#include "sagui.h"
#include "mp3.h"
#include "mp3_tabelas.h"

/* ====================================================================== */
/* Matemática mínima (só para montar tabelas, uma vez, ao abrir o 1º MP3)  */
/* ====================================================================== */

#define PI 3.14159265358979323846

/* Raiz quadrada pelo método de Newton: se r é um chute para a raiz de x,
 * a média entre r e x/r é um chute melhor (e dobra os dígitos certos a cada
 * volta). */
static double m_raiz(double x)
{
    if (x <= 0) return 0;
    double r = x > 1 ? x : 1;
    for (int i = 0; i < 64; i++) r = 0.5 * (r + x / r);
    return r;
}

/* Raiz cúbica, também por Newton: r <- r - (r^3 - x) / (3 r^2). */
static double m_raiz_cubica(double x)
{
    if (x <= 0) return 0;
    double r = x > 1 ? x : 1;
    for (int i = 0; i < 80; i++) r = r - (r * r * r - x) / (3 * r * r);
    return r;
}

/* Cosseno pela série de Taylor (1 - x²/2! + x⁴/4! - ...), depois de trazer o
 * ângulo para entre -pi e pi (o cosseno se repete a cada 2 pi). Com o ângulo
 * pequeno assim, 20 termos sobram para a precisão do double. */
static double m_cos(double x)
{
    double voltas = x / (2 * PI);
    int k = (int)(voltas + (voltas >= 0 ? 0.5 : -0.5));
    x -= k * (2 * PI);
    double x2 = x * x, termo = 1, soma = 1;
    for (int n = 1; n <= 20; n++) {
        termo *= -x2 / ((2 * n - 1) * (2 * n));
        soma += termo;
    }
    return soma;
}

static double m_sen(double x) { return m_cos(x - PI / 2); }

/* ====================================================================== */
/* Tabelas calculadas (pelas fórmulas da norma)                            */
/* ====================================================================== */

#define MAX_VALOR 8207          /* maior inteiro do Huffman: 15 + 2^13 - 1, mais um */
#define DESL_EXP  340           /* o expoente (em quartos) mais negativo possível */
#define N_EXP     392

static int   tabelas_prontas;
static float pot43[MAX_VALOR];  /* n^(4/3) */
static float pot2q[N_EXP];      /* 2^(e/4), para e de -340 a 51 */
static float cos36[36][18];     /* a IMDCT dos blocos longos (36 saídas, 18 entradas) */
static float cos12[12][6];      /* a IMDCT dos blocos curtos */
static float janela[4][36];     /* as 4 janelas da IMDCT (2.4.3.4.10.3) */
static float alias_cs[8], alias_ca[8];
static float sint_a[16][32];    /* a matriz do banco de síntese, linhas 0 a 15 */
static float sint_b[16][32];    /* idem, linhas 33 a 48 */
static float janela_sint[512];  /* D[i] já vezes 32768 (saída em 16 bits) */
static float int_esq[7], int_dir[7];   /* estéreo de intensidade do MPEG-1 */
static float raiz_meio;         /* 1/raiz(2), do estéreo meio/lado */

/* As árvores de Huffman. Cada tabela da norma vira uma árvore binária: a
 * partir da raiz, cada bit lido escolhe o filho 0 ou o filho 1, até chegar
 * a uma folha, que diz o par de valores. Um filho positivo é o número de
 * outro nó; um negativo é uma folha (-(símbolo+1)); o símbolo é x*16 + y
 * (ou v*8 + w*4 + x*2 + y nas tabelas da região count1). */
#define N_NOS 1500
static short arvore[N_NOS][2];
static int   n_nos;
static short raiz_tab[34];      /* nó raiz de cada tabela (-1: não existe; a 0 não tem bits) */

static void montar_arvore(int tabela, int lado, const unsigned char *c8, const unsigned short *c16,
                          const unsigned char *tam, int quadras)
{
    int raiz = n_nos++;
    arvore[raiz][0] = arvore[raiz][1] = 0;
    raiz_tab[tabela] = (short)raiz;
    int total = quadras ? 16 : lado * lado;
    for (int i = 0; i < total; i++) {
        unsigned cod = c8 ? c8[i] : c16[i];
        int simbolo = quadras ? i : ((i / lado) << 4) | (i % lado);
        int no = raiz;
        for (int b = tam[i] - 1; b >= 0; b--) {
            int bit = (cod >> b) & 1;
            if (b == 0) {
                arvore[no][bit] = (short)-(simbolo + 1);
            } else {
                if (arvore[no][bit] <= 0) {
                    if (n_nos >= N_NOS) return;
                    arvore[n_nos][0] = arvore[n_nos][1] = 0;
                    arvore[no][bit] = (short)n_nos++;
                }
                no = arvore[no][bit];
            }
        }
    }
}

static void montar_tabelas(void)
{
    if (tabelas_prontas) return;

    /* Requantização (2.4.3.4): o valor de verdade é o inteiro elevado a 4/3
     * (o codificador elevou a 3/4 para "apertar" os valores grandes). E
     * n^(4/3) = n vezes a raiz cúbica de n. */
    for (int i = 0; i < MAX_VALOR; i++) pot43[i] = (float)(i * m_raiz_cubica(i));

    /* Os ganhos são potências de 2 com expoente em quartos: 2^(e/4). A parte
     * inteira de e/4 é dobrar ou dividir por 2; a fração (0, 1/4, 2/4, 3/4)
     * sai da raiz quarta de 2. */
    double r4 = m_raiz(m_raiz(2.0));
    for (int i = 0; i < N_EXP; i++) {
        int e = i - DESL_EXP, q = e >> 2, resto = e & 3;      /* e = 4q + resto, resto de 0 a 3 */
        double v = 1;
        for (int k = 0; k < resto; k++) v *= r4;
        for (; q > 0; q--) v *= 2;
        for (; q < 0; q++) v *= 0.5;
        pot2q[i] = (float)v;
    }

    /* IMDCT (2.4.3.4.10.2): x[i] = soma em k de X[k] cos(pi/(2n) (2i+1+n/2)(2k+1)),
     * com n = 36 (blocos longos) ou n = 12 (curtos). */
    for (int i = 0; i < 36; i++)
        for (int k = 0; k < 18; k++) cos36[i][k] = (float)m_cos(PI / 72 * (2 * i + 1 + 18) * (2 * k + 1));
    for (int i = 0; i < 12; i++)
        for (int k = 0; k < 6; k++) cos12[i][k] = (float)m_cos(PI / 24 * (2 * i + 1 + 6) * (2 * k + 1));

    /* As janelas (2.4.3.4.10.3). Tipo 0, normal: meio arco de seno em 36
     * amostras. Tipo 2, curta: meio arco em 12. Tipos 1 e 3 fazem a passagem
     * entre as duas: 1 ("início") sobe como a longa e desce como a curta; 3
     * ("fim") é a 1 de trás para a frente. */
    for (int i = 0; i < 36; i++) {
        double longa = m_sen(PI / 36 * (i + 0.5));
        janela[0][i] = (float)longa;
        janela[1][i] = (float)(i < 18 ? longa : i < 24 ? 1.0 : i < 30 ? m_sen(PI / 12 * (i - 18 + 0.5)) : 0.0);
        janela[2][i] = (float)(i < 12 ? m_sen(PI / 12 * (i + 0.5)) : 0.0);
        janela[3][i] = (float)(i < 6 ? 0.0 : i < 12 ? m_sen(PI / 12 * (i - 6 + 0.5)) : i < 18 ? 1.0 : longa);
    }

    /* Redução de alias (2.4.3.4.10.1, tabela 3-B.9): a norma dá oito números
     * c[i]; os coeficientes são cs = 1/raiz(1+c²) e ca = c/raiz(1+c²). */
    static const double c[8] = {-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037};
    for (int i = 0; i < 8; i++) {
        double d = m_raiz(1 + c[i] * c[i]);
        alias_cs[i] = (float)(1 / d);
        alias_ca[i] = (float)(c[i] / d);
    }

    /* Banco de síntese (2.4.3.2): V[i] = soma em k de cos((16+i)(2k+1) pi/64) S[k],
     * com i de 0 a 63. Só 32 das 64 linhas precisam ser calculadas, porque o
     * cosseno se repete: chamando o ângulo de a = (16+i)(2k+1) pi/64,
     *   - a linha 32-i tem o ângulo (2k+1) pi - a, cujo cosseno é -cos(a):
     *     V[32-i] = -V[i] (e por isso V[16] = 0);
     *   - a linha 96-i tem o ângulo (2k+1) 2 pi - a, de cosseno igual:
     *     V[96-i] = V[i], ou seja, V[48+j] = V[48-j].
     * Então bastam as linhas 0 a 15 (sint_a) e 33 a 48 (sint_b). */
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 32; k++) {
            sint_a[i][k] = (float)m_cos((16 + i) * (2 * k + 1) * PI / 64);
            sint_b[i][k] = (float)m_cos((16 + 33 + i) * (2 * k + 1) * PI / 64);
        }

    /* A janela de síntese D[i]. A norma dá os 512 números; por trás deles há
     * um filtro simétrico h (h[512-i] = h[i]) com o sinal trocado nos blocos
     * ímpares de 64 (D[i] = -h[i] quando i/64 é ímpar). Disso sai a segunda
     * metade: D[512-i] = -D[i], menos nos múltiplos de 64, em que
     * D[512-i] = D[i]. O 32768 é para a saída já sair na escala dos 16 bits. */
    for (int i = 0; i <= 256; i++) janela_sint[i] = (float)(janela_d[i] / 65536.0 * 32768.0);
    for (int i = 1; i < 256; i++) janela_sint[512 - i] = (i % 64) ? -janela_sint[i] : janela_sint[i];

    /* Estéreo de intensidade do MPEG-1 (2.4.3.4.9.3): a razão entre os lados é
     * tan(is_pos pi/12); esquerdo = razão/(1+razão), direito = 1/(1+razão).
     * Com is_pos = 6 a tangente é infinita: tudo no esquerdo. */
    for (int i = 0; i < 6; i++) {
        double t = m_sen(i * PI / 12) / m_cos(i * PI / 12);
        int_esq[i] = (float)(t / (1 + t));
        int_dir[i] = (float)(1 / (1 + t));
    }
    int_esq[6] = 1;
    int_dir[6] = 0;
    raiz_meio = (float)m_raiz(0.5);

    /* As árvores de Huffman, a partir dos códigos da tabela 3-B.7. */
    n_nos = 0;
    for (int i = 0; i < 34; i++) raiz_tab[i] = -1;
    montar_arvore(1, 2, hc1, 0, ht1, 0);
    montar_arvore(2, 3, hc2, 0, ht2, 0);
    montar_arvore(3, 3, hc3, 0, ht3, 0);
    montar_arvore(5, 4, hc5, 0, ht5, 0);
    montar_arvore(6, 4, hc6, 0, ht6, 0);
    montar_arvore(7, 6, hc7, 0, ht7, 0);
    montar_arvore(8, 6, hc8, 0, ht8, 0);
    montar_arvore(9, 6, hc9, 0, ht9, 0);
    montar_arvore(10, 8, hc10, 0, ht10, 0);
    montar_arvore(11, 8, hc11, 0, ht11, 0);
    montar_arvore(12, 8, hc12, 0, ht12, 0);
    montar_arvore(13, 16, hc13, 0, ht13, 0);
    montar_arvore(15, 16, hc15, 0, ht15, 0);
    montar_arvore(16, 16, 0, hc16, ht16, 0);
    montar_arvore(24, 16, 0, hc24, ht24, 0);
    for (int i = 17; i < 24; i++) raiz_tab[i] = raiz_tab[16];
    for (int i = 25; i < 32; i++) raiz_tab[i] = raiz_tab[24];
    montar_arvore(32, 0, hcA, 0, htA, 1);
    montar_arvore(33, 0, hcB, 0, htB, 1);

    tabelas_prontas = 1;
}

/* ====================================================================== */
/* Estruturas                                                              */
/* ====================================================================== */

/* Leitor de bits: o MP3 não respeita fronteira de byte; os campos têm 1, 4,
 * 9, 12... bits, sempre com o bit mais alto primeiro. Depois do fim, lê zeros. */
typedef struct {
    const unsigned char *d;
    unsigned total;             /* bits disponíveis */
    unsigned pos;               /* próximo bit */
} bits_t;

static inline unsigned um_bit(bits_t *b)
{
    unsigned p = b->pos++;
    return p < b->total ? (b->d[p >> 3] >> (7 - (p & 7))) & 1u : 0u;
}

static unsigned pegar(bits_t *b, int n)
{
    unsigned v = 0;
    while (n-- > 0) v = (v << 1) | um_bit(b);
    return v;
}

/* O cabeçalho de 4 bytes de cada quadro (2.4.1.3). */
typedef struct {
    int v1;                     /* 1: MPEG-1 (2 grânulos); 0: MPEG-2 (1 grânulo, taxas baixas) */
    int ind_taxa;               /* linha das tabelas de faixas: 0 a 2 MPEG-1, 3 a 5 MPEG-2 */
    unsigned taxa;
    int crc;                    /* há 2 bytes de conferência depois do cabeçalho */
    int modo;                   /* 0 estéreo, 1 estéreo conjunto, 2 dois canais, 3 mono */
    int ext;                    /* no estéreo conjunto: bit 1 = meio/lado, bit 0 = intensidade */
    int canais;
    int tam;                    /* tamanho do quadro inteiro, em bytes */
    int tam_lateral;            /* tamanho da informação lateral */
} cab_t;

/* A informação lateral de um grânulo de um canal (2.4.1.7), com os nomes da
 * norma entre parênteses. */
typedef struct {
    int bits;                   /* (part2_3_length) bits de fatores de escala + Huffman */
    int grandes;                /* (big_values) pares de valores "grandes" */
    int ganho;                  /* (global_gain) */
    int compressao;             /* (scalefac_compress) */
    int tipo;                   /* (block_type) 0 normal, 1 início, 2 curtos, 3 fim */
    int troca;                  /* (window_switching_flag) */
    int misto;                  /* (mixed_block_flag) */
    int tabela[3];              /* (table_select) a tabela de Huffman de cada região */
    int ganho_sub[3];           /* (subblock_gain) */
    int regiao0, regiao1;       /* (region0_count, region1_count) */
    int preflag;
    int escala;                 /* (scalefac_scale) */
    int tab_count1;             /* (count1table_select) */
} granulo_t;

/* O que cada canal guarda de um quadro para o outro. */
typedef struct {
    float sobra[32][18];        /* a segunda metade da IMDCT, que soma com o próximo grânulo */
    float v[1024];              /* o vetor V do banco de síntese (os 16 últimos blocos de 64) */
    int   vpos;
    unsigned char fe_l[23];     /* fatores de escala das faixas longas */
    unsigned char fe_s[13][3];  /* e das curtas, por janela */
    unsigned char lim_l[23];    /* MPEG-2: o valor "ilegal" da posição de intensidade */
    unsigned char lim_s[13][3];
} canal_t;

#define TAM_RESERVA 2048        /* 511 bytes guardados + o maior quadro (1441 bytes) */
#define AQUECIMENTO 10          /* quadros decodificados "no escuro" depois de um pulo */

struct mp3_s {
    const unsigned char *dados;
    unsigned tam;               /* sem a etiqueta ID3v1 do fim */
    unsigned inicio;            /* onde está o primeiro quadro de som */
    unsigned pos;               /* o próximo quadro a decodificar */
    unsigned taxa, canais, amostras_quadro, total_quadros;
    unsigned char reserva[TAM_RESERVA];
    int res_n;
    int escala_int;             /* MPEG-2: intensity_scale do quadro atual */
    unsigned descartar;         /* quadros a decodificar e jogar fora (depois de um pulo) */
    canal_t canal[2];
    int   inteiros[576];        /* a saída do Huffman */
    float xr[2][576];           /* as linhas de frequência de cada canal */
    float tmp[576];
    float tempo[32][18];        /* a saída da IMDCT: 18 amostras de cada sub-banda */
    short pcm[1152 * 2];        /* o som pronto do quadro atual */
    int pcm_n, pcm_pos;
};

static void zerar(void *p, unsigned n)
{
    unsigned char *q = p;
    while (n--) *q++ = 0;
}

/* ====================================================================== */
/* 1. Cabeçalho do quadro (2.4.1.3)                                        */
/* ====================================================================== */

/* Os 32 bits do cabeçalho: 11 bits "1" de sincronismo; 2 bits de versão (11
 * MPEG-1, 10 MPEG-2); 2 de camada (01 é a Layer III); 1 de proteção (0 = há
 * CRC); 4 de taxa de bits (índice numa tabela); 2 de taxa de amostragem; 1 de
 * enchimento (o quadro tem 1 byte a mais); 1 livre; 2 de modo; 2 de extensão
 * do modo; e 4 que não mudam o som (direitos, original, ênfase).
 * Devolve 0 se os 4 bytes não são um cabeçalho que este decodificador toca
 * (o "formato livre", de taxa 0, e o MPEG-2.5 ficam de fora). */
static int ler_cabecalho(const unsigned char *p, cab_t *c)
{
    static const unsigned short kbps1[15] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const unsigned short kbps2[15] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static const unsigned taxas[3] = {44100, 48000, 32000};
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return 0;
    int versao = (p[1] >> 3) & 3, camada = (p[1] >> 1) & 3;
    if (versao < 2 || camada != 1) return 0;
    int ib = p[2] >> 4, it = (p[2] >> 2) & 3;
    if (ib == 0 || ib == 15 || it == 3) return 0;
    c->v1 = versao == 3;
    c->ind_taxa = c->v1 ? it : it + 3;
    c->taxa = c->v1 ? taxas[it] : taxas[it] / 2;
    c->crc = !(p[1] & 1);
    c->modo = p[3] >> 6;
    c->ext = (p[3] >> 4) & 3;
    c->canais = c->modo == 3 ? 1 : 2;
    /* Um quadro tem 1152 amostras (576 no MPEG-2); com a taxa de bits fixa no
     * quadro, o tamanho em bytes é 1152/8 * bits por segundo / taxa. */
    unsigned kbps = c->v1 ? kbps1[ib] : kbps2[ib];
    c->tam = (int)((c->v1 ? 144000u : 72000u) * kbps / c->taxa) + ((p[2] >> 1) & 1);
    c->tam_lateral = c->v1 ? (c->canais == 1 ? 17 : 32) : (c->canais == 1 ? 9 : 17);
    return 1;
}

/* Procura o próximo quadro a partir de pos. Dados que não são som (etiquetas,
 * lixo) podem ter, por acaso, bytes parecidos com um cabeçalho; por isso só
 * vale o cabeçalho que é seguido por outro, igual na versão e na taxa, bem
 * onde o quadro termina (ou que termina junto com o arquivo). */
static int achar_quadro(const mp3_t *m, unsigned pos, cab_t *c)
{
    for (; pos + 4 <= m->tam; pos++) {
        if (m->dados[pos] != 0xFF || !ler_cabecalho(m->dados + pos, c)) continue;
        unsigned fim = pos + (unsigned)c->tam;
        if (fim > m->tam) continue;
        if (fim + 4 > m->tam) return (int)pos;
        cab_t prox;
        if (ler_cabecalho(m->dados + fim, &prox) && prox.taxa == c->taxa) return (int)pos;
    }
    return -1;
}

/* O quadro que começa em *pos (ou o primeiro depois dele, se ali não há um). */
static int proximo_quadro(const mp3_t *m, unsigned *pos, cab_t *c)
{
    if (*pos + 4 > m->tam) return 0;
    if (ler_cabecalho(m->dados + *pos, c) && *pos + (unsigned)c->tam <= m->tam) return 1;
    int p = achar_quadro(m, *pos + 1, c);
    if (p < 0) return 0;
    *pos = (unsigned)p;
    return 1;
}

/* ====================================================================== */
/* 2. Informação lateral (2.4.1.7; MPEG-2: 13818-3, 2.4.1.7)               */
/* ====================================================================== */

/* Vem logo depois do cabeçalho e diz como ler os dados principais: onde eles
 * começam (main_data_begin), quais fatores de escala o 2º grânulo reaproveita
 * do 1º (scfsi) e, para cada grânulo de cada canal, os campos de granulo_t. */
static void ler_lateral(const cab_t *c, const unsigned char *p, int *inicio_dados, int scfsi[2][4], granulo_t g[2][2])
{
    bits_t b = {p, (unsigned)c->tam_lateral * 8, 0};
    int ngr = c->v1 ? 2 : 1;
    if (c->v1) {
        *inicio_dados = (int)pegar(&b, 9);
        pegar(&b, c->canais == 1 ? 5 : 3);                  /* bits privados */
        for (int ch = 0; ch < c->canais; ch++)
            for (int i = 0; i < 4; i++) scfsi[ch][i] = (int)pegar(&b, 1);
    } else {
        *inicio_dados = (int)pegar(&b, 8);
        pegar(&b, c->canais == 1 ? 1 : 2);
    }
    for (int gr = 0; gr < ngr; gr++)
        for (int ch = 0; ch < c->canais; ch++) {
            granulo_t *q = &g[gr][ch];
            q->bits = (int)pegar(&b, 12);
            q->grandes = (int)pegar(&b, 9);
            q->ganho = (int)pegar(&b, 8);
            q->compressao = (int)pegar(&b, c->v1 ? 4 : 9);
            q->troca = (int)pegar(&b, 1);
            q->tipo = 0;
            q->misto = 0;
            q->ganho_sub[0] = q->ganho_sub[1] = q->ganho_sub[2] = 0;
            if (q->troca) {
                /* Bloco especial: só duas regiões de Huffman, de limites fixos. */
                q->tipo = (int)pegar(&b, 2);
                q->misto = (int)pegar(&b, 1);
                q->tabela[0] = (int)pegar(&b, 5);
                q->tabela[1] = (int)pegar(&b, 5);
                q->tabela[2] = 0;
                for (int i = 0; i < 3; i++) q->ganho_sub[i] = (int)pegar(&b, 3);
                q->regiao0 = (q->tipo == 2 && !q->misto) ? 8 : 7;
                q->regiao1 = 36;
            } else {
                for (int i = 0; i < 3; i++) q->tabela[i] = (int)pegar(&b, 5);
                q->regiao0 = (int)pegar(&b, 4);
                q->regiao1 = (int)pegar(&b, 3);
            }
            q->preflag = c->v1 ? (int)pegar(&b, 1) : 0;     /* no MPEG-2 sai do scalefac_compress */
            q->escala = (int)pegar(&b, 1);
            q->tab_count1 = (int)pegar(&b, 1);
        }
}

/* ====================================================================== */
/* 4. Fatores de escala (2.4.2.7; MPEG-2: 13818-3, 2.4.3.2)                */
/* ====================================================================== */

/* Cada faixa de frequências tem um "fator de escala": um ganho próprio, que
 * o codificador usa para pôr mais precisão onde o ouvido nota mais.
 *
 * MPEG-1: scalefac_compress escolhe dois tamanhos em bits (slen1 para as
 * faixas graves, slen2 para as agudas). Nos blocos longos são 21 fatores, em
 * 4 grupos (faixas 0-5, 6-10, 11-15, 16-20); no 2º grânulo, um grupo com o
 * bit scfsi ligado não é transmitido: vale o do 1º grânulo. Nos blocos
 * curtos são 12 faixas vezes 3 janelas. */
static void fatores_mpeg1(canal_t *c, bits_t *b, const granulo_t *g, int gr, const int *scfsi)
{
    int s1 = slen_tab[0][g->compressao], s2 = slen_tab[1][g->compressao];
    if (g->tipo == 2) {
        int primeira = 0;
        if (g->misto) {
            for (int f = 0; f < 8; f++) c->fe_l[f] = (unsigned char)pegar(b, s1);
            primeira = 3;
        }
        for (int f = primeira; f < 12; f++)
            for (int w = 0; w < 3; w++) c->fe_s[f][w] = (unsigned char)pegar(b, f < 6 ? s1 : s2);
        c->fe_s[12][0] = c->fe_s[12][1] = c->fe_s[12][2] = 0;
    } else {
        static const unsigned char grupo[5] = {0, 6, 11, 16, 21};
        for (int k = 0; k < 4; k++) {
            if (gr == 1 && scfsi[k]) continue;
            for (int f = grupo[k]; f < grupo[k + 1]; f++) c->fe_l[f] = (unsigned char)pegar(b, k < 2 ? s1 : s2);
        }
        c->fe_l[21] = c->fe_l[22] = 0;
    }
    for (int f = 0; f < 23; f++) c->lim_l[f] = 7;
    for (int f = 0; f < 13; f++) c->lim_s[f][0] = c->lim_s[f][1] = c->lim_s[f][2] = 7;
}

/* MPEG-2: os 9 bits de scalefac_compress dão quatro tamanhos (slen1 a slen4)
 * por contas de divisão e resto, e uma linha da tabela nr_fatores diz
 * quantos fatores têm cada tamanho. O canal direito de um quadro com estéreo
 * de intensidade usa outra conta (ali os "fatores" são posições de
 * intensidade, e o bit mais baixo é o intensity_scale). */
static void fatores_mpeg2(mp3_t *m, canal_t *c, bits_t *b, granulo_t *g, int intensidade)
{
    int sfc = g->compressao, slen[4] = {0, 0, 0, 0}, linha;
    g->preflag = 0;
    if (!intensidade) {
        if (sfc < 400) {
            slen[0] = (sfc >> 4) / 5; slen[1] = (sfc >> 4) % 5; slen[2] = (sfc & 15) >> 2; slen[3] = sfc & 3;
            linha = 0;
        } else if (sfc < 500) {
            sfc -= 400;
            slen[0] = (sfc >> 2) / 5; slen[1] = (sfc >> 2) % 5; slen[2] = sfc & 3;
            linha = 1;
        } else {
            sfc -= 500;
            slen[0] = sfc / 3; slen[1] = sfc % 3;
            linha = 2;
            g->preflag = 1;
        }
    } else {
        m->escala_int = sfc & 1;
        sfc >>= 1;
        if (sfc < 180) {
            slen[0] = sfc / 36; slen[1] = (sfc % 36) / 6; slen[2] = sfc % 6;
            linha = 3;
        } else if (sfc < 244) {
            sfc -= 180;
            slen[0] = (sfc & 63) >> 4; slen[1] = (sfc & 15) >> 2; slen[2] = sfc & 3;
            linha = 4;
        } else {
            sfc -= 244;
            slen[0] = sfc / 3; slen[1] = sfc % 3;
            linha = 5;
        }
    }
    int forma = g->tipo == 2 ? (g->misto ? 2 : 1) : 0;
    const unsigned char *quantos = nr_fatores[linha][forma];
    unsigned char valor[48], limite[48];
    int n = 0;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < quantos[k] && n < 48; i++, n++) {
            valor[n] = (unsigned char)pegar(b, slen[k]);
            limite[n] = (unsigned char)((1 << slen[k]) - 1);
        }
    for (; n < 48; n++) valor[n] = limite[n] = 0;
    for (int f = 0; f < 23; f++) c->fe_l[f] = c->lim_l[f] = 0;
    for (int f = 0; f < 13; f++)
        for (int w = 0; w < 3; w++) c->fe_s[f][w] = c->lim_s[f][w] = 0;
    if (forma == 0) {
        for (int f = 0; f < 22; f++) { c->fe_l[f] = valor[f]; c->lim_l[f] = limite[f]; }
    } else {
        int k = 0, primeira = 0;
        if (forma == 2) {
            for (; k < 6; k++) { c->fe_l[k] = valor[k]; c->lim_l[k] = limite[k]; }
            primeira = 3;
        }
        for (int f = primeira; f < 12; f++)
            for (int w = 0; w < 3; w++, k++) { c->fe_s[f][w] = valor[k]; c->lim_s[f][w] = limite[k]; }
    }
}

/* ====================================================================== */
/* 5. Huffman (2.4.2.7 e tabela 3-B.7)                                     */
/* ====================================================================== */

static inline int descer(bits_t *b, int no)
{
    for (;;) {
        int filho = arvore[no][um_bit(b)];
        if (filho < 0) return -filho - 1;
        if (filho == 0) return 0;           /* não acontece: os códigos são completos */
        no = filho;
    }
}

/* As 576 linhas vêm das mais graves para as mais agudas, em três partes:
 *
 *   - "valores grandes" (big_values pares): cada par (x, y) tem um código de
 *     Huffman. Esta parte é dividida em até 3 regiões, cada uma com a tabela
 *     que o codificador achou melhor. Nas tabelas 16 a 31, o valor 15 quer
 *     dizer "15 ou mais": seguem "linbits" bits com o quanto a mais. Depois
 *     de cada valor diferente de zero vem 1 bit de sinal.
 *   - "count1": quadras (v, w, x, y) em que cada valor é 0, 1 ou -1, com uma
 *     de duas tabelas, até acabarem os bits do grânulo;
 *   - o resto (os agudos) é zero e não gasta bit nenhum.
 */
static void huffman(bits_t *b, const granulo_t *g, const cab_t *c, unsigned fim, int *is)
{
    const unsigned short *fl = faixas_longas[c->ind_taxa];
    int grandes = g->grandes * 2;
    if (grandes > 576) grandes = 576;
    /* Os limites das regiões são dados em faixas de fator de escala. Num bloco
     * com troca de janela não há região 2, e a região 0 vai até a faixa 8
     * (blocos curtos: 9 faixas de janela, isto é, 3 faixas curtas inteiras,
     * 36 linhas). */
    int r1, r2;
    if (g->troca) {
        r1 = (g->tipo == 2 && !g->misto) ? 36 : fl[8];
        r2 = 576;
    } else {
        int a = g->regiao0 + 1, z = g->regiao0 + g->regiao1 + 2;
        r1 = fl[a > 22 ? 22 : a];
        r2 = fl[z > 22 ? 22 : z];
    }
    if (r1 > grandes) r1 = grandes;
    if (r2 > grandes) r2 = grandes;

    int linha = 0;
    for (int regiao = 0; regiao < 3; regiao++) {
        int ate = regiao == 0 ? r1 : regiao == 1 ? r2 : grandes;
        int t = g->tabela[regiao], raiz = raiz_tab[t];
        int extras = t >= 16 ? linbits_tab[t - 16] : 0;
        while (linha < ate) {
            int x = 0, y = 0;
            if (raiz >= 0 && b->pos < fim) {
                int s = descer(b, raiz);
                x = s >> 4;
                y = s & 15;
                if (extras && x == 15) x += (int)pegar(b, extras);
                if (x && um_bit(b)) x = -x;
                if (extras && y == 15) y += (int)pegar(b, extras);
                if (y && um_bit(b)) y = -y;
            }
            is[linha++] = x;
            is[linha++] = y;
        }
    }
    int raiz = raiz_tab[32 + g->tab_count1];
    while (linha <= 572 && b->pos < fim) {
        int s = descer(b, raiz), q[4];
        for (int i = 0; i < 4; i++) {
            q[i] = (s >> (3 - i)) & 1;
            if (q[i] && um_bit(b)) q[i] = -1;
        }
        /* Uma quadra que passa do fim dos bits do grânulo é enchimento, não som. */
        if (b->pos > fim) break;
        for (int i = 0; i < 4; i++) is[linha++] = q[i];
    }
    while (linha < 576) is[linha++] = 0;
}

/* ====================================================================== */
/* 6. Requantização (2.4.3.4)                                              */
/* ====================================================================== */

/* De inteiro para valor de verdade:
 *
 *   xr = sinal * |is|^(4/3) * 2^((ganho_global - 210) / 4) * 2^(-(mult * fator))
 *
 * em que "fator" é o fator de escala da faixa (mais a pré-ênfase, se preflag)
 * e mult é 1/2 ou 1 (scalefac_scale). Nos blocos curtos cada janela ainda tem
 * o seu ganho (8 * subblock_gain a menos no expoente). Tudo isso é um único
 * expoente em quartos, e, que escolhe uma entrada de pot2q. */
static inline void converter(const int *is, float *xr, int de, int ate, int e)
{
    e += DESL_EXP;
    if (e < 0) e = 0;
    if (e >= N_EXP) e = N_EXP - 1;
    float ganho = pot2q[e];
    for (int i = de; i < ate; i++) {
        int v = is[i];
        xr[i] = v == 0 ? 0.0f : v > 0 ? pot43[v] * ganho : -pot43[-v] * ganho;
    }
}

static void requantizar(const canal_t *c, const granulo_t *g, const cab_t *cab, const int *is, float *xr)
{
    const unsigned short *fl = faixas_longas[cab->ind_taxa];
    const unsigned char *fc = faixas_curtas[cab->ind_taxa];
    int base = g->ganho - 210, mult = g->escala ? 4 : 2;
    if (g->tipo == 2) {
        /* Blocos curtos: as linhas vêm por faixa, depois por janela. Num
         * bloco misto, as 36 primeiras linhas são de faixas longas. */
        int i = 0, primeira = 0;
        if (g->misto) {
            for (int f = 0; fl[f] < 36; f++) converter(is, xr, fl[f], fl[f + 1], base - mult * c->fe_l[f]);
            i = 36;
            primeira = 3;
        }
        for (int f = primeira; f < 13; f++) {
            int larg = fc[f + 1] - fc[f];
            for (int w = 0; w < 3; w++, i += larg)
                converter(is, xr, i, i + larg, base - 8 * g->ganho_sub[w] - mult * c->fe_s[f][w]);
        }
    } else {
        for (int f = 0; f < 22; f++)
            converter(is, xr, fl[f], fl[f + 1], base - mult * (c->fe_l[f] + (g->preflag ? pretab[f] : 0)));
    }
}

/* ====================================================================== */
/* 7. Reordenação dos blocos curtos (2.4.3.4.8)                            */
/* ====================================================================== */

/* Num bloco curto há 3 transformadas pequenas (3 "janelas" no tempo), de 192
 * linhas cada. No arquivo, as linhas vêm por faixa e, dentro da faixa, a
 * janela 0 inteira, depois a 1, depois a 2. Para a IMDCT, elas precisam
 * estar por frequência: linha 0 das três janelas, linha 1 das três... */
static void reordenar(mp3_t *m, const granulo_t *g, const cab_t *cab, float *xr)
{
    const unsigned char *fc = faixas_curtas[cab->ind_taxa];
    int primeira = g->misto ? 3 : 0, i = g->misto ? 36 : 0, de = i;
    for (int f = primeira; f < 13; f++) {
        int larg = fc[f + 1] - fc[f], base = 3 * fc[f];
        for (int w = 0; w < 3; w++)
            for (int k = 0; k < larg; k++) m->tmp[base + 3 * k + w] = xr[i++];
    }
    for (i = de; i < 576; i++) xr[i] = m->tmp[i];
}

/* ====================================================================== */
/* 8. Estéreo conjunto (2.4.3.4.9; MPEG-2: 13818-3, 2.4.3.2)               */
/* ====================================================================== */

static int faixa_zerada(const float *x, int ini, int passo, int n)
{
    for (int k = 0; k < n; k++)
        if (x[ini + passo * k] != 0.0f) return 0;
    return 1;
}

/* Dois truques para gastar menos bits quando os dois lados são parecidos:
 *
 *   - meio/lado (MS): em vez de esquerdo e direito, o arquivo traz
 *     M = (E + D)/raiz(2) e S = (E - D)/raiz(2). A volta é a mesma conta:
 *     E = (M + S)/raiz(2), D = (M - S)/raiz(2).
 *   - intensidade: nos agudos (onde o ouvido só percebe de que lado o som
 *     vem pelo volume), o arquivo traz um canal só, no esquerdo, e no lugar
 *     do fator de escala do direito uma "posição" (is_pos) que diz como
 *     dividir esse som entre os lados. Vale para as faixas acima da última
 *     em que o direito tem algum valor diferente de zero; uma posição
 *     "ilegal" (7 no MPEG-1) diz que a faixa não usa intensidade.
 *
 * Nos blocos curtos a conta do "acima da última faixa com valor" é feita
 * para cada janela separadamente. A última faixa (que não tem fator de
 * escala) usa a posição da faixa anterior.
 */
static void estereo(mp3_t *m, const cab_t *cab, const granulo_t *g)
{
    float *e = m->xr[0], *d = m->xr[1];
    int ms = cab->ext & 2, intens = cab->ext & 1;
    if (!intens) {
        if (!ms) return;
        for (int i = 0; i < 576; i++) {
            float a = e[i], b = d[i];
            e[i] = (a + b) * raiz_meio;
            d[i] = (a - b) * raiz_meio;
        }
        return;
    }

    /* A lista das faixas do canal direito, já na ordem depois da reordenação:
     * início, passo entre as linhas (3 nas janelas curtas), quantidade,
     * classe (0 = longas; 1 a 3 = janela curta), posição e seu valor ilegal. */
    const unsigned short *fl = faixas_longas[cab->ind_taxa];
    const unsigned char *fc = faixas_curtas[cab->ind_taxa];
    const canal_t *c = &m->canal[1];
    struct { short ini, n; unsigned char passo, classe, pos, lim; } fx[48];
    int nf = 0;
    if (g->tipo != 2 || g->misto) {
        for (int f = 0; f < 22 && (g->tipo != 2 || fl[f] < 36); f++, nf++) {
            int q = f < 21 ? f : 20;
            fx[nf].ini = (short)fl[f]; fx[nf].n = (short)(fl[f + 1] - fl[f]); fx[nf].passo = 1; fx[nf].classe = 0;
            fx[nf].pos = c->fe_l[q]; fx[nf].lim = c->lim_l[q];
        }
    }
    if (g->tipo == 2) {
        for (int f = g->misto ? 3 : 0; f < 13; f++)
            for (int w = 0; w < 3; w++, nf++) {
                int q = f < 12 ? f : 11;
                fx[nf].ini = (short)(3 * fc[f] + w); fx[nf].n = (short)(fc[f + 1] - fc[f]); fx[nf].passo = 3;
                fx[nf].classe = (unsigned char)(1 + w);
                fx[nf].pos = c->fe_s[q][w]; fx[nf].lim = c->lim_s[q][w];
            }
    }
    /* Das faixas mais agudas para as mais graves: enquanto o direito for só
     * zeros naquela classe, a faixa é candidata à intensidade. Num bloco
     * misto, as faixas longas (as mais graves) só são candidatas se todas as
     * curtas, acima delas, também forem. */
    int livre[4] = {1, 1, 1, 1};
    double i0 = m->escala_int ? 0.70710678118654752 : 0.84089641525371454;   /* 2^(-1/2) e 2^(-1/4) */
    for (int k = nf - 1; k >= 0; k--) {
        int cl = fx[k].classe, ini = fx[k].ini, passo = fx[k].passo, n = fx[k].n;
        if (cl == 0 && g->tipo == 2 && !(livre[1] && livre[2] && livre[3])) livre[0] = 0;
        if (!faixa_zerada(d, ini, passo, n)) livre[cl] = 0;
        int pos = fx[k].pos;
        int legal = cab->v1 ? pos < 7 : pos != fx[k].lim;
        if (livre[cl] && legal) {
            float ke, kd;
            if (cab->v1) {
                ke = int_esq[pos];
                kd = int_dir[pos];
            } else {
                /* MPEG-2: posição ímpar abaixa o esquerdo, par abaixa o
                 * direito, em passos de i0 (2^(-1/4) ou 2^(-1/2)). */
                double p = 1;
                for (int j = 0; j < (pos + 1) / 2; j++) p *= i0;
                ke = (pos & 1) ? (float)p : 1.0f;
                kd = (pos & 1) ? 1.0f : (float)p;
            }
            for (int j = 0; j < n; j++) {
                float a = e[ini + passo * j];
                e[ini + passo * j] = a * ke;
                d[ini + passo * j] = a * kd;
            }
        } else if (ms) {
            for (int j = 0; j < n; j++) {
                float a = e[ini + passo * j], b = d[ini + passo * j];
                e[ini + passo * j] = (a + b) * raiz_meio;
                d[ini + passo * j] = (a - b) * raiz_meio;
            }
        }
    }
}

/* ====================================================================== */
/* 9 a 11. Alias, IMDCT com janela e soma sobreposta, inversão             */
/* ====================================================================== */

/* As 576 linhas são 32 sub-bandas de 18 linhas. O filtro que separou as
 * sub-bandas no codificador não é perfeito: um pouco de cada sub-banda
 * "vaza" para a vizinha (o "alias"). O codificador compensa isso misturando
 * as 8 linhas de cada lado de cada fronteira, e aqui a mistura é desfeita
 * (as "borboletas" da figura A.5). Blocos curtos não têm essa etapa; num
 * bloco misto, só a fronteira entre as duas sub-bandas longas. */
static void reduzir_alias(const granulo_t *g, float *xr)
{
    int fronteiras = g->tipo == 2 ? (g->misto ? 1 : 0) : 31;
    for (int sb = 1; sb <= fronteiras; sb++)
        for (int i = 0; i < 8; i++) {
            float baixo = xr[18 * sb - 1 - i], alto = xr[18 * sb + i];
            xr[18 * sb - 1 - i] = baixo * alias_cs[i] - alto * alias_ca[i];
            xr[18 * sb + i] = alto * alias_cs[i] + baixo * alias_ca[i];
        }
}

/* A IMDCT transforma as 18 linhas de uma sub-banda em 36 amostras no tempo
 * (ou, nos blocos curtos, 6 linhas em 12 amostras, três vezes). Sai o dobro
 * do que entrou porque os blocos se sobrepõem pela metade: as 18 primeiras
 * amostras somam com as 18 últimas do grânulo anterior (guardadas em
 * "sobra") e as 18 últimas ficam para o próximo. A janela (uma curva que
 * começa e termina em zero) faz a emenda ser suave. As três transformadas
 * curtas ficam nas posições 6, 12 e 18 das 36 amostras, também sobrepostas.
 *
 * No fim, a inversão de frequência (2.4.3.4.10.4): nas sub-bandas ímpares,
 * as amostras ímpares trocam de sinal (o banco de filtros "espelha" as
 * frequências dessas sub-bandas, e isso desfaz o espelho). */
static void imdct(mp3_t *m, canal_t *c, const granulo_t *g, const float *xr)
{
    /* Sub-bandas acima da última linha diferente de zero só têm a sobra. */
    int ultima = 575;
    while (ultima >= 0 && xr[ultima] == 0.0f) ultima--;
    int usadas = ultima < 0 ? 0 : ultima / 18 + 1;

    for (int sb = 0; sb < 32; sb++) {
        float z[36];
        float *sobra = c->sobra[sb], *saida = m->tempo[sb];
        const float *x = xr + 18 * sb;
        if (sb >= usadas) {
            for (int i = 0; i < 18; i++) { saida[i] = sobra[i]; sobra[i] = 0; }
        } else {
            int tipo = (g->tipo == 2 && g->misto && sb < 2) ? 0 : g->tipo;
            if (tipo != 2) {
                const float *jan = janela[tipo];
                for (int i = 0; i < 36; i++) {
                    const float *co = cos36[i];
                    float s = 0;
                    for (int k = 0; k < 18; k++) s += x[k] * co[k];
                    z[i] = s * jan[i];
                }
            } else {
                for (int i = 0; i < 36; i++) z[i] = 0;
                for (int w = 0; w < 3; w++)
                    for (int i = 0; i < 12; i++) {
                        const float *co = cos12[i];
                        float s = 0;
                        for (int k = 0; k < 6; k++) s += x[3 * k + w] * co[k];
                        z[6 + 6 * w + i] += s * janela[2][i];
                    }
            }
            for (int i = 0; i < 18; i++) { saida[i] = z[i] + sobra[i]; sobra[i] = z[18 + i]; }
        }
        if (sb & 1)
            for (int i = 1; i < 18; i += 2) saida[i] = -saida[i];
    }
}

/* ====================================================================== */
/* 12. Banco de filtros de síntese (2.4.3.2, figura A.2)                   */
/* ====================================================================== */

/* Entram 32 amostras, uma de cada sub-banda (S[k]); saem 32 amostras de som.
 * A receita da norma:
 *   1. o vetor V (1024 valores) anda 64 posições, e as 64 primeiras recebem
 *      V[i] = soma em k de cos((16+i)(2k+1) pi/64) S[k];
 *   2. monta-se U com 512 valores de V: de cada 128, os 32 primeiros e os 32
 *      que começam na posição 96;
 *   3. U é multiplicado pela janela D, e cada amostra de saída j é a soma dos
 *      16 produtos nas posições j, j+32, j+64...
 * Em vez de andar com os 1024 valores a cada vez, "vpos" diz onde o V começa
 * e a memória é usada em círculo. */
static void sintese(canal_t *c, const float *s, short *saida)
{
    c->vpos = (c->vpos - 64) & 1023;
    float *v = c->v + c->vpos;
    for (int i = 0; i < 16; i++) {
        const float *a = sint_a[i], *b = sint_b[i];
        float sa = 0, sb = 0;
        for (int k = 0; k < 32; k++) { sa += a[k] * s[k]; sb += b[k] * s[k]; }
        v[i] = sa;
        v[32 - i] = -sa;
        v[33 + i] = sb;
        v[63 - i] = sb;
    }
    v[16] = 0;
    for (int j = 0; j < 32; j++) {
        float soma = 0;
        for (int i = 0; i < 8; i++) {
            soma += c->v[(c->vpos + 128 * i + j) & 1023] * janela_sint[64 * i + j];
            soma += c->v[(c->vpos + 128 * i + 96 + j) & 1023] * janela_sint[64 * i + 32 + j];
        }
        int a = (int)(soma + (soma >= 0 ? 0.5f : -0.5f));
        if (a > 32767) a = 32767;
        if (a < -32768) a = -32768;
        saida[2 * j] = (short)a;
    }
}

/* ====================================================================== */
/* Um quadro inteiro                                                       */
/* ====================================================================== */

/* Decodifica o próximo quadro para m->pcm. Devolve 0 no fim do arquivo. */
static int decodificar_quadro(mp3_t *m)
{
    cab_t cab;
    unsigned pos = m->pos;
    if (!proximo_quadro(m, &pos, &cab)) return 0;
    const unsigned char *p = m->dados + pos;
    m->pos = pos + (unsigned)cab.tam;

    int ngr = cab.v1 ? 2 : 1;
    m->pcm_n = ngr * 576;
    m->pcm_pos = 0;
    int cabecalho = 4 + (cab.crc ? 2 : 0);
    int n_dados = cab.tam - cabecalho - cab.tam_lateral;
    if (n_dados < 0) {
        zerar(m->pcm, sizeof(m->pcm));
        return 1;
    }
    int inicio_dados, scfsi[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    granulo_t g[2][2];
    ler_lateral(&cab, p + cabecalho, &inicio_dados, scfsi, g);

    /* 3. A reserva de bits (2.4.2.7). O quadro tem tamanho fixo, mas um trecho
     * difícil da música precisa de mais bits que um fácil. Então os dados de
     * um quadro podem começar ANTES dele, no espaço que sobrou nos quadros
     * anteriores: main_data_begin diz quantos bytes antes (até 511). Para
     * isso, os dados principais de todos os quadros (sem cabeçalhos nem
     * informação lateral) são vistos como uma fita contínua; "reserva" guarda
     * o fim dessa fita. Logo depois de abrir ou de pular, os bytes de antes
     * não existem: o quadro sai mudo. */
    int inicio = m->res_n - inicio_dados;
    int corte = m->res_n - 511;
    if (corte > 0) {
        for (int i = 0; i < 511; i++) m->reserva[i] = m->reserva[i + corte];
        m->res_n = 511;
        inicio -= corte;
    }
    for (int i = 0; i < n_dados; i++) m->reserva[m->res_n + i] = p[cabecalho + cab.tam_lateral + i];
    m->res_n += n_dados;
    if (inicio < 0) {
        zerar(m->pcm, sizeof(m->pcm));
        return 1;
    }
    bits_t b = {m->reserva, (unsigned)m->res_n * 8, (unsigned)inicio * 8};

    for (int gr = 0; gr < ngr; gr++) {
        for (int ch = 0; ch < cab.canais; ch++) {
            granulo_t *q = &g[gr][ch];
            canal_t *c = &m->canal[ch];
            unsigned fim = b.pos + (unsigned)q->bits;
            if (cab.v1) fatores_mpeg1(c, &b, q, gr, scfsi[ch]);
            else fatores_mpeg2(m, c, &b, q, cab.modo == 1 && (cab.ext & 1) && ch == 1);
            huffman(&b, q, &cab, fim, m->inteiros);
            b.pos = fim;
            requantizar(c, q, &cab, m->inteiros, m->xr[ch]);
            if (q->tipo == 2) reordenar(m, q, &cab, m->xr[ch]);
        }
        if (cab.modo == 1 && cab.canais == 2) estereo(m, &cab, &g[gr][1]);
        for (int ch = 0; ch < cab.canais; ch++) {
            canal_t *c = &m->canal[ch];
            reduzir_alias(&g[gr][ch], m->xr[ch]);
            imdct(m, c, &g[gr][ch], m->xr[ch]);
            short *saida = m->pcm + gr * 576 * 2 + ch;
            for (int t = 0; t < 18; t++) {
                float s[32];
                for (int k = 0; k < 32; k++) s[k] = m->tempo[k][t];
                sintese(c, s, saida + t * 64);
            }
        }
    }
    if (cab.canais == 1)
        for (int i = 0; i < m->pcm_n; i++) m->pcm[2 * i + 1] = m->pcm[2 * i];
    return 1;
}

/* ====================================================================== */
/* A interface (mp3.h)                                                     */
/* ====================================================================== */

static unsigned be32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

mp3_t *mp3_abrir(const unsigned char *dados, unsigned tam)
{
    if (!dados || tam < 4) return 0;
    /* Etiqueta ID3v1: os últimos 128 bytes, começando por "TAG". */
    if (tam >= 128 && dados[tam - 128] == 'T' && dados[tam - 127] == 'A' && dados[tam - 126] == 'G') tam -= 128;
    /* Etiqueta ID3v2, no começo: "ID3", versão (2 bytes), opções (1 byte) e o
     * tamanho em 4 bytes de 7 bits cada (o bit mais alto é sempre 0, para a
     * etiqueta nunca parecer um cabeçalho de quadro). O tamanho não conta os
     * 10 bytes do cabeçalho nem os 10 do rodapé (opção 0x10). */
    unsigned pos = 0;
    while (pos + 10 <= tam && dados[pos] == 'I' && dados[pos + 1] == 'D' && dados[pos + 2] == '3') {
        unsigned t = ((unsigned)(dados[pos + 6] & 127) << 21) | ((unsigned)(dados[pos + 7] & 127) << 14) |
                     ((unsigned)(dados[pos + 8] & 127) << 7) | (unsigned)(dados[pos + 9] & 127);
        t += 10 + ((dados[pos + 5] & 0x10) ? 10 : 0);
        if (t > tam - pos) break;
        pos += t;
    }

    mp3_t *m = aloca(sizeof(mp3_t));
    if (!m) return 0;
    zerar(m, sizeof(mp3_t));
    m->dados = dados;
    m->tam = tam;
    cab_t cab;
    int p = achar_quadro(m, pos, &cab);
    if (p < 0) {
        libera(m);
        return 0;
    }
    montar_tabelas();
    m->inicio = (unsigned)p;
    m->taxa = cab.taxa;
    m->canais = (unsigned)cab.canais;
    m->amostras_quadro = cab.v1 ? 1152 : 576;

    /* O quadro Xing/Info: um primeiro quadro sem som, em que, logo depois da
     * informação lateral, vem "Xing" (taxa de bits variável) ou "Info" (fixa),
     * 4 bytes de opções e, se a opção 1 estiver ligada, quantos quadros o
     * arquivo tem. Ele não é tocado. */
    const unsigned char *x = dados + p + 4 + cab.tam_lateral;
    if (4 + cab.tam_lateral + 12 <= cab.tam &&
        ((x[0] == 'X' && x[1] == 'i' && x[2] == 'n' && x[3] == 'g') ||
         (x[0] == 'I' && x[1] == 'n' && x[2] == 'f' && x[3] == 'o'))) {
        if (be32(x + 4) & 1) m->total_quadros = be32(x + 8);
        m->inicio += (unsigned)cab.tam;
    }
    if (m->total_quadros == 0) {
        /* Sem essa informação, conta os quadros (só pula de cabeçalho em
         * cabeçalho, é rápido). */
        unsigned q = m->inicio;
        while (proximo_quadro(m, &q, &cab)) {
            m->total_quadros++;
            q += (unsigned)cab.tam;
        }
    }
    m->pos = m->inicio;
    return m;
}

unsigned mp3_taxa(mp3_t *m) { return m ? m->taxa : 0; }
unsigned mp3_canais(mp3_t *m) { return m ? m->canais : 0; }

unsigned mp3_duracao_ms(mp3_t *m)
{
    if (!m || !m->taxa) return 0;
    /* Em duas partes, para a conta não passar de 32 bits. */
    unsigned amostras = m->total_quadros * m->amostras_quadro;
    return amostras / m->taxa * 1000 + amostras % m->taxa * 1000 / m->taxa;
}

int mp3_decodificar(mp3_t *m, short *saida, int max)
{
    if (!m || !saida) return 0;
    int feitos = 0;
    while (feitos < max) {
        if (m->pcm_pos >= m->pcm_n) {
            if (!decodificar_quadro(m)) break;
            if (m->descartar) {
                m->descartar--;
                m->pcm_pos = m->pcm_n;
                continue;
            }
        }
        int n = m->pcm_n - m->pcm_pos;
        if (n > max - feitos) n = max - feitos;
        for (int i = 0; i < 2 * n; i++) saida[2 * feitos + i] = m->pcm[2 * m->pcm_pos + i];
        m->pcm_pos += n;
        feitos += n;
    }
    return feitos;
}

int mp3_ir_para_ms(mp3_t *m, unsigned ms)
{
    if (!m || !m->taxa) return -1;
    /* Em que quadro esse instante cai. Como cada quadro pode ter um tamanho
     * (taxa de bits variável), o jeito certo é contar os quadros desde o
     * começo, pulando de cabeçalho em cabeçalho. */
    unsigned amostras = ms / 1000 * m->taxa + ms % 1000 * m->taxa / 1000;
    unsigned alvo = amostras / m->amostras_quadro;
    /* Um quadro depende dos anteriores (a reserva de bits e a soma sobreposta
     * da IMDCT): a decodificação recomeça alguns quadros antes, e o som deles
     * é jogado fora. */
    unsigned antes = alvo < AQUECIMENTO ? alvo : AQUECIMENTO;
    unsigned q = m->inicio;
    cab_t cab;
    for (unsigned i = 0; i < alvo - antes; i++) {
        if (!proximo_quadro(m, &q, &cab)) { q = m->tam; antes = 0; break; }
        q += (unsigned)cab.tam;
    }
    m->pos = q;
    m->descartar = antes;
    m->res_n = 0;
    m->pcm_n = m->pcm_pos = 0;
    zerar(m->canal, sizeof(m->canal));
    return 0;
}

void mp3_fechar(mp3_t *m)
{
    if (m) libera(m);
}
