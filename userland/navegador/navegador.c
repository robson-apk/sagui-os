/* navegador.c - o Cipó, o navegador do SaguiOS (H62; o nome e o motor, H82).
 *
 * O nome (sugerido por M4R4G0N no pull request #3, e usado por Robson de
 * Lima Barbosa no #6): o sagui atravessa a mata pulando de cipó em cipó; o
 * navegador atravessa a web pulando de link em link.
 *
 *   [<] [>] [R] [ endereço ......................... ] [Ir] [+Fav] [Favoritos]
 *   +------------------------------------------------------------------+-+
 *   |  a página (o motor pinta; a rolagem é só um deslocamento)        |#|
 *   +------------------------------------------------------------------+-+
 *   status: o endereço do link sob o mouse, "Carregando...", erros
 *
 * Endereços aceitos: http://, https://, um caminho de arquivo do SaguiOS
 * (/usb/pagina.html) e as páginas internas sagui:inicio e sagui:favoritos.
 * Sem "http" na frente, vai https://.
 *
 * Como uma página abre (2.1): o corpo chega inteiro (rede.c, com cookies),
 * vira UTF-8, os scripts rodam (js/, que devolve o HTML do DOM depois do
 * "load"), e o motor (motor/) lê o HTML e o CSS, diagrama e pinta. Depois as
 * fontes da web e as imagens chegam, umas poucas por vez, e a cada leva o
 * layout é refeito. Se o usuário fizer alguma coisa no meio, o resto fica
 * para trás. Com JavaScript, a página continua viva: timers, animações e
 * cliques mudam o DOM, e a página é relida no mesmo ponto da rolagem.
 *
 * A janela, a barra, o histórico e os favoritos são da 2.0; a ligação com o
 * motor e o JavaScript (ganchos, cache, iframes, página viva, campos) veio
 * do Cipó do pull request #6 (Robson de Lima Barbosa), adaptada à 2.0.
 *
 * Teclado: setas, PgUp/PgDn, Home/End rolam; Backspace volta; Ctrl+L vai
 * para o endereço. Favoritos ficam em /usb/.saguios/favoritos.txt (ou
 * /tmp/favoritos.txt sem pendrive), uma linha "endereço<TAB>título" cada. */
#include "navegador.h"

#define NOME_NAVEGADOR "Cipó"

/* O tamanho da janela muda (H78): começa em 960 x 660. */
static int larg_jan = 960, alt_jan = 660;
#define LARG       larg_jan
#define ALT        alt_jan
#define BARRA      34
#define STATUS     20
#define ROLAGEM_L  14
#define VISTA_L    (LARG - ROLAGEM_L)
#define VISTA_A    (ALT - BARRA - STATUS)
#define MAX_URL    MAX_URL_NAV
#define MAX_PAGINA (6 * 1024 * 1024)
#define MAX_RECURSO (6 * 1024 * 1024)
#define MAX_HIST   64

static jan_t *j;
static motor_t *motor;            /* a página atual */
static int altura;                /* altura da página diagramada */
static char url_atual[MAX_URL];
static int rolagem;
static char status[200];
static char link_sob_mouse[MAX_URL];
static char titulo_js[128];       /* document.title = (vale mais que o <title>) */

static struct { char url[MAX_URL]; int rolagem; } hist[MAX_HIST];
static int hist_qtd, hist_pos = -1;

/* Barra de endereço própria (a caixa_t do kit só guarda 128 letras). */
static struct { char t[MAX_URL]; int cursor, inicio; bool foco, tudo; } ende;   /* tudo: texto todo selecionado */

static struct { int x, l; const char *texto; } botoes[] = {
    { 6, 26, "<" }, { 34, 26, ">" }, { 62, 26, "R" },
    { 0, 34, "Ir" }, { 0, 50, "+Fav" }, { 0, 88, "Favoritos" },
};
#define BT_VOLTAR 0
#define BT_AVANCAR 1
#define BT_RECARREGAR 2
#define BT_IR 3
#define BT_FAV 4
#define BT_FAVORITOS 5
#define ENDE_X 94
#define ENDE_L (LARG - ENDE_X - 6 - 34 - 50 - 88 - 18)

static void carregar(const char *pedido, bool novo_no_historico, int rolagem_inicial);

/* ------------------------------------------------------------ desenho */

static void limitar_rolagem(void)
{
    int max = altura - VISTA_A;
    if (rolagem > max) rolagem = max;
    if (rolagem < 0) rolagem = 0;
}

static void desenhar_botao(int i, bool apertado)
{
    int x = botoes[i].x, l = botoes[i].l;
    g_retangulo(j, x, 5, l, 24, apertado ? COR_BOTAO_APERTADO : COR_BOTAO);
    g_moldura(j, x, 5, l, 24, COR_BORDA);
    bool apagado = (i == BT_VOLTAR && hist_pos <= 0) || (i == BT_AVANCAR && hist_pos >= hist_qtd - 1);
    g_texto(j, x + (l - g_largura_texto(botoes[i].texto)) / 2, 9, botoes[i].texto, apagado ? COR_BORDA : COR_TEXTO);
}

static void desenhar_endereco(void)
{
    g_retangulo(j, ENDE_X, 5, ENDE_L, 24, COR_BRANCO);
    g_moldura(j, ENDE_X, 5, ENDE_L, 24, ende.foco ? COR_SELECAO : COR_BORDA);
    int cabe = (ENDE_L - 10) / LETRA_L, n = (int)tam_texto(ende.t);
    if (ende.cursor < ende.inicio) ende.inicio = ende.cursor;
    if (ende.cursor > ende.inicio + cabe) ende.inicio = ende.cursor - cabe;
    if (!ende.foco) ende.inicio = 0;
    int m = n - ende.inicio < cabe ? n - ende.inicio : cabe;
    if (ende.foco && ende.tudo && m > 0) g_retangulo(j, ENDE_X + 4, 8, m * LETRA_L + 2, 18, COR_SELECAO);
    if (m > 0) g_texto_n(j, ENDE_X + 5, 9, ende.t + ende.inicio, m, ende.foco && ende.tudo ? COR_BRANCO : COR_TEXTO);
    if (ende.foco) g_retangulo(j, ENDE_X + 5 + (ende.cursor - ende.inicio) * LETRA_L, 8, 1, 18, COR_TEXTO);
}

static void desenhar_barra(void)
{
    g_retangulo(j, 0, 0, LARG, BARRA, COR_FUNDO);
    g_retangulo(j, 0, BARRA - 1, LARG, 1, COR_BORDA);
    for (int i = 0; i < 6; i++) desenhar_botao(i, false);
    desenhar_endereco();
}

/* A interface do SaguiOS desenha em Latin-1; a página é UTF-8. */
static void utf8_para_sistema(char *s)
{
    int k = 0, n = (int)tam_texto(s);
    for (int i = 0; i < n;) {
        uint32_t cp;
        i += utf8_ler(s + i, n - i, &cp);
        s[k++] = cp < 0x100 ? (char)cp : cp == 0x2013 || cp == 0x2014 ? '-' : cp == 0x2019 || cp == 0x2018 ? '\'' : '?';
    }
    s[k] = 0;
}

static void desenhar_status(void)
{
    g_retangulo(j, 0, ALT - STATUS, LARG, STATUS, COR_FUNDO);
    g_retangulo(j, 0, ALT - STATUS, LARG, 1, COR_BORDA);
    const char *t = link_sob_mouse[0] ? link_sob_mouse : status;
    int cabe = (LARG - 12) / LETRA_L;
    g_texto_n(j, 6, ALT - STATUS + 2, t, cabe, COR_TEXTO_FRACO);
}

static void desenhar_rolagem(void)
{
    int x = VISTA_L, y = BARRA;
    g_retangulo(j, x, y, ROLAGEM_L, VISTA_A, 0xE6E8EA);
    if (altura <= VISTA_A) return;
    int a = VISTA_A * VISTA_A / altura;
    if (a < 20) a = 20;
    int ya = y + (int)((int)rolagem * (VISTA_A - a) / (altura - VISTA_A));
    g_retangulo(j, x + 2, ya, ROLAGEM_L - 4, a, 0xA2A9B1);
}

/* Só a página e a barra de rolagem. */
static void desenhar_pagina(void)
{
    if (motor) motor_pintar(motor, j->px + BARRA * j->larg, VISTA_L, j->larg, rolagem, VISTA_A);
    else g_retangulo(j, 0, BARRA, VISTA_L, VISTA_A, COR_BRANCO);
    desenhar_rolagem();
}

static void desenhar_tudo(void)
{
    desenhar_pagina();
    desenhar_barra();
    desenhar_status();
    jan_mostrar(j);
}

static void definir_status(const char *t)
{
    copia_texto(status, t, sizeof status);
    desenhar_status();
    jan_mostrar_area(j, 0, ALT - STATUS, LARG, STATUS);
}

static void definir_endereco(const char *u)
{
    copia_texto(ende.t, u, sizeof ende.t);
    ende.cursor = (int)tam_texto(ende.t);
    ende.inicio = 0;
}

/* ------------------------------------------------------------ texto da página */

/* O motor trabalha em UTF-8. charset: 1 UTF-8, 0 Latin-1/Windows-1252, -1 não
 * sabe (UTF-8 válido fica como está; senão é Windows-1252). Do #6, com o
 * charset declarado da 2.0. Devolve o bloco novo (o antigo é liberado). */
static char *para_utf8(char *s, uint32_t *tam, int charset)
{
    int n = (int)*tam;
    if (charset == 1) return s;
    if (charset < 0) {
        bool valido = true;
        for (int i = 0; i < n;) {
            uint32_t cp; int k = utf8_ler(s + i, n - i, &cp);
            if (k == 1 && (uint8_t)s[i] >= 0x80) { valido = false; break; }
            i += k;
        }
        if (valido) return s;
    }
    int cresce = 0;
    for (int i = 0; i < n; i++) if ((uint8_t)s[i] >= 0x80) cresce += (uint8_t)s[i] < 0xA0 ? 2 : 1;
    if (!cresce) return s;
    char *d = aloca((size_t)n + (size_t)cresce + 1);
    if (!d) return s;
    static const uint16_t cp1252[32] = { 0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0x8D, 0x17D, 0x8F,
                                         0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178 };
    int k = 0;
    for (int i = 0; i < n; i++) {
        uint8_t c = (uint8_t)s[i];
        k += utf8_por(d + k, c >= 0x80 && c < 0xA0 ? cp1252[c - 0x80] : c);
    }
    d[k] = 0;
    libera(s);
    *tam = (uint32_t)k;
    return d;
}

static bool sem_caixa(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        char x = a[i] >= 'A' && a[i] <= 'Z' ? (char)(a[i] + 32) : a[i];
        if (x != b[i]) return false;
    }
    return true;
}

/* "charset=" num texto (Content-Type ou o começo do HTML): 1 UTF-8, 0 outro, -1 nada. */
static int charset_em(const char *t, int n)
{
    for (int i = 0; i + 8 < n; i++)
        if (sem_caixa(t + i, "charset=", 8)) {
            const char *v = t + i + 8;
            while (*v == '"' || *v == '\'' || *v == ' ') v++;
            return sem_caixa(v, "utf-8", 5) || sem_caixa(v, "utf8", 4) ? 1 : 0;
        }
    return -1;
}

/* ------------------------------------------------------------ ganchos do motor e do JavaScript */

/* Cache dos recursos da página (CSS, imagens, scripts): a página viva é
 * relida a cada mudança do DOM e não deve baixar tudo de novo. Limpo ao
 * navegar. (#6) */
#define CACHE_N 48
#define CACHE_MAX (6u << 20)
static struct { char *url; uint8_t *d; unsigned n; } cache[CACHE_N];
static int qtd_cache; static unsigned cache_total;

static void cache_limpar(void)
{
    for (int i = 0; i < qtd_cache; i++) { libera(cache[i].url); libera(cache[i].d); }
    qtd_cache = 0; cache_total = 0;
}

static uint8_t *copia_bloco(const uint8_t *d, unsigned n)
{
    uint8_t *c = aloca(n + 1);
    if (c) { memcpy(c, d, n); c[n] = 0; }
    return c;
}

/* Evento que chegou enquanto a página carregava (tratado depois, no laço). */
static evento_t evento_guardado;
static bool tem_evento_guardado;

static uint8_t *baixar_sem_cache(const char *u, unsigned *n, int *codigo, char *cab, int cmax, char *final, int fmax)
{
    char end[MAX_URL];
    endereco_resolver(url_atual, u, end, sizeof end);
    if (comeca_com(end, "sagui:") || comeca_com(end, "javascript:")) return NULL;
    char msg[200];
    formatar_texto(msg, sizeof msg, "Baixando %s", end);
    definir_status(msg);
    rede_resposta_t r;
    int s = rede_buscar(end, &r, MAX_RECURSO, cab, cmax);
    if (codigo) *codigo = s < 0 ? 0 : s;
    if (final) copia_texto(final, r.final, (size_t)fmax);
    if (s < 0 || !r.d) { registrar("navegador: recurso %s: %s", end, r.erro[0] ? r.erro : "veio vazio"); libera(r.d); return NULL; }
    *n = r.n;
    return r.d;
}

/* Recurso da página (CSS, imagem, fonte, script): só os que deram certo. */
static uint8_t *baixar_recurso(const char *u, unsigned *n)
{
    char end[MAX_URL];
    endereco_resolver(url_atual, u, end, sizeof end);
    for (int i = 0; i < qtd_cache; i++)
        if (compara_texto(cache[i].url, end) == 0) { *n = cache[i].n; return copia_bloco(cache[i].d, cache[i].n); }
    int codigo = 0;
    uint8_t *d = baixar_sem_cache(end, n, &codigo, NULL, 0, NULL, 0);
    if (d && codigo >= 400) { libera(d); return NULL; }
    if (d && qtd_cache < CACHE_N && cache_total + *n <= CACHE_MAX) {
        int k = (int)tam_texto(end);
        char *cu = aloca((unsigned)k + 1);
        uint8_t *cd = copia_bloco(d, *n);
        if (cu && cd) { memcpy(cu, end, (unsigned)k + 1); cache[qtd_cache].url = cu; cache[qtd_cache].d = cd; cache[qtd_cache].n = *n; qtd_cache++; cache_total += *n; }
        else { libera(cu); libera(cd); }
    }
    return d;
}

/* fetch/XHR: corpo de qualquer código, com código, cabeçalhos e URL final. */
static uint8_t *js_resposta(const char *u, unsigned *n, int *codigo, char *cab, int cmax, char *final, int fmax)
{
    *codigo = 0; cab[0] = 0; final[0] = 0;
    return baixar_sem_cache(u, n, codigo, cab, cmax, final, fmax);
}

static void js_titulo(const char *t) { copia_texto(titulo_js, t, sizeof titulo_js); }
static void js_alerta(const char *t)
{
    registrar("navegador: alert %s", t);
    char msg[200];
    formatar_texto(msg, sizeof msg, "Alerta da página: %s", t);
    utf8_para_sistema(msg);
    copia_texto(status, msg, sizeof status);
}

/* H96: imagens já decodificadas nesta página. Quando um script muda o DOM,
 * a página é lida de novo num motor novo, que pedia todas as imagens outra
 * vez (2,8 s por vez no quanticbit.qzz.io, rolando). A chave é o conteúdo
 * (tamanho + hash); falhas também ficam, para não tentar de novo. O motor
 * libera os pixels que recebe, então sai sempre uma cópia. */
#define IMGC_N   48
#define IMGC_MAX (16u << 20)
static struct { unsigned n, hash; uint32_t *px; int l, a; } imgc[IMGC_N];
static int qtd_imgc; static unsigned imgc_total;

static void imgc_limpar(void)
{
    for (int i = 0; i < qtd_imgc; i++) libera(imgc[i].px);
    qtd_imgc = 0; imgc_total = 0;
}

static unsigned hash_bloco(const uint8_t *d, unsigned n) { unsigned x = 2166136261u; for (unsigned i = 0; i < n; i++) x = (x ^ d[i]) * 16777619u; return x; }

static bool decodificar(const uint8_t *d, unsigned n, uint32_t **px, int *l, int *a)
{
    unsigned h = hash_bloco(d, n);
    for (int i = 0; i < qtd_imgc; i++) {
        if (imgc[i].n != n || imgc[i].hash != h) continue;
        if (!imgc[i].px) return false;                    /* já falhou antes */
        size_t t = (size_t)imgc[i].l * (size_t)imgc[i].a * 4;
        uint32_t *c = aloca(t);
        if (!c) return false;
        memcpy(c, imgc[i].px, t);
        *px = c; *l = imgc[i].l; *a = imgc[i].a;
        return true;
    }
    bool ok = imagem_decodificar(d, n, px, l, a);
    if (ok) registrar("navegador: imagem %dx%d", *l, *a);
    else registrar("navegador: imagem nao decodificou (%u bytes)", n);
    if (qtd_imgc < IMGC_N) {
        size_t t = ok ? (size_t)*l * (size_t)*a * 4 : 0;
        uint32_t *c = NULL;
        if (ok && imgc_total + t <= IMGC_MAX && (c = aloca(t))) memcpy(c, *px, t);
        if (!ok || c) { imgc[qtd_imgc].n = n; imgc[qtd_imgc].hash = h; imgc[qtd_imgc].px = c; imgc[qtd_imgc].l = ok ? *l : 0; imgc[qtd_imgc].a = ok ? *a : 0; qtd_imgc++; imgc_total += (unsigned)t; }
    }
    return ok;
}

static const motor_ganchos_t ganchos_motor = { baixar_recurso, decodificar, tela2d_pixels };

/* Layout pedido por um script (medidas): um motor à parte diagrama o DOM
 * atual com o CSS em cache e devolve as caixas. Fica vivo para o
 * getComputedStyle até o próximo pedido ou a próxima página. (#6) */
static motor_t *medidor;
static char *medidor_html;     /* H96: o DOM que o medidor diagramou por último */
static int medidor_n = -1;
static int js_layout(const char *html, int n, int *caixas, int qtd)
{
    /* Scripts pedem medidas muitas vezes sem mudar o DOM (rolagem,
     * IntersectionObserver): com o mesmo HTML, as caixas são as mesmas. */
    if (medidor && medidor_html && n == medidor_n && memcmp(html, medidor_html, (size_t)n) == 0) {
        motor_caixas(medidor, caixas, qtd);
        return 1;
    }
    if (medidor_html) { libera(medidor_html); medidor_html = NULL; }
    medidor_n = -1;
    medidor_html = aloca((size_t)n + 1);
    if (medidor_html) { memcpy(medidor_html, html, (size_t)n); medidor_n = n; }
    if (medidor) motor_fim(medidor);
    medidor = motor_novo(&ganchos_motor);
    if (!medidor) return 0;
    motor_ler(medidor, html, n, url_atual);
    motor_layout(medidor, VISTA_L, VISTA_A);
    motor_caixas(medidor, caixas, qtd);
    return 1;
}
static bool js_estilo(int k, const char *prop, char *o, int max) { return medidor && motor_estilo_de(medidor, k, prop, o, max); }
static struct fonte *js_fonte(const char *fam, int peso, int ital, int larg) { return medidor ? motor_fonte_nomeada(medidor, fam, peso, ital, larg) : NULL; }

/* iframes: cada quadro tem o seu motor (para pintar e para achar o clique). (#6) */
#define MAX_QM 16
static struct { int tela; motor_t *m; } qm[MAX_QM];
static int js_quadro_pintar(int tela, const char *html, int n, const char *url, int l, int a)
{
    int k = 0;
    while (k < MAX_QM && !(qm[k].m && qm[k].tela == tela)) k++;
    if (k == MAX_QM) { k = 0; while (k < MAX_QM && qm[k].m) k++; }
    if (k == MAX_QM) return 0;
    if (qm[k].m) motor_fim(qm[k].m);
    motor_t *m = motor_novo(&ganchos_motor);
    qm[k].m = m; qm[k].tela = tela;
    if (!m) return 0;
    motor_ler(m, html, n, url);
    motor_layout(m, l, a);
    while (motor_imagens(m, 8)) motor_layout(m, l, a);
    int tl, ta;
    uint32_t *px = tela2d_px_escrita(tela, &tl, &ta);
    if (!px) return 0;
    int w = l < tl ? l : tl, h = a < ta ? a : ta;
    motor_pintar(m, px, w, tl, 0, h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) px[y * tl + x] |= 0xFF000000u;
    return 1;
}
static int js_quadro_alvo(int tela, int x, int y)
{
    for (int k = 0; k < MAX_QM; k++) {
        if (!qm[k].m || qm[k].tela != tela) continue;
        const motor_alvo_t *a = motor_alvo(qm[k].m, x, y);
        if (!a) return -1;
        if (a->js >= 0) return a->js;
        if (a->href && comeca_com(a->href, "sagui-js:")) return texto_para_int(a->href + 9);
        return -1;
    }
    return -1;
}
static void quadros_esquecer(void) { for (int k = 0; k < MAX_QM; k++) { if (qm[k].m) motor_fim(qm[k].m); qm[k].m = NULL; } }

static bool tem_script(const char *h, uint32_t n)
{
    for (uint32_t i = 0; i + 7 < n; i++) if (h[i] == '<' && sem_caixa(h + i + 1, "script", 6)) return true;
    return false;
}

/* Roda os scripts: devolve o HTML do DOM depois do load (ou o mesmo). */
static char *rodar_scripts(char *h, uint32_t *n)
{
    static const js_hooks_t ganchos = { baixar_recurso, js_titulo, js_alerta, js_resposta, js_layout, js_estilo, js_fonte,
                                        js_quadro_pintar, js_quadro_alvo };
    quadros_esquecer();
    if (!tem_script(h, *n)) return h;
    definir_status("Rodando os scripts da página...");
    unsigned t0 = tempo_ms();
    int t = 0;
    js_janela(VISTA_L, VISTA_A, 0, 0);
    char *novo = js_pagina(h, (int)*n, url_atual, &ganchos, &t);
    registrar("navegador: scripts em %u ms (%s)", tempo_ms() - t0, novo ? "o DOM mudou" : "nada mudou");
    if (!novo) return h;
    libera(h);
    *n = (uint32_t)t;
    return novo;
}

/* ------------------------------------------------------------ páginas internas */

typedef struct { char *d; uint32_t n, cap; } texto_t;

static void acrescentar(texto_t *r, const char *s, unsigned n)
{
    if (r->n + n + 1 > r->cap) {
        uint32_t cap = r->cap ? r->cap * 2 : 4096;
        while (cap < r->n + n + 1) cap *= 2;
        char *novo = realoca(r->d, cap);
        if (!novo) return;
        r->d = novo; r->cap = cap;
    }
    memcpy(r->d + r->n, s, n);
    r->n += n;
    r->d[r->n] = 0;
}
#define T(r, s) acrescentar(r, s, (unsigned)tam_texto(s))

static void html_escapar(texto_t *r, const char *s, int n)
{
    for (int i = 0; i < n && s[i]; i++) {
        if (s[i] == '<') T(r, "&lt;");
        else if (s[i] == '&') T(r, "&amp;");
        else if (s[i] == '"') T(r, "&quot;");
        else acrescentar(r, s + i, 1);
    }
}

static void pasta_config(char *out, int max)
{
    info_arquivo_t i;
    copia_texto(out, info_arquivo("/usb/.saguios", &i) >= 0 ? "/usb/.saguios" : "/tmp", (size_t)max);
}

static void pagina_interna(const char *url, texto_t *r)
{
    T(r, "<html><head><meta charset=\"iso-8859-1\">");
    if (compara_texto(url, "sagui:favoritos") == 0) {
        T(r, "<title>Favoritos</title></head><body><h1>Favoritos</h1>");
        char caminho[64];
        pasta_config(caminho, sizeof caminho);
        junta_texto(caminho, "/favoritos.txt", sizeof caminho);
        rede_resposta_t f;
        int n = 0;
        if (rede_buscar(caminho, &f, 128 * 1024, NULL, 0) == 200 && f.d) {
            T(r, "<ul>");
            for (char *p = (char *)f.d; *p;) {
                char *fim = acha_char(p, '\n');
                if (fim) *fim = 0;
                char *tab = acha_char(p, '\t');
                if (tab) *tab = 0;
                if (*p) {
                    T(r, "<li><a href=\"");
                    html_escapar(r, p, MAX_URL);
                    T(r, "\">");
                    html_escapar(r, tab && tab[1] ? tab + 1 : p, MAX_URL);
                    T(r, "</a></li>");
                    n++;
                }
                if (!fim) break;
                p = fim + 1;
            }
            T(r, "</ul>");
        }
        libera(f.d);
        if (!n) T(r, "<p>Nenhum favorito ainda. Abra uma página e clique em <b>+Fav</b>.</p>");
        T(r, "<p><small>Guardados em ");
        html_escapar(r, caminho, 64);
        T(r, "</small></p>");
    } else {
        T(r, "<title>" NOME_NAVEGADOR "</title>"
             "<style>body{font-family:sans-serif;max-width:760px;margin:24px auto;padding:0 16px;color:#1e2328}"
             "h1{color:#0b7a3e}a{color:#1a4fb0}.dica{background:#eef6f0;border-left:4px solid #0b7a3e;padding:8px 12px}</style>"
             "</head><body><h1>" NOME_NAVEGADOR "</h1>"
             "<p><i>O sagui atravessa a mata pulando de cipó em cipó; o Cipó atravessa a web pulando de link em link.</i></p>"
             "<p>O navegador do SaguiOS: HTML e CSS (flex, grid, tabelas), JavaScript, fontes TrueType, "
             "imagens PNG, JPEG, GIF e WebP, Canvas e SVG, e HTTPS com o TLS 1.3 do próprio sistema.</p>"
             "<h2>Para começar</h2><ul>"
             "<li><a href=\"https://pt.wikipedia.org/wiki/Sagui\">Wikipédia: Sagui</a></li>"
             "<li><a href=\"https://pt.wikipedia.org/wiki/Wikip%C3%A9dia:P%C3%A1gina_principal\">Wikipédia em português</a></li>"
             "<li><a href=\"https://example.com/\">example.com</a></li>"
             "<li><a href=\"https://raw.githubusercontent.com/erigobeli/sagui-os/main/README.md\">README do SaguiOS (texto)</a></li>"
             "<li><a href=\"sagui:favoritos\">Favoritos</a></li>"
             "</ul>"
             /* Sites leves, sem HTTPS (ideia do pull request #3, de M4R4G0N): abrem
              * mesmo quando o TLS não consegue (relógio errado, certificado raro). */
             "<h2>Sites leves (sem HTTPS)</h2><ul>"
             "<li><a href=\"http://info.cern.ch/\">info.cern.ch</a>: o primeiro site da web, de 1991</li>"
             "<li><a href=\"http://68k.news/\">68k.news</a>: notícias em páginas simples, para computadores antigos</li>"
             "<li><a href=\"http://neverssl.com/\">neverssl.com</a>: para testar a conexão sem HTTPS</li>"
             "</ul><h2>Procurar na Wikipédia</h2>"
             "<form action=\"https://pt.wikipedia.org/w/index.php\"><input type=\"hidden\" name=\"title\" value=\"Especial:Pesquisar\">"
             "<input type=\"search\" name=\"search\"> <input type=\"submit\" value=\"Procurar\"></form>"
             /* Busca na web: o Google bloqueia navegadores que ele não conhece
              * ("tráfego incomum"); o DuckDuckGo Lite responde em HTML simples,
              * sem JavaScript. */
             "<h2>Procurar na web</h2>"
             "<form action=\"https://lite.duckduckgo.com/lite/\" method=\"post\">"
             "<input type=\"text\" name=\"q\"> <input type=\"submit\" value=\"Procurar no DuckDuckGo\"></form>"
             "<h2>Dicas</h2><div class=\"dica\"><ul><li>Setas, PgUp, PgDn, Home e End rolam a página.</li>"
             "<li>Backspace volta para a página anterior; Ctrl+L vai para o endereço.</li>"
             "<li>Também abre arquivos do SaguiOS, como <a href=\"/leia-me.txt\">/leia-me.txt</a>.</li></ul></div>");
    }
    T(r, "</body></html>");
}

static void pagina_erro(texto_t *r, const char *url, const char *erro)
{
    T(r, "<html><head><meta charset=\"iso-8859-1\"><title>Não deu para abrir</title></head><body><h1>Não deu para abrir a página</h1><p>");
    html_escapar(r, url, MAX_URL);
    T(r, "</p><p><b>Motivo:</b> ");
    html_escapar(r, erro, 200);
    T(r, "</p><p><a href=\"sagui:inicio\">Página inicial</a></p></body></html>");
}

/* ------------------------------------------------------------ carregar */

static void guardar_rolagem(void)
{
    if (hist_pos >= 0) hist[hist_pos].rolagem = rolagem;
}

static void diagramar(void)
{
    if (!motor) { altura = 0; return; }
    altura = motor_layout(motor, VISTA_L, VISTA_A);
    limitar_rolagem();
}

/* Lê o HTML (UTF-8) num motor novo e diagrama. */
static void ler_pagina(const char *h, int n)
{
    if (motor) motor_fim(motor);
    motor = motor_novo(&ganchos_motor);
    if (!motor) { altura = 0; return; }
    motor_ler(motor, h, n, url_atual);
    diagramar();
}

/* Fontes da web e imagens, umas poucas por vez. Para se chegar um evento. */
static void carregar_imagens(void)
{
    while (motor) {
        evento_t e;
        if (!tem_evento_guardado && proximo_evento(j->id, &e, false) == 1) {
            if (e.tipo != EV_MOVER) {
                /* Uma ação do usuário interrompe; o evento é tratado depois, no laço principal. */
                evento_guardado = e;
                tem_evento_guardado = true;
                break;
            }
        }
        if (tem_evento_guardado) break;
        if (!motor_imagens(motor, 2)) break;
        diagramar();
        desenhar_pagina();
        jan_mostrar_area(j, 0, BARRA, LARG, VISTA_A);
    }
    if (motor) {
        int nos, cmds, imgs;
        motor_numeros(motor, &nos, &cmds, &imgs);
        registrar("navegador: imagens %s (%d de %d)", tem_evento_guardado ? "interrompidas" : "prontas", imgs, motor_imagens_total(motor));
    }
}

static void mostrar_titulo(void)
{
    char t[128];
    copia_texto(t, titulo_js[0] ? titulo_js : motor && motor_titulo(motor)[0] ? motor_titulo(motor) : url_atual, sizeof t);
    utf8_para_sistema(t);
    /* O kernel guarda títulos curtos: o da página é cortado com "...". */
    char curto[28], titulo[64];
    copia_texto(curto, t, sizeof curto);
    if (tam_texto(t) >= sizeof curto) copia_texto(curto + sizeof curto - 4, "...", 4);
    formatar_texto(titulo, sizeof titulo, "%s - " NOME_NAVEGADOR, curto);
    titulo_janela(j->id, titulo);
}

static void carregar(const char *pedido, bool novo_no_historico, int rolagem_inicial)
{
    char url[MAX_URL];
    if (!endereco_tem_esquema(pedido) && pedido[0] != '/') formatar_texto(url, sizeof url, "https://%s", pedido);
    else copia_texto(url, pedido, sizeof url);
    /* Só o fragmento mudou: rola até a âncora, sem buscar de novo. */
    const char *frag = acha_char(url, '#');
    if (frag && motor && compara_texto_n(url, url_atual, (size_t)(frag - url)) == 0 &&
        (url_atual[frag - url] == 0 || url_atual[frag - url] == '#')) {
        int y = motor_ancora(motor, frag + 1);
        if (y >= 0) {
            guardar_rolagem();
            rolagem = y - 4;
            limitar_rolagem();
            copia_texto(url_atual, url, sizeof url_atual);
            definir_endereco(url);
            desenhar_tudo();
            registrar("navegador: ancora %s em %d", frag + 1, y);
            return;
        }
    }
    guardar_rolagem();
    ende.foco = false;
    link_sob_mouse[0] = 0;
    titulo_js[0] = 0;
    definir_endereco(url);
    char msg[220];
    formatar_texto(msg, sizeof msg, "Carregando %s ...", url);
    copia_texto(status, msg, sizeof status);
    desenhar_barra();
    desenhar_status();
    jan_mostrar(j);
    unsigned inicio = tempo_ms();
    /* a página anterior: motores, scripts, fontes da web, cache */
    if (medidor) { motor_fim(medidor); medidor = NULL; }
    if (medidor_html) { libera(medidor_html); medidor_html = NULL; }
    medidor_n = -1;
    if (motor) { motor_fim(motor); motor = NULL; }
    quadros_esquecer();
    motor_fontes_limpar();
    js_fim();
    cache_limpar();
    imgc_limpar();
    copia_texto(url_atual, url, sizeof url_atual);

    texto_t pag = { 0, 0, 0 };
    char tipo[64] = "text/html", final[MAX_URL], erro[160] = "";
    int situacao = 200;
    uint32_t bytes = 0;
    if (comeca_com(url, "sagui:")) { pagina_interna(url, &pag); copia_texto(final, url, sizeof final); }
    else {
        rede_resposta_t r;
        situacao = rede_buscar(url, &r, MAX_PAGINA, NULL, 0);
        copia_texto(tipo, r.tipo[0] ? r.tipo : "text/html", sizeof tipo);
        copia_texto(final, r.final[0] ? r.final : url, sizeof final);
        copia_texto(erro, r.erro, sizeof erro);
        bytes = r.n;
        if (situacao < 0 || !r.d) {
            libera(r.d);
            pagina_erro(&pag, url, erro[0] ? erro : "a página veio vazia");
            copia_texto(tipo, "text/html", sizeof tipo);
            registrar("navegador: erro em %s: %s", url, erro);
        } else if (comeca_com(tipo, "image/")) {
            /* Uma imagem sozinha: página com um <img> que aponta para ela. */
            libera(r.d);
            T(&pag, "<html><head><title>Imagem</title></head><body><img src=\"");
            html_escapar(&pag, final, MAX_URL);
            T(&pag, "\"></body></html>");
        } else if (comeca_com(tipo, "video/mp4") || comeca_com(tipo, "audio/mp4")) {
            /* O vídeo toca num programa próprio, enquanto baixa (H82). */
            libera(r.d);
            char *args[] = { "video", final, NULL };
            int pid = executar("/bin/video", args);
            registrar("navegador: video %s pid %d", final, pid);
            pagina_erro(&pag, url, pid > 0 ? "o vídeo abriu no tocador de vídeo" : "não consegui abrir o tocador de vídeo");
        } else if (comeca_com(tipo, "text/html") || comeca_com(tipo, "application/xhtml")) {
            int cs = charset_em(tipo, (int)tam_texto(tipo));
            if (cs < 0) cs = charset_em((const char *)r.d, r.n < 2048 ? (int)r.n : 2048);
            uint32_t n = r.n;
            char *u8 = para_utf8((char *)r.d, &n, cs);
            pag.d = u8; pag.n = n; pag.cap = n + 1;
        } else {
            /* Texto puro: cada linha como veio, em letra de máquina de escrever. */
            int cs = charset_em(tipo, (int)tam_texto(tipo));
            uint32_t n = r.n;
            char *u8 = para_utf8((char *)r.d, &n, cs);
            const char *ult = acha_ultimo_char(final, '/');
            T(&pag, "<html><head><meta charset=\"utf-8\"><title>");
            html_escapar(&pag, ult && ult[1] ? ult + 1 : final, 120);
            T(&pag, "</title></head><body><pre style=\"white-space:pre-wrap\">");
            for (uint32_t i = 0; i < n; i++) {
                if (u8[i] == '\r') continue;   /* fim de linha do Windows: o \n basta */
                if (u8[i] == '<') T(&pag, "&lt;");
                else if (u8[i] == '&') T(&pag, "&amp;");
                else acrescentar(&pag, u8 + i, 1);
            }
            T(&pag, "</pre></body></html>");
            libera(u8);
        }
    }
    if (!pag.d) { definir_status("memória insuficiente"); return; }
    /* As páginas internas e de erro são escritas aqui, em Latin-1. */
    if (comeca_com(pag.d, "<html><head><meta charset=\"iso-8859-1\">")) pag.d = para_utf8(pag.d, &pag.n, 0);
    copia_texto(url_atual, final[0] && !comeca_com(url, "sagui:") ? final : url, sizeof url_atual);
    if (acha_char(url, '#') && !acha_char(url_atual, '#')) junta_texto(url_atual, acha_char(url, '#'), sizeof url_atual);
    pag.d = rodar_scripts(pag.d, &pag.n);
    ler_pagina(pag.d, (int)pag.n);
    libera(pag.d);
    copia_texto(url_atual, url, sizeof url_atual);
    rolagem = rolagem_inicial;
    if (frag && motor) { int y = motor_ancora(motor, frag + 1); if (y >= 0) rolagem = y - 4; }
    limitar_rolagem();
    if (novo_no_historico) {
        if (hist_pos < MAX_HIST - 1) hist_pos++;
        else memmove(&hist[0], &hist[1], sizeof hist[0] * (MAX_HIST - 1));
        copia_texto(hist[hist_pos].url, url, sizeof hist[hist_pos].url);
        hist[hist_pos].rolagem = 0;
        hist_qtd = hist_pos + 1;
    }
    mostrar_titulo();
    formatar_texto(status, sizeof status, "%s%s%u KB em %u ms", erro[0] ? erro : "", erro[0] ? " | " : "", bytes / 1024, tempo_ms() - inicio);
    int nos = 0, cmds = 0, imgs = 0;
    if (motor) motor_numeros(motor, &nos, &cmds, &imgs);
    registrar("navegador: carregada %s (%u bytes, %s, situacao %d) em %u ms: %d nos, %d pecas, %d links, %d imagens, altura %d",
              url, bytes, tipo[0] ? tipo : "?", situacao, tempo_ms() - inicio, nos, cmds, motor ? motor_links(motor) : 0,
              motor ? motor_imagens_total(motor) : 0, altura);
    char tit[128];
    copia_texto(tit, titulo_js[0] ? titulo_js : motor ? motor_titulo(motor) : "", sizeof tit);
    utf8_para_sistema(tit);
    registrar("navegador: titulo %s", tit);
    desenhar_tudo();
    carregar_imagens();
    /* Imagens novas podem ter mudado a altura: a âncora pode ter descido. */
    if (frag && motor) { int y = motor_ancora(motor, frag + 1); if (y >= 0) { rolagem = y - 4; limitar_rolagem(); } }
    formatar_texto(status, sizeof status, "%s%sPronto: %u KB em %u ms", erro[0] ? erro : "", erro[0] ? " | " : "", bytes / 1024, tempo_ms() - inicio);
    desenhar_tudo();
    registrar("navegador: pronto %s", url);
}

/* ------------------------------------------------------------ página viva (JavaScript) */

/* O DOM mudou: relê a página no mesmo ponto da rolagem, com o foco no campo. (#6) */
static void reler(char *h, int n)
{
    int campo = motor && motor_tem_foco(motor) ? motor_foco_campo(motor) : -1;
    int r = rolagem;
    ler_pagina(h, n);
    libera(h);
    if (motor && campo >= 0) motor_focar_campo(motor, campo);
    while (motor && motor_imagens(motor, 8)) diagramar();
    rolagem = r;
    limitar_rolagem();
    desenhar_pagina();
    jan_mostrar_area(j, 0, BARRA, LARG, VISTA_A);
}

static void clique_js(int id)
{
    int n = 0;
    char *h = js_clicar(id, &n);
    registrar("navegador: clique js %d (%s)", id, h ? "o DOM mudou" : "nada mudou");
    if (h) reler(h, n);
}

static unsigned ult_tique, hash_viva;
static int js_rol;

static unsigned hash_texto(const char *h, int n) { unsigned x = 2166136261u; for (int i = 0; i < n; i++) x = (x ^ (uint8_t)h[i]) * 16777619u; return x; }

static void pagina_viva(void)
{
    int n = 0, nr = 0;
    unsigned agora = tempo_ms(), v0 = tela2d_versao();
    char *hr = NULL, *h;
    if (rolagem != js_rol) { js_rol = rolagem; hr = js_rolar(0, js_rol, &nr); }   /* scroll e observadores antes dos timers */
    h = js_tique((int)(agora - ult_tique > 1000 ? 1000 : agora - ult_tique), &n);
    if (h) libera(hr); else { h = hr; n = nr; }   /* vale o estado mais novo */
    ult_tique = agora;
    int px, py;
    if (js_rolagem_pedida(&px, &py)) { rolagem = py; limitar_rolagem(); }
    if (!h) {
        /* só um <canvas> mudou: pinta de novo, sem layout */
        if (tela2d_versao() != v0 && motor) { desenhar_pagina(); jan_mostrar_area(j, 0, BARRA, LARG, VISTA_A); }
        return;
    }
    unsigned x = hash_texto(h, n);
    if (x == hash_viva) { libera(h); return; }
    hash_viva = x;
    reler(h, n);
    registrar("navegador: pagina viva redesenhada");
}

/* ------------------------------------------------------------ formulários */

static void enviar(const motor_alvo_t *quem, int campo)
{
    char url[MAX_URL];
    if (campo >= 0) {
        char *h; int n;
        int cancelou = js_enviar(campo, &h, &n);
        if (cancelou) { if (h) reler(h, n); return; }
        libera(h);
        if (motor_envio_campo(motor, campo, url, sizeof url)) { registrar("navegador: formulario -> %s", url); carregar(url, true, 0); }
        return;
    }
    if (motor_envio(motor, quem, url, sizeof url)) { registrar("navegador: formulario -> %s", url); carregar(url, true, 0); }
}

/* Tecla num campo com foco (UTF-8 por dentro; o teclado manda Latin-1). (#6) */
static void tecla_no_campo(unsigned t)
{
    if (t == TECLA_ESC) { motor_foco(motor, NULL); diagramar(); desenhar_tudo(); return; }
    if (t == '\n' || t == '\r') { enviar(NULL, motor_foco_campo(motor)); return; }
    char v[1024];
    copia_texto(v, motor_foco_valor(motor), sizeof v);
    int k = (int)tam_texto(v);
    if (t == 8) {
        if (!k) return;
        do k--; while (k > 0 && ((uint8_t)v[k] & 0xC0) == 0x80);
        v[k] = 0;
    } else if (t >= 32 && t < 256 && t != 127) {
        if (k + 4 >= (int)sizeof v) return;
        k += utf8_por(v + k, t);
        v[k] = 0;
    } else return;
    motor_foco_definir(motor, v);
    int campo = motor_foco_campo(motor);
    if (campo >= 0) {
        int n = 0;
        char *h = js_campo(campo, v, t == 8 ? 8 : (int)t, &n);
        if (h) { reler(h, n); return; }
    }
    diagramar();
    desenhar_pagina();
    jan_mostrar_area(j, 0, BARRA, LARG, VISTA_A);
}

/* ------------------------------------------------------------ favoritos */

static void favoritar(void)
{
    if (!motor || comeca_com(url_atual, "sagui:")) return;
    char caminho[64];
    pasta_config(caminho, sizeof caminho);
    junta_texto(caminho, "/favoritos.txt", sizeof caminho);
    int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_ACRESCENTAR);
    if (fd < 0) { definir_status("não consegui gravar os favoritos"); return; }
    char t[128], linha[MAX_URL + 160];
    copia_texto(t, titulo_js[0] ? titulo_js : motor_titulo(motor), sizeof t);
    utf8_para_sistema(t);
    int n = formatar_texto(linha, sizeof linha, "%s\t%s\n", url_atual, t);
    escrever_tudo(fd, linha, (unsigned)n);
    fechar(fd);
    char msg[200];
    formatar_texto(msg, sizeof msg, "Favorito guardado em %s", caminho);
    definir_status(msg);
    registrar("navegador: favorito %s", url_atual);
}

/* ------------------------------------------------------------ eventos */

static int botao_em(int x, int y)
{
    if (y < 5 || y >= 29) return -1;
    for (int i = 0; i < 6; i++) if (x >= botoes[i].x && x < botoes[i].x + botoes[i].l) return i;
    return -1;
}

static void voltar(int passo)
{
    int novo = hist_pos + passo;
    if (novo < 0 || novo >= hist_qtd) return;
    guardar_rolagem();
    hist_pos = novo;
    carregar(hist[hist_pos].url, false, hist[hist_pos].rolagem);
}

static void rolar(int d)
{
    int antes = rolagem;
    rolagem += d;
    limitar_rolagem();
    if (rolagem != antes) desenhar_tudo();
}

static void editar_texto(char *t, int max, int *cursor, unsigned tecla)
{
    int n = (int)tam_texto(t);
    if (tecla == TECLA_ESQUERDA) { if (*cursor > 0) (*cursor)--; }
    else if (tecla == TECLA_DIREITA) { if (*cursor < n) (*cursor)++; }
    else if (tecla == TECLA_HOME) *cursor = 0;
    else if (tecla == TECLA_END) *cursor = n;
    else if (tecla == 8) { if (*cursor > 0) { memmove(t + *cursor - 1, t + *cursor, (size_t)(n - *cursor + 1)); (*cursor)--; } }
    else if (tecla == TECLA_DELETE) { if (*cursor < n) memmove(t + *cursor, t + *cursor + 1, (size_t)(n - *cursor)); }
    else if (tecla >= 32 && tecla < 256 && tecla != 127 && n < max - 1) {
        memmove(t + *cursor + 1, t + *cursor, (size_t)(n - *cursor + 1));
        t[*cursor] = (char)tecla;
        (*cursor)++;
    }
}

static void focar_endereco(void)
{
    ende.foco = true; ende.tudo = true;
    ende.cursor = (int)tam_texto(ende.t);
    if (motor && motor_tem_foco(motor)) { motor_foco(motor, NULL); diagramar(); }
    desenhar_tudo();
}

static void tratar_tecla(const evento_t *e)
{
    unsigned t = e->tecla;
    if ((t == 12 || t == 'l') && (e->modificadores & 2)) { focar_endereco(); return; }
    if (ende.foco) {
        if (t == '\n' || t == '\r') {
            char u[MAX_URL];
            copia_texto(u, ende.t, sizeof u);
            if (u[0]) carregar(u, true, 0);
            return;
        }
        if (t == TECLA_ESC) { ende.foco = false; definir_endereco(url_atual); desenhar_tudo(); return; }
        if (ende.tudo) {
            ende.tudo = false;
            /* Com tudo selecionado, digitar ou apagar troca o endereço inteiro; as setas só desfazem a seleção. */
            if (t == 8 || t == TECLA_DELETE || (t >= 32 && t < 256)) { ende.t[0] = 0; ende.cursor = 0; ende.inicio = 0; }
            if (t == 8 || t == TECLA_DELETE) { desenhar_barra(); jan_mostrar_area(j, 0, 0, LARG, BARRA); return; }
        }
        editar_texto(ende.t, sizeof ende.t, &ende.cursor, t);
        desenhar_barra();
        jan_mostrar_area(j, 0, 0, LARG, BARRA);
        return;
    }
    if (motor && motor_tem_foco(motor)) { tecla_no_campo(t); return; }
    switch (t) {
    case TECLA_CIMA: rolar(-48); break;
    case TECLA_BAIXO: rolar(48); break;
    case TECLA_PGUP: rolar(-(VISTA_A - 40)); break;
    case TECLA_PGDN: case ' ': rolar(VISTA_A - 40); break;
    case TECLA_HOME: rolar(-altura); break;
    case TECLA_END: rolar(altura); break;
    case 8: voltar(-1); break;
    default: break;
    }
}

static bool arrastando_rolagem;
static int arrasto_y0, arrasto_rol0;

static void abrir_link(const char *href)
{
    char u[MAX_URL];
    copia_texto(u, href, sizeof u);
    registrar("navegador: link %s", u);
    if (comeca_com(u, "sagui-js:")) { clique_js(texto_para_int(u + 9)); return; }
    if (comeca_com(u, "javascript:") || comeca_com(u, "mailto:")) { definir_status("Esse tipo de link não funciona aqui."); return; }
    carregar(u, true, 0);
}

static void tratar_clique(const evento_t *e)
{
    if (e->y < BARRA) {
        int b = botao_em(e->x, e->y);
        if (b >= 0) {
            desenhar_botao(b, true);
            jan_mostrar_area(j, 0, 0, LARG, BARRA);
            switch (b) {
            case BT_VOLTAR: voltar(-1); break;
            case BT_AVANCAR: voltar(1); break;
            case BT_RECARREGAR: if (url_atual[0]) carregar(url_atual, false, rolagem); break;
            case BT_IR: if (ende.t[0]) { char u[MAX_URL]; copia_texto(u, ende.t, sizeof u); carregar(u, true, 0); } break;
            case BT_FAV: favoritar(); break;
            case BT_FAVORITOS: carregar("sagui:favoritos", true, 0); break;
            }
            desenhar_tudo();
            return;
        }
        if (e->x >= ENDE_X && e->x < ENDE_X + ENDE_L) {
            ende.tudo = !ende.foco;              /* o primeiro clique seleciona tudo; o segundo põe o cursor */
            ende.foco = true;
            if (motor && motor_tem_foco(motor)) { motor_foco(motor, NULL); diagramar(); }
            int c = ende.inicio + (e->x - ENDE_X - 5 + LETRA_L / 2) / LETRA_L;
            int n = (int)tam_texto(ende.t);
            ende.cursor = c < 0 ? 0 : c > n ? n : c;
            desenhar_tudo();
        }
        return;
    }
    if (e->y >= ALT - STATUS) return;
    if (e->x >= VISTA_L) {                       /* barra de rolagem */
        if (altura <= VISTA_A) return;
        int a = VISTA_A * VISTA_A / altura;
        if (a < 20) a = 20;
        int ya = BARRA + (int)((int)rolagem * (VISTA_A - a) / (altura - VISTA_A));
        if (e->y >= ya && e->y < ya + a) { arrastando_rolagem = true; arrasto_y0 = e->y; arrasto_rol0 = rolagem; }
        else rolar(e->y < ya ? -(VISTA_A - 40) : VISTA_A - 40);
        return;
    }
    if (!motor) return;
    bool tinha_foco = ende.foco;
    ende.foco = false;
    int py = e->y - BARRA + rolagem;
    const motor_alvo_t *ma = motor_alvo(motor, e->x, py);
    if (ma && ma->tipo == MA_LINK && ma->href) { abrir_link(ma->href); return; }
    if (ma && ma->tipo == MA_QUADRO) {
        /* dentro de um iframe: o quadro acha o elemento e roda o clique na janela dele */
        js_clicar_quadro(ma->js, e->x - ma->rx, py - ma->ry);
        desenhar_tudo();
        return;
    }
    if (ma && ma->tipo == MA_CAMPO) {
        /* foco no campo: a digitação vai para dentro da caixa */
        motor_foco(motor, ma);
        registrar("navegador: foco no campo %d", motor_foco_campo(motor));
        diagramar();
        desenhar_tudo();
        return;
    }
    if (ma && ma->tipo == MA_BOTAO) {
        /* o clique vai ao script; depois, se o botão é de envio, o formulário
         * vai (a não ser que o script cancele). ma deixa de valer se o clique
         * reler a página: guarda o que precisa. */
        int js = ma->js, campo = ma->campo;
        bool envia = ma->envia, cancelou = false;
        if (js >= 0) { clique_js(js); cancelou = js_clique_cancelado(); }
        if (envia && !cancelou) enviar(js >= 0 ? NULL : ma, campo);
        return;
    }
    if (motor_tem_foco(motor)) { motor_foco(motor, NULL); diagramar(); desenhar_tudo(); return; }
    if (tinha_foco) desenhar_tudo();
}

static void tratar_movimento(const evento_t *e)
{
    if (arrastando_rolagem) {
        if (!(e->botoes & 1)) { arrastando_rolagem = false; return; }
        int a = VISTA_A * VISTA_A / (altura ? altura : 1);
        if (a < 20) a = 20;
        if (VISTA_A - a <= 0) return;
        rolagem = arrasto_rol0 + (int)((int)(e->y - arrasto_y0) * (altura - VISTA_A) / (VISTA_A - a));
        limitar_rolagem();
        desenhar_tudo();
        return;
    }
    char novo[MAX_URL] = "";
    if (motor && e->y >= BARRA && e->y < ALT - STATUS && e->x < VISTA_L) {
        const motor_alvo_t *ma = motor_alvo(motor, e->x, e->y - BARRA + rolagem);
        if (ma && ma->tipo == MA_LINK && ma->href && !comeca_com(ma->href, "sagui-js:")) {
            copia_texto(novo, ma->href, sizeof novo);
            utf8_para_sistema(novo);
        }
    }
    if (compara_texto(novo, link_sob_mouse) != 0) {
        copia_texto(link_sob_mouse, novo, sizeof link_sob_mouse);
        desenhar_status();
        jan_mostrar_area(j, 0, ALT - STATUS, LARG, STATUS);
    }
}

static void posicionar_botoes(void)
{
    botoes[BT_IR].x = ENDE_X + ENDE_L + 6;
    botoes[BT_FAV].x = botoes[BT_IR].x + botoes[BT_IR].l + 6;
    botoes[BT_FAVORITOS].x = botoes[BT_FAV].x + botoes[BT_FAV].l + 6;
}

/* A janela mudou de tamanho (H78): só o layout muda, nada é baixado de novo;
 * o script recebe resize (e pode mudar a página). */
static void mudar_tamanho(const evento_t *e)
{
    jan_novo_tamanho(j, e);
    larg_jan = j->larg;
    alt_jan = j->alt;
    posicionar_botoes();
    if (motor) {
        int alt_antes = altura;
        int nn = 0;
        char *hh = js_ativo() ? js_redimensionar(VISTA_L, VISTA_A, &nn) : NULL;
        if (hh) reler(hh, nn);
        else diagramar();
        /* Mantém o começo da parte visível mais ou menos no mesmo lugar. */
        if (alt_antes > 0) rolagem = (int)((long)rolagem * altura / alt_antes);
        limitar_rolagem();
    }
    desenhar_tudo();
    registrar("navegador: tamanho %dx%d, pagina com altura %d", LARG, ALT, altura);
}

int principal(int argc, char **argv)
{
    j = jan_abrir(NOME_NAVEGADOR, LARG, ALT);
    if (!j) { imprimir_em(ERROS, "navegador: não consegui abrir a janela\n"); return 1; }
    jan_redimensionavel(j, 520, 300);
    posicionar_botoes();
    registrar("navegador: janela %d", j->id);
    carregar(argc > 1 ? argv[1] : "sagui:inicio", true, 0);
    for (;;) {
        /* o script pediu outra página (location.href, assign, replace, reload) */
        char u[MAX_URL]; int subst = 0;
        if (js_navegacao(u, sizeof u, &subst)) {
            registrar("navegador: o script navegou para %s", u);
            if (subst && hist_pos >= 0) { hist_pos--; hist_qtd = hist_pos + 1; }
            carregar(u, true, 0);
            continue;
        }
        evento_t e;
        if (tem_evento_guardado) { e = evento_guardado; tem_evento_guardado = false; }
        else if (js_vivo() || (js_ativo() && rolagem != js_rol)) {
            /* página viva: timers e animações no ritmo deles, sem travar os eventos */
            if (proximo_evento(j->id, &e, false) != 1) {
                unsigned passo = js_animando() ? 16 : 100;
                if (tempo_ms() - ult_tique >= passo || rolagem != js_rol) pagina_viva();
                else dormir(10);
                continue;
            }
        } else if (proximo_evento(j->id, &e, true) != 1) continue;
        switch (e.tipo) {
        case EV_FECHAR:
            jan_fechar(j);
            return 0;
        case EV_REDIMENSIONAR:
            mudar_tamanho(&e);
            break;
        case EV_CLIQUE:
            if (e.botoes & 1) tratar_clique(&e);
            break;
        case EV_SOLTOU:
            arrastando_rolagem = false;
            break;
        case EV_MOVER:
            tratar_movimento(&e);
            break;
        case EV_TECLA:
            tratar_tecla(&e);
            break;
        }
    }
}
