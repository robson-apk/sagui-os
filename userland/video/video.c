/* video - tocador de MP4 (H.264 + AAC-LC) do SaguiOS (H82).
 *
 *   video /caminho/filme.mp4
 *   video http://servidor/filme.mp4      (também https://)
 *
 * Veio do pull request #6 (Robson de Lima Barbosa). Na 2.1 usa o HTTP e o
 * TLS da 2.0 (http_obter) e o som HDA (som_abrir/som_enviar, 48 kHz, 16
 * bits, estéreo): o áudio do arquivo (44,1 kHz, por exemplo) é convertido
 * para 48 kHz por interpolação linear, como no tocar. Pela rede, o arquivo
 * é baixado inteiro antes de tocar (o HTTP da 2.0 entrega o corpo de uma vez
 * só; tocar enquanto baixa fica para depois). O relógio é o de parede.
 * O vídeo pode ser H.264 Baseline, Main ou High (D048). */
#include "sagui.h"
#include "gui.h"
#include "http.h"
#include "h264.h"
#include "aac.h"
#include "mp4.h"

#define LARG_MAX 854
#define MAX_VIDEO (96u << 20)

static uint8_t *dados;
static unsigned tam, cap, total;
static bool baixando;   /* sempre falso na 2.1: o arquivo chega inteiro */
static void baixar_mais(void) { }

static int receber(void *ctx, const char *d, unsigned n)
{
    (void)ctx;
    if (tam + n > MAX_VIDEO) return -1;
    if (tam + n > cap) {
        unsigned nc = cap ? cap * 2 : 4u << 20;
        while (nc < tam + n) nc *= 2;
        uint8_t *q = realoca(dados, nc);
        if (!q) return -1;
        dados = q;
        cap = nc;
    }
    memcpy(dados + tam, d, n);
    tam += n;
    if ((tam >> 20) != ((tam - n) >> 20)) registrar("video: %u MB baixados", tam >> 20);
    return 0;
}

static bool abrir_url(const char *u)
{
    http_resposta_t r;
    int ok = http_obter(u, receber, NULL, &r);
    if (ok < 0 || r.situacao != 200) {
        imprimir_em(ERROS, "video: %s\n", r.erro[0] ? r.erro : "o servidor não mandou o vídeo");
        registrar("video: %s: situacao %d %s", u, r.situacao, r.erro);
        return false;
    }
    total = tam;
    return true;
}

static bool abrir_arquivo(const char *c)
{
    info_arquivo_t inf;
    if (info_arquivo(c, &inf) < 0) return false;
    int fd = abrir(c, ABRIR_LER);
    if (fd < 0) return false;
    cap = inf.tamanho + 16;
    dados = aloca(cap);
    if (!dados) { fechar(fd); return false; }
    while (tam < inf.tamanho) {
        int r = ler(fd, dados + tam, inf.tamanho - tam);
        if (r <= 0) break;
        tam += (unsigned)r;
    }
    fechar(fd);
    total = tam;
    return true;
}

/* YUV 4:2:0 -> RGB (BT.601, faixa limitada), com redução 2x opcional. */
static int tab_y[256], tab_rv[256], tab_gu[256], tab_gv[256], tab_bu[256];

static void tabelas_cor(void)
{
    for (int i = 0; i < 256; i++) {
        tab_y[i] = 298 * (i - 16) + 128;
        tab_rv[i] = 409 * (i - 128);
        tab_gu[i] = -100 * (i - 128);
        tab_gv[i] = -208 * (i - 128);
        tab_bu[i] = 516 * (i - 128);
    }
}
static inline uint32_t sat(int v) { v >>= 8; return (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void desenhar(jan_t *j, const h264_imagem_t *im, int esc, int oy)
{
    int l = im->larg / esc, a = im->alt / esc;
    if (l > j->larg) l = j->larg;
    if (a > j->alt - oy) a = j->alt - oy;
    for (int y = 0; y < a; y++) {
        int sy = y * esc;
        const uint8_t *py = im->y + sy * im->passo_y;
        const uint8_t *pu = im->u + (sy / 2) * im->passo_c, *pv = im->v + (sy / 2) * im->passo_c;
        uint32_t *d = j->px + (y + oy) * j->larg;
        for (int x = 0; x < l; x++) {
            int sx = x * esc, Y = tab_y[py[sx]], U = pu[sx / 2], V = pv[sx / 2];
            d[x] = (sat(Y + tab_rv[V]) << 16) | (sat(Y + tab_gu[U] + tab_gv[V]) << 8) | sat(Y + tab_bu[U]);
        }
    }
}

static unsigned em_ms(uint32_t t, uint32_t esc)
{
    if (!esc) esc = 1000;
    return t / esc * 1000 + t % esc * 1000 / esc;
}

static bool disponivel_ate(uint32_t fim) { return fim <= tam; }

int principal(int argc, char **argv)
{
    if (argc < 2) { imprimir_em(ERROS, "uso: video <arquivo.mp4 | http(s)://...>\n"); return 1; }
    bool rede = comeca_com(argv[1], "http://") || comeca_com(argv[1], "https://");
    if (!(rede ? abrir_url(argv[1]) : abrir_arquivo(argv[1]))) {
        imprimir_em(ERROS, "video: não consegui abrir %s\n", argv[1]);
        registrar("video: nao abriu %s", argv[1]);
        return 1;
    }
    /* espera o índice */
    mp4_t m;
    int r;
    while ((r = mp4_ler(dados, tam, &m)) != 0) {
        if (r == MP4_SEM_TRILHA || !baixando) { imprimir_em(ERROS, "video: MP4 sem trilha legível (%d)\n", r); return 1; }
        baixar_mais();
    }
    /* mp4_ler aponta para dentro de "dados", que pode mudar de lugar: copia. */
    static uint8_t avcc[512], asc[64];
    int avcc_n = (int)(m.avcc_n < sizeof avcc ? m.avcc_n : sizeof avcc), asc_n = (int)(m.asc_n < sizeof asc ? m.asc_n : sizeof asc);
    if (m.avcc) memcpy(avcc, m.avcc, (size_t)avcc_n);
    if (m.asc) memcpy(asc, m.asc, (size_t)asc_n);
    registrar("video: %dx%d, %d quadros (escala %u); audio %d quadros %d Hz", m.larg, m.alt, m.n_video,
              m.escala_video, m.n_audio, m.taxa);

    h264_t *h = NULL;
    int tam_len = 4;
    if (m.n_video) {
        h = h264_novo();
        int rc = h ? h264_config(h, avcc, avcc_n, &tam_len) : -99;
        if (rc < 0) {
            /* o decodificador diz o que o vídeo tem que ele não faz (entrelaçado, 10 bits...) */
            const char *motivo = h && h264_motivo(h) ? h264_motivo(h) : "formato desconhecido";
            imprimir_em(ERROS, "video: H.264 não suportado (%s)\n", motivo);
            registrar("video: H.264 recusado (%d, avcC %d bytes): %s", rc, avcc_n, motivo);
            h = NULL;
        }
    }
    aac_t *a = m.n_audio && asc_n ? aac_novo(asc, asc_n) : NULL;
    if (m.n_audio && !a) { imprimir_em(ERROS, "video: áudio não suportado (só AAC-LC)\n"); registrar("video: AAC recusado (asc %d bytes: %02x %02x)", asc_n, asc[0], asc[1]); }
    if (!h && !a) return 1;

    int esc = m.larg > LARG_MAX ? 2 : 1;
    int jl = h ? m.larg / esc : 320, ja = h ? m.alt / esc : 40;
    /* título: o nome do arquivo, sem a consulta, curto (o kernel recusa títulos longos) */
    char titulo[40];
    const char *nome = acha_ultimo_char(argv[1], '/');
    copia_texto(titulo, nome ? nome + 1 : argv[1], sizeof titulo);
    char *qm = acha_char(titulo, '?');
    if (qm) *qm = 0;
    jan_t *j = jan_abrir(titulo, jl, ja);
    if (!j) { registrar("video: janela recusada"); return 1; }
    g_retangulo(j, 0, 0, j->larg, j->alt, 0);
    jan_mostrar(j);
    tabelas_cor();

    static int16_t pcm[2048];        /* um quadro AAC: 1024 amostras estéreo */
    static int16_t saida[2 * 1200];  /* o mesmo quadro em 48 kHz (até 1115 amostras para 44,1 kHz) */
    int pcm_bytes = 0, pcm_feito = 0;
    int som = a ? som_abrir() : -1;
    if (a && som < 0) { registrar("video: sem placa de som, segue sem audio"); a = NULL; }
    /* posição na entrada em ponto fixo (16 bits de fração), e a amostra
     * anterior, para interpolar entre o fim de um quadro e o começo do outro */
    uint32_t passo = a ? (uint32_t)((double)aac_taxa(a) * 65536.0 / SOM_TAXA + 0.5) : 0, pos = 0;   /* sem divisão de 64 bits (libgcc) */
    int16_t ant[2] = { 0, 0 };
    int iv = 0, ia = 0, mostrados = 0, pulados = 0, erros_video = 0;
    unsigned inicio = tempo_ms(), parado_desde = 0;
    unsigned ultimo_quadro = 0;
    unsigned tempo_parado = 0, tempo_decod = 0, tempo_audio = 0;
    bool fim = false, esvaziado = false;
    while (!fim) {
        evento_t e;
        while (proximo_evento(j->id, &e, false) == 1)
            if (e.tipo == EV_FECHAR || (e.tipo == EV_TECLA && e.tecla == 27)) fim = true;
        unsigned agora = tempo_ms() - inicio;
        bool trabalhou = false;

        /* áudio: mantém a placa cheia */
        while (a && ia < m.n_audio) {
            if (pcm_feito >= pcm_bytes) {
                mp4_amostra_t *s = &m.audio[ia];
                if (!disponivel_ate(s->pos + s->tam)) break;
                unsigned ta = tempo_ms();
                int n = aac_quadro(a, dados + s->pos, (int)s->tam, pcm);
                tempo_audio += tempo_ms() - ta;
                /* para 48 kHz: amostra k da saída = entrada em pos (com a anterior antes do índice 0) */
                int k = 0;
                while (n > 0 && (pos >> 16) < (uint32_t)n && k < 1200) {
                    uint32_t i = pos >> 16, f = pos & 0xFFFF;
                    for (int c = 0; c < 2; c++) {
                        int x0 = i == 0 ? ant[c] : pcm[(i - 1) * 2 + c], x1 = pcm[i * 2 + c];
                        saida[k * 2 + c] = (int16_t)(x0 + (int)(((int64_t)(x1 - x0) * f) >> 16));
                    }
                    k++;
                    pos += passo;
                }
                if (n > 0) { pos -= (uint32_t)n << 16; ant[0] = pcm[(n - 1) * 2]; ant[1] = pcm[(n - 1) * 2 + 1]; }
                pcm_bytes = k * 4;
                pcm_feito = 0;
                ia++;
                continue;
            }
            int aceitos = som_enviar(som, (uint8_t *)saida + pcm_feito, (unsigned)(pcm_bytes - pcm_feito));
            if (aceitos < 0) { registrar("video: a placa de som falhou, segue sem audio"); a = NULL; break; }
            if (aceitos == 0) break;
            pcm_feito += aceitos;
            trabalhou = true;
        }

        /* vídeo */
        if (h && iv < m.n_video) {
            mp4_amostra_t *s = &m.video[iv];
            unsigned t = em_ms(s->tempo, m.escala_video);
            if (!disponivel_ate(s->pos + s->tam)) {
                if (!parado_desde) parado_desde = tempo_ms();
            } else if (agora >= t) {
                if (parado_desde) { unsigned p = tempo_ms() - parado_desde; inicio += p; tempo_parado += p; parado_desde = 0; }
                unsigned td = tempo_ms();
                h264_amostra(h, dados + s->pos, (int)s->tam, tam_len);
                tempo_decod += tempo_ms() - td;
                iv++;
                unsigned prox = iv < m.n_video
                    ? em_ms(m.video[iv].tempo, m.escala_video) : t + 40;
                unsigned dt = tempo_ms();
                h264_imagem_t im;
                bool mostrou = false;
                while (h264_imagem(h, &im)) {   /* com quadros B saem na ordem de exibição, às vezes 2 de uma vez */
                    erros_video = im.erros;
                    if (dt - inicio <= prox + 20 || dt - ultimo_quadro >= 80) {
                        ultimo_quadro = dt;
                        desenhar(j, &im, esc, 0);
                        mostrou = true;
                        mostrados++;
                    } else pulados++;
                }
                if (mostrou) jan_mostrar(j);
                trabalhou = true;
            }
        }
        if (h && iv >= m.n_video && !esvaziado) {   /* os quadros retidos para reordenar */
            esvaziado = true;
            h264_esvaziar(h);
            h264_imagem_t im;
            while (h264_imagem(h, &im)) { erros_video = im.erros; desenhar(j, &im, esc, 0); jan_mostrar(j); mostrados++; }
        }
        if ((!h || iv >= m.n_video) && (!a || ia >= m.n_audio)) fim = true;

        /* rede: baixa quando há folga ou quando falta dado */
        if (baixando && (parado_desde || !trabalhou)) { baixar_mais(); trabalhou = true; }
        if (!trabalhou) dormir(5);
    }
    registrar("video: fim; %d quadros mostrados, %d pulados, %u ms; esperando rede %u ms, decodificando %u ms, audio %u ms, baixados %u bytes",
              mostrados, pulados, tempo_ms() - inicio, tempo_parado, tempo_decod, tempo_audio, tam);
    /* quantos quadros o decodificador entregou e quantas fatias ele não conseguiu ler */
    if (h) registrar("video: H.264: %d de %d quadros decodificados, %d erros", mostrados + pulados, m.n_video, erros_video);
    imprimir("video: %d quadros mostrados, %d pulados\n", mostrados, pulados);
    if (som >= 0) som_fechar(som);
    jan_fechar(j);
    return 0;
}
