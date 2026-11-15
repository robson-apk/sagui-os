/* musica [arquivo ou pasta...] - tocador de música em janela (H87).
 *
 * Toca WAV (PCM de 8 ou 16 bits) e M4A/MP4 com áudio AAC-LC (o leitor de MP4
 * e o decodificador AAC do tocador de vídeo, userland/video, ligados a este
 * programa pelo Makefile). Sem argumentos, abre a pasta /sons.
 *
 * A placa de som recebe 16 bits estéreo a 48 kHz; a conversão de taxa é por
 * interpolação linear (posição em ponto fixo 16.16), como no "tocar". O laço
 * não espera a placa: entrega o que ela aceita e volta aos eventos, então os
 * botões respondem enquanto toca.
 *
 * Teclas: espaço toca/pausa; N próxima; P anterior; setas esquerda/direita
 * voltam/avançam 5 s; + e - volume; Esc fecha. Clicar na barra de progresso
 * pula para o ponto; duplo clique na lista toca a faixa. */
#include "sagui.h"
#include "gui.h"
#include "icones.h"
#include "../video/mp4.h"
#include "../video/aac.h"

#define LARG 560
#define ALT  440
#define MAX_FAIXAS 128
#define SOM_TAXA 48000
#define MAX_BYTES (64u * 1024 * 1024)

static jan_t *j;
static char faixas[MAX_FAIXAS][64];   /* nomes, para a lista */
static char caminhos[MAX_FAIXAS][160];
static int qtd, atual = -1;
static lista_t lista;

/* ---- a faixa aberta ---- */
static uint8_t *dados;
static unsigned tam;
static bool eh_wav;
/* WAV */
static unsigned w_ini, w_quadros, w_bq, w_bits, w_canais;
/* M4A */
static mp4_t m;
static aac_t *dec;
static int16_t pcm[2048];
static int pcm_n, ia;               /* amostras no pcm (por canal); próximo quadro AAC */
static int16_t ant[2];

static unsigned taxa, total_quadros;   /* na taxa original */
static uint32_t pos;                   /* 16.16 na taxa original (WAV) ou dentro do pcm (M4A) */
static uint32_t passo;
static unsigned tocados;               /* quadros de 48 kHz já entregues à placa */
static unsigned base_quadros;          /* posição (taxa original) do começo da contagem */
static bool tocando, acabou;
static int som = -1;
static int volume;
static char aviso[120];

/* H88: os botões de tocar com ícones no lugar de "|<" e ">|". */
static botao_t b_ant = { 16, 112, 56, 30, "", false, icone_anterior }, b_tocar = { 80, 112, 96, 30, "Tocar", false, icone_tocar },
               b_prox = { 184, 112, 56, 30, "", false, icone_proxima }, b_menos = { 380, 112, 34, 30, "-", false },
               b_mais = { 510, 112, 34, 30, "+", false };

static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned le32(const uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }

static bool eh_audio(const char *nome)
{
    const char *p = acha_ultimo_char(nome, '.');
    if (!p) return false;
    char e[8];
    int n = 0;
    for (; p[n] && n < 7; n++) e[n] = (char)(p[n] >= 'A' && p[n] <= 'Z' ? p[n] + 32 : p[n]);
    e[n] = 0;
    return !compara_texto(e, ".wav") || !compara_texto(e, ".m4a") || !compara_texto(e, ".mp4") || !compara_texto(e, ".aac");
}

static void adicionar(const char *c)
{
    if (qtd >= MAX_FAIXAS) return;
    for (int i = 0; i < qtd; i++) if (!compara_texto(caminhos[i], c)) return;
    copia_texto(caminhos[qtd], c, sizeof caminhos[0]);
    const char *b = acha_ultimo_char(c, '/');
    copia_texto(faixas[qtd], b ? b + 1 : c, sizeof faixas[0]);
    qtd++;
}

static void adicionar_pasta(const char *pasta)
{
    entrada_pasta_t e;
    char c[160];
    int inicio = qtd;
    for (int i = 0; listar(pasta, i, &e) == 1; i++) {
        if (e.tipo == TIPO_PASTA || !eh_audio(e.nome)) continue;
        formatar_texto(c, sizeof c, "%s%s%s", pasta, compara_texto(pasta, "/") ? "/" : "", e.nome);
        adicionar(c);
    }
    /* em ordem alfabética (a FAT lista na ordem em que foram gravados) */
    for (int i = inicio + 1; i < qtd; i++)
        for (int k = i; k > inicio && compara_texto(faixas[k - 1], faixas[k]) > 0; k--) {
            char tn[64], tc[160];
            copia_texto(tn, faixas[k], sizeof tn); copia_texto(faixas[k], faixas[k - 1], sizeof tn); copia_texto(faixas[k - 1], tn, sizeof tn);
            copia_texto(tc, caminhos[k], sizeof tc); copia_texto(caminhos[k], caminhos[k - 1], sizeof tc); copia_texto(caminhos[k - 1], tc, sizeof tc);
        }
}

/* ---------------------------------------------------------------- abrir */

static void fechar_faixa(void)
{
    if (dec) { aac_fim(dec); dec = NULL; }
    if (dados && !eh_wav) mp4_fim(&m);
    libera(dados);
    dados = NULL;
}

static bool abrir_wav(void)
{
    if (tam < 44 || memcmp(dados, "RIFF", 4) || memcmp(dados + 8, "WAVE", 4)) return false;
    unsigned formato = 0, canais = 0, tx = 0, bits = 0, ini = 0, t_dados = 0;
    for (unsigned p = 12; p + 8 <= tam;) {
        unsigned t = le32(dados + p + 4);
        if (!memcmp(dados + p, "fmt ", 4) && t >= 16) {
            formato = le16(dados + p + 8); canais = le16(dados + p + 10);
            tx = le32(dados + p + 12); bits = le16(dados + p + 22);
        } else if (!memcmp(dados + p, "data", 4)) {
            ini = p + 8; t_dados = t;
            if (ini + t_dados > tam) t_dados = tam - ini;
            break;
        }
        if (t > tam) break;
        p += 8 + t + (t & 1);
    }
    if (formato != 1 || (bits != 8 && bits != 16) || canais < 1 || canais > 2 || tx < 4000 || tx > 96000 || !ini) {
        copia_texto(aviso, "WAV não suportado (só PCM de 8 ou 16 bits)", sizeof aviso);
        return false;
    }
    eh_wav = true;
    w_ini = ini; w_bits = bits; w_canais = canais; w_bq = bits / 8 * canais;
    w_quadros = t_dados / w_bq;
    taxa = tx;
    total_quadros = w_quadros;
    return true;
}

static bool abrir_m4a(void)
{
    if (mp4_ler(dados, tam, &m) != 0) return false;
    if (!m.n_audio || !m.asc) { mp4_fim(&m); copia_texto(aviso, "o arquivo não tem áudio", sizeof aviso); return false; }
    dec = aac_novo(m.asc, (int)m.asc_n);
    if (!dec) { mp4_fim(&m); copia_texto(aviso, "áudio não suportado (só AAC-LC)", sizeof aviso); return false; }
    eh_wav = false;
    taxa = (unsigned)aac_taxa(dec);
    total_quadros = (unsigned)m.n_audio * 1024u;
    ia = 0;
    pcm_n = 0;
    return true;
}

static void ir_para(unsigned quadro);

static bool abrir_faixa(int i)
{
    fechar_faixa();
    aviso[0] = 0;
    acabou = false;
    atual = i;
    lista.sel = i;
    info_arquivo_t inf;
    int r = info_arquivo(caminhos[i], &inf);
    if (r < 0 || inf.tamanho > MAX_BYTES) { copia_texto(aviso, r < 0 ? nome_erro(r) : "arquivo grande demais", sizeof aviso); goto erro; }
    dados = aloca(inf.tamanho + 1);
    if (!dados) { copia_texto(aviso, "sem memória", sizeof aviso); goto erro; }
    int fd = abrir(caminhos[i], ABRIR_LER);
    if (fd < 0) { copia_texto(aviso, nome_erro(fd), sizeof aviso); goto erro; }
    tam = 0;
    while (tam < inf.tamanho) { int k = ler(fd, dados + tam, inf.tamanho - tam); if (k <= 0) break; tam += (unsigned)k; }
    fechar(fd);
    if (!abrir_wav() && !abrir_m4a()) { if (!aviso[0]) copia_texto(aviso, "formato não reconhecido", sizeof aviso); goto erro; }
    /* passo = taxa / 48000 em 16.16; 65536 / 48000 = 1024 / 750 */
    passo = taxa * 1024u / 750u;
    ir_para(0);
    registrar("musica: tocando %s (%u Hz, %s, %u s)", caminhos[i], taxa, eh_wav ? "wav" : "aac", total_quadros / taxa);
    return true;
erro:
    libera(dados); dados = NULL;
    registrar("musica: erro em %s: %s", caminhos[i], aviso);
    return false;
}

/* ---------------------------------------------------------------- amostras */

static void ir_para(unsigned quadro)
{
    if (quadro >= total_quadros) quadro = total_quadros ? total_quadros - 1 : 0;
    ant[0] = ant[1] = 0;
    if (!eh_wav) {   /* WAV: gerar() recomeça de base_quadros quando tocados = 0 */
        ia = (int)(quadro / 1024u);
        pcm_n = 0;
        pos = 0;
    }
    base_quadros = quadro;
    tocados = 0;
}

/* WAV: a posição fica em quadros + fração separados, para arquivos longos. */
static unsigned w_k;
static uint32_t w_fr;

static int16_t amostra_wav(unsigned k, unsigned c)
{
    unsigned o = c < w_canais ? c : 0;
    const uint8_t *p = dados + w_ini + k * w_bq;
    return w_bits == 16 ? (int16_t)le16(p + o * 2) : (int16_t)(((int)p[o] - 128) << 8);
}

/* Gera até "max" quadros de 48 kHz estéreo. Devolve quantos (0 = fim). */
static int gerar(int16_t *saida, int max)
{
    int q = 0;
    if (eh_wav) {
        if (tocados == 0) { w_k = base_quadros; w_fr = 0; }
        while (q < max && w_k + 1 < w_quadros) {
            for (unsigned c = 0; c < 2; c++) {
                int a = amostra_wav(w_k, c), b = amostra_wav(w_k + 1, c);
                saida[2 * q + c] = (int16_t)(a + (int)(((int64_t)(b - a) * (int64_t)w_fr) >> 16));
            }
            q++;
            w_fr += passo;
            w_k += w_fr >> 16;
            w_fr &= 0xFFFF;
        }
        return q;
    }
    while (q < max) {
        unsigned k = pos >> 16;
        if ((int)k >= pcm_n) {   /* acabou o quadro AAC: decodifica o próximo */
            if (pcm_n > 0) { ant[0] = pcm[2 * (pcm_n - 1)]; ant[1] = pcm[2 * (pcm_n - 1) + 1]; }
            pos -= (uint32_t)pcm_n << 16;
            pcm_n = 0;
            while (pcm_n == 0 && ia < m.n_audio) {
                mp4_amostra_t *s = &m.audio[ia++];
                if (s->pos + s->tam > tam) { ia = m.n_audio; break; }
                int n = aac_quadro(dec, dados + s->pos, (int)s->tam, pcm);
                if (n > 0) pcm_n = n;
            }
            if (pcm_n == 0) break;
            continue;
        }
        uint32_t fr = pos & 0xFFFF;
        for (int c = 0; c < 2; c++) {
            int a = k == 0 ? ant[c] : pcm[2 * (k - 1) + c], b = pcm[2 * k + c];   /* interpola com a anterior */
            saida[2 * q + c] = (int16_t)(a + (int)(((int64_t)(b - a) * (int64_t)fr) >> 16));
        }
        q++;
        pos += passo;
    }
    return q;
}

/* ---------------------------------------------------------------- tela */

static unsigned segundos_agora(void)
{
    if (!taxa) return 0;
    return base_quadros / taxa + tocados / SOM_TAXA;
}

static void desenhar(void)
{
    g_retangulo(j, 0, 0, LARG, ALT, 0x1C2230);
    const char *nome = atual >= 0 ? faixas[atual] : "Nenhuma faixa";
    g_texto_n(j, 16, 16, nome, (LARG - 32) / LETRA_L, 0xFFFFFF);
    char t[96];
    unsigned agora = segundos_agora(), total = taxa ? total_quadros / taxa : 0;
    if (agora > total) agora = total;
    if (aviso[0]) g_texto_n(j, 16, 40, aviso, (LARG - 32) / LETRA_L, 0xF38BA8);
    else {
        formatar_texto(t, sizeof t, "%u:%02u / %u:%02u   %s", agora / 60, agora % 60, total / 60, total % 60,
                       !dados ? "" : acabou ? "fim" : tocando ? "tocando" : "pausado");
        g_texto(j, 16, 40, t, 0xA6ADC8);
    }
    /* barra de progresso */
    g_retangulo(j, 16, 72, LARG - 32, 12, 0x313244);
    if (total) g_retangulo(j, 16, 72, (int)((LARG - 32) * agora / total), 12, 0x3CDC6E);
    b_tocar.texto = tocando ? "Pausar" : "Tocar";
    b_tocar.icone = tocando ? icone_pausar : icone_tocar;
    botao_desenhar(j, &b_ant);
    botao_desenhar(j, &b_tocar);
    botao_desenhar(j, &b_prox);
    g_texto(j, 300, 119, "Volume", 0xA6ADC8);
    botao_desenhar(j, &b_menos);
    g_retangulo(j, 420, 121, 84, 12, 0x313244);
    g_retangulo(j, 420, 121, 84 * volume / 100, 12, 0x89DCEB);
    botao_desenhar(j, &b_mais);
    lista_desenhar(j, &lista);
    jan_mostrar(j);
}

static void desenhar_tempo(void)   /* só a linha do tempo e a barra, várias vezes por segundo */
{
    char t[96];
    unsigned agora = segundos_agora(), total = taxa ? total_quadros / taxa : 0;
    if (agora > total) agora = total;
    if (aviso[0]) return;
    g_retangulo(j, 16, 40, LARG - 32, 16, 0x1C2230);
    formatar_texto(t, sizeof t, "%u:%02u / %u:%02u   %s", agora / 60, agora % 60, total / 60, total % 60,
                   !dados ? "" : acabou ? "fim" : tocando ? "tocando" : "pausado");
    g_texto(j, 16, 40, t, 0xA6ADC8);
    g_retangulo(j, 16, 72, LARG - 32, 12, 0x313244);
    if (total) g_retangulo(j, 16, 72, (int)((LARG - 32) * agora / total), 12, 0x3CDC6E);
    jan_mostrar_area(j, 0, 36, LARG, 52);
}

/* ---------------------------------------------------------------- ações */

static void tocar_faixa(int i)
{
    if (i < 0 || i >= qtd) return;
    if (abrir_faixa(i)) tocando = true;
    else tocando = false;
}

static void pausar(void)
{
    if (!dados) { tocar_faixa(atual >= 0 ? atual : 0); return; }
    if (acabou) { ir_para(0); acabou = false; tocando = true; registrar("musica: de novo"); return; }
    tocando = !tocando;
    registrar("musica: %s", tocando ? "continua" : "pausa");
}

static void pular(int segundos)
{
    if (!dados) return;
    int alvo = (int)segundos_agora() + segundos;
    if (alvo < 0) alvo = 0;
    ir_para((unsigned)alvo * taxa);
    acabou = false;
    registrar("musica: posicao %d s", alvo);
}

static void mudar_volume(int d)
{
    volume = som_volume(volume + d < 0 ? 0 : volume + d > 100 ? 100 : volume + d);
    if (volume < 0) volume = 0;
    registrar("musica: volume %d", volume);
}

int principal(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        info_arquivo_t inf;
        if (info_arquivo(argv[i], &inf) < 0) continue;
        if (inf.tipo == TIPO_PASTA) adicionar_pasta(argv[i]);
        else adicionar(argv[i]);
    }
    if (argc == 2 && qtd == 1) {   /* um arquivo só: as outras músicas da pasta entram depois dele */
        char pasta[160];
        copia_texto(pasta, argv[1], sizeof pasta);
        char *b = acha_ultimo_char(pasta, '/');
        if (b) { if (b == pasta) pasta[1] = 0; else *b = 0; adicionar_pasta(pasta); }
    }
    if (argc < 2) adicionar_pasta("/sons");
    j = jan_abrir("Música", LARG, ALT);
    if (!j) return 1;
    lista = (lista_t){ 16, 160, LARG - 32, ALT - 176, faixas, qtd, qtd ? 0 : -1, 0, NULL };
    volume = som_volume(-1);
    if (volume < 0) volume = 0;
    som = som_abrir();
    if (som < 0) copia_texto(aviso, "sem placa de som", sizeof aviso);
    registrar("musica: pronto, %d faixas", qtd);
    if (argc > 1 && qtd) tocar_faixa(0);
    desenhar();

    static int16_t bloco[2 * 960];
    int bloco_n = 0, bloco_feito = 0;   /* bytes */
    unsigned ultimo_desenho = 0, ultimo_seg = (unsigned)-1;
    evento_t e;
    for (;;) {
        /* 1) som: entrega o que a placa aceitar, sem esperar */
        if (tocando && dados && som >= 0) {
            for (int voltas = 0; voltas < 8; voltas++) {
                if (bloco_feito == bloco_n) {
                    int q = gerar(bloco, 960);
                    if (q == 0) {
                        tocando = false; acabou = true;
                        registrar("musica: fim de %s", faixas[atual]);
                        if (atual + 1 < qtd) tocar_faixa(atual + 1);
                        desenhar();
                        break;
                    }
                    bloco_n = q * 4; bloco_feito = 0;
                    tocados += (unsigned)q;
                }
                int r = som_enviar(som, (char *)bloco + bloco_feito, (unsigned)(bloco_n - bloco_feito));
                if (r <= 0) break;
                bloco_feito += r;
            }
        }
        /* 2) eventos */
        bool algum = false;
        while (proximo_evento(j->id, &e, false) == 1) {
            algum = true;
            bool redesenhar = true;
            if (e.tipo == EV_FECHAR) goto fim;
            if (botao_tratar(j, &b_tocar, &e)) pausar();
            else if (botao_tratar(j, &b_ant, &e)) { if (segundos_agora() > 3) pular(-100000); else tocar_faixa(atual > 0 ? atual - 1 : 0); bloco_n = bloco_feito = 0; }
            else if (botao_tratar(j, &b_prox, &e)) { tocar_faixa(atual + 1 < qtd ? atual + 1 : 0); bloco_n = bloco_feito = 0; }
            else if (botao_tratar(j, &b_menos, &e)) mudar_volume(-10);
            else if (botao_tratar(j, &b_mais, &e)) mudar_volume(10);
            else if (e.tipo == EV_CLIQUE && e.y >= 68 && e.y < 90 && e.x >= 16 && e.x < LARG - 16 && dados) {
                unsigned total = total_quadros / taxa, alvo = (unsigned)(e.x - 16) * total / (LARG - 32);
                ir_para(alvo * taxa);
                acabou = false;
                bloco_n = bloco_feito = 0;
                registrar("musica: posicao %u s", alvo);
            } else if (e.tipo == EV_TECLA) {
                unsigned t = e.tecla;
                if (t == TECLA_ESC || t == 27) goto fim;
                else if (t == ' ') pausar();
                else if (t == 'n' || t == 'N') { tocar_faixa(atual + 1 < qtd ? atual + 1 : 0); bloco_n = bloco_feito = 0; }
                else if (t == 'p' || t == 'P') { tocar_faixa(atual > 0 ? atual - 1 : 0); bloco_n = bloco_feito = 0; }
                else if (t == TECLA_DIREITA) { pular(5); bloco_n = bloco_feito = 0; }
                else if (t == TECLA_ESQUERDA) { pular(-5); bloco_n = bloco_feito = 0; }
                else if (t == '+' || t == '=') mudar_volume(10);
                else if (t == '-') mudar_volume(-10);
                else if (lista_tratar(j, &lista, &e) == LISTA_ATIVOU) { tocar_faixa(lista.sel); bloco_n = bloco_feito = 0; }
            } else if (lista_tratar(j, &lista, &e) == LISTA_ATIVOU) { tocar_faixa(lista.sel); bloco_n = bloco_feito = 0; }
            else redesenhar = e.tipo == EV_CLIQUE;
            if (redesenhar) desenhar();
        }
        /* 3) relógio na tela, uma vez por segundo */
        unsigned seg = segundos_agora();
        if (seg != ultimo_seg && tempo_ms() - ultimo_desenho > 200) { ultimo_seg = seg; ultimo_desenho = tempo_ms(); desenhar_tempo(); }
        if (!algum) dormir(tocando ? 5 : 30);
    }
fim:
    if (som >= 0) som_fechar(som);
    fechar_faixa();
    jan_fechar(j);
    return 0;
}
