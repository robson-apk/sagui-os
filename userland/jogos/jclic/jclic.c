/* jclic - visor de atividades educativas JClic (H92).
 *
 *     jclic [projeto.jclic.zip | projeto.jclic]
 *
 * Trazido do visor de JClic do fork SaguiOS-Extended de Alejandro
 * Anuncibay e reescrito sobre o main atual. O JClic é um formato aberto de
 * atividades educativas (do XTEC, o serviço de educação da Catalunha): um
 * XML com as atividades, normalmente num ZIP com as imagens. Sem argumento,
 * mostra os projetos de /jogos/jclic (o exemplo que vem com o sistema) e do
 * pendrive (/usb e /usb/jclic).
 *
 * Joga-se: tela de informação, quebra-cabeças (duplo, de troca e de
 * buraco), associação simples e complexa, memória, explorar, identificar,
 * caça-palavras e cruzadinha (regras em motor.c). Atividades de texto e de
 * tipos desconhecidos mostram um aviso e podem ser puladas. Imagens PNG e
 * JPEG (os decodificadores do Cipó); GIF não (aparece o nome do arquivo).
 *
 * Teclas: N ou seta para a direita = próxima atividade, P ou seta para a
 * esquerda = anterior, F5 = recomeçar, Esc = voltar à lista; na cruzadinha
 * as letras vão para a grade. */
#include "sagui.h"
#include "gui.h"
#include "motor.h"
#include "zip.h"

#define LARG    800
#define ALT     560
#define TOPO_A  40
#define MSG_A   48
#define BASE_A  40
#define AREA_Y  (TOPO_A + MSG_A + 6)
#define AREA_A  (ALT - AREA_Y - BASE_A - 6)
#define MAX_PROJ 48
#define MAX_IMG  32

static jan_t *j;
static bool na_lista = true;

/* ------------------------------------------------------------ o projeto aberto */

static uint8_t *arquivo;            /* o arquivo inteiro (o ZIP, quando é ZIP) */
static uint32_t tam_arquivo;
static zip_t zip;
static bool eh_zip;
static char pasta[160];             /* pasta do .jclic solto (as imagens ficam ao lado) */
static projeto_t proj;
static atividade_t ativ;
static int atual = -1;
static unsigned t_inicio;

static struct { char nome[64]; imagem_t *im; bool falhou; } imagens[MAX_IMG];
static int qtd_imagens;

static uint8_t *ler_tudo(const char *caminho, uint32_t *n)
{
    info_arquivo_t inf;
    if (info_arquivo(caminho, &inf) < 0 || inf.tamanho > 32u * 1024 * 1024) return NULL;
    uint8_t *d = aloca(inf.tamanho + 1);
    int fd = abrir(caminho, ABRIR_LER);
    if (!d || fd < 0) { libera(d); if (fd >= 0) fechar(fd); return NULL; }
    uint32_t k = 0;
    int r;
    while (k < inf.tamanho && (r = ler(fd, d + k, inf.tamanho - k)) > 0) k += (uint32_t)r;
    fechar(fd);
    d[k] = 0;
    *n = k;
    return d;
}

static void imagens_limpar(void)
{
    for (int k = 0; k < qtd_imagens; k++) libera(imagens[k].im);
    qtd_imagens = 0;
}

/* Uma imagem do projeto (do ZIP ou da pasta), decodificada uma vez só. */
static imagem_t *imagem(const char *nome)
{
    if (!nome) return NULL;
    for (int k = 0; k < qtd_imagens; k++) if (!compara_texto(imagens[k].nome, nome)) return imagens[k].im;
    if (qtd_imagens >= MAX_IMG) return NULL;
    int k = qtd_imagens++;
    copia_texto(imagens[k].nome, nome, sizeof imagens[k].nome);
    imagens[k].im = NULL;
    uint32_t n = 0;
    uint8_t *d = NULL;
    if (eh_zip) d = zip_extrair(&zip, zip_achar(&zip, nome), &n);
    else { char c[256]; formatar_texto(c, sizeof c, "%s/%s", pasta, nome); d = ler_tudo(c, &n); }
    if (d) {
        char erro[80];
        if (n > 8 && d[0] == 0x89 && d[1] == 'P') imagens[k].im = png_ler(d, n, erro, sizeof erro);
        else if (n > 3 && d[0] == 0xFF && d[1] == 0xD8) imagens[k].im = jpeg_ler(d, n, erro, sizeof erro);
        libera(d);
    }
    if (!imagens[k].im) registrar("jclic: imagem %s nao abriu (so PNG e JPEG)", nome);
    return imagens[k].im;
}

static void fechar_projeto(void)
{
    if (atual >= 0) atividade_liberar(&ativ);
    atual = -1;
    imagens_limpar();
    if (proj.raiz) projeto_liberar(&proj);
    if (eh_zip) zip_fechar(&zip);
    eh_zip = false;
    libera(arquivo);
    arquivo = NULL;
}

static bool abrir_projeto(const char *caminho, char *erro, int max)
{
    fechar_projeto();
    arquivo = ler_tudo(caminho, &tam_arquivo);
    if (!arquivo) { copia_texto(erro, "não consegui ler o arquivo", (size_t)max); return false; }
    copia_texto(pasta, caminho, sizeof pasta);
    char *b = acha_ultimo_char(pasta, '/');
    if (b) *b = 0; else copia_texto(pasta, ".", sizeof pasta);
    char *xml;
    uint32_t n;
    if (tam_arquivo > 4 && arquivo[0] == 'P' && arquivo[1] == 'K') {
        if (zip_abrir(&zip, arquivo, tam_arquivo) < 0) { copia_texto(erro, "ZIP estragado", (size_t)max); return false; }
        eh_zip = true;
        int k = -1;
        for (int i = 0; i < zip.qtd && k < 0; i++) {
            int l = (int)tam_texto(zip.e[i].nome);
            if (l > 6 && !compara_texto(zip.e[i].nome + l - 6, ".jclic")) k = i;
        }
        xml = (char *)zip_extrair(&zip, k, &n);
        if (!xml) { copia_texto(erro, "o ZIP não tem um .jclic que eu consiga ler", (size_t)max); return false; }
    } else {
        xml = aloca(tam_arquivo + 1);
        if (!xml) { copia_texto(erro, "sem memória", (size_t)max); return false; }
        memcpy(xml, arquivo, tam_arquivo + 1);
        n = tam_arquivo;
    }
    if (projeto_ler(&proj, xml, (int)n, erro, max) < 0) { projeto_liberar(&proj); return false; }
    registrar("jclic: projeto \"%s\" (%d atividades)", proj.titulo, proj.n);
    return true;
}

/* ------------------------------------------------------------ desenho */

static uint32_t mistura(uint32_t a, uint32_t b, int t)
{
    uint32_t r = (((a >> 16) & 255) * (uint32_t)(256 - t) + ((b >> 16) & 255) * (uint32_t)t) >> 8;
    uint32_t g = (((a >> 8) & 255) * (uint32_t)(256 - t) + ((b >> 8) & 255) * (uint32_t)t) >> 8;
    uint32_t c = ((a & 255) * (uint32_t)(256 - t) + (b & 255) * (uint32_t)t) >> 8;
    return r << 16 | g << 8 | c;
}

extern const unsigned char fonte_sagui[256][16];

/* Uma letra da fonte do sistema ampliada "e" vezes. */
static void letra(int x, int y, int ch, int e, uint32_t cor)
{
    const unsigned char *g = fonte_sagui[(uint8_t)ch];
    for (int l = 0; l < 16; l++)
        for (int c = 0; c < 8; c++)
            if (g[l] & (0x80 >> c)) g_retangulo(j, x + c * e, y + l * e, e, e, cor);
}

/* Texto quebrado em linhas por palavras, centralizado na caixa, no maior
 * tamanho (1x ou 2x) que couber. */
static void texto_caixa(int x, int y, int w, int h, const char *t, uint32_t cor)
{
    if (!t || !*t) return;
    for (int e = 2; e >= 1; e--) {
        int por_linha = (w - 8) / (8 * e);
        if (por_linha < 1) continue;
        const char *ini[24];
        int tam[24], nl = 0;
        const char *p = t;
        while (*p && nl < 24) {
            while (*p == ' ') p++;
            if (!*p) break;
            const char *fim = p;
            int corte = 0;
            while (fim[corte] && fim[corte] != '\n' && corte < por_linha) corte++;
            if (fim[corte] && fim[corte] != '\n' && fim[corte] != ' ') {      /* não corta no meio de uma palavra */
                int k = corte;
                while (k > 0 && fim[k] != ' ') k--;
                if (k > 0) corte = k;
            }
            ini[nl] = p; tam[nl++] = corte;
            p += corte;
            if (*p == '\n') p++;
        }
        if (nl * 16 * e > h - 4 && e > 1) continue;
        int y0 = y + (h - nl * 16 * e) / 2;
        for (int l = 0; l < nl; l++) {
            int x0 = x + (w - tam[l] * 8 * e) / 2;
            for (int k = 0; k < tam[l]; k++) {
                if (y0 + l * 16 * e + 16 * e > y + h) break;
                letra(x0 + k * 8 * e, y0 + l * 16 * e, (uint8_t)ini[l][k], e, cor);
            }
        }
        return;
    }
}

/* Um pedaço (sx,sy,sw,sh) da imagem esticado na caixa (vizinho mais próximo). */
static void desenhar_imagem(const imagem_t *im, int sx, int sy, int sw, int sh, int x, int y, int w, int h)
{
    if (!im || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
    for (int yy = 0; yy < h; yy++) {
        int py = y + yy;
        if (py < 0 || py >= j->alt) continue;
        int iy = sy + yy * sh / h;
        if (iy >= (int)im->alt) iy = (int)im->alt - 1;
        for (int xx = 0; xx < w; xx++) {
            int px = x + xx;
            if (px < 0 || px >= j->larg) continue;
            int ix = sx + xx * sw / w;
            if (ix >= (int)im->larg) ix = (int)im->larg - 1;
            uint32_t c = im->px[iy * (int)im->larg + ix], a = c >> 24;
            uint32_t *d = &j->px[py * j->larg + px];
            *d = a == 255 ? c & 0xFFFFFF : mistura(*d, c & 0xFFFFFF, (int)a);
        }
    }
}

static const char *const sp_banana[12] = {
    "......SS", ".....SYD", ".....YYD", "....YYYD", "...YYYD.", "..YYYYD.",
    ".YYYYD..", "YYYYD...", "YYYD....", ".DD.....", "........", "........",
};
static uint32_t paleta(char c) { return c == 'Y' ? 0xFFD52E : c == 'D' ? 0xB07A12 : c == 'S' ? 0x5A3E1A : 0xFF000000; }

static int erro_p = -1, erro_i = -1;
static unsigned erro_ate;

static void desenhar_caixa(int p, int i)
{
    bloco_t *c = &M.p[p].c[i];
    int x = c->x, y = c->y, w = c->w, h = c->h;
    if (c->est == CX_VAZIA) {
        g_retangulo(j, x, y, w, h, mistura(ativ.cor_fundo, 0x000000, 24));
        g_moldura(j, x, y, w, h, 0xB0B8C0);
        return;
    }
    if (c->est == CX_VIRADA) {
        /* O verso das cartas da memória: verde com uma banana. */
        g_retangulo(j, x + 1, y + 1, w - 2, h - 2, 0x2E9E52);
        g_moldura(j, x + 3, y + 3, w - 6, h - 6, 0xFFD52E);
        int e = (w < h ? w : h) / 20;
        if (e < 1) e = 1;
        g_desenho(j, x + w / 2 - 4 * e, y + h / 2 - 6 * e, sp_banana, 8, 12, paleta, e);
        g_moldura(j, x, y, w, h, 0x1F6B2A);
        return;
    }
    const grupo_t *g = c->g;
    const celula_t *cel = c->cel >= 0 && c->cel < g->n ? &g->c[c->cel] : NULL;
    g_retangulo(j, x, y, w, h, cel ? cel->cor_fundo : g->cor_fundo);
    if (cel && cel->imagem) {
        imagem_t *im = imagem(cel->imagem);
        if (im) desenhar_imagem(im, 0, 0, (int)im->larg, (int)im->alt, x + 2, y + 2, w - 4, h - 4);
        else texto_caixa(x, y, w, h, cel->imagem, 0x6A737C);
    } else if (g->imagem && c->cel >= 0) {
        /* Peça de quebra-cabeça: o pedaço da imagem do grupo que é dela. */
        imagem_t *im = imagem(g->imagem);
        if (im) {
            int col = c->cel % g->cols, lin = c->cel / g->cols;
            int pw = (int)im->larg / g->cols, ph = (int)im->alt / g->linhas;
            desenhar_imagem(im, col * pw, lin * ph, pw, ph, x, y, w, h);
        }
    }
    if (cel && cel->texto && *cel->texto) texto_caixa(x, y, w, h, cel->texto, cel->cor_texto);
    if (c->est == CX_ACERTO) {
        for (int k = 0; k < 3; k++) g_moldura(j, x + k, y + k, w - 2 * k, h - 2 * k, 0x1F8A3A);
    } else if (g->borda) g_moldura(j, x, y, w, h, 0x5A6470);
    if (M.sel_p == p && M.sel_i == i)
        for (int k = 0; k < 3; k++) g_moldura(j, x + k, y + k, w - 2 * k, h - 2 * k, 0xE8A812);
    if (erro_p == p && erro_i == i && tempo_ms() < erro_ate)
        for (int k = 0; k < 3; k++) g_moldura(j, x + k, y + k, w - 2 * k, h - 2 * k, 0xC8161F);
}

static void desenhar_grade(void)
{
    int l = M.gcw;
    bool cruz = ativ.tipo == AT_CRUZADINHA;
    int px = M.foco_x, py = M.foco_y;
    /* Na cruzadinha, a palavra do cursor fica destacada. */
    int ax = px, ay = py, dx = M.direcao ? 0 : 1, dy = M.direcao ? 1 : 0;
    for (int y = 0; y < ativ.gl; y++)
        for (int x = 0; x < ativ.gc; x++) {
            int sx = M.gx + x * l, sy = M.gy + y * l;
            char ch;
            uint32_t fundo = 0xFFFFFF;
            if (cruz) {
                if (ativ.grade[y][x] == '*') { g_retangulo(j, sx, sy, l, l, 0x1E2328); continue; }
                ch = M.escrito[y][x];
                bool na_palavra = false;
                if (px >= 0) {
                    int cx = px, cy = py;
                    while (cx >= 0 && cy >= 0 && ativ.grade[cy][cx] != '*') { if (cx == x && cy == y) na_palavra = true; cx -= dx; cy -= dy; }
                    cx = px; cy = py;
                    while (cx < ativ.gc && cy < ativ.gl && ativ.grade[cy][cx] != '*') { if (cx == x && cy == y) na_palavra = true; cx += dx; cy += dy; }
                }
                if (na_palavra) fundo = 0xFFF3B0;
                if (x == ax && y == ay) fundo = 0xFFD52E;
                if (M.resolvida) fundo = 0xDCEFD0;
            } else {
                ch = M.letra[y][x];
                if (M.marcada[y][x]) fundo = 0xFFE35A;
                if (x == M.ini_x && y == M.ini_y) fundo = 0x9AD8F0;
            }
            g_retangulo(j, sx, sy, l, l, fundo);
            g_moldura(j, sx, sy, l + 1, l + 1, 0x8A939C);
            if (cruz && M.numero[y][x]) {
                char n[4];
                formatar_texto(n, sizeof n, "%d", M.numero[y][x]);
                g_texto(j, sx + 2, sy, n, 0x6A737C);
            }
            if (ch && ch != ' ') letra(sx + l / 2 - (l >= 32 ? 8 : 4), sy + l / 2 - (l >= 32 ? 16 : 8), (uint8_t)ch, l >= 32 ? 2 : 1, 0x1E2328);
        }
    if (!cruz) {
        /* Ao lado, a lista das palavras, riscadas quando achadas. */
        int x = M.gx + ativ.gc * l + 24, y = M.gy;
        for (int w = 0; w < ativ.n_palavras && x < LARG - 60; w++) {
            g_texto(j, x, y + w * 20, ativ.palavra[w], M.achou[w] ? 0x1F8A3A : COR_TEXTO);
            if (M.achou[w]) g_retangulo(j, x, y + w * 20 + 8, g_largura_texto(ativ.palavra[w]), 1, 0x1F8A3A);
        }
    }
}

static botao_t bt_lista, bt_ant, bt_rec, bt_prox;

static void desenhar_atividade(void)
{
    g_retangulo(j, 0, 0, LARG, ALT, ativ.cor_fundo);
    g_retangulo(j, 0, 0, LARG, TOPO_A, 0x1F6B2A);
    g_retangulo(j, 0, TOPO_A - 3, LARG, 3, 0xFFD52E);
    g_texto(j, 12, 4, proj.titulo, 0xFFF3B0);
    char t[160];
    formatar_texto(t, sizeof t, "%d de %d: %s (%s)", atual + 1, proj.n, ativ.nome, nome_do_tipo(ativ.tipo));
    g_texto(j, 12, 20, t, 0xDCEFD0);
    /* A mensagem: a do começo, a do fim, a pista da cruzadinha ou um aviso. */
    const char *msg = ativ.msg_inicial;
    if (M.resolvida && ativ.msg_final) msg = ativ.msg_final;
    else if (M.resolvida) msg = "Muito bem!";
    if (ativ.tipo == AT_CRUZADINHA && motor_pista() && !M.resolvida) msg = motor_pista();
    if (ativ.tipo == AT_OUTRA) msg = "Este tipo de atividade ainda não funciona aqui. Clique em Próxima.";
    if (msg && *msg) {
        g_retangulo(j, 8, TOPO_A + 4, LARG - 16, MSG_A - 4, M.resolvida ? 0xDCEFD0 : 0xFFF3B0);
        g_moldura(j, 8, TOPO_A + 4, LARG - 16, MSG_A - 4, 0xB07A12);
        texto_caixa(8, TOPO_A + 4, LARG - 16, MSG_A - 4, msg, COR_TEXTO);
    }
    if (motor_eh_grade()) desenhar_grade();
    else
        for (int p = 0; p < 2; p++)
            if (M.p[p].existe) for (int i = 0; i < M.p[p].n; i++) desenhar_caixa(p, i);
    g_retangulo(j, 0, ALT - BASE_A, LARG, BASE_A, 0xE6E9EC);
    g_retangulo(j, 0, ALT - BASE_A, LARG, 1, COR_BORDA);
    botao_desenhar(j, &bt_lista);
    botao_desenhar(j, &bt_ant);
    botao_desenhar(j, &bt_rec);
    botao_desenhar(j, &bt_prox);
    if (M.alvo > 0) {
        formatar_texto(t, sizeof t, "Acertos: %d de %d   Tentativas: %d   %d s", M.acertos, M.alvo, M.tentativas,
                       (int)(tempo_ms() - t_inicio) / 1000);
        g_texto(j, LARG - 12 - g_largura_texto(t), ALT - BASE_A + 12, t, COR_TEXTO);
    }
    jan_mostrar(j);
}

/* ------------------------------------------------------------ atividades */

static void comecar(int k)
{
    if (k < 0 || k >= proj.n) return;
    if (atual >= 0) atividade_liberar(&ativ);
    imagens_limpar();
    atual = k;
    projeto_atividade(&proj, k, &ativ);
    uint32_t s = 0;
    bytes_aleatorios(&s, sizeof s);
    motor_iniciar(&ativ, s | 1);
    motor_dispor(8, AREA_Y, LARG - 16, AREA_A);
    t_inicio = tempo_ms();
    erro_p = -1;
    registrar("jclic: atividade %d de %d: %s (%s)", k + 1, proj.n, ativ.nome, nome_do_tipo(ativ.tipo));
    for (int p = 0; p < 2; p++)
        for (int i = 0; M.p[p].existe && i < M.p[p].n; i++) {
            bloco_t *c = &M.p[p].c[i];
            registrar("jclic: caixa p%d i%d orig %d em (%d,%d,%d,%d)", p, i, c->orig, c->x, c->y, c->w, c->h);
        }
    if (motor_eh_grade()) {
        registrar("jclic: grade x0=%d y0=%d casa=%d", M.gx, M.gy, M.gcw);
        for (int y = 0; y < ativ.gl; y++) registrar("jclic: g%d %s", y, ativ.tipo == AT_CACA_PALAVRAS ? M.letra[y] : ativ.grade[y]);
    }
    desenhar_atividade();
}

static void depois_da_jogada(int p, int i)
{
    if (M.som == SOM_ERRO) {
        erro_p = p; erro_i = i; erro_ate = tempo_ms() + 450;
        static const nota_t n[] = { { 220, 120 } };
        tocar(n, 1, BIP_SUBSTITUIR);
    } else if (M.som == SOM_ACERTO) {
        static const nota_t n[] = { { 660, 60 }, { 880, 80 } };
        tocar(n, 2, BIP_SUBSTITUIR);
    } else if (M.som == SOM_FIM) {
        static const nota_t n[] = { { 523, 80 }, { 659, 80 }, { 784, 80 }, { 1047, 200 } };
        tocar(n, 4, BIP_SUBSTITUIR);
    }
    if (M.som != SOM_NADA) registrar("jclic: acertos %d de %d tentativas %d", M.acertos, M.alvo, M.tentativas);
    if (M.som == SOM_FIM) registrar("jclic: resolvida (%s)", ativ.nome);
    desenhar_atividade();
}

/* ------------------------------------------------------------ a lista de projetos */

static lista_t lista;
static char nomes[MAX_PROJ][64];
static char caminhos[MAX_PROJ][128];

static bool eh_projeto(const char *n)
{
    int l = (int)tam_texto(n);
    return (l > 10 && !compara_texto(n + l - 10, ".jclic.zip")) || (l > 6 && !compara_texto(n + l - 6, ".jclic"));
}

static void listar_pasta(const char *p, const char *marca)
{
    entrada_pasta_t e;
    for (int k = 0; listar(p, k, &e) > 0 && lista.qtd < MAX_PROJ; k++) {
        if (!eh_projeto(e.nome)) continue;
        formatar_texto(nomes[lista.qtd], sizeof nomes[0], "%s%s", e.nome, marca);
        formatar_texto(caminhos[lista.qtd], sizeof caminhos[0], "%s/%s", p, e.nome);
        lista.qtd++;
    }
}

static void mostrar_lista(void)
{
    fechar_projeto();
    na_lista = true;
    lista.x = 16; lista.y = 76; lista.l = LARG - 32; lista.a = ALT - 76 - 60;
    lista.itens = nomes;
    lista.qtd = 0;
    lista.topo = 0;
    listar_pasta("/jogos/jclic", "");
    listar_pasta("/usb/jclic", "  (pendrive)");
    listar_pasta("/usb", "  (pendrive)");
    lista.sel = lista.qtd ? 0 : -1;
    g_retangulo(j, 0, 0, LARG, ALT, COR_FUNDO);
    g_retangulo(j, 0, 0, LARG, 60, 0x1F6B2A);
    g_retangulo(j, 0, 57, LARG, 3, 0xFFD52E);
    g_texto(j, 16, 10, "Atividades JClic", 0xFFF3B0);
    g_texto(j, 16, 32, "Escolha um projeto (Enter ou duplo clique).", 0xDCEFD0);
    if (lista.qtd) lista_desenhar(j, &lista);
    else g_texto(j, 16, 90, "Nenhum projeto em /jogos/jclic nem no pendrive.", COR_TEXTO);
    g_texto(j, 16, ALT - 46, "Projetos .jclic.zip (ou .jclic com as imagens ao lado) em /usb ou /usb/jclic.", COR_TEXTO_FRACO);
    g_texto(j, 16, ALT - 26, "N/P: próxima e anterior  F5: recomeçar  Esc: volta para esta lista", COR_TEXTO_FRACO);
    titulo_janela(j->id, "Atividades JClic");
    jan_mostrar(j);
    for (int k = 0; k < lista.qtd; k++) registrar("jclic: item %d %s", k, caminhos[k]);
    registrar("jclic: lista pronta (%d projetos)", lista.qtd);
}

static void carregar(const char *caminho)
{
    char erro[120];
    if (!abrir_projeto(caminho, erro, sizeof erro)) {
        registrar("jclic: %s: %s", caminho, erro);
        char m[200];
        formatar_texto(m, sizeof m, "Não consegui abrir o projeto: %s.", erro);
        mensagem("Atividades JClic", m);
        mostrar_lista();
        return;
    }
    na_lista = false;
    char t[128];
    formatar_texto(t, sizeof t, "JClic - %s", proj.titulo);
    titulo_janela(j->id, t);
    comecar(0);
}

/* ------------------------------------------------------------ principal */

int principal(int argc, char **argv)
{
    j = jan_abrir("Atividades JClic", LARG, ALT);
    if (!j) return 1;
    bt_lista = (botao_t){ 12, ALT - BASE_A + 8, 70, 24, "Lista", false };
    bt_ant = (botao_t){ 92, ALT - BASE_A + 8, 100, 24, "< Anterior", false };
    bt_rec = (botao_t){ 202, ALT - BASE_A + 8, 100, 24, "Recomeçar", false };
    bt_prox = (botao_t){ 312, ALT - BASE_A + 8, 100, 24, "Próxima >", false };
    if (argc > 1) carregar(argv[1]);
    else mostrar_lista();
    evento_t e;
    unsigned ultimo_segundo = 0;
    for (;;) {
        /* Na atividade, acorda de vez em quando: o relógio dos acertos, as
         * cartas da memória que voltam e o vermelho do erro que some. */
        if (proximo_evento(j->id, &e, na_lista) != 1) {
            if (na_lista) continue;
            bool mudou = motor_tique(tempo_ms());
            if (erro_p >= 0 && tempo_ms() >= erro_ate) { erro_p = -1; mudou = true; }
            unsigned s = (tempo_ms() - t_inicio) / 1000;
            if (s != ultimo_segundo && M.alvo > 0 && !M.resolvida) { ultimo_segundo = s; mudou = true; }
            if (mudou) desenhar_atividade();
            dormir(50);
            continue;
        }
        if (e.tipo == EV_FECHAR) break;
        if (na_lista) {
            int r = lista_tratar(j, &lista, &e);
            if (r == LISTA_ATIVOU && lista.sel >= 0) carregar(caminhos[lista.sel]);
            else if (r == LISTA_SELECIONOU) jan_mostrar(j);
            continue;
        }
        if (botao_tratar(j, &bt_lista, &e)) { mostrar_lista(); continue; }
        if (botao_tratar(j, &bt_ant, &e)) { comecar(atual - 1); continue; }
        if (botao_tratar(j, &bt_rec, &e)) { comecar(atual); continue; }
        if (botao_tratar(j, &bt_prox, &e)) { comecar(atual + 1); continue; }
        if (e.tipo == EV_CLIQUE && e.y < ALT - BASE_A) {
            int p, i;
            if (motor_eh_grade()) {
                if (motor_casa_em(e.x, e.y, &p, &i)) { motor_clique_casa(p, i); depois_da_jogada(-1, -1); registrar("jclic: casa %d,%d", p, i); }
            } else if (motor_caixa_em(e.x, e.y, &p, &i)) {
                motor_clique(p, i);
                depois_da_jogada(p, i);
                registrar("jclic: clique p%d i%d", p, i);
            }
        } else if (e.tipo == EV_TECLA) {
            unsigned t = e.tecla;
            bool cruz = ativ.tipo == AT_CRUZADINHA && M.foco_x >= 0;
            if (t == TECLA_ESC) mostrar_lista();
            else if (t == TECLA_F5) comecar(atual);
            else if (cruz && (t == 8 || (t >= 'a' && t <= 'z') || (t >= 'A' && t <= 'Z') || (t >= 0xC0 && t <= 0xFE))) {
                motor_letra((int)t);
                depois_da_jogada(-1, -1);
            } else if (t == TECLA_DIREITA || t == 'n' || t == 'N') comecar(atual + 1);
            else if (t == TECLA_ESQUERDA || t == 'p' || t == 'P') comecar(atual - 1);
        }
    }
    fechar_projeto();
    jan_fechar(j);
    return 0;
}
