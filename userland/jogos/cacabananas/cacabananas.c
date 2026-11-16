/* cacabananas - Caça-Bananas, o jogo do bananal do SaguiOS (H92).
 *
 * Trazido do buscaminas do fork SaguiOS-Extended de Alejandro Anuncibay e
 * reescrito sobre o main atual, com o tema de banana do SaguiOS: o campo é
 * um bananal coberto de folhas, a "mina" é uma banana-bomba com pavio, a
 * bandeira é uma bandeirinha com a cara do sagui e, ao ganhar, o sagui
 * aparece com um cacho de bananas. Todos os desenhos estão aqui, em texto.
 *
 * Níveis: Fácil 9x9 com 10 bombas, Médio 16x16 com 40, Difícil 30x16 com 99
 * e Personalizado. Mouse: botão esquerdo abre a casa; o direito põe a
 * bandeira (e, depois, o "?"); clicar num número com todas as bandeiras em
 * volta abre as vizinhas. Teclado: setas, espaço ou Enter abre, B põe
 * bandeira, F2 começa outra partida, 1 a 4 trocam o nível. O cronômetro
 * começa no primeiro clique, que nunca acha bomba (regras.c).
 *
 * Os melhores tempos ficam nas configurações do sistema (config_gravar),
 * que vão para o pendrive quando há um. Opções: --nivel 1|2|3, --semente N
 * (partida repetível, para os testes). */
#include "sagui.h"
#include "gui.h"
#include "regras.h"

#define CASA     24
#define FAIXA    26          /* abas dos níveis */
#define CAB_Y    FAIXA
#define CAB_A    52
#define TAB_Y    (CAB_Y + CAB_A + 6)
#define MARGEM   12
#define RODAPE   28
#define LARG_MIN 360

static const char *const nomes_nivel[4] = { "Fácil", "Médio", "Difícil", "Personalizado" };
static const char *const chaves_recorde[3] = { "cacabananas.recorde.facil", "cacabananas.recorde.medio",
                                               "cacabananas.recorde.dificil" };

static jan_t *jan;
static campo_t campo;
static int nivel = 1;                     /* 1 a 4 */
static int p_cols = 20, p_linhas = 12, p_bombas = 40;   /* o personalizado */
static uint32_t semente;
static unsigned t_inicio, t_fim;
static int recorde[3];
static int cur_x = -1, cur_y = -1;        /* cursor do teclado */
static int larg, alt, x0;
static bool apertando, sair_logo;
static int ultimo_segundo = -1;
static char aviso[96];

/* ------------------------------------------------------------ desenhos (pixel art nossa) */

/* A banana-bomba: banana amarela com faixas escuras, pavio e faísca. */
static const char *const sp_bomba[16] = {
    "..........o.o...",
    "...........O....",
    "..........o.o...",
    ".........k......",
    "........k.......",
    ".......SS.......",
    "......YYY.......",
    ".....YYYD.......",
    "....YYYYD.......",
    "...DYYYYD.......",
    "..YYDYYYYD......",
    ".YYYYDDYYYD.....",
    ".YYYYYYDDYYD....",
    "..YYYYYYYYYYD...",
    "...DYYYYYYYD....",
    ".....DDDDDD.....",
};

/* A bandeirinha do sagui: um mastro e um pano com a cara dele. */
static const char *const sp_bandeira[14] = {
    "..M.........",
    "..MWAAAAW...",
    "..MAEEEEA...",
    "..MAPEEPA...",
    "..MARRRRA...",
    "..MAAAAA....",
    "..M.........",
    "..M.........",
    "..M.........",
    "..M.........",
    ".MMM........",
    "MMMMM.......",
    "............",
    "............",
};

/* A cara do sagui no botão de partida nova: normal, assustado (botão
 * apertado), feliz com banana (ganhou) e tonto (perdeu). */
static const char *const sp_sagui[4][16] = {
    { "................",
      ".WW..........WW.",
      "WWWW.EEEEEE.WWWW",
      "WWWEEEEWWEEEEWWW",
      ".WWEERRRRRREEWW.",
      "...ERRRRRRRRE...",
      "...ERPPRRPPRE...",
      "...ERPPRRPPRE...",
      "...ERRRRRRRRE...",
      "...ERRRPPRRRE...",
      "....ERRRRRRE....",
      "....ERPPPPRE....",
      ".....ERRRRE.....",
      "......EEEE......",
      "................",
      "................" },
    { "................",
      ".WW..........WW.",
      "WWWW.EEEEEE.WWWW",
      "WWWEEEEWWEEEEWWW",
      ".WWEERRRRRREEWW.",
      "...ERWWRRWWRE...",
      "...ERWPRRWPRE...",
      "...ERWWRRWWRE...",
      "...ERRRRRRRRE...",
      "...ERRRPPRRRE...",
      "....ERRPPRRE....",
      "....ERRPPRRE....",
      ".....ERRRRE.....",
      "......EEEE......",
      "................",
      "................" },
    { "................",
      ".WW..........WW.",
      "WWWW.EEEEEE.WWWW",
      "WWWEEEEWWEEEEWWW",
      ".WWEERRRRRREEWW.",
      "...ERRRRRRRRE...",
      "...EPPRRRRPPE...",
      "...ERRRRRRRRE...",
      "...ERRRPPRRRE...",
      "...EPRRRRRRPE.YY",
      "....EPPPPPPE.YYD",
      "....ERRRRRRE.YD.",
      ".....ERRRRE.YYD.",
      "......EEEE.YYD..",
      "..........YYD...",
      "..........DD...." },
    { "................",
      ".WW..........WW.",
      "WWWW.EEEEEE.WWWW",
      "WWWEEEEWWEEEEWWW",
      ".WWEERRRRRREEWW.",
      "...EPRPRRPRPE...",
      "...ERPRRRRPRE...",
      "...EPRPRRPRPE...",
      "...ERRRRRRRRE...",
      "...ERRRPPRRRE...",
      "....ERRRRRRE....",
      "....ERPPPPRE....",
      ".....EPRRPE.....",
      "......EEEE......",
      "................",
      "................" },
};

/* O cacho de bananas da tela de vitória. */
static const char *const sp_cacho[12] = {
    ".....MM.....",
    ".....MM.....",
    "....DMMD....",
    "...YDYYDY...",
    "..YYDYYDYY..",
    ".YYDYYYYDYY.",
    ".YYDYYYYDYY.",
    "YYDYYYYYYDYY",
    "YYDYYYYYYDYY",
    "DYDYYYYYYDYD",
    ".D.DYYYYD.D.",
    "....DDDD....",
};

static uint32_t paleta(char c)
{
    switch (c) {
    case 'Y': return 0xFFD52E;   /* banana */
    case 'D': return 0xB07A12;   /* banana na sombra, faixas da bomba */
    case 'S': return 0x5A3E1A;   /* cabinho */
    case 'k': return 0x2A2016;   /* pavio */
    case 'o': return 0xFF8A1E;   /* faísca */
    case 'O': return 0xFFF3A8;
    case 'M': return 0x6E4A26;   /* mastro, galho */
    case 'A': return 0xFFE35A;   /* pano da bandeira */
    case 'W': return 0xF5F5F0;   /* tufos brancos do sagui */
    case 'E': return 0x3C2D23;   /* pelo escuro */
    case 'R': return 0xC08E78;   /* rosto */
    case 'P': return 0x17120F;   /* olhos e boca */
    default:  return 0xFF000000;
    }
}

/* As cores dos números: fortes o bastante para ler sobre a terra clara. */
static const uint32_t cor_numero[9] = { 0, 0x1A5FC8, 0x1F8A3A, 0xC8322A, 0x5A2A9A, 0x9A4A12, 0x16868C, 0x2A2420, 0x6A737C };

/* ------------------------------------------------------------ recordes */

static void ler_recordes(void)
{
    for (int k = 0; k < 3; k++) {
        char v[16];
        if (config_ler(chaves_recorde[k], v, sizeof v) > 0) recorde[k] = texto_para_int(v);
    }
}

/* ------------------------------------------------------------ desenho */

static void texto_grosso(int x, int y, const char *t, uint32_t cor)
{
    g_texto(jan, x, y, t, cor);
    g_texto(jan, x + 1, y, t, cor);
}

static int segundos(void)
{
    if (campo.estado == CAMPO_ESPERA) return 0;
    unsigned fim = campo.estado == CAMPO_JOGANDO ? tempo_ms() : t_fim;
    int s = (int)(fim - t_inicio) / 1000;
    return s > 999 ? 999 : s;
}

static void desenhar_casa(int x, int y)
{
    int px = x0 + x * CASA, py = TAB_Y + y * CASA;
    uint8_t c = CAMPO_CASA(&campo, x, y);
    if (!(c & C_ABERTA)) {
        /* Folha de bananeira: verde com a nervura do meio mais clara e as
         * listras diagonais (alternando o lado, como num canteiro). */
        bool par = ((x + y) & 1) == 0;
        g_retangulo(jan, px, py, CASA, CASA, par ? 0x3FA34D : 0x379445);
        for (int k = 3; k < CASA - 3; k += 5)
            g_linha(jan, px + CASA / 2, py + k, par ? px + 3 : px + CASA - 4, py + k + 4, par ? 0x52B860 : 0x48AA56);
        g_retangulo(jan, px + CASA / 2, py + 2, 1, CASA - 4, 0x8FD88F);
        g_retangulo(jan, px, py + CASA - 1, CASA, 1, 0x1F6B2A);
        g_retangulo(jan, px + CASA - 1, py, 1, CASA, 0x1F6B2A);
        if (c & C_BANDEIRA) g_desenho(jan, px + 6, py + 5, sp_bandeira, 12, 14, paleta, 1);
        else if (c & C_DUVIDA) texto_grosso(px + 8, py + 4, "?", 0xFFF3A8);
    } else {
        g_retangulo(jan, px, py, CASA, CASA, (c & C_ESTOUROU) ? 0xE8604A : 0xEAD9B0);   /* terra */
        g_retangulo(jan, px, py + CASA - 1, CASA, 1, 0xC9B488);
        g_retangulo(jan, px + CASA - 1, py, 1, CASA, 0xC9B488);
        if (c & C_BOMBA) g_desenho(jan, px + 4, py + 4, sp_bomba, 16, 16, paleta, 1);
        else {
            int n = campo_vizinhas(&campo, x, y, C_BOMBA);
            char t[2] = { (char)('0' + n), 0 };
            if (n) texto_grosso(px + 8, py + 4, t, cor_numero[n]);
        }
    }
    if (c & C_ERRADA) {                    /* bandeira errada: um X vermelho */
        g_linha(jan, px + 3, py + 3, px + CASA - 4, py + CASA - 4, 0xC8161F);
        g_linha(jan, px + CASA - 4, py + 3, px + 3, py + CASA - 4, 0xC8161F);
    }
    if (x == cur_x && y == cur_y) g_moldura(jan, px + 1, py + 1, CASA - 3, CASA - 3, 0xFFD52E);
}

/* Um número grande (a fonte do sistema em dobro), num visor escuro. */
static void visor(int x, int y, int v, const char *rotulo)
{
    char t[8];
    if (v < -99) v = -99;
    if (v > 999) v = 999;
    formatar_texto(t, sizeof t, "%03d", v < 0 ? -v : v);
    if (v < 0) t[0] = '-';
    g_retangulo(jan, x, y, 64, 36, 0x18202A);
    g_moldura(jan, x, y, 64, 36, 0xFFD52E);
    extern const unsigned char fonte_sagui[256][16];
    for (int k = 0; k < 3; k++) {
        const unsigned char *g = fonte_sagui[(uint8_t)t[k]];
        for (int lin = 0; lin < 16; lin++)
            for (int col = 0; col < 8; col++)
                if (g[lin] & (0x80 >> col)) g_retangulo(jan, x + 8 + k * 16 + col * 2, y + 2 + lin * 2, 2, 2, 0xFFD52E);
    }
    g_texto(jan, x, y + 36, rotulo, COR_TEXTO_FRACO);
}

static void rosto_pos(int *x, int *y) { *x = larg / 2 - 22; *y = CAB_Y + 4; }

static void desenhar_cabecalho(void)
{
    g_retangulo(jan, 0, CAB_Y, larg, CAB_A, 0xDCEFD0);
    visor(MARGEM, CAB_Y + 2, campo.bombas - campo.bandeiras, "");
    visor(larg - MARGEM - 64, CAB_Y + 2, segundos(), "");
    int fx, fy;
    rosto_pos(&fx, &fy);
    g_retangulo(jan, fx, fy, 44, 44, apertando ? 0xE8C440 : 0xFFE35A);
    g_moldura(jan, fx, fy, 44, 44, 0xB07A12);
    int k = campo.estado == CAMPO_GANHOU ? 2 : campo.estado == CAMPO_PERDEU ? 3 : apertando ? 1 : 0;
    g_desenho(jan, fx + 6, fy + 6, sp_sagui[k], 16, 16, paleta, 2);
}

static void aba_pos(int k, int *x, int *l)
{
    int xx = 6;
    for (int i = 0; i < k; i++) xx += g_largura_texto(nomes_nivel[i]) + 20;
    *x = xx;
    *l = g_largura_texto(nomes_nivel[k]) + 14;
}

static void desenhar_abas(void)
{
    g_retangulo(jan, 0, 0, larg, FAIXA, 0x1F6B2A);
    for (int k = 0; k < 4; k++) {
        int x, l;
        aba_pos(k, &x, &l);
        bool sel = k == nivel - 1;
        if (sel) g_retangulo(jan, x, 3, l, FAIXA - 3, 0xFFD52E);
        g_texto(jan, x + 7, 5, nomes_nivel[k], sel ? 0x3C2D23 : 0xDCEFD0);
    }
}

/* A vitória: o sagui segura um cacho de bananas, numa faixa sobre o campo. */
static void desenhar_vitoria(void)
{
    int l = 260 < larg - 20 ? 260 : larg - 20, a = 104, x = (larg - l) / 2, y = TAB_Y + 10;
    g_retangulo(jan, x, y, l, a, 0xFFF3B0);
    g_moldura(jan, x, y, l, a, 0xB07A12);
    g_desenho(jan, x + 10, y + 12, sp_sagui[2], 16, 16, paleta, 3);
    g_desenho(jan, x + 64, y + 40, sp_cacho, 12, 12, paleta, 4);
    g_texto(jan, x + 122, y + 30, "Você achou", COR_TEXTO);
    g_texto(jan, x + 122, y + 48, "todas as bombas!", COR_TEXTO);
    char t[32];
    formatar_texto(t, sizeof t, "%d s", segundos());
    texto_grosso(x + 122, y + 70, t, 0x1F6B2A);
}

static void redesenhar(void)
{
    g_retangulo(jan, 0, 0, larg, alt, 0xDCEFD0);
    desenhar_abas();
    desenhar_cabecalho();
    for (int y = 0; y < campo.linhas; y++)
        for (int x = 0; x < campo.cols; x++) desenhar_casa(x, y);
    if (campo.estado == CAMPO_GANHOU) desenhar_vitoria();
    const char *t = aviso[0] ? aviso : "Botão direito: bandeira. F2: outra partida.";
    g_texto(jan, MARGEM, alt - RODAPE + 6, t, COR_TEXTO);
    jan_mostrar(jan);
}

static void cronometro(void)
{
    int s = segundos();
    if (s == ultimo_segundo) return;
    ultimo_segundo = s;
    desenhar_cabecalho();
    jan_mostrar_area(jan, 0, CAB_Y, larg, CAB_A);
}

/* ------------------------------------------------------------ registro (para os testes) */

static void registrar_estado(const char *acao)
{
    static const char *const nomes[4] = { "espera", "jogando", "perdeu", "ganhou" };
    registrar("cacabananas: %s -> %s abertas=%d bandeiras=%d bombas=%d", acao, nomes[campo.estado],
              campo.abertas, campo.bandeiras, campo.bombas);
}

static void registrar_bombas(void)
{
    for (int y = 0; y < campo.linhas; y++) {
        char l[CAMPO_MAX_COLS + 1];
        for (int x = 0; x < campo.cols; x++) l[x] = (CAMPO_CASA(&campo, x, y) & C_BOMBA) ? '*' : '.';
        l[campo.cols] = 0;
        registrar("cacabananas: b%d %s", y, l);
    }
}

/* ------------------------------------------------------------ partidas e níveis */

static void nova_partida(void)
{
    int c = 9, l = 9, b = 10;
    if (nivel == 2) { c = 16; l = 16; b = 40; }
    else if (nivel == 3) { c = 30; l = 16; b = 99; }
    else if (nivel == 4) { c = p_cols; l = p_linhas; b = p_bombas; }
    campo_novo(&campo, c, l, b, semente);
    aviso[0] = 0;
    ultimo_segundo = -1;
    cur_x = cur_y = -1;
}

static void abrir_janela(void)
{
    larg = campo.cols * CASA + 2 * MARGEM;
    if (larg < LARG_MIN) larg = LARG_MIN;
    alt = TAB_Y + campo.linhas * CASA + RODAPE;
    x0 = (larg - campo.cols * CASA) / 2;
    if (jan) jan_fechar(jan);
    jan = jan_abrir("Caça-Bananas", larg, alt);
    if (!jan) { sair_logo = true; return; }
    registrar("cacabananas: tabuleiro x0=%d y0=%d casa=%d cols=%d linhas=%d", x0, TAB_Y, CASA, campo.cols, campo.linhas);
}

static void trocar_nivel(int n)
{
    if (n == 4) {
        char r[40];
        formatar_texto(r, sizeof r, "%d,%d,%d", p_cols, p_linhas, p_bombas);
        if (!pedir_texto("Personalizado", "Colunas, linhas e bombas (ex.: 20,12,40):", r, sizeof r)) { redesenhar(); return; }
        int v[3] = { p_cols, p_linhas, p_bombas }, k = 0;
        for (const char *p = r; *p && k < 3;) {
            while (*p == ' ') p++;
            v[k++] = texto_para_int(p);
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
        }
        p_cols = v[0]; p_linhas = v[1]; p_bombas = v[2];
    }
    nivel = n;
    nova_partida();
    p_cols = campo.cols; p_linhas = campo.linhas; p_bombas = campo.bombas;   /* já corrigidos por campo_novo */
    abrir_janela();
    if (sair_logo) return;
    redesenhar();
    registrar_estado("nivel");
}

static void partida_nova_sorteada(void)
{
    uint32_t b = 0;
    bytes_aleatorios(&b, sizeof b);
    semente = (b ^ tempo_ms()) | 1;
    nova_partida();
    redesenhar();
    registrar_estado("nova");
}

/* Depois de cada jogada: começou? acabou? */
static void conferir(int estado_antes)
{
    if (estado_antes == CAMPO_ESPERA && campo.estado != CAMPO_ESPERA) { t_inicio = tempo_ms(); registrar_bombas(); }
    if (estado_antes != CAMPO_JOGANDO || campo.estado == CAMPO_JOGANDO) return;
    t_fim = tempo_ms();
    if (campo.estado == CAMPO_PERDEU) {
        copia_texto(aviso, "Bum! Era uma banana-bomba. F2: de novo.", sizeof aviso);
        static const nota_t bum[] = { { 220, 90 }, { 150, 220 } };
        tocar(bum, 2, BIP_SUBSTITUIR);
        return;
    }
    int s = segundos(), k = nivel - 1;
    if (s < 1) s = 1;
    formatar_texto(aviso, sizeof aviso, "Você ganhou em %d s!", s);
    if (k < 3 && (recorde[k] == 0 || s < recorde[k])) {
        recorde[k] = s;
        char v[16];
        formatar_texto(v, sizeof v, "%d", s);
        config_gravar(chaves_recorde[k], v);
        formatar_texto(aviso, sizeof aviso, "Novo recorde no %s: %d s!", nomes_nivel[k], s);
        registrar("cacabananas: recorde %s %d", nomes_nivel[k], s);
    }
    static const nota_t viva[] = { { 784, 90 }, { 988, 90 }, { 1319, 180 } };
    tocar(viva, 3, BIP_SUBSTITUIR);
}

/* ------------------------------------------------------------ entrada */

static bool casa_em(int mx, int my, int *x, int *y)
{
    if (mx < x0 || my < TAB_Y) return false;
    *x = (mx - x0) / CASA;
    *y = (my - TAB_Y) / CASA;
    return *x < campo.cols && *y < campo.linhas;
}

static void tratar_clique(const evento_t *e)
{
    if (e->y < FAIXA) {
        for (int k = 0; k < 4; k++) {
            int x, l;
            aba_pos(k, &x, &l);
            if (e->x >= x && e->x < x + l) { trocar_nivel(k + 1); return; }
        }
        return;
    }
    int fx, fy;
    rosto_pos(&fx, &fy);
    if (e->x >= fx && e->x < fx + 44 && e->y >= fy && e->y < fy + 44) { partida_nova_sorteada(); return; }
    int x, y;
    if (!casa_em(e->x, e->y, &x, &y)) return;
    cur_x = x; cur_y = y;
    int antes = campo.estado;
    if (e->botoes & 2) campo_marcar(&campo, x, y, true);
    else {
        apertando = true;
        if (CAMPO_CASA(&campo, x, y) & C_ABERTA) campo_acorde(&campo, x, y);
        else campo_abrir(&campo, x, y);
    }
    conferir(antes);
    redesenhar();
    char acao[40];
    formatar_texto(acao, sizeof acao, "clique %d,%d", x, y);
    registrar_estado(acao);
}

static void tratar_tecla(unsigned t)
{
    if (t == TECLA_F2) { partida_nova_sorteada(); return; }
    if (t >= '1' && t <= '4') { trocar_nivel((int)(t - '0')); return; }
    if (cur_x < 0) { cur_x = campo.cols / 2; cur_y = campo.linhas / 2; }
    int antes = campo.estado;
    if (t == TECLA_ESQUERDA && cur_x > 0) cur_x--;
    else if (t == TECLA_DIREITA && cur_x < campo.cols - 1) cur_x++;
    else if (t == TECLA_CIMA && cur_y > 0) cur_y--;
    else if (t == TECLA_BAIXO && cur_y < campo.linhas - 1) cur_y++;
    else if (t == ' ' || t == '\n') {
        if (CAMPO_CASA(&campo, cur_x, cur_y) & C_ABERTA) campo_acorde(&campo, cur_x, cur_y);
        else campo_abrir(&campo, cur_x, cur_y);
    } else if (t == 'b' || t == 'B') campo_marcar(&campo, cur_x, cur_y, true);
    else return;
    conferir(antes);
    redesenhar();
    if (t == ' ' || t == '\n' || t == 'b' || t == 'B') {
        char acao[40];
        formatar_texto(acao, sizeof acao, "tecla %d,%d", cur_x, cur_y);
        registrar_estado(acao);
    }
}

int principal(int argc, char **argv)
{
    for (int k = 1; k < argc; k++) {
        if (compara_texto(argv[k], "--nivel") == 0 && k + 1 < argc) nivel = texto_para_int(argv[++k]);
        else if (compara_texto(argv[k], "--semente") == 0 && k + 1 < argc) semente = (uint32_t)texto_para_int(argv[++k]);
        else { imprimir("uso: cacabananas [--nivel 1|2|3] [--semente N]\n"); return 2; }
    }
    if (nivel < 1 || nivel > 3) nivel = 1;
    if (semente == 0) {
        uint32_t b = 0;
        bytes_aleatorios(&b, sizeof b);
        semente = (b ^ tempo_ms()) | 1;
    }
    ler_recordes();
    nova_partida();
    abrir_janela();
    if (sair_logo) return 1;
    redesenhar();
    registrar("cacabananas: pronto (nivel %s)", nomes_nivel[nivel - 1]);

    evento_t e;
    while (!sair_logo) {
        /* Parado, espera o próximo evento; jogando, olha de 50 em 50 ms
         * para o cronômetro andar. */
        bool jogando = campo.estado == CAMPO_JOGANDO;
        if (proximo_evento(jan->id, &e, !jogando) != 1) {
            if (jogando) { cronometro(); dormir(50); }
            continue;
        }
        switch (e.tipo) {
        case EV_FECHAR: sair_logo = true; break;
        case EV_CLIQUE: tratar_clique(&e); break;
        case EV_SOLTOU: if (apertando) { apertando = false; desenhar_cabecalho(); jan_mostrar_area(jan, 0, CAB_Y, larg, CAB_A); } break;
        case EV_TECLA: tratar_tecla(e.tecla); break;
        default: break;
        }
    }
    registrar("cacabananas: fechado");
    jan_fechar(jan);
    return 0;
}
