/* terminal - o Terminal em janela (H24, emulação completa na H55).
 *
 * Abre uma janela com abas; em cada aba roda um sagsh, ligado por dois pipes:
 *   teclas da janela  -> pipe de entrada -> entrada 0 do sagsh
 *   saída 1 e 2 do sagsh -> pipe de saída -> desenhado na janela
 * As teclas vão como o terminal do kernel entrega no modo bruto (caracteres
 * Latin-1; setas e afins como ESC + letra), então o sagsh funciona igual.
 *
 * Na saída, o Terminal entende as sequências de controle que os programas de
 * servidor usam (as mesmas dos terminais de texto clássicos, num subconjunto
 * escolhido para rodar vim, htop, less e os prompts coloridos):
 *   - ESC [ ... letra ("CSI"): mover o cursor (A B C D E F G H d f), apagar
 *     (J K X P), inserir e remover linhas e letras (L M @), rolar (S T),
 *     região de rolagem (r), cores e estilos (m: 16, 256 e 24 bits),
 *     guardar e voltar o cursor (s u), modos (h l: cursor visível, tela
 *     alternativa, teclas de cursor do "aplicativo", quebra automática) e
 *     perguntas que pedem resposta (n: posição do cursor; c: que terminal é);
 *   - ESC 7 / ESC 8, ESC M (sobe rolando), ESC D, ESC E, ESC c (reinicia);
 *   - ESC ] ... (OSC): o título da janela vira o nome da aba;
 *   - ESC ( 0: o conjunto "gráfico" antigo (q = ─, x = │, l = ┌ ...).
 * O texto vem em UTF-8; o que existe no Latin-1 aparece como está, e as
 * molduras e blocos usam desenhos próprios da fonte (posições 0x80 a 0x9F).
 *
 * Mais: rolagem para trás (Shift + PgUp/PgDn), copiar arrastando o mouse
 * (o que foi marcado vai para a área de cópia do Terminal) e o menu do
 * botão direito (H81): colar, interromper, limpar, abas. */
#include "sagui.h"
#include "gui.h"

extern const unsigned char fonte_sagui[256][16];

/* O tamanho da grade muda quando a janela muda (H78): os vetores têm lugar
 * para o maior tamanho, e cols/lins dizem quanto está em uso. */
#define MAX_COLS 170
#define MAX_LINS 48
static int cols = 94, lins = 28;
#define COLS cols
#define LINS lins
#define MARGEM 4
#define ABAS_A 24
#define LARG (COLS * LETRA_L + 2 * MARGEM)
#define ALT  (LINS * LETRA_A + 2 * MARGEM + ABAS_A)
#define TOPO (ABAS_A + MARGEM)
#define MAX_ABAS 4
#define HISTORICO 300
#define COR_FUNDO_T_GLOBAL 0x12161E

/* Atributos de uma célula. */
#define A_NEGRITO   1
#define A_SUBLINHA  2
#define A_INVERTIDO 4
#define COR_PADRAO_FRENTE 7
#define COR_PADRAO_FUNDO  0

typedef struct { uint8_t c, attr, frente, fundo; } celula_t;

typedef struct {
    bool     usada;
    char     nome[24];
    int      shell, entrada, saida;            /* pid e pipes */
    celula_t tela[MAX_LINS][MAX_COLS], alternativa[MAX_LINS][MAX_COLS];
    celula_t historico[HISTORICO][MAX_COLS];
    int      hist_qtd, hist_ini, olhando;      /* rolagem para trás: quantas linhas acima */
    bool     na_alternativa;
    int      col, lin, col_salva, lin_salva;
    uint8_t  attr, frente, fundo;
    int      topo_rol, base_rol;               /* região de rolagem */
    bool     cursor_visivel, quebra, apos_fim, teclas_app, grafico;
    /* decodificador de escape */
    int      estado;                           /* 0 texto, 1 ESC, 2 CSI, 3 OSC, 4 ESC (, 5 OSC com ESC */
    char     privado;
    int      params[16], qtd_params;
    char     osc[64];
    int      osc_n;
    /* UTF-8 */
    uint32_t cod;
    int      faltam;
    uint8_t  primeiro;                         /* o byte que abriu a sequência */
    bool     dois_bytes;                       /* já veio pelo menos uma continuação */
    uint8_t  ultima;                           /* última letra escrita (para ESC [ n b) */
    bool     avisar_tamanho;
    bool     programa;                         /* a aba roda um programa (terminal -e), não o sagsh */
    bool     terminou;                         /* o programa acabou: a próxima tecla fecha a aba */                   /* ESC [ ? 7727 h: o programa quer saber quando o tamanho muda (H78) */
} aba_t;

static jan_t *j;
static aba_t abas[MAX_ABAS];
static int atual_aba;
static int suj_l0 = MAX_LINS, suj_l1 = -1;
static bool abas_sujas = true;
static char area_copia[MAX_COLS * MAX_LINS + MAX_LINS];
static int sel_ini_l = -1, sel_ini_c, sel_fim_l, sel_fim_c;

/* ------------------------------------------------------------ cores */

/* As 16 cores básicas (escuras e claras), nas cores do SaguiOS; 16 a 231 são
 * um cubo 6x6x6; 232 a 255, cinzas. */
static uint32_t cor(int i)
{
    static const uint32_t basicas[16] = {
        0x12161E, 0xD9534F, 0x5FB33A, 0xE0B92A, 0x2A6FD8, 0xA55EC8, 0x2AA9B8, 0xD8DDE2,
        0x5A6470, 0xFF7B72, 0x7EE07E, 0xFFDF4A, 0x6FA8FF, 0xD48BF0, 0x5FD7E6, 0xFFFFFF,
    };
    if (i < 16) return basicas[i];
    if (i < 232) {
        i -= 16;
        static const uint8_t nivel[6] = { 0, 95, 135, 175, 215, 255 };
        return ((uint32_t)nivel[i / 36] << 16) | ((uint32_t)nivel[i / 6 % 6] << 8) | nivel[i % 6];
    }
    uint32_t c = (uint32_t)(8 + (i - 232) * 10);
    return (c << 16) | (c << 8) | c;
}

/* Cor de 24 bits -> a mais perto no cubo de 256 (o Terminal guarda 1 byte por cor). */
static uint8_t cor_proxima(int r, int g, int b)
{
    int ri = r < 48 ? 0 : (r < 115 ? 1 : (r - 35) / 40), gi = g < 48 ? 0 : (g < 115 ? 1 : (g - 35) / 40), bi = b < 48 ? 0 : (b < 115 ? 1 : (b - 35) / 40);
    return (uint8_t)(16 + 36 * ri + 6 * gi + bi);
}

/* ------------------------------------------------------------ desenho */

static celula_t (*grade(aba_t *a))[MAX_COLS] { return a->na_alternativa ? a->alternativa : a->tela; }

static void sujar(int l) { if (l < suj_l0) suj_l0 = l; if (l > suj_l1) suj_l1 = l; }
static void sujar_tudo(void) { suj_l0 = 0; suj_l1 = LINS - 1; }

static bool selecionada(int l, int c)
{
    if (sel_ini_l < 0) return false;
    int a = sel_ini_l * COLS + sel_ini_c, b = sel_fim_l * COLS + sel_fim_c, p = l * COLS + c;
    if (a > b) { int t = a; a = b; b = t; }
    return p >= a && p <= b;
}

static void desenhar_celula(int l, int c, celula_t ce, bool cursor)
{
    uint32_t fr = cor(ce.frente < 8 && (ce.attr & A_NEGRITO) ? ce.frente + 8 : ce.frente), fu = cor(ce.fundo);
    if (ce.attr & A_INVERTIDO) { uint32_t t = fr; fr = fu; fu = t; }
    if (cursor || selecionada(l, c)) { uint32_t t = fr; fr = fu; fu = t; }
    int x0 = MARGEM + c * LETRA_L, y0 = TOPO + l * LETRA_A;
    const unsigned char *g = fonte_sagui[ce.c];
    for (int y = 0; y < LETRA_A; y++) {
        uint32_t *p = j->px + (y0 + y) * j->larg + x0;
        uint8_t bits = g[y];
        if ((ce.attr & A_SUBLINHA) && y == 14) bits = 0xFF;
        for (int x = 0; x < 8; x++) p[x] = (bits & (0x80 >> x)) ? fr : fu;
    }
}

static void desenhar_linha(aba_t *a, int l)
{
    celula_t (*g)[MAX_COLS] = grade(a);
    for (int c = 0; c < COLS; c++) {
        celula_t ce;
        if (a->olhando > 0) {   /* olhando o histórico: as linhas de cima vêm de lá */
            int h = l - a->olhando;
            if (h < 0) {
                int idx = (a->hist_ini + a->hist_qtd + h) % HISTORICO;
                ce = a->historico[idx][c];
            } else ce = g[h][c];
        } else ce = g[l][c];
        bool cursor = a->olhando == 0 && a->cursor_visivel && l == a->lin && c == a->col;
        desenhar_celula(l, c, ce, cursor);
    }
}

static void desenhar_abas(void)
{
    g_retangulo(j, 0, 0, LARG, ABAS_A, 0x0E2A18);
    int x = 4;
    for (int i = 0; i < MAX_ABAS; i++) {
        if (!abas[i].usada) continue;
        uint32_t fundo = i == atual_aba ? 0x12161E : 0x1F3D2A;
        g_retangulo(j, x, 3, 150, ABAS_A - 3, fundo);
        g_texto_n(j, x + 8, 5, abas[i].nome, 17, i == atual_aba ? 0xFFFFFF : 0x9FB8A4);
        x += 154;
    }
    if (x + 24 < LARG) { g_texto(j, x + 6, 5, "+", 0x9FD8A8); }
    g_retangulo(j, 0, ABAS_A, LARG, MARGEM, COR_FUNDO_T_GLOBAL);
}

static void mostrar(void)
{
    aba_t *a = &abas[atual_aba];
    if (abas_sujas) { desenhar_abas(); jan_mostrar_area(j, 0, 0, LARG, ABAS_A + MARGEM); abas_sujas = false; }
    if (suj_l1 < 0) return;
    for (int l = suj_l0; l <= suj_l1; l++) desenhar_linha(a, l);
    jan_mostrar_area(j, 0, TOPO + suj_l0 * LETRA_A, LARG, (suj_l1 - suj_l0 + 1) * LETRA_A);
    suj_l0 = LINS; suj_l1 = -1;
}

/* ------------------------------------------------------------ a tela de uma aba */

static celula_t vazia(aba_t *a) { return (celula_t){ ' ', 0, COR_PADRAO_FRENTE, a->fundo }; }

static void limpar_faixa(aba_t *a, int l, int c0, int c1)
{
    celula_t (*g)[MAX_COLS] = grade(a);
    for (int c = c0; c <= c1 && c < COLS; c++) g[l][c] = vazia(a);
    if (a == &abas[atual_aba]) sujar(l);
}

static void guardar_no_historico(aba_t *a, int l)
{
    if (a->na_alternativa) return;
    int idx = (a->hist_ini + a->hist_qtd) % HISTORICO;
    memcpy(a->historico[idx], a->tela[l], sizeof a->tela[l]);
    if (a->hist_qtd < HISTORICO) a->hist_qtd++;
    else a->hist_ini = (a->hist_ini + 1) % HISTORICO;
}

/* Rola a região [topo, base] n linhas para cima (n < 0: para baixo). */
static void rolar(aba_t *a, int n)
{
    celula_t (*g)[MAX_COLS] = grade(a);
    int t = a->topo_rol, b = a->base_rol;
    if (n > 0) {
        for (int k = 0; k < n; k++) {
            if (t == 0) guardar_no_historico(a, 0);
            memmove(g[t], g[t + 1], sizeof g[0] * (size_t)(b - t));
            for (int c = 0; c < COLS; c++) g[b][c] = vazia(a);
        }
    } else {
        for (int k = 0; k < -n; k++) {
            memmove(g[t + 1], g[t], sizeof g[0] * (size_t)(b - t));
            for (int c = 0; c < COLS; c++) g[t][c] = vazia(a);
        }
    }
    if (a == &abas[atual_aba]) sujar_tudo();
}

static void nova_linha(aba_t *a)
{
    if (a->lin == a->base_rol) rolar(a, 1);
    else if (a->lin < LINS - 1) a->lin++;
}

static void reiniciar_aba(aba_t *a)
{
    a->attr = 0; a->frente = COR_PADRAO_FRENTE; a->fundo = COR_PADRAO_FUNDO;
    a->topo_rol = 0; a->base_rol = LINS - 1;
    a->cursor_visivel = true; a->quebra = true; a->apos_fim = false; a->teclas_app = false; a->grafico = false;
    a->col = a->lin = 0;
    for (int l = 0; l < LINS; l++) for (int c = 0; c < COLS; c++) a->tela[l][c] = a->alternativa[l][c] = vazia(a);
}

/* ------------------------------------------------------------ texto */

/* Unicode -> posição na fonte. */
static uint8_t glifo(uint32_t u)
{
    if (u < 0x80 || (u >= 0xA0 && u < 0x100)) return (uint8_t)u;
    switch (u) {
    case 0x2500: case 0x2501: case 0x254C: return 0x80;                         /* ─ */
    case 0x2502: case 0x2503: case 0x254E: return 0x81;                         /* │ */
    case 0x250C: case 0x256D: case 0x250F: return 0x82;                         /* ┌ ╭ */
    case 0x2510: case 0x256E: case 0x2513: return 0x83;                         /* ┐ ╮ */
    case 0x2514: case 0x2570: case 0x2517: return 0x84;                         /* └ ╰ */
    case 0x2518: case 0x256F: case 0x251B: return 0x85;                         /* ┘ ╯ */
    case 0x251C: case 0x2523: return 0x86;  case 0x2524: case 0x252B: return 0x87;
    case 0x252C: case 0x2533: return 0x88;  case 0x2534: case 0x253B: return 0x89;
    case 0x253C: case 0x254B: return 0x8A;
    case 0x2550: return 0x8B; case 0x2551: return 0x8C; case 0x2554: return 0x8D; case 0x2557: return 0x8E;
    case 0x255A: return 0x8F; case 0x255D: return 0x90;
    case 0x2588: return 0x91; case 0x2580: return 0x92; case 0x2584: return 0x93;
    case 0x2591: return 0x94; case 0x2592: return 0x95; case 0x2593: return 0x96;
    case 0x258C: return 0x97; case 0x2590: return 0x98;
    case 0x2190: return 0x99; case 0x2191: return 0x9A; case 0x2192: return 0x9B; case 0x2193: return 0x9C;
    case 0x2022: case 0x00B7: case 0x25CF: return 0x9D;
    case 0x2026: return 0x9E;
    case 0x25C6: case 0x2666: return 0x9F;
    case 0x2018: case 0x2019: return '\'';
    case 0x201C: case 0x201D: return '"';
    case 0x2013: case 0x2014: return '-';
    default: return '?';
    }
}

/* O conjunto "gráfico" antigo (ESC ( 0): letras viram pedaços de moldura. */
static uint8_t grafico_antigo(uint8_t c)
{
    switch (c) {
    case 'q': return 0x80; case 'x': return 0x81; case 'l': return 0x82; case 'k': return 0x83;
    case 'm': return 0x84; case 'j': return 0x85; case 't': return 0x86; case 'u': return 0x87;
    case 'w': return 0x88; case 'v': return 0x89; case 'n': return 0x8A; case 'a': return 0x95;
    case '`': return 0x9F; case '~': return 0x9D; case 'o': case 's': return '_';
    default: return c;
    }
}

static void escrever_letra(aba_t *a, uint8_t g)
{
    if (a->grafico) g = grafico_antigo(g);
    if (a->apos_fim) {                         /* a linha anterior encheu: quebra só agora */
        a->apos_fim = false;
        if (a->quebra) { a->col = 0; nova_linha(a); }
    }
    a->ultima = g;
    grade(a)[a->lin][a->col] = (celula_t){ g, a->attr, a->frente, a->fundo };
    if (a == &abas[atual_aba]) sujar(a->lin);
    if (a->col == COLS - 1) a->apos_fim = true;
    else a->col++;
}

/* ------------------------------------------------------------ sequências */

static int param(aba_t *a, int i, int padrao) { return i < a->qtd_params && a->params[i] > 0 ? a->params[i] : padrao; }

static void responder(aba_t *a, const char *t) { escrever_tudo(a->entrada, t, (unsigned)tam_texto(t)); }

static void cores_sgr(aba_t *a)
{
    if (a->qtd_params == 0) { a->attr = 0; a->frente = COR_PADRAO_FRENTE; a->fundo = COR_PADRAO_FUNDO; return; }
    for (int i = 0; i < a->qtd_params; i++) {
        int p = a->params[i];
        if (p == 0) { a->attr = 0; a->frente = COR_PADRAO_FRENTE; a->fundo = COR_PADRAO_FUNDO; }
        else if (p == 1) a->attr |= A_NEGRITO;
        else if (p == 4) a->attr |= A_SUBLINHA;
        else if (p == 7) a->attr |= A_INVERTIDO;
        else if (p == 22) a->attr &= (uint8_t)~A_NEGRITO;
        else if (p == 24) a->attr &= (uint8_t)~A_SUBLINHA;
        else if (p == 27) a->attr &= (uint8_t)~A_INVERTIDO;
        else if (p >= 30 && p <= 37) a->frente = (uint8_t)(p - 30);
        else if (p == 39) a->frente = COR_PADRAO_FRENTE;
        else if (p >= 40 && p <= 47) a->fundo = (uint8_t)(p - 40);
        else if (p == 49) a->fundo = COR_PADRAO_FUNDO;
        else if (p >= 90 && p <= 97) a->frente = (uint8_t)(p - 90 + 8);
        else if (p >= 100 && p <= 107) a->fundo = (uint8_t)(p - 100 + 8);
        else if ((p == 38 || p == 48) && i + 2 < a->qtd_params && a->params[i + 1] == 5) {
            uint8_t c = (uint8_t)a->params[i + 2];
            if (p == 38) a->frente = c; else a->fundo = c;
            i += 2;
        } else if ((p == 38 || p == 48) && i + 4 < a->qtd_params && a->params[i + 1] == 2) {
            uint8_t c = cor_proxima(a->params[i + 2], a->params[i + 3], a->params[i + 4]);
            if (p == 38) a->frente = c; else a->fundo = c;
            i += 4;
        }
    }
}

/* Manda o tamanho para o programa, no formato da resposta do xterm a
 * "ESC [ 18 t": ESC [ 8 ; linhas ; colunas t. */
static void avisar_tamanho(aba_t *a)
{
    char t[32];
    formatar_texto(t, sizeof t, "\x1b[8;%d;%dt", LINS, COLS);
    escrever_tudo(a->entrada, t, (unsigned)tam_texto(t));
}

static void modo(aba_t *a, bool ligar)
{
    for (int i = 0; i < a->qtd_params; i++) {
        int p = a->params[i];
        if (a->privado == '?') {
            if (p == 25) a->cursor_visivel = ligar;
            else if (p == 7) a->quebra = ligar;
            else if (p == 1) a->teclas_app = ligar;
            else if (p == 7727) {                /* modo próprio do SaguiOS: avisar o tamanho (o ssh usa) */
                a->avisar_tamanho = ligar;
                if (ligar) avisar_tamanho(a);
            }
            else if (p == 1049 || p == 47 || p == 1047) {
                if (ligar && !a->na_alternativa) {
                    a->col_salva = a->col; a->lin_salva = a->lin;
                    a->na_alternativa = true;
                    for (int l = 0; l < LINS; l++) for (int c = 0; c < COLS; c++) a->alternativa[l][c] = vazia(a);
                } else if (!ligar && a->na_alternativa) {
                    a->na_alternativa = false;
                    a->col = a->col_salva; a->lin = a->lin_salva;
                }
                if (a == &abas[atual_aba]) sujar_tudo();
            }
        }
    }
}

static void csi(aba_t *a, char f)
{
    celula_t (*g)[MAX_COLS] = grade(a);
    int n = param(a, 0, 1);
    if (a == &abas[atual_aba]) sujar(a->lin);
    a->apos_fim = false;
    switch (f) {
    case 'A': a->lin = a->lin - n < a->topo_rol ? (a->lin >= a->topo_rol ? a->topo_rol : 0) : a->lin - n; break;
    case 'B': a->lin = a->lin + n > a->base_rol ? (a->lin <= a->base_rol ? a->base_rol : LINS - 1) : a->lin + n; break;
    case 'C': a->col = a->col + n >= COLS ? COLS - 1 : a->col + n; break;
    case 'D': a->col = a->col - n < 0 ? 0 : a->col - n; break;
    case 'E': a->col = 0; a->lin = a->lin + n >= LINS ? LINS - 1 : a->lin + n; break;
    case 'F': a->col = 0; a->lin = a->lin - n < 0 ? 0 : a->lin - n; break;
    case 'G': case '`': a->col = n - 1 >= COLS ? COLS - 1 : n - 1; break;
    case 'd': a->lin = n - 1 >= LINS ? LINS - 1 : n - 1; break;
    case 'H': case 'f': {
        int l = param(a, 0, 1) - 1, c = param(a, 1, 1) - 1;
        a->lin = l < 0 ? 0 : (l >= LINS ? LINS - 1 : l);
        a->col = c < 0 ? 0 : (c >= COLS ? COLS - 1 : c);
        break;
    }
    case 'J': {
        int m = a->qtd_params ? a->params[0] : 0;
        if (m == 0) { limpar_faixa(a, a->lin, a->col, COLS - 1); for (int l = a->lin + 1; l < LINS; l++) limpar_faixa(a, l, 0, COLS - 1); }
        else if (m == 1) { limpar_faixa(a, a->lin, 0, a->col); for (int l = 0; l < a->lin; l++) limpar_faixa(a, l, 0, COLS - 1); }
        else { for (int l = 0; l < LINS; l++) limpar_faixa(a, l, 0, COLS - 1); if (m == 3) a->hist_qtd = 0; }
        break;
    }
    case 'K': {
        int m = a->qtd_params ? a->params[0] : 0;
        if (m == 0) limpar_faixa(a, a->lin, a->col, COLS - 1);
        else if (m == 1) limpar_faixa(a, a->lin, 0, a->col);
        else limpar_faixa(a, a->lin, 0, COLS - 1);
        break;
    }
    case 'X': limpar_faixa(a, a->lin, a->col, a->col + n - 1); break;
    case 'b': {                                 /* REP: repete a última letra n vezes (o ncurses usa) */
        bool g = a->grafico;
        a->grafico = false;                     /* a letra guardada já foi convertida */
        for (int k = 0; k < n && k < COLS * LINS; k++) escrever_letra(a, a->ultima);
        a->grafico = g;
        break;
    }
    case 'P':
        for (int c = a->col; c < COLS; c++) g[a->lin][c] = c + n < COLS ? g[a->lin][c + n] : vazia(a);
        break;
    case '@':
        for (int c = COLS - 1; c >= a->col; c--) g[a->lin][c] = c - n >= a->col ? g[a->lin][c - n] : vazia(a);
        break;
    case 'L': case 'M':
        if (a->lin >= a->topo_rol && a->lin <= a->base_rol) {
            int topo = a->topo_rol;
            a->topo_rol = a->lin;
            rolar(a, f == 'L' ? -n : n);
            a->topo_rol = topo;
        }
        break;
    case 'S': rolar(a, n); break;
    case 'T': rolar(a, -n); break;
    case 'm': cores_sgr(a); break;
    case 'r': {
        int t = param(a, 0, 1) - 1, b = param(a, 1, LINS) - 1;
        if (t < b && b < LINS) { a->topo_rol = t; a->base_rol = b; a->lin = 0; a->col = 0; }
        break;
    }
    case 's': a->col_salva = a->col; a->lin_salva = a->lin; break;
    case 'u': a->col = a->col_salva; a->lin = a->lin_salva; break;
    case 'h': modo(a, true); break;
    case 'l': modo(a, false); break;
    case 'n':
        if (param(a, 0, 0) == 6) {
            char t[24];
            formatar_texto(t, sizeof t, "\x1b[%d;%dR", a->lin + 1, a->col + 1);
            responder(a, t);
        } else if (param(a, 0, 0) == 5) responder(a, "\x1b[0n");
        break;
    case 'c': if (a->privado == 0) responder(a, "\x1b[?1;2c"); break;
    default: break;
    }
    if (a == &abas[atual_aba]) sujar(a->lin);
}

static void receber_byte(aba_t *a, uint8_t b)
{
    switch (a->estado) {
    case 1:   /* depois de ESC */
        a->estado = 0;
        if (b == '[') { a->estado = 2; a->qtd_params = 0; a->params[0] = 0; a->privado = 0; return; }
        if (b == ']') { a->estado = 3; a->osc_n = 0; return; }
        if (b == '(') { a->estado = 4; return; }
        if (b == '7') { a->col_salva = a->col; a->lin_salva = a->lin; }
        else if (b == '8') { a->col = a->col_salva; a->lin = a->lin_salva; }
        else if (b == 'M') { if (a->lin == a->topo_rol) rolar(a, -1); else if (a->lin > 0) a->lin--; }
        else if (b == 'D') nova_linha(a);
        else if (b == 'E') { a->col = 0; nova_linha(a); }
        else if (b == 'c') { reiniciar_aba(a); if (a == &abas[atual_aba]) sujar_tudo(); }
        return;
    case 2:   /* CSI: parâmetros e a letra final */
        if (b == '?' || b == '>' || b == '=') { a->privado = (char)b; return; }
        if (b >= '0' && b <= '9') {
            if (a->qtd_params == 0) a->qtd_params = 1;
            int i = a->qtd_params - 1;
            if (a->params[i] < 10000) a->params[i] = a->params[i] * 10 + (b - '0');
            return;
        }
        if (b == ';' || b == ':') {
            if (a->qtd_params == 0) a->qtd_params = 1;
            if (a->qtd_params < 16) a->params[a->qtd_params++] = 0;
            return;
        }
        a->estado = 0;
        if (b >= 0x40 && b <= 0x7E) csi(a, (char)b);
        return;
    case 3:   /* OSC: até BEL ou ESC \ */
        if (b == 7 || b == 0x9C) goto fim_osc;
        if (b == 27) { a->estado = 5; return; }
        if (a->osc_n < (int)sizeof a->osc - 1) a->osc[a->osc_n++] = (char)b;
        return;
    case 5:
        if (b != '\\') { a->estado = 0; return; }
    fim_osc:
        a->estado = 0;
        a->osc[a->osc_n] = 0;
        if ((a->osc[0] == '0' || a->osc[0] == '2') && a->osc[1] == ';') {
            copia_texto(a->nome, a->osc + 2, sizeof a->nome);
            abas_sujas = true;
        }
        return;
    case 4:   /* ESC ( conjunto */
        a->estado = 0;
        a->grafico = b == '0';
        return;
    }
    /* Texto. Os programas do SaguiOS escrevem em Latin-1 ("ç" = um byte
     * 0xE7) e os servidores em UTF-8 ("ç" = 0xC3 0xA7). Um byte que poderia
     * começar um caractere UTF-8 fica guardado; se o seguinte não for uma
     * continuação, ele era Latin-1 e aparece como tal. */
    if (a->faltam > 0) {                       /* continuação de UTF-8? */
        if ((b & 0xC0) == 0x80) {
            a->cod = (a->cod << 6) | (b & 0x3F);
            a->dois_bytes = true;
            if (--a->faltam == 0) escrever_letra(a, glifo(a->cod));
            return;
        }
        a->faltam = 0;
        if (!a->dois_bytes) escrever_letra(a, a->primeiro);   /* era Latin-1 */
        else escrever_letra(a, '?');           /* UTF-8 cortado no meio */
    }
    if (b == 27) { a->estado = 1; return; }
    if (b >= 0xC2 && b <= 0xF4) {              /* pode ser começo de UTF-8 */
        a->faltam = b >= 0xF0 ? 3 : (b >= 0xE0 ? 2 : 1);
        a->cod = b & (b >= 0xF0 ? 0x07 : (b >= 0xE0 ? 0x0F : 0x1F));
        a->primeiro = b;
        a->dois_bytes = false;
        return;
    }
    if (a == &abas[atual_aba]) sujar(a->lin);
    switch (b) {
    case '\n': case 0x0B: case 0x0C: a->apos_fim = false; nova_linha(a); a->col = 0; break;
    case '\r': a->apos_fim = false; a->col = 0; break;
    case '\t': a->col = (a->col + 8) & ~7; if (a->col >= COLS) a->col = COLS - 1; break;
    case '\b': a->apos_fim = false; if (a->col > 0) a->col--; break;
    case 7: break;
    case 0x0E: a->grafico = true; break;
    case 0x0F: a->grafico = false; break;
    default:
        if (b >= 0x20 && b != 0x7F) escrever_letra(a, b);   /* Latin-1 direto (o sagsh e os programas do SaguiOS) */
        break;
    }
    if (a == &abas[atual_aba]) sujar(a->lin);
}

/* ------------------------------------------------------------ abas */

static char **programa_argv;   /* terminal -e programa args: a primeira aba roda ele */

static int abrir_aba(void)
{
    int i = 0;
    while (i < MAX_ABAS && abas[i].usada) i++;
    if (i == MAX_ABAS) return -1;
    aba_t *a = &abas[i];
    memset(a, 0, sizeof *a);
    int entrada[2], saida[2];
    if (criar_pipe(entrada) < 0 || criar_pipe(saida) < 0) return -1;
    /* O shell nasce com o pipe de entrada como 0 e o de saída como 1 e 2. */
    int g0 = duplicar(ENTRADA, 13), g1 = duplicar(SAIDA, 14), g2 = duplicar(ERROS, 15);
    duplicar(entrada[0], ENTRADA);
    duplicar(saida[1], SAIDA);
    duplicar(saida[1], ERROS);
    char *args[] = { "sagsh", NULL };
    char **quem = programa_argv ? programa_argv : args;
    int shell = executar(programa_argv ? programa_argv[0] : "/bin/sagsh", quem);
    duplicar(g0, ENTRADA); duplicar(g1, SAIDA); duplicar(g2, ERROS);
    fechar(g0); fechar(g1); fechar(g2);
    fechar(entrada[0]);
    fechar(saida[1]);
    if (shell < 0) { fechar(entrada[1]); fechar(saida[0]); return -1; }
    a->usada = true;
    a->shell = shell;
    a->entrada = entrada[1];
    a->saida = saida[0];
    if (programa_argv) {
        const char *nome = acha_ultimo_char(programa_argv[0], '/');
        copia_texto(a->nome, nome ? nome + 1 : programa_argv[0], sizeof a->nome);
        a->programa = true;
        programa_argv = NULL;                    /* as abas novas (+) são shells */
    } else formatar_texto(a->nome, sizeof a->nome, "Terminal %d", i + 1);
    reiniciar_aba(a);
    registrar("terminal: aba %d aberta (sagsh %d)", i + 1, shell);
    return i;
}

static void fechar_aba(int i)
{
    aba_t *a = &abas[i];
    if (a->shell > 0) matar(a->shell);
    fechar(a->entrada);
    fechar(a->saida);
    a->usada = false;
    for (int k = 0; k < MAX_ABAS; k++)
        if (abas[k].usada) { atual_aba = k; break; }
    abas_sujas = true;
    sujar_tudo();
}

static int abas_abertas(void)
{
    int n = 0;
    for (int i = 0; i < MAX_ABAS; i++) if (abas[i].usada) n++;
    return n;
}

/* Tecla da janela -> bytes para o shell, no formato do modo bruto. */
static void enviar_tecla(aba_t *a, unsigned t)
{
    char b[2];
    int n = 1;
    if (t < 0x100) b[0] = (char)t;
    else {
        b[0] = 27;
        switch (t) {
        case TECLA_CIMA: b[1] = TECLA_B_CIMA; break;
        case TECLA_BAIXO: b[1] = TECLA_B_BAIXO; break;
        case TECLA_ESQUERDA: b[1] = TECLA_B_ESQUERDA; break;
        case TECLA_DIREITA: b[1] = TECLA_B_DIREITA; break;
        case TECLA_HOME: b[1] = TECLA_B_HOME; break;
        case TECLA_END: b[1] = TECLA_B_END; break;
        case TECLA_DELETE: b[1] = TECLA_B_DELETE; break;
        case TECLA_PGUP: b[1] = TECLA_B_PGUP; break;
        case TECLA_PGDN: b[1] = TECLA_B_PGDN; break;
        case TECLA_ESC: b[1] = 27; break;
        default: return;
        }
        n = 2;
    }
    escrever_tudo(a->entrada, b, (unsigned)n);
}

/* Ctrl+C: encerra o programa que está rodando na aba (o "primeiro plano").
 * O shell só lê a tecla quando está esperando uma linha; com um programa
 * rodando, ninguém lia o Ctrl+C e um programa travado (o tocar no G470, com a
 * placa de som parada) não tinha como ser parado. Aqui o terminal procura os
 * descendentes do shell da aba e os encerra; o shell volta ao prompt sozinho.
 * Programas que tratam o Ctrl+C eles mesmos (o ssh manda para a outra
 * máquina; o Sagui Code apaga a pergunta) recebem a tecla normalmente.
 * Devolve true se encerrou alguém. */
static bool interromper(aba_t *a)
{
    static info_processo_t v[64];
    int n = info_sistema(INFO_PROCESSOS, v, 64);
    if (n <= 0 || a->shell <= 0) return false;
    if (n > 64) n = 64;
    /* Marca os descendentes: repete até não aparecer ninguém novo. */
    static bool alvo[64];
    for (int i = 0; i < n; i++) alvo[i] = false;
    bool mudou = true;
    while (mudou) {
        mudou = false;
        for (int i = 0; i < n; i++) {
            if (alvo[i] || v[i].estado == 4) continue;
            bool filho = v[i].pai == a->shell;
            for (int k = 0; k < n && !filho; k++) if (alvo[k] && v[i].pai == v[k].pid) filho = true;
            if (filho) { alvo[i] = mudou = true; }
        }
    }
    /* Aba aberta com "-e programa": o próprio programa é o "shell" da aba. */
    bool so_o_programa = true;
    for (int i = 0; i < n; i++) if (alvo[i]) so_o_programa = false;
    for (int i = 0; i < n; i++) {
        bool eh = alvo[i] || (so_o_programa && v[i].pid == a->shell && compara_texto(v[i].nome, "sagsh") != 0);
        if (!eh) continue;
        if (compara_texto(v[i].nome, "ssh") == 0 || compara_texto(v[i].nome, "saguicode") == 0) return false;
        alvo[i] = true;
    }
    bool algum = false;
    for (int i = 0; i < n; i++)
        if (alvo[i]) { matar(v[i].pid); registrar("terminal: Ctrl+C encerrou %s (%d)", v[i].nome, v[i].pid); algum = true; }
    if (algum) {
        const char *t = "^C\r\n";
        for (int i = 0; t[i]; i++) receber_byte(a, (uint8_t)t[i]);
    }
    return algum;
}

/* ------------------------------------------------------------ copiar e colar */

static void posicao(int x, int y, int *l, int *c)
{
    *c = (x - MARGEM) / LETRA_L;
    *l = (y - TOPO) / LETRA_A;
    if (*c < 0) *c = 0;
    if (*c >= COLS) *c = COLS - 1;
    if (*l < 0) *l = 0;
    if (*l >= LINS) *l = LINS - 1;
}

static void copiar_selecao(aba_t *a)
{
    int ini = sel_ini_l * COLS + sel_ini_c, fim = sel_fim_l * COLS + sel_fim_c;
    if (ini > fim) { int t = ini; ini = fim; fim = t; }
    int k = 0;
    celula_t (*g)[MAX_COLS] = grade(a);
    for (int l = ini / COLS; l <= fim / COLS; l++) {
        int c0 = l == ini / COLS ? ini % COLS : 0, c1 = l == fim / COLS ? fim % COLS : COLS - 1;
        int ultimo = c1;
        while (ultimo >= c0 && g[l][ultimo].c == ' ') ultimo--;   /* sem espaços no fim da linha */
        for (int c = c0; c <= ultimo; c++) {
            uint8_t ch = g[l][c].c;
            area_copia[k++] = (char)(ch >= 0x80 && ch < 0xA0 ? '+' : ch);
        }
        if (l != fim / COLS) area_copia[k++] = '\n';
    }
    area_copia[k] = 0;
    registrar("terminal: copiado (%d letras)", k);
}

/* O programa de "terminal -e" acabou: a aba fica aberta para a pessoa ler. */
static void terminar_programa(aba_t *a, int codigo)
{
    /* Escreve o que ainda estiver no pipe antes do aviso. */
    char buf[512];
    int n;
    while (disponivel(a->saida) > 0 && (n = ler(a->saida, buf, sizeof buf)) > 0)
        for (int k = 0; k < n; k++) receber_byte(a, (uint8_t)buf[k]);
    char t[96];
    if (codigo == -1) formatar_texto(t, sizeof t, "\r\n\x1b[90m[o programa terminou. Aperte uma tecla para fechar.]\x1b[0m");
    else formatar_texto(t, sizeof t, "\r\n\x1b[90m[o programa terminou (código %d). Aperte uma tecla para fechar.]\x1b[0m", codigo);
    for (const char *p = t; *p; p++) receber_byte(a, (uint8_t)*p);
    a->terminou = true;
    registrar("terminal: programa da aba %s terminou (codigo %d)", a->nome, codigo);
}

/* ------------------------------------------------------------ tamanho (H78) */

/* A janela mudou: recalcula linhas e colunas. Se a tela encolheu e o cursor
 * ficaria de fora, as linhas de cima sobem para o histórico. */
static void mudar_tamanho(int l, int a_px)
{
    j->larg = l;
    j->alt = a_px;
    int nc = (l - 2 * MARGEM) / LETRA_L, nl = (a_px - 2 * MARGEM - ABAS_A) / LETRA_A;
    if (nc > MAX_COLS) nc = MAX_COLS;
    if (nl > MAX_LINS) nl = MAX_LINS;
    if (nc < 20) nc = 20;
    if (nl < 5) nl = 5;
    for (int i = 0; i < MAX_ABAS; i++) {
        aba_t *a = &abas[i];
        if (!a->usada) continue;
        int sobe = a->lin - (nl - 1);
        if (sobe > 0 && !a->na_alternativa) {
            for (int k = 0; k < sobe; k++) guardar_no_historico(a, k);
            for (int r = 0; r + sobe < LINS; r++) for (int c = 0; c < MAX_COLS; c++) a->tela[r][c] = a->tela[r + sobe][c];
            a->lin -= sobe;
        }
        /* Linhas e colunas novas começam vazias. */
        int validas = LINS - (sobe > 0 && !a->na_alternativa ? sobe : 0);
        for (int r = 0; r < nl; r++)
            for (int c = 0; c < nc; c++)
                if (r >= validas || c >= COLS) { a->tela[r][c] = vazia(a); a->alternativa[r][c] = vazia(a); }
        if (a->lin >= nl) a->lin = nl - 1;
        if (a->col >= nc) a->col = nc - 1;
        a->topo_rol = 0;
        a->base_rol = nl - 1;
    }
    cols = nc;
    lins = nl;
    g_retangulo(j, 0, 0, l, a_px, COR_FUNDO_T_GLOBAL);
    abas_sujas = true;
    sujar_tudo();
    for (int i = 0; i < MAX_ABAS; i++) if (abas[i].usada && abas[i].avisar_tamanho) avisar_tamanho(&abas[i]);
    registrar("terminal: tamanho %dx%d (%d colunas, %d linhas)", l, a_px, cols, lins);
}

/* ------------------------------------------------------------ laço */

/* Cópia da primeira aba para a serial, que é o log lido pelos testes. Sai por
 * linha inteira: copiando cada pedaço lido do pipe, uma linha escrita em
 * partes ("inicio " e depois o número) podia ser cortada pelo aviso do kernel
 * de que o programa terminou, que chega entre os dois pedaços. O que sobra sem
 * '\n' (o prompt, por exemplo) sai quando o pipe fica 30 ms sem nada novo. */
static char serial_pend[2048];
static int serial_qtd;
static unsigned serial_quando;

static void copia_serial(const char *b, int n)
{
    for (int k = 0; k < n; k++) {
        if (serial_qtd == (int)sizeof serial_pend) { escrever(SAIDA, serial_pend, (unsigned)serial_qtd); serial_qtd = 0; }
        serial_pend[serial_qtd++] = b[k];
    }
    int ultima = -1;
    for (int k = serial_qtd - 1; k >= 0; k--) if (serial_pend[k] == '\n') { ultima = k; break; }
    if (ultima >= 0) {
        escrever(SAIDA, serial_pend, (unsigned)(ultima + 1));
        serial_qtd -= ultima + 1;
        memmove(serial_pend, serial_pend + ultima + 1, (size_t)serial_qtd);
    }
    serial_quando = tempo_ms();
}

static void copia_serial_resto(void)
{
    if (serial_qtd > 0 && tempo_ms() - serial_quando >= 30) {
        escrever(SAIDA, serial_pend, (unsigned)serial_qtd);
        serial_qtd = 0;
    }
}

int principal(int argc, char **argv)
{
    /* terminal -e programa [argumentos]: abre já rodando o programa (o Editor
     * e a área de trabalho usam para programas de texto). */
    if (argc >= 3 && compara_texto(argv[1], "-e") == 0) programa_argv = argv + 2;
    j = jan_abrir("Terminal", LARG, ALT);
    if (!j) return 1;
    jan_redimensionavel(j, 40 * LETRA_L + 2 * MARGEM, 6 * LETRA_A + 2 * MARGEM + ABAS_A);
    g_retangulo(j, 0, 0, LARG, ALT, COR_FUNDO_T_GLOBAL);
    if (abrir_aba() < 0) { imprimir("terminal: não consegui abrir o sagsh\n"); return 1; }
    atual_aba = 0;
    sujar_tudo();
    mostrar();

    char buf[1024];
    evento_t e;
    for (;;) {
        bool fez = false;
        copia_serial_resto();
        for (int i = 0; i < MAX_ABAS; i++) {
            aba_t *a = &abas[i];
            if (!a->usada) continue;
            int d = disponivel(a->saida);
            if (d == ERRO_PIPE_QUEBRADO && a->programa) {
                if (!a->terminou) terminar_programa(a, -1);
                continue;
            }
            if (d == ERRO_PIPE_QUEBRADO) { fechar_aba(i); fez = true; continue; }   /* o shell terminou (sair) */
            if (d > 0) {
                int n = ler(a->saida, buf, sizeof buf);
                for (int k = 0; k < n; k++) receber_byte(a, (uint8_t)buf[k]);
                if (n > 0 && i == 0) copia_serial(buf, n);   /* cópia para a serial (os testes leem) */
                if (n > 0 && i == atual_aba && a->olhando) { a->olhando = 0; sujar_tudo(); }
                fez = true;
            }
        }
        if (abas_abertas() == 0) break;
        aba_t *a = &abas[atual_aba];
        while (proximo_evento(j->id, &e, false) == 1) {
            fez = true;
            if (e.tipo == EV_FECHAR) goto fim;
            if (e.tipo == EV_REDIMENSIONAR) { mudar_tamanho(e.x, e.y); continue; }
            if (e.tipo == EV_TECLA) {
                if ((e.modificadores & 1) && (e.tecla == TECLA_PGUP || e.tecla == TECLA_PGDN)) {   /* Shift: histórico */
                    int passo = e.tecla == TECLA_PGUP ? LINS / 2 : -LINS / 2;
                    a->olhando += passo;
                    if (a->olhando > a->hist_qtd) a->olhando = a->hist_qtd;
                    if (a->olhando < 0) a->olhando = 0;
                    sujar_tudo();
                    continue;
                }
                if (a->olhando) { a->olhando = 0; sujar_tudo(); }
                if (a->terminou) { fechar_aba(atual_aba); continue; }   /* qualquer tecla fecha */
                if (e.tecla == 3 && interromper(a)) { sujar_tudo(); continue; }
                enviar_tecla(a, e.tecla);
            } else if (e.tipo == EV_CLIQUE && e.y < ABAS_A) {
                int i = e.x / 154, x_mais = 4 + abas_abertas() * 154;
                int k = -1, vista = 0;
                for (int t = 0; t < MAX_ABAS; t++) if (abas[t].usada) { if (vista == i) k = t; vista++; }
                if (e.botoes & 2) { if (k >= 0 && abas_abertas() > 1) fechar_aba(k); }   /* botão direito na aba: fecha */
                else if (k >= 0) { atual_aba = k; abas_sujas = true; sujar_tudo(); }
                else if (e.x >= x_mais && e.x < x_mais + 24) {
                    int nova = abrir_aba();
                    if (nova >= 0) { atual_aba = nova; abas_sujas = true; sujar_tudo(); }
                }
            } else if (e.tipo == EV_CLIQUE && (e.botoes & 2)) {   /* botão direito: menu (H81); "Colar" é o primeiro */
                jan_menu(j, e.x, e.y, "Colar|Interromper (Ctrl+C)|Limpar a tela (Ctrl+L)|Nova aba|Fechar esta aba");
            } else if (e.tipo == EV_MENU) {
                registrar("terminal: menu %u", e.tecla);
                if (e.tecla == 0 && area_copia[0]) escrever_tudo(a->entrada, area_copia, (unsigned)tam_texto(area_copia));
                else if (e.tecla == 1) { if (!interromper(a)) enviar_tecla(a, 3); sujar_tudo(); }
                else if (e.tecla == 2) enviar_tecla(a, 12);
                else if (e.tecla == 3) { int nova = abrir_aba(); if (nova >= 0) { atual_aba = nova; abas_sujas = true; sujar_tudo(); } }
                else if (e.tecla == 4 && abas_abertas() > 1) fechar_aba(atual_aba);
            } else if (e.tipo == EV_CLIQUE) {
                if (sel_ini_l >= 0) { sel_ini_l = -1; sujar_tudo(); }
                posicao(e.x, e.y, &sel_ini_l, &sel_ini_c);
                sel_fim_l = sel_ini_l; sel_fim_c = sel_ini_c;
                sel_ini_l = -2 - sel_ini_l;           /* marcando (ainda não soltou) */
            } else if (e.tipo == EV_SOLTOU && sel_ini_l <= -2) {
                sel_ini_l = -2 - sel_ini_l;
                posicao(e.x, e.y, &sel_fim_l, &sel_fim_c);
                if (sel_fim_l == sel_ini_l && sel_fim_c == sel_ini_c) sel_ini_l = -1;   /* só um clique */
                else { copiar_selecao(a); sujar_tudo(); }
            }
        }
        mostrar();
        /* Shells que terminaram (o pipe pode continuar aberto por um programa gráfico filho). */
        int c, pid;
        while ((pid = recolher(&c)) > 0)
            for (int i = 0; i < MAX_ABAS; i++) if (abas[i].usada && abas[i].shell == pid) {
                abas[i].shell = -1;
                if (abas[i].programa) { if (!abas[i].terminou) terminar_programa(&abas[i], c); }
                else fechar_aba(i);
            }
        if (abas_abertas() == 0) break;
        if (!fez) dormir(10);
    }
fim:
    for (int i = 0; i < MAX_ABAS; i++) if (abas[i].usada && abas[i].shell > 0) matar(abas[i].shell);
    jan_fechar(j);
    return 0;
}
