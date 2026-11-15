/* visualizador [imagem] - mostra imagens PNG, JPEG, GIF, WebP e BMP (H87).
 *
 * Os decodificadores são os do Cipó (userland/navegador/imagens.c, png.c,
 * jpeg.c e terceiros/libwebp-vp8), ligados a este programa pelo Makefile; o
 * BMP (24 e 32 bits, sem compressão) é lido aqui.
 *
 * A imagem abre ajustada à janela (sem ampliar as pequenas). Ao reduzir,
 * cada pixel da tela é a média dos pixels da imagem que ele cobre (a redução
 * fica guardada até mudar o zoom ou a janela); ao ampliar, pixel mais
 * próximo, para ver os pixels.
 *
 * Teclas: + e - zoom; 0 ajusta; 1 tamanho real; setas, PgUp e PgDn passam
 * para a imagem anterior ou a próxima da mesma pasta (com zoom maior que a
 * janela, as setas movem a imagem); arrastar com o mouse também move;
 * A abre outro arquivo; Esc fecha. */
#include "sagui.h"
#include "gui.h"

bool imagem_decodificar(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a);   /* navegador/imagens.c */

#define BARRA   34
#define RODAPE  24
#define FUNDO   0x1E1F22
#define MAX_ARQ 256
#define MAX_BYTES (24u * 1024 * 1024)

static jan_t *j;
static char caminho[200];
static uint32_t *img;          /* 0xAARRGGBB */
static int il, ia;
static int zoom_pct;           /* 0 = ajustar à janela */
static int dx, dy;             /* deslocamento quando a imagem é maior que a área */
static char erro[120];
static uint32_t *reduzida;     /* cache da redução */
static int rl, ra, r_escala;

static char irmaos[MAX_ARQ][64];
static int qtd_irmaos, indice;

static botao_t bts[7] = {
    { 8, 4, 64, 26, "Abrir", false }, { 80, 4, 34, 26, "<", false }, { 118, 4, 34, 26, ">", false },
    { 164, 4, 34, 26, "-", false }, { 202, 4, 34, 26, "+", false }, { 244, 4, 70, 26, "Ajustar", false },
    { 318, 4, 52, 26, "100%", false },
};

static bool eh_imagem(const char *nome)
{
    const char *p = acha_ultimo_char(nome, '.');
    if (!p) return false;
    char e[8];
    int n = 0;
    for (; p[n] && n < 7; n++) e[n] = (char)(p[n] >= 'A' && p[n] <= 'Z' ? p[n] + 32 : p[n]);
    e[n] = 0;
    return !compara_texto(e, ".png") || !compara_texto(e, ".jpg") || !compara_texto(e, ".jpeg") ||
           !compara_texto(e, ".gif") || !compara_texto(e, ".webp") || !compara_texto(e, ".bmp");
}

/* ---------------------------------------------------------------- BMP */

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static bool bmp_ler(const uint8_t *d, unsigned n, uint32_t **saida, int *pl, int *pa)
{
    if (n < 54 || d[0] != 'B' || d[1] != 'M') return false;
    uint32_t inicio = le32(d + 10), cab = le32(d + 14);
    int32_t l = (int32_t)le32(d + 18), a = (int32_t)le32(d + 22);
    int bpp = d[28] | d[29] << 8;
    uint32_t comp = le32(d + 30);
    if (cab < 40 || (bpp != 24 && bpp != 32) || (comp != 0 && !(comp == 3 && bpp == 32))) return false;
    bool de_cima = a < 0;
    if (a < 0) a = -a;
    if (l <= 0 || a <= 0 || (uint64_t)l * (uint64_t)a > 4096u * 4096u) return false;
    uint32_t passo = (((uint32_t)l * (uint32_t)bpp + 31) / 32) * 4;
    if ((uint64_t)inicio + (uint64_t)passo * (uint32_t)a > n) return false;
    uint32_t *px = aloca((size_t)l * (size_t)a * 4);
    if (!px) return false;
    for (int y = 0; y < a; y++) {
        const uint8_t *s = d + inicio + passo * (uint32_t)(de_cima ? y : a - 1 - y);
        uint32_t *o = px + (size_t)y * (size_t)l;
        for (int x = 0; x < l; x++, s += bpp / 8)
            o[x] = 0xFF000000u | (uint32_t)s[2] << 16 | (uint32_t)s[1] << 8 | s[0];
    }
    *saida = px; *pl = l; *pa = a;
    return true;
}

/* ---------------------------------------------------------------- arquivos */

static void listar_irmaos(void)
{
    char pasta[200];
    copia_texto(pasta, caminho, sizeof pasta);
    char *b = acha_ultimo_char(pasta, '/');
    const char *nome = caminho;
    if (b) { nome = b + 1; if (b == pasta) pasta[1] = 0; else *b = 0; }
    else copia_texto(pasta, ".", sizeof pasta);
    qtd_irmaos = 0;
    indice = 0;
    entrada_pasta_t e;
    for (int i = 0; qtd_irmaos < MAX_ARQ && listar(pasta, i, &e) == 1; i++) {
        if (e.tipo == TIPO_PASTA || !eh_imagem(e.nome)) continue;
        /* em ordem alfabética: a FAT do pendrive lista na ordem em que foram gravados */
        int k = qtd_irmaos++;
        while (k > 0 && compara_texto(irmaos[k - 1], e.nome) > 0) { copia_texto(irmaos[k], irmaos[k - 1], 64); k--; }
        copia_texto(irmaos[k], e.nome, 64);
    }
    for (int i = 0; i < qtd_irmaos; i++) if (!compara_texto(irmaos[i], nome)) indice = i;
}

static void irmao(int passo)
{
    if (qtd_irmaos < 2) return;
    indice = (indice + passo + qtd_irmaos) % qtd_irmaos;
    char *b = acha_ultimo_char(caminho, '/');
    if (b) b[1] = 0; else caminho[0] = 0;
    junta_texto(caminho, irmaos[indice], sizeof caminho);
}

static void carregar(void)
{
    libera(img); img = NULL;
    libera(reduzida); reduzida = NULL;
    erro[0] = 0;
    zoom_pct = 0;
    dx = dy = 0;
    info_arquivo_t inf;
    int r = info_arquivo(caminho, &inf);
    if (r < 0) { formatar_texto(erro, sizeof erro, "%s", nome_erro(r)); goto fim; }
    if (inf.tamanho > MAX_BYTES) { copia_texto(erro, "arquivo grande demais", sizeof erro); goto fim; }
    uint8_t *d = aloca(inf.tamanho + 1);
    if (!d) { copia_texto(erro, "sem memória", sizeof erro); goto fim; }
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) { libera(d); formatar_texto(erro, sizeof erro, "%s", nome_erro(fd)); goto fim; }
    unsigned tem = 0;
    while (tem < inf.tamanho) { int k = ler(fd, d + tem, inf.tamanho - tem); if (k <= 0) break; tem += (unsigned)k; }
    fechar(fd);
    unsigned t0 = tempo_ms();
    if (!bmp_ler(d, tem, &img, &il, &ia) && !imagem_decodificar(d, tem, &img, &il, &ia)) {
        img = NULL;
        copia_texto(erro, "formato não reconhecido ou arquivo estragado", sizeof erro);
    }
    libera(d);
    if (img) registrar("visualizador: abriu %s (%dx%d, %u ms)", caminho, il, ia, tempo_ms() - t0);
fim:
    if (erro[0]) registrar("visualizador: erro em %s: %s", caminho, erro);
    const char *nome = acha_ultimo_char(caminho, '/');
    char t[96];
    formatar_texto(t, sizeof t, "%s - Visualizador", nome ? nome + 1 : caminho);
    titulo_janela(j->id, t);
}

/* ---------------------------------------------------------------- desenho */

static int area_l(void) { return j->larg; }
static int area_a(void) { return j->alt - BARRA - RODAPE; }

/* Escala em milésimos. */
static int escala(void)
{
    if (!img) return 1000;
    if (zoom_pct) return zoom_pct * 10;
    int ex = area_l() * 1000 / il, ey = area_a() * 1000 / ia;
    int e = ex < ey ? ex : ey;
    return e > 1000 ? 1000 : (e < 1 ? 1 : e);
}

static uint32_t misturar(uint32_t p)
{
    uint32_t al = p >> 24;
    if (al == 255) return p & 0xFFFFFF;
    uint32_t r = ((p >> 16 & 255) * al + (FUNDO >> 16 & 255) * (255 - al)) / 255;
    uint32_t g = ((p >> 8 & 255) * al + (FUNDO >> 8 & 255) * (255 - al)) / 255;
    uint32_t b = ((p & 255) * al + (FUNDO & 255) * (255 - al)) / 255;
    return r << 16 | g << 8 | b;
}

/* Redução por média de área (escala <= 1000). */
static void reduzir(int e)
{
    int nl = il * e / 1000, na = ia * e / 1000;
    if (nl < 1) nl = 1;
    if (na < 1) na = 1;
    if (reduzida && rl == nl && ra == na && r_escala == e) return;
    libera(reduzida);
    reduzida = aloca((size_t)nl * (size_t)na * 4);
    if (!reduzida) return;
    rl = nl; ra = na; r_escala = e;
    for (int y = 0; y < na; y++) {
        int y0 = y * ia / na, y1 = (y + 1) * ia / na;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < nl; x++) {
            int x0 = x * il / nl, x1 = (x + 1) * il / nl;
            if (x1 <= x0) x1 = x0 + 1;
            uint32_t sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) {
                const uint32_t *s = img + (size_t)yy * (size_t)il;
                for (int xx = x0; xx < x1; xx++) {
                    uint32_t p = s[xx];
                    sa += p >> 24; sr += p >> 16 & 255; sg += p >> 8 & 255; sb += p & 255; n++;
                }
            }
            reduzida[(size_t)y * (size_t)nl + (size_t)x] = (sa / n) << 24 | (sr / n) << 16 | (sg / n) << 8 | (sb / n);
        }
    }
}

static void limitar_deslocamento(int vl, int va)
{
    int mx = vl - area_l(), my = va - area_a();
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (dx > mx) dx = mx;
    if (dy > my) dy = my;
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
}

static void desenhar(void)
{
    g_retangulo(j, 0, 0, j->larg, BARRA, 0xE6E9EC);
    g_retangulo(j, 0, BARRA - 1, j->larg, 1, COR_BORDA);
    for (int i = 0; i < 7; i++) botao_desenhar(j, &bts[i]);
    int al = area_l(), aa = area_a();
    g_retangulo(j, 0, BARRA, al, aa, FUNDO);
    int e = escala();
    char rod[200];
    if (img) {
        int vl = il * e / 1000, va = ia * e / 1000;   /* até 4096 * 16000: cabe em 32 bits */
        if (vl < 1) vl = 1;
        if (va < 1) va = 1;
        limitar_deslocamento(vl, va);
        int ox = vl < al ? (al - vl) / 2 : -dx, oy = va < aa ? (aa - va) / 2 : -dy;
        if (e <= 1000) reduzir(e);
        for (int y = 0; y < aa; y++) {
            int sy = y - oy;
            if (sy < 0 || sy >= va) continue;
            uint32_t *o = j->px + (size_t)(BARRA + y) * (size_t)j->larg;
            for (int x = 0; x < al; x++) {
                int sx = x - ox;
                if (sx < 0 || sx >= vl) continue;
                uint32_t p;
                if (e <= 1000 && reduzida) p = reduzida[(size_t)(sy < ra ? sy : ra - 1) * (size_t)rl + (size_t)(sx < rl ? sx : rl - 1)];
                else p = img[(size_t)(sy * 1000 / e) * (size_t)il + (size_t)(sx * 1000 / e)];
                o[x] = misturar(p);
            }
        }
        formatar_texto(rod, sizeof rod, "%dx%d  |  %d%%%s  |  %d de %d  |  +/- zoom, 0 ajusta, 1 real, setas passam",
                       il, ia, e / 10, zoom_pct ? "" : " (ajustada)", indice + 1, qtd_irmaos);
    } else {
        const char *msg = caminho[0] ? erro : "Nenhuma imagem. Clique em Abrir.";
        g_texto(j, (al - g_largura_texto(msg)) / 2, BARRA + aa / 2 - 8, msg, 0xE0E0E0);
        formatar_texto(rod, sizeof rod, "%s", caminho[0] ? caminho : "PNG, JPEG, GIF, WebP e BMP");
    }
    g_retangulo(j, 0, j->alt - RODAPE, j->larg, RODAPE, 0xE6E9EC);
    g_retangulo(j, 0, j->alt - RODAPE, j->larg, 1, COR_BORDA);
    g_texto_n(j, 8, j->alt - RODAPE + 4, rod, (j->larg - 16) / LETRA_L, COR_TEXTO_FRACO);
    jan_mostrar(j);
}

/* ---------------------------------------------------------------- ações */

static void mudar_zoom(int novo)
{
    if (!img) return;
    int antes = escala();
    if (novo < 2) novo = 2;
    if (novo > 1600) novo = 1600;
    /* mantém o centro da área no mesmo ponto da imagem */
    int cx = dx + area_l() / 2, cy = dy + area_a() / 2;
    zoom_pct = novo;
    int depois = escala();
    dx = (int)((double)cx * depois / antes) - area_l() / 2;   /* sem divisão de 64 bits (libgcc) */
    dy = (int)((double)cy * depois / antes) - area_a() / 2;
    registrar("visualizador: zoom %d%%", zoom_pct);
}

/* Passos de 25%: 100, 125, 156...; nos zooms pequenos, pelo menos 1 ponto. */
static void ampliar(void) { int z = escala() / 10; mudar_zoom(z * 5 / 4 > z ? z * 5 / 4 : z + 1); }
static void reduzir_zoom(void) { int z = escala() / 10; mudar_zoom(z * 4 / 5 < z ? z * 4 / 5 : z - 1); }

static void abrir_outro(void)
{
    char novo[200];
    copia_texto(novo, caminho, sizeof novo);
    if (!pedir_texto("Abrir imagem", "Caminho da imagem:", novo, sizeof novo) || !novo[0]) return;
    copia_texto(caminho, novo, sizeof caminho);
    listar_irmaos();
    carregar();
}

static bool imagem_maior(void)
{
    int e = escala();
    return img && (il * e / 1000 > area_l() || ia * e / 1000 > area_a());
}

int principal(int argc, char **argv)
{
    j = jan_abrir("Visualizador", 760, 540);   /* cabe em 1024x768 acima da doca */
    if (!j) return 1;
    jan_redimensionavel(j, 420, 260);
    if (argc > 1) {
        copia_texto(caminho, argv[1], sizeof caminho);
        listar_irmaos();
        carregar();
    }
    desenhar();
    registrar("visualizador: pronto");
    evento_t e;
    bool arrastando = false;
    int ax = 0, ay = 0;
    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        bool redesenhar = false;
        if (e.tipo == EV_FECHAR) break;
        if (e.tipo == EV_REDIMENSIONAR) { jan_novo_tamanho(j, &e); redesenhar = true; }
        if (botao_tratar(j, &bts[0], &e)) { abrir_outro(); redesenhar = true; }
        if (botao_tratar(j, &bts[1], &e)) { irmao(-1); carregar(); redesenhar = true; }
        if (botao_tratar(j, &bts[2], &e)) { irmao(1); carregar(); redesenhar = true; }
        if (botao_tratar(j, &bts[3], &e)) { reduzir_zoom(); redesenhar = true; }
        if (botao_tratar(j, &bts[4], &e)) { ampliar(); redesenhar = true; }
        if (botao_tratar(j, &bts[5], &e)) { zoom_pct = 0; dx = dy = 0; redesenhar = true; }
        if (botao_tratar(j, &bts[6], &e)) { mudar_zoom(100); redesenhar = true; }
        if (e.tipo == EV_CLIQUE && e.y >= BARRA && e.y < j->alt - RODAPE) { arrastando = true; ax = e.x; ay = e.y; }
        if (e.tipo == EV_SOLTOU) arrastando = false;
        if (e.tipo == EV_MOVER && arrastando && (e.botoes & 1)) {
            dx -= e.x - ax; dy -= e.y - ay; ax = e.x; ay = e.y;
            redesenhar = true;
        }
        if (e.tipo == EV_TECLA) {
            unsigned t = e.tecla;
            bool mover = imagem_maior();
            redesenhar = true;
            if (t == TECLA_ESC || t == 27) break;
            else if (t == '+' || t == '=') ampliar();
            else if (t == '-') reduzir_zoom();
            else if (t == '0') { zoom_pct = 0; dx = dy = 0; registrar("visualizador: zoom ajustado"); }
            else if (t == '1') mudar_zoom(100);
            else if (t == 'a' || t == 'A') abrir_outro();
            else if (t == TECLA_PGDN || t == ' ' || (!mover && (t == TECLA_DIREITA || t == TECLA_BAIXO))) { irmao(1); carregar(); }
            else if (t == TECLA_PGUP || (!mover && (t == TECLA_ESQUERDA || t == TECLA_CIMA))) { irmao(-1); carregar(); }
            else if (t == TECLA_DIREITA) dx += 64;
            else if (t == TECLA_ESQUERDA) dx -= 64;
            else if (t == TECLA_BAIXO) dy += 64;
            else if (t == TECLA_CIMA) dy -= 64;
            else if (t == TECLA_HOME) { irmao(-indice); carregar(); }
            else redesenhar = false;
        }
        if (redesenhar) desenhar();
    }
    jan_fechar(j);
    return 0;
}
