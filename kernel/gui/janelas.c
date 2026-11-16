/* janelas.c - o gerenciador de janelas (H21). Ver D021.
 *
 * O kernel guarda a lista de janelas em ordem de empilhamento (a última é a
 * de cima e tem o foco), desenha as molduras e entrega os eventos. O
 * conteúdo de cada janela é desenhado pelo próprio programa, no anel 3,
 * em páginas que o kernel compartilha com ele.
 *
 * Redesenhar = recompor só o retângulo que mudou: fundo pronto da área de
 * trabalho, janelas de baixo para cima (moldura + conteúdo), barra superior
 * por último. Tudo recortado no retângulo, num buffer de trás (vídeo).
 *
 * Uma tarefa do kernel acorda a cada tique, consome as filas do mouse e do
 * teclado, arrasta, foca e fecha janelas, e entrega os eventos. Ela e as
 * chamadas de sistema mexem nas mesmas listas; por isso a tarefa trabalha
 * com as interrupções desligadas (as chamadas já rodam assim). */
#include "gui.h"
#include "hda.h"
#include "abi.h"
#include "console.h"
#include "memoria.h"
#include "mouse.h"
#include "paginacao.h"
#include "processo.h"
#include "relogio.h"
#include "sistema.h"
#include "teclado.h"
#include "texto.h"
#include "video.h"
#include "disco.h"
#include "vidro.h"
#include "suave.h"
#include "configuracoes.h"

#define MAX_JANELAS     32
#define FILA_EVENTOS    64
#define BARRA_TITULO    24
#define BORDA           2
#define JANELAS_USUARIO 0xA0000000u   /* onde os pixels aparecem no espaço do programa */
#define ESPACO_JANELA   0x00400000u   /* 4 MB por janela */
#define DUPLO_CLIQUE    45            /* tiques (450 ms) */

typedef struct {
    bool       usada;
    int        id;
    int        pid;
    int        x, y;          /* canto da moldura */
    int        larg, alt;     /* área de conteúdo */
    int        vl, va;        /* H96: tamanho com que o programa desenha em px agora */
    int        pend_l, pend_a;    /* tamanho do último EV_REDIMENSIONAR já lido (0 = nenhum) */
    char       titulo[48];
    uint32_t  *px;            /* pixels (físico = virtual no kernel) */
    uint32_t   paginas;
    uint32_t   virt;          /* endereço dos pixels no espaço do programa */
    evento_t   fila[FILA_EVENTOS];
    uint32_t   ini, fim;
    bool       minimizada;    /* H75: escondida, só o ícone na doca */
    bool       escondida;     /* durante a animação de abrir ou restaurar (H76) */
    bool       redimensionavel;   /* H78: o programa aceita mudar de tamanho */
    int        lmin, amin;        /* menor tamanho aceito */
    uint32_t   cap_px;            /* quantos pixels cabem na memória da janela */
    bool       maximizada;
    int        antes[4];          /* x, y, largura, altura antes de maximizar */
    bool       encaixada;         /* H94: presa numa metade ou num quarto da tela */
} janela_t;

static janela_t janelas[MAX_JANELAS];
static int ordem[MAX_JANELAS], qtd;   /* índices em janelas[], de baixo para cima */
static int proximo_id = 1;
static bool ativa;
static superficie_t tela;
static int suj_x0, suj_y0, suj_x1, suj_y1;   /* retângulo a recompor */

/* Redimensionar (H78): qual janela, por onde (1 direita, 2 embaixo, 3 canto)
 * e o contorno que segue o mouse até soltar. */
static int redimensionando = -1, redim_modo, redim_l, redim_a;

/* Estado do mouse no gerenciador. */
static int encaixe_zona;                      /* H94: 0 nenhuma, 1 tela toda, 2 esq., 3 dir., 4-7 quartos */
static int arr_x0, arr_y0;                     /* onde a janela estava ao ser pega */
static int arrastando = -1, arr_dx, arr_dy;   /* janela sendo arrastada e onde foi pega */
static int captura = -1;                       /* janela que recebeu o último clique */
static uint32_t ult_clique_tique;
static int ult_clique_x, ult_clique_y, ult_clique_alvo = -999;

bool gui_ativa(void) { return ativa; }

/* ------------------------------------------------------------ notificação (H34) */
#define AVISO_L 420
#define AVISO_A 62
#define AVISO_COLUNAS 49   /* letras por linha dentro da caixa */
static char aviso_texto[96];
static uint32_t aviso_ate;   /* tique em que some; 0 = sem aviso */

static void aviso_retangulo(int *x, int *y) { *x = (int)video_largura() - AVISO_L - 12; *y = BARRA_SUPERIOR + 10; }

void gui_aviso(const char *texto)
{
    misturador_som_sistema(SOM_AVISO);
    txt_copia_n(aviso_texto, texto, sizeof aviso_texto);
    aviso_ate = relogio_tiques() + 5 * TIQUES_POR_SEGUNDO;
    int x, y;
    aviso_retangulo(&x, &y);
    gui_redesenhar(x - 2, y - 2, AVISO_L + 4, AVISO_A + 4);
    klog("[GUI] notificacao: %s\n", texto);
}

static void desenhar_aviso(superficie_t *t)
{
    if (!aviso_ate) return;
    int x, y;
    aviso_retangulo(&x, &y);
    sup_retangulo(t, x, y, AVISO_L, AVISO_A, 0x18202A);
    sup_moldura(t, x, y, AVISO_L, AVISO_A, 0xFFDF4A);
    sup_retangulo(t, x, y, 6, AVISO_A, 0xFFDF4A);
    sup_texto(t, x + 16, y + 6, "SaguiOS", 0xFFDF4A);
    /* Até duas linhas, quebrando no último espaço que couber. */
    const char *p = aviso_texto;
    for (int l = 0; l < 2 && *p; l++) {
        int n = (int)txt_tamanho(p);
        if (n > AVISO_COLUNAS) {
            n = AVISO_COLUNAS;
            while (n > 0 && p[n] != ' ') n--;
            if (n == 0) n = AVISO_COLUNAS;
        }
        char linha[AVISO_COLUNAS + 1];
        txt_copia_n(linha, p, (size_t)n + 1);
        sup_texto(t, x + 16, y + 24 + l * 17, linha, 0xFFFFFF);
        p += n;
        while (*p == ' ') p++;
    }
}

static int moldura_l(janela_t *j) { return j->larg + 2 * BORDA; }
static int moldura_a(janela_t *j) { return j->alt + BARRA_TITULO + BORDA; }

void gui_redesenhar(int x, int y, int l, int a)
{
    if (l <= 0 || a <= 0) return;
    if (suj_x0 >= suj_x1) { suj_x0 = x; suj_y0 = y; suj_x1 = x + l; suj_y1 = y + a; return; }
    if (x < suj_x0) suj_x0 = x;
    if (y < suj_y0) suj_y0 = y;
    if (x + l > suj_x1) suj_x1 = x + l;
    if (y + a > suj_y1) suj_y1 = y + a;
}

/* Inclui a sombra em volta da moldura (sem isso, arrastar deixava rastros). */
static void redesenhar_janela(janela_t *j)
{
    int m = VIDRO_SOMBRA + 1;
    gui_redesenhar(j->x - m, j->y - m, moldura_l(j) + 2 * m, moldura_a(j) + 2 * m + 4);
}

/* A janela de cima que não está minimizada tem o foco. */
static janela_t *focada(void)
{
    for (int k = qtd - 1; k >= 0; k--)
        if (!janelas[ordem[k]].minimizada) return &janelas[ordem[k]];
    return NULL;
}

/* ------------------------------------------------------------ tema (H76) e efeitos (H77) */

typedef struct {
    uint32_t titulo_foco, titulo, texto_foco, texto;   /* barras de título */
    int      k_titulo;                                  /* quanto da cor vai no vidro (de 256) */
    uint32_t topo, topo_nome, topo_frase, topo_relogio; /* barra de cima */
    int      k_topo;
    uint32_t doca, doca_destaque, doca_ponto, doca_texto;
    int      k_doca;
    bool     claro;
    /* H81: o visual do Robson (#1) como opção de tema. */
    uint32_t destaque;      /* item sob o mouse nos menus */
    uint32_t filete;        /* linha embaixo da barra de título com foco (0 = sem) */
    bool     botoes_cores;  /* botões redondos: vermelho, amarelo e verde */
    bool     titulo_forte;  /* título em negrito com sombra */
} tema_t;

static const tema_t tema_escuro = {
    0x0B7A3E, 0x3A4450, 0xFFFFFF, 0xD0D4D8, 190,
    0x101820, 0xFFDF4A, 0xA8B4C2, 0xFFFFFF, 175,
    0x101820, 0xFFFFFF, 0xFFDF4A, 0xFFFFFF, 160, false,
    0x0B7A3E, 0, false, false,
};
static const tema_t tema_claro = {
    0x2E9E5B, 0xC4CCD4, 0xFFFFFF, 0x2A323A, 205,
    0xF2F4F6, 0x0B6A36, 0x4A5560, 0x1E2328, 200,
    0xF2F4F6, 0x1E2328, 0x0B7A3E, 0x1E2328, 185, true,
    0x0B7A3E, 0, false, false,
};
/* "Mata": as cores do pull request #1 (Robson de Lima Barbosa): barra verde
 * quase opaca, filete amarelo, botões redondos coloridos, título forte. */
static const tema_t tema_mata = {
    0x149A50, 0x5A6470, 0xFFFFFF, 0xE0E4E8, 238,
    0x14201A, 0xFFDF4A, 0x9AA8B8, 0xFFFFFF, 215,
    0x0E2A1A, 0xFFFFFF, 0xE8BE30, 0xFFFFFF, 200, false,
    0x149A50, 0xE8BE30, true, true,
};
/* "Azul": as cores do pull request #7 (Gabriel), em azul-marinho. */
static const tema_t tema_azul = {
    0x1E3A8A, 0x1E293B, 0xFFFFFF, 0xCBD5E1, 200,
    0x0F172A, 0x60A5FA, 0x94A3B8, 0xFFFFFF, 185,
    0x0F172A, 0x3B82F6, 0x60A5FA, 0xFFFFFF, 170, false,
    0x2563EB, 0x60A5FA, true, false,
};
static const tema_t *tema = &tema_escuro;
static bool doca_ligada = true;
static void desenhar_tarefas(void);
static bool efeitos_automaticos = true, efeitos_lento;

static void ler_tema(void)
{
    const char *t = config_obter("aparencia.tema");
    tema = !t ? &tema_escuro : txt_compara(t, "claro") == 0 ? &tema_claro : txt_compara(t, "mata") == 0 ? &tema_mata
         : txt_compara(t, "azul") == 0 ? &tema_azul : &tema_escuro;
    /* Barra de tarefas no lugar da doca (H81, ideia do Robson). */
    const char *b = config_obter("aparencia.barra");
    bool doca = !b || txt_compara(b, "tarefas") != 0;
    if (doca != doca_ligada) { doca_ligada = doca; gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura()); }
    const char *e = config_obter("aparencia.efeitos");
    efeitos_automaticos = !e || txt_compara(e, "auto") == 0;
    if (efeitos_automaticos) vidro_efeitos = !efeitos_lento;
    else vidro_efeitos = txt_compara(e, "leves") != 0;
}

/* ------------------------------------------------------------ doca (H75) */

typedef struct { const char *nome, *programa, *processo; const uint32_t *px; } fixo_t;   /* px: ícone 40x40 (H81) */
static const fixo_t fixos[] = {
    { "Arquivos", "/bin/arquivos", "arquivos", arte_px_arquivos_40 },
    { "Terminal", "/bin/terminal", "terminal", arte_px_terminal_40 },
    { "Cipó", "/bin/navegador", "navegador", arte_px_cipo_40 },
    { "Editor", "/bin/editor", "editor", arte_px_editor_40 },
    { "Configurações", "/bin/configuracoes", "configuracoes", arte_px_configuracoes_40 },
};
#define QTD_FIXOS (int)(sizeof fixos / sizeof fixos[0])
#define DOCA_CELULA 52
#define DOCA_A      56
#define DOCA_RAIO   18     /* H88b: cantos bem redondos */
#define MAX_DOCA    14

typedef struct {
    const char       *nome;
    const uint32_t   *px;
    const char       *programa;   /* NULL: só uma janela aberta */
    int               janela;     /* índice em janelas[] de uma janela desse programa, ou -1 */
} item_doca_t;
static item_doca_t doca[MAX_DOCA];
static int qtd_doca, doca_x, doca_y, doca_l, doca_sob = -1;

static const char *nome_processo(janela_t *j)
{
    processo_t *p = processo_por_pid(j->pid);
    return p ? p->nome : "";
}

static const uint32_t *px_de(const char *processo)
{
    static const struct { const char *p; const uint32_t *px; } t[] = {
        { "calculadora", arte_px_calculadora_40 }, { "banana", arte_px_banana_40 }, { "sagui3d", arte_px_sagui3d_40 },
        { "sobre-o-sistema", arte_px_sobre_40 }, { "energia", arte_px_energia_40 }, { "cipo", arte_px_cipo_40 },
        { "sagui-monitor", arte_px_monitor_40 }, { "top", arte_px_monitor_40 },                     /* H88 */
        { "visualizador", arte_px_visualizador_40 }, { "musica", arte_px_musica_40 }, { "tocador", arte_px_tocador_40 },
        { "saguicode-janela", arte_px_saguicode_40 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) if (txt_compara(t[i].p, processo) == 0) return t[i].px;
    return arte_px_instalado_40;
}

/* Monta a lista: os fixos e, depois, os programas abertos que não são fixos. */
static void montar_doca(void)
{
    qtd_doca = 0;
    for (int f = 0; f < QTD_FIXOS; f++) {
        item_doca_t *it = &doca[qtd_doca++];
        it->nome = fixos[f].nome; it->px = fixos[f].px; it->programa = fixos[f].programa; it->janela = -1;
        for (int k = qtd - 1; k >= 0; k--)
            if (txt_compara(nome_processo(&janelas[ordem[k]]), fixos[f].processo) == 0) { it->janela = ordem[k]; break; }
    }
    for (int k = 0; k < qtd && qtd_doca < MAX_DOCA; k++) {
        janela_t *j = &janelas[ordem[k]];
        const char *np = nome_processo(j);
        bool fixo = false, repetido = false;
        for (int f = 0; f < QTD_FIXOS; f++) if (txt_compara(np, fixos[f].processo) == 0) fixo = true;
        for (int d = QTD_FIXOS; d < qtd_doca; d++) if (doca[d].janela >= 0 && janelas[doca[d].janela].pid == j->pid) repetido = true;
        if (fixo || repetido) continue;
        item_doca_t *it = &doca[qtd_doca++];
        it->nome = j->titulo; it->px = px_de(np); it->programa = NULL; it->janela = ordem[k];
    }
    int antes_x = doca_x, antes_l = doca_l;
    doca_l = qtd_doca * DOCA_CELULA + 16;
    doca_x = ((int)video_largura() - doca_l) / 2;
    doca_y = (int)video_altura() - DOCA_A - 6;
    if (antes_l != doca_l || antes_x != doca_x) {
        /* Mudou de tamanho: redesenha onde ela estava e onde está. */
        int m = VIDRO_SOMBRA + VIDRO_RAIO;
        int x0 = antes_x < doca_x ? antes_x : doca_x, x1 = antes_x + antes_l > doca_x + doca_l ? antes_x + antes_l : doca_x + doca_l;
        gui_redesenhar(x0 - m, doca_y - 30, x1 - x0 + 2 * m, DOCA_A + 36 + m);
    }
}

static void redesenhar_doca(void)
{
    /* Com a barra de tarefas no lugar da doca, quem muda é a barra de cima. */
    if (!doca_ligada) { gui_redesenhar(0, 0, (int)video_largura(), BARRA_SUPERIOR); return; }
    int m = VIDRO_SOMBRA + VIDRO_RAIO;
    gui_redesenhar(doca_x - m - DOCA_CELULA, doca_y - 30, doca_l + 2 * m + 2 * DOCA_CELULA, DOCA_A + 36);
}

static int doca_item_em(int x, int y)
{
    if (!doca_ligada || y < doca_y || y >= doca_y + DOCA_A || x < doca_x + 8) return -1;
    int i = (x - doca_x - 8) / DOCA_CELULA;
    return i < qtd_doca ? i : -1;
}

static void desenhar_doca(void)
{
    if (!doca_ligada) return;
    vidro_sombra_r(&tela, doca_x, doca_y, doca_l, DOCA_A, DOCA_RAIO);
    vidro_pintar_r(&tela, doca_x, doca_y, doca_l, DOCA_A, tema->doca, tema->k_doca, CANTOS_EM_CIMA | CANTOS_EMBAIXO, DOCA_RAIO);
    janela_t *f = focada();
    for (int i = 0; i < qtd_doca; i++) {
        int cx = doca_x + 8 + i * DOCA_CELULA;
        bool aberto = doca[i].janela >= 0;
        if (aberto && f && &janelas[doca[i].janela] == f)
            vidro_pintar_r(&tela, cx + 2, doca_y + 4, DOCA_CELULA - 4, DOCA_A - 8, tema->doca_destaque, 50, CANTOS_EM_CIMA | CANTOS_EMBAIXO, 12);
        sup_icone(&tela, cx + (DOCA_CELULA - ICONE_PEQUENO) / 2, doca_y + 4, doca[i].px, ICONE_PEQUENO);
        if (aberto) sup_retangulo(&tela, cx + DOCA_CELULA / 2 - 3, doca_y + DOCA_A - 10, 6, 3, tema->doca_ponto);
        if (i == QTD_FIXOS - 1 && qtd_doca > QTD_FIXOS)
            sup_retangulo(&tela, cx + DOCA_CELULA + 1, doca_y + 12, 1, DOCA_A - 24, tema->doca_texto);
    }
    /* O nome do item sob o mouse, num balão acima da doca. */
    if (doca_sob >= 0 && doca_sob < qtd_doca) {
        char nome[40];
        txt_copia_n(nome, doca[doca_sob].nome, sizeof nome);
        int l = texto_largura(nome) + 16, x = doca_x + 8 + doca_sob * DOCA_CELULA + DOCA_CELULA / 2 - l / 2;
        vidro_pintar_r(&tela, x, doca_y - 28, l, 22, tema->doca, tema->k_doca + 40 > 255 ? 255 : tema->k_doca + 40,
                       CANTOS_EM_CIMA | CANTOS_EMBAIXO, 11);
        sup_texto(&tela, x + 8, doca_y - 25, nome, tema->doca_texto);
    }
}

/* ------------------------------------------------------------ menu Iniciar (H75, H89) */

/* H89: o menu do sagui virou um menu Iniciar com todos os programas: uma
 * caixa de busca no alto (o teclado vai para ela enquanto o menu está
 * aberto), as categorias logo abaixo e a grade de ícones. Embaixo, Sobre e
 * Desligar. Os programas instalados no pendrive entram em "Instalados". */
enum { CAT_TODOS, CAT_INTERNET, CAT_ESCRITORIO, CAT_MIDIA, CAT_JOGOS, CAT_SISTEMA, CAT_DESENVOLVIMENTO,
       CAT_INSTALADOS, QTD_CATS };
static const char *const cat_nomes[QTD_CATS] = {
    "Todos", "Internet", "Escritório", "Mídia", "Jogos", "Sistema", "Programação", "Instalados",
};
typedef struct {
    const char *nome, *programa, *extra;   /* extra: argumento (o Sagui Code roda no Terminal) */
    int cat;
    const uint32_t *px;                    /* 40 x 40 */
    const char *palavras;                  /* outras palavras que a busca acha */
} programa_menu_t;
static const programa_menu_t catalogo[] = {
    { "Cipó", "/bin/navegador", NULL, CAT_INTERNET, arte_px_cipo_40, "navegador internet web site pagina google" },
    { "Arquivos", "/bin/arquivos", NULL, CAT_ESCRITORIO, arte_px_arquivos_40, "pastas documentos explorador lixeira" },
    { "Editor", "/bin/editor", NULL, CAT_ESCRITORIO, arte_px_editor_40, "texto bloco de notas escrever" },
    { "Calculadora", "/bin/calculadora", NULL, CAT_ESCRITORIO, arte_px_calculadora_40, "contas numeros" },
    { "Música", "/bin/musica", NULL, CAT_MIDIA, arte_px_musica_40, "tocador audio som wav m4a aac" },
    { "Tocador", "/bin/tocador", NULL, CAT_MIDIA, arte_px_tocador_40, "musica mp3 wav audio som" },
    { "Visualizador", "/bin/visualizador", NULL, CAT_MIDIA, arte_px_visualizador_40, "imagens fotos png jpeg gif" },
    { "Banana", "/bin/banana", NULL, CAT_JOGOS, arte_px_banana_40, "jogo" },
    { "Sagui 3D", "/bin/sagui3d", NULL, CAT_JOGOS, arte_px_sagui3d_40, "jogo 3d" },
    { "Chuva de letras", "/bin/chuva", NULL, CAT_JOGOS, arte_px_chuva_40, "matrix protetor de tela" },
    { "Triângulos 3D", "/bin/triangulos", NULL, CAT_JOGOS, arte_px_triangulos_40, "fps desempenho demonstracao" },
    { "Terminal", "/bin/terminal", NULL, CAT_SISTEMA, arte_px_terminal_40, "console shell comandos prompt sagsh" },
    { "Configurações", "/bin/configuracoes", NULL, CAT_SISTEMA, arte_px_configuracoes_40, "ajustes preferencias tema rede som aparencia" },
    { "Sagui Monitor", "/bin/sagui-monitor", NULL, CAT_SISTEMA, arte_px_monitor_40, "processos tarefas top cpu memoria" },
    { "Sobre o SaguiOS", "/bin/sobre-o-sistema", NULL, CAT_SISTEMA, arte_px_sobre_40, "versao informacoes sistema" },
    { "Desligar", "/bin/energia", NULL, CAT_SISTEMA, arte_px_energia_40, "energia reiniciar sair" },
    { "Sagui Code", "/bin/saguicode-janela", NULL, CAT_DESENVOLVIMENTO, arte_px_saguicode_40, "ia agente programar codigo" },
    { "Sagui Code (terminal)", "/bin/terminal", "/bin/saguicode", CAT_DESENVOLVIMENTO, arte_px_codigo_40, "ia agente programar codigo terminal" },
    { "Componentes", "/bin/componentes", NULL, CAT_DESENVOLVIMENTO, arte_px_componentes_40, "demonstracao botoes interface libsagui" },
    { "Instalar o SaguiOS", "/bin/instalar", NULL, CAT_SISTEMA, arte_px_instalado_40, "instalar disco hd gravar instalacao particao" },   /* H97 */
};
#define QTD_CATALOGO (int)(sizeof catalogo / sizeof catalogo[0])
#define MENU_X 4
#define MENU_Y (BARRA_SUPERIOR + 4)
#define MENU_L 620
#define MENU_ALT 544
#define MENU_ITEM 26          /* menu de contexto */
#define MENU_COLUNAS 5
#define MENU_LINHAS 4
#define MENU_CEL_L 108
#define MENU_CEL_A 92
#define MENU_GRADE_X (MENU_X + (MENU_L - MENU_COLUNAS * MENU_CEL_L) / 2)
#define MENU_GRADE_Y (MENU_Y + 106)
#define MENU_BUSCA_Y (MENU_Y + 16)
#define MENU_CHIPS_Y (MENU_Y + 62)
#define MENU_RODAPE_Y (MENU_Y + MENU_ALT - 50)
/* Alvos do mouse: 0..n-1 um programa da grade; os outros abaixo. */
#define ALVO_CAT    1000      /* + categoria */
#define ALVO_SOBRE  2000
#define ALVO_DESLIGAR 2001
#define ALVO_BUSCA  2002

static bool menu_aberto;
static int menu_sob = -1, menu_sel, menu_cat;
static char menu_busca[40];
/* A lista filtrada: índices do catálogo, ou -(1 + i) para o ícone i da área
 * de trabalho (programa instalado). */
static int menu_res[MENU_COLUNAS * MENU_LINHAS], menu_qtd;

static void abrir_programa(const char *caminho);
static void desenhar_selecao(void);   /* H90: laço da área de trabalho */

static void redesenhar_menu(void)
{
    gui_redesenhar(0, 0, MENU_X + MENU_L + VIDRO_SOMBRA + VIDRO_RAIO, MENU_Y + MENU_ALT + 4 + VIDRO_SOMBRA + VIDRO_RAIO);
}

/* Letra para a busca: minúscula e sem acento. O kernel guarda texto em
 * Latin-1 (-fexec-charset), e o teclado também entrega Latin-1. */
static int letra_simples(const char **p)
{
    int c = *(const unsigned char *)(*p)++;
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0) {
        c |= 0x20;                               /* À-Þ viram à-þ */
        return (c >= 0xE0 && c <= 0xE5) ? 'a' : c == 0xE7 ? 'c' : (c >= 0xE8 && c <= 0xEB) ? 'e'
             : (c >= 0xEC && c <= 0xEF) ? 'i' : (c >= 0xF2 && c <= 0xF6) ? 'o' : (c >= 0xF9 && c <= 0xFC) ? 'u' : c;
    }
    return c;
}

/* O texto contém a busca (começando em qualquer palavra ou letra)? */
static bool contem(const char *texto, const char *busca)
{
    if (!busca[0]) return true;
    for (const char *t = texto; *t; ) {
        const char *a = t, *b = busca;
        for (;;) {
            if (!*b) return true;
            if (!*a) break;
            if (letra_simples(&a) != letra_simples(&b)) break;
        }
        letra_simples(&t);                       /* avança um caractere */
    }
    return false;
}

static void menu_filtrar(void)
{
    /* H97: ligado pelo disco instalado (o instalador grava
     * sistema.instalado = sim nele), o instalador sai do menu. */
    const char *inst = config_obter("sistema.instalado");
    bool ja_instalado = inst && txt_compara(inst, "sim") == 0;
    menu_qtd = 0;
    bool buscando = menu_busca[0] != 0;
    for (int i = 0; i < QTD_CATALOGO && menu_qtd < MENU_COLUNAS * MENU_LINHAS; i++) {
        const programa_menu_t *p = &catalogo[i];
        if (!buscando && menu_cat != CAT_TODOS && p->cat != menu_cat) continue;
        if (ja_instalado && txt_compara(p->programa, "/bin/instalar") == 0) continue;
        if (buscando && !contem(p->nome, menu_busca) && !contem(p->palavras, menu_busca)) continue;
        menu_res[menu_qtd++] = i;
    }
    for (int i = 0; i < area_qtd_icones() && menu_qtd < MENU_COLUNAS * MENU_LINHAS; i++) {
        if (area_icone_tipo(i) != 1) continue;
        if (!buscando && menu_cat != CAT_TODOS && menu_cat != CAT_INSTALADOS) continue;
        if (buscando && !contem(area_icone_nome(i), menu_busca)) continue;
        menu_res[menu_qtd++] = -(1 + i);
    }
    if (menu_sel >= menu_qtd) menu_sel = menu_qtd ? menu_qtd - 1 : 0;
}

static bool ha_instalados(void)
{
    for (int i = 0; i < area_qtd_icones(); i++) if (area_icone_tipo(i) == 1) return true;
    return false;
}

/* Folga dos chips: a sobra da fileira dividida entre eles, para a fileira
 * caber sempre no painel (com ou sem "Instalados"). */
#define CHIP_VAO 6
static int chip_folga(void)
{
    int n = 0, soma = 0;
    for (int c = 0; c < QTD_CATS; c++) {
        if (c == CAT_INSTALADOS && !ha_instalados()) continue;
        n++;
        soma += texto_largura(cat_nomes[c]);
    }
    int f = (MENU_L - 32 - soma - (n - 1) * CHIP_VAO) / n;
    return f > 22 ? 22 : f;
}
static int chip_largura(int c) { return texto_largura(cat_nomes[c]) + chip_folga(); }

/* Nome que cabe em l pixels: corta com "..". */
static void nome_cortado(char *saida, int tam, const char *nome, int l)
{
    txt_copia_n(saida, nome, tam);
    if (texto_largura(saida) <= l) return;
    int n = (int)txt_tamanho(saida);
    while (n > 0) {
        saida[--n] = 0;
        char t[48];
        snformatar(t, sizeof t, "%s..", saida);
        if (texto_largura(t) <= l) { txt_copia_n(saida, t, tam); return; }
    }
}

static void desenhar_menu(void)
{
    if (!menu_aberto) return;
    menu_filtrar();
    vidro_sombra_r(&tela, MENU_X, MENU_Y, MENU_L, MENU_ALT, 14);
    /* Texto pequeno em cima do vidro: quase opaco (o Eli achou transparente
     * demais no G470). H89: com a grade grande, o conteúdo das janelas de
     * trás atrapalhava a leitura; 250 de 256 deixa só um toque do fundo. */
    vidro_pintar_r(&tela, MENU_X, MENU_Y, MENU_L, MENU_ALT, tema->topo, 250, CANTOS_EM_CIMA | CANTOS_EMBAIXO, 14);
    suave_t sv = { tela.px, tela.larg, tela.cx0, tela.cy0, tela.cx1, tela.cy1 };
    suave_contorno(&sv, MENU_X, MENU_Y, MENU_L, MENU_ALT, 14, 0xFFFFFF, 40);
    uint32_t texto = tema->topo_relogio, fraco = cor_mistura(tema->topo, tema->topo_relogio, 1, 2);
    uint32_t caixa = tema->claro ? 0xFFFFFF : cor_mistura(tema->topo, 0xFFFFFF, 1, 9);

    /* Busca */
    int bx = MENU_X + 16, bl = MENU_L - 32;
    suave_ret(&sv, bx, MENU_BUSCA_Y, bl, 34, 10, caixa, caixa, 256);
    suave_contorno(&sv, bx, MENU_BUSCA_Y, bl, 34, 10, menu_busca[0] ? tema->destaque : fraco, 120);
    sup_icone(&tela, bx + 10, MENU_BUSCA_Y + 7, tema->claro ? arte_px_lupa_20 : arte_px_lupa_20, 20);
    if (menu_busca[0]) {
        sup_texto(&tela, bx + 40, MENU_BUSCA_Y + 9, menu_busca, texto);
        sup_retangulo(&tela, bx + 41 + texto_largura(menu_busca), MENU_BUSCA_Y + 8, 2, 18, tema->destaque);
    } else {
        sup_retangulo(&tela, bx + 40, MENU_BUSCA_Y + 8, 2, 18, tema->destaque);
        sup_texto(&tela, bx + 46, MENU_BUSCA_Y + 9, "Pesquisar programas", fraco);
    }

    /* Categorias (somem enquanto há busca: a busca olha todas) */
    if (!menu_busca[0]) {
        int x = MENU_X + 16;
        for (int c = 0; c < QTD_CATS; c++) {
            if (c == CAT_INSTALADOS && !ha_instalados()) continue;
            int l = chip_largura(c);
            bool ativo = c == menu_cat, sob = menu_sob == ALVO_CAT + c;
            uint32_t fundo = ativo ? tema->destaque : sob ? cor_mistura(caixa, tema->destaque, 1, 3) : caixa;
            suave_ret(&sv, x, MENU_CHIPS_Y, l, 26, 13, fundo, fundo, 256);
            sup_texto(&tela, x + (l - texto_largura(cat_nomes[c])) / 2, MENU_CHIPS_Y + 5, cat_nomes[c], ativo ? 0xFFFFFF : texto);
            x += l + CHIP_VAO;
        }
    } else {
        char t[72];
        snformatar(t, sizeof t, menu_qtd ? "Resultados para \"%s\"" : "Nada encontrado para \"%s\"", menu_busca);
        sup_texto(&tela, MENU_X + 20, MENU_CHIPS_Y + 5, t, fraco);
    }

    /* Grade */
    for (int k = 0; k < menu_qtd; k++) {
        int cx = MENU_GRADE_X + (k % MENU_COLUNAS) * MENU_CEL_L, cy = MENU_GRADE_Y + (k / MENU_COLUNAS) * MENU_CEL_A;
        bool sob = menu_sob == k, sel = menu_busca[0] && menu_sel == k;
        if (sob || sel)
            suave_ret(&sv, cx + 2, cy + 2, MENU_CEL_L - 4, MENU_CEL_A - 4, 12,
                      sob ? tema->destaque : cor_mistura(caixa, tema->destaque, 1, 2), sob ? tema->destaque : cor_mistura(caixa, tema->destaque, 1, 2), 256);
        int r = menu_res[k];
        const char *nome = r >= 0 ? catalogo[r].nome : area_icone_nome(-r - 1);
        const uint32_t *px = r >= 0 ? catalogo[r].px : arte_px_instalado_40;
        sup_icone(&tela, cx + (MENU_CEL_L - 40) / 2, cy + 10, px, 40);
        /* O nome em até duas linhas, quebrado num espaço. */
        char l1[48], l2[48];
        int lmax = MENU_CEL_L - 8;
        txt_copia_n(l1, nome, sizeof l1);
        l2[0] = 0;
        if (texto_largura(l1) > lmax) {
            int corte = -1;
            for (int q = 0; l1[q]; q++)
                if (l1[q] == ' ') {
                    l1[q] = 0;
                    if (texto_largura(l1) <= lmax) corte = q;
                    l1[q] = ' ';
                }
            if (corte > 0) {
                l1[corte] = 0;
                nome_cortado(l2, sizeof l2, l1 + corte + 1, lmax);
            } else nome_cortado(l1, sizeof l1, nome, lmax);
        }
        uint32_t ct = sob ? 0xFFFFFF : texto;
        sup_texto(&tela, cx + (MENU_CEL_L - texto_largura(l1)) / 2, cy + 56, l1, ct);
        if (l2[0]) sup_texto(&tela, cx + (MENU_CEL_L - texto_largura(l2)) / 2, cy + 72, l2, ct);
    }

    /* Rodapé: Sobre e Desligar */
    sup_retangulo(&tela, MENU_X + 16, MENU_RODAPE_Y - 6, MENU_L - 32, 1, cor_mistura(tema->topo, tema->topo_relogio, 1, 4));
    sup_icone(&tela, MENU_X + 22, MENU_RODAPE_Y + 9, arte_px_sobre_20, 20);
    sup_texto(&tela, MENU_X + 50, MENU_RODAPE_Y + 11, "SaguiOS", texto);
    static const char *const rot[2] = { "Sobre", "Desligar" };
    for (int b = 0; b < 2; b++) {
        int l = 112, x = MENU_X + MENU_L - 16 - (2 - b) * (l + 8) + 8, y = MENU_RODAPE_Y + 4;
        bool sob = menu_sob == ALVO_SOBRE + b;
        if (sob) suave_ret(&sv, x, y, l, 32, 10, tema->destaque, tema->destaque, 256);
        sup_icone(&tela, x + 12, y + 6, b ? arte_px_energia_20 : arte_px_sobre_20, 20);
        sup_texto(&tela, x + 40, y + 8, rot[b], sob ? 0xFFFFFF : texto);
    }
}

/* H93: o retângulo de um alvo do menu, para redesenhar só ele quando o
 * mouse passa (antes o menu inteiro, e a barra de cima junto, era refeito a
 * cada célula). */
static bool menu_alvo_ret(int alvo, int *x, int *y, int *l, int *a)
{
    if (alvo >= 0 && alvo < MENU_COLUNAS * MENU_LINHAS) {
        *x = MENU_GRADE_X + (alvo % MENU_COLUNAS) * MENU_CEL_L; *y = MENU_GRADE_Y + (alvo / MENU_COLUNAS) * MENU_CEL_A;
        *l = MENU_CEL_L; *a = MENU_CEL_A;
        return true;
    }
    if (alvo >= ALVO_CAT && alvo < ALVO_CAT + QTD_CATS) {
        int cx = MENU_X + 16;
        for (int c = 0; c < QTD_CATS; c++) {
            if (c == CAT_INSTALADOS && !ha_instalados()) continue;
            int cl = chip_largura(c);
            if (ALVO_CAT + c == alvo) { *x = cx; *y = MENU_CHIPS_Y; *l = cl; *a = 26; return true; }
            cx += cl + CHIP_VAO;
        }
        return false;
    }
    if (alvo == ALVO_SOBRE || alvo == ALVO_DESLIGAR) {
        int b = alvo - ALVO_SOBRE, bl = 112;
        *x = MENU_X + MENU_L - 16 - (2 - b) * (bl + 8) + 8; *y = MENU_RODAPE_Y + 4; *l = bl; *a = 32;
        return true;
    }
    return false;
}

static void redesenhar_alvo(int alvo)
{
    int x, y, l, a;
    if (menu_alvo_ret(alvo, &x, &y, &l, &a)) gui_redesenhar(x, y, l, a);
}

/* O corpo do menu (da busca para baixo), sem a barra de cima. */
static void redesenhar_corpo_menu(void)
{
    gui_redesenhar(MENU_X, MENU_BUSCA_Y, MENU_L, MENU_Y + MENU_ALT - MENU_BUSCA_Y);
}

static int menu_alvo_em(int x, int y)
{
    if (!menu_aberto || x < MENU_X || x >= MENU_X + MENU_L || y < MENU_Y || y >= MENU_Y + MENU_ALT) return -1;
    if (y >= MENU_BUSCA_Y && y < MENU_BUSCA_Y + 34) return ALVO_BUSCA;
    if (!menu_busca[0] && y >= MENU_CHIPS_Y && y < MENU_CHIPS_Y + 26) {
        int cx = MENU_X + 16;
        for (int c = 0; c < QTD_CATS; c++) {
            if (c == CAT_INSTALADOS && !ha_instalados()) continue;
            int l = chip_largura(c);
            if (x >= cx && x < cx + l) return ALVO_CAT + c;
            cx += l + CHIP_VAO;
        }
        return -1;
    }
    if (y >= MENU_GRADE_Y && y < MENU_GRADE_Y + MENU_LINHAS * MENU_CEL_A && x >= MENU_GRADE_X &&
        x < MENU_GRADE_X + MENU_COLUNAS * MENU_CEL_L) {
        int k = (y - MENU_GRADE_Y) / MENU_CEL_A * MENU_COLUNAS + (x - MENU_GRADE_X) / MENU_CEL_L;
        return k < menu_qtd ? k : -1;
    }
    if (y >= MENU_RODAPE_Y + 4 && y < MENU_RODAPE_Y + 36)
        for (int b = 0; b < 2; b++) {
            int l = 112, bx = MENU_X + MENU_L - 16 - (2 - b) * (l + 8) + 8;
            if (x >= bx && x < bx + l) return ALVO_SOBRE + b;
        }
    return -1;
}

static void menu_abrir(bool abrir)
{
    menu_aberto = abrir;
    menu_sob = -1;
    menu_sel = 0;
    menu_busca[0] = 0;
    if (abrir) { menu_filtrar(); klog("[GUI] menu aberto\n"); }
    redesenhar_menu();
}

/* Abre o k-ésimo resultado e fecha o menu. */
static void menu_lancar(int k)
{
    if (k < 0 || k >= menu_qtd) return;
    int r = menu_res[k];
    menu_abrir(false);
    if (r < 0) { klog("[GUI] menu: instalado %s\n", area_icone_nome(-r - 1)); area_icone_abrir(-r - 1); return; }
    const programa_menu_t *p = &catalogo[r];
    klog("[GUI] menu: %s\n", p->nome);
    if (p->extra) {
        char *argv[] = { "terminal", "-e", (char *)p->extra, NULL };
        int pid = processo_executar(p->programa, 3, argv, NULL);
        klog("[GUI] abrir %s -e %s (pid %d)\n", p->programa, p->extra, pid);
    } else abrir_programa(p->programa);
}

/* O teclado enquanto o menu está aberto: digitar busca, setas escolhem,
 * Enter abre, Esc fecha. */
static void menu_tecla(int t)
{
    int n = (int)txt_tamanho(menu_busca);
    if (t == TECLA_ESC) { menu_abrir(false); return; }
    if (t == '\n') { menu_lancar(menu_busca[0] ? menu_sel : 0); return; }
    if (t == '\b') {
        if (n > 0) menu_busca[n - 1] = 0;
    } else if (t == TECLA_DIREITA) menu_sel = menu_sel + 1 < menu_qtd ? menu_sel + 1 : menu_sel;
    else if (t == TECLA_ESQUERDA) menu_sel = menu_sel > 0 ? menu_sel - 1 : 0;
    else if (t == TECLA_BAIXO) menu_sel = menu_sel + MENU_COLUNAS < menu_qtd ? menu_sel + MENU_COLUNAS : menu_sel;
    else if (t == TECLA_CIMA) menu_sel = menu_sel >= MENU_COLUNAS ? menu_sel - MENU_COLUNAS : menu_sel;
    else if ((t >= 0x20 && t < 0x7F) || (t >= 0xA0 && t <= 0xFF)) {   /* Latin-1, como o kernel */
        if (n + 1 < (int)sizeof menu_busca) { menu_busca[n] = (char)t; menu_busca[n + 1] = 0; menu_sel = 0; }
    } else return;
    if (menu_busca[0] == 0) menu_sel = 0;
    klog("[GUI] menu: busca \"%s\" (%d resultados)\n", menu_busca, menu_qtd);
    redesenhar_corpo_menu();
}

/* Menus do botão direito (H81, menus de contexto do pull request #1 do
 * Robson): num ícone (Abrir, Renomear...), na área vazia, na barra de título
 * de uma janela e os menus próprios dos programas (SIS_MENU_JANELA, cuja
 * escolha volta ao programa em EV_MENU). Um menu só fica aberto por vez. */
enum { CTX_ICONE, CTX_AREA, CTX_JANELA, CTX_PROGRAMA };
enum { J_MINIMIZAR = 100, J_MAXIMIZAR, J_FECHAR, A_TERMINAL, A_ARQUIVOS, A_FUNDO, A_MOSTRAR_AREA };
#define CTX_MAX 10
static struct { char nome[32]; int acao; } ctx[CTX_MAX];
static int ctx_qtd, ctx_x, ctx_y, ctx_l = 190, ctx_tipo, ctx_alvo, ctx_sob = -1;
static bool ctx_aberto;

static void redesenhar_ctx(void)
{
    int m = VIDRO_SOMBRA + VIDRO_RAIO;
    gui_redesenhar(ctx_x - m, ctx_y - m, ctx_l + 2 * m, ctx_qtd * MENU_ITEM + 8 + 2 * m + 4);
}

static void ctx_novo(int tipo, int alvo) { ctx_qtd = 0; ctx_tipo = tipo; ctx_alvo = alvo; }

static void ctx_item(const char *nome, int acao)
{
    if (ctx_qtd == CTX_MAX) return;
    txt_copia_n(ctx[ctx_qtd].nome, nome, sizeof ctx[ctx_qtd].nome);
    ctx[ctx_qtd++].acao = acao;
}

static void ctx_mostrar(int x, int y)
{
    ctx_l = 150;
    for (int i = 0; i < ctx_qtd; i++) if (texto_largura(ctx[i].nome) + 32 > ctx_l) ctx_l = texto_largura(ctx[i].nome) + 32;
    int a = ctx_qtd * MENU_ITEM + 8, vl = (int)video_largura(), va = (int)video_altura();
    ctx_x = x + ctx_l > vl - 4 ? vl - 4 - ctx_l : x;
    ctx_y = y + a > va - 4 ? va - 4 - a : y;
    if (ctx_y < BARRA_SUPERIOR) ctx_y = BARRA_SUPERIOR;
    ctx_aberto = true;
    ctx_sob = -1;
    redesenhar_ctx();
    klog("[GUI] menu aberto (tipo %d, %d itens) em (%d,%d)\n", ctx_tipo, ctx_qtd, ctx_x, ctx_y);
}

static void abrir_ctx(int icone, int x, int y)
{
    ctx_novo(CTX_ICONE, icone);
    int tipo = area_icone_tipo(icone);
    ctx_item("Abrir", ACAO_ABRIR);
    if (tipo == 2) ctx_item("Remover com segurança", ACAO_DESMONTAR);
    else {
        ctx_item("Renomear...", ACAO_RENOMEAR);
        if (tipo == 1) ctx_item("Remover do pendrive...", ACAO_REMOVER);
        else ctx_item("Esconder o ícone", ACAO_ESCONDER);
    }
    ctx_mostrar(x, y);
    klog("[GUI] menu do icone %d aberto\n", icone);
}

static void abrir_ctx_area(int x, int y)
{
    ctx_novo(CTX_AREA, 0);
    ctx_item("Novo Terminal", A_TERMINAL);
    ctx_item("Arquivos", A_ARQUIVOS);
    ctx_item("Trocar o fundo...", A_FUNDO);
    ctx_item("Mostrar a área de trabalho", A_MOSTRAR_AREA);
    ctx_mostrar(x, y);
}

static int ctx_item_em(int x, int y)
{
    if (!ctx_aberto || x < ctx_x || x >= ctx_x + ctx_l || y < ctx_y + 4) return -1;
    int i = (y - ctx_y - 4) / MENU_ITEM;
    return i < ctx_qtd ? i : -1;
}

static void desenhar_ctx(void)
{
    if (!ctx_aberto) return;
    int a = ctx_qtd * MENU_ITEM + 8;
    vidro_sombra(&tela, ctx_x, ctx_y, ctx_l, a);
    vidro_pintar(&tela, ctx_x, ctx_y, ctx_l, a, tema->topo, 238, CANTOS_EM_CIMA | CANTOS_EMBAIXO);
    for (int i = 0; i < ctx_qtd; i++) {
        int y = ctx_y + 4 + i * MENU_ITEM;
        if (i == ctx_sob) {
            suave_t sv = { tela.px, tela.larg, tela.cx0, tela.cy0, tela.cx1, tela.cy1 };
            suave_ret(&sv, ctx_x + 5, y + 1, ctx_l - 10, MENU_ITEM - 2, 6, tema->destaque, tema->destaque, 256);
            if (tema->filete) suave_ret(&sv, ctx_x + 5, y + 6, 3, MENU_ITEM - 12, 1, tema->filete, tema->filete, 256);
        }
        sup_texto(&tela, ctx_x + 14, y + 5, ctx[i].nome, i == ctx_sob ? 0xFFFFFF : tema->topo_relogio);
    }
}

/* Abre um programa; "caminho" pode trazer argumentos separados por espaço
 * (o Sagui Code abre como "/bin/terminal -e /bin/saguicode"). */
static void abrir_programa(const char *caminho)
{
    static char linha[128];
    char *argv[6];
    int argc = 0;
    txt_copia_n(linha, caminho, sizeof linha);
    for (char *p = linha; *p && argc < 5;) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    if (!argc) return;
    argv[argc] = NULL;
    const char *prog = argv[0];
    for (const char *p = prog; *p; p++) if (*p == '/') argv[0] = (char *)p + 1;
    int pid = processo_executar(prog, argc, argv, NULL);
    klog("[GUI] abrir %s (pid %d)\n", caminho, pid);
}

/* ------------------------------------------------------------ animações (H76) */

/* Um contorno que vai de um retângulo a outro em alguns tiques: abrir,
 * fechar, minimizar e restaurar. A janela que está abrindo fica escondida
 * até o contorno chegar. */
#define MAX_ANIM 6
#define ANIM_TIQUES 10
typedef struct { bool ativa; int de[4], para[4]; int passo; int janela; } anim_t;
static anim_t anims[MAX_ANIM];

static void anim_retangulo(anim_t *a, int r[4])
{
    for (int k = 0; k < 4; k++) r[k] = a->de[k] + (a->para[k] - a->de[k]) * a->passo / ANIM_TIQUES;
}

static void anim_sujar(anim_t *a)
{
    int r[4];
    anim_retangulo(a, r);
    gui_redesenhar(r[0] - 2, r[1] - 2, r[2] + 4, r[3] + 4);
}

static void animar(int de_x, int de_y, int de_l, int de_a, int x, int y, int l, int a, int janela)
{
    if (!vidro_efeitos) return;
    for (int i = 0; i < MAX_ANIM; i++) {
        if (anims[i].ativa) continue;
        anims[i] = (anim_t){ true, { de_x, de_y, de_l, de_a }, { x, y, l, a }, 0, janela };
        if (janela >= 0) janelas[janela].escondida = true;
        anim_sujar(&anims[i]);
        return;
    }
}

static void anim_tique(void)
{
    for (int i = 0; i < MAX_ANIM; i++) {
        anim_t *a = &anims[i];
        if (!a->ativa) continue;
        anim_sujar(a);
        if (++a->passo > ANIM_TIQUES) {
            a->ativa = false;
            if (a->janela >= 0 && janelas[a->janela].usada) {
                janelas[a->janela].escondida = false;
                redesenhar_janela(&janelas[a->janela]);
            }
            continue;
        }
        anim_sujar(a);
    }
}

static void desenhar_anims(void)
{
    for (int i = 0; i < MAX_ANIM; i++) {
        if (!anims[i].ativa) continue;
        int r[4];
        anim_retangulo(&anims[i], r);
        sup_moldura(&tela, r[0], r[1], r[2], r[3], 0xFFFFFF);
        sup_moldura(&tela, r[0] + 1, r[1] + 1, r[2] - 2, r[3] - 2, 0x0B7A3E);
    }
}

/* ------------------------------------------------------------ composição */

/* Botões da barra de título (H88): discos e símbolos com a borda suavizada
 * (suave.h). Antes eram linhas de pixels inteiros, e o X e o círculo
 * saíam em escada. */
static void disco(const suave_t *sv, int bx, int by, uint32_t topo, uint32_t base)
{
    suave_ret(sv, bx, by, 16, 16, 8, topo, base, 256);
    suave_contorno(sv, bx, by, 16, 16, 8, 0x000000, 40);
}

static void simbolo_fechar(const suave_t *sv, int bx, int by, uint32_t cor)
{
    suave_traco(sv, bx * 8 + 40, by * 8 + 40, bx * 8 + 88, by * 8 + 88, 13, cor);
    suave_traco(sv, bx * 8 + 88, by * 8 + 40, bx * 8 + 40, by * 8 + 88, 13, cor);
}

static void simbolo_minimizar(const suave_t *sv, int bx, int by, uint32_t cor)
{
    suave_traco(sv, bx * 8 + 34, by * 8 + 68, bx * 8 + 94, by * 8 + 68, 14, cor);
}

static void simbolo_maximizar(const suave_t *sv, int bx, int by, uint32_t cor, bool maximizada, uint32_t fundo)
{
    if (maximizada) {
        suave_contorno(sv, bx + 5, by + 2, 9, 9, 2, cor, 256);
        suave_ret(sv, bx + 2, by + 5, 9, 9, 2, fundo, fundo, 256);
        suave_contorno(sv, bx + 2, by + 5, 9, 9, 2, cor, 256);
    } else {
        suave_contorno(sv, bx + 3, by + 3, 11, 11, 2, cor, 256);
        suave_contorno(sv, bx + 4, by + 4, 9, 9, 1, cor, 200);
    }
}

static void desenhar_moldura(janela_t *j, bool foco)
{
    int x = j->x, y = j->y, l = moldura_l(j), a = moldura_a(j);
    uint32_t cor = foco ? tema->titulo_foco : tema->titulo;
    suave_t sv = { tela.px, tela.larg, tela.cx0, tela.cy0, tela.cx1, tela.cy1 };
    vidro_sombra(&tela, x, y, l, a);
    vidro_guardar_cantos(&tela, x, y, l, a);          /* os de baixo, depois do conteúdo */
    vidro_pintar(&tela, x, y, l, BARRA_TITULO, cor, tema->k_titulo, CANTOS_EM_CIMA);
    sup_retangulo(&tela, x, y + BARRA_TITULO, BORDA, a - BARRA_TITULO, cor);
    sup_retangulo(&tela, x + l - BORDA, y + BARRA_TITULO, BORDA, a - BARRA_TITULO, cor);
    sup_retangulo(&tela, x, y + a - BORDA, l, BORDA, cor);
    if (foco && tema->filete) sup_retangulo(&tela, x, y + BARRA_TITULO - 1, l, 1, tema->filete);
    /* O título não passa por baixo dos botões: corta onde eles começam. */
    int bx = x + l - BORDA - 20, by = y + 4, mx = bx - 24, qx = bx - 48;
    int fim_titulo = (j->redimensionavel ? qx : mx) - 8;
    superficie_t t2 = tela;
    if (t2.cx1 > fim_titulo) t2.cx1 = fim_titulo;
    if (tema->titulo_forte) {
        sup_texto_forte(&t2, x + 11, y + 5, j->titulo, cor_mistura(cor, 0x000000, 3, 5));
        sup_texto_forte(&t2, x + 10, y + 4, j->titulo, foco ? tema->texto_foco : tema->texto);
    } else sup_texto(&t2, x + 12, y + 4, j->titulo, foco ? tema->texto_foco : tema->texto);
    uint32_t tc = foco ? tema->texto_foco : tema->texto;
    if (tema->botoes_cores) {
        /* Fechar vermelho, minimizar amarelo, maximizar verde; sem foco,
         * tons apagados. */
        disco(&sv, bx, by, foco ? 0xF06A5E : 0x9A6A66, foco ? 0xC8352C : 0x7A524E);
        simbolo_fechar(&sv, bx, by, 0xFFFFFF);
        disco(&sv, mx, by, foco ? 0xF8D25A : 0xA89A6A, foco ? 0xD49E1C : 0x887A4E);
        simbolo_minimizar(&sv, mx, by, 0xFFFFFF);
        if (j->redimensionavel) {
            disco(&sv, qx, by, foco ? 0x56CA84 : 0x7C9A88, foco ? 0x22904F : 0x5C7A68);
            simbolo_maximizar(&sv, qx, by, 0xFFFFFF, j->maximizada, foco ? 0x34A862 : 0x6C8A78);
        }
        return;
    }
    /* Fechar: um disco vermelho com um X; minimizar: um traço; maximizar:
     * um quadrado de cantos redondos. */
    disco(&sv, bx, by, foco ? 0xEE5A50 : 0x9A6A66, foco ? 0xC8352C : 0x7A524E);
    simbolo_fechar(&sv, bx, by, 0xFFFFFF);
    simbolo_minimizar(&sv, mx, by, tc);
    if (j->redimensionavel) simbolo_maximizar(&sv, qx, by, tc, j->maximizada, cor);
}

/* ------------------------------------------------------------ tamanho (H78) */

static void entregar(janela_t *j, evento_t *e);

static void tamanho_maximo(janela_t *j, int *l, int *a)
{
    *l = (int)video_largura() - 2 * BORDA;
    *a = (doca_ligada ? doca_y - 4 : (int)video_altura()) - BARRA_SUPERIOR - BARRA_TITULO - BORDA;
    if (j->cap_px && (uint32_t)(*l * *a) > j->cap_px) *a = (int)(j->cap_px / (uint32_t)*l);
}

/* Muda posição e tamanho e avisa o programa, que redesenha o conteúdo. */
static void aplicar_tamanho(janela_t *j, int x, int y, int l, int a)
{
    int lmax, amax;
    tamanho_maximo(j, &lmax, &amax);
    if (l < j->lmin) l = j->lmin;
    if (a < j->amin) a = j->amin;
    if (l > lmax) l = lmax;
    if ((uint32_t)(l * a) > j->cap_px) a = (int)(j->cap_px / (uint32_t)l);
    redesenhar_janela(j);
    /* H96: px continua com o desenho antigo (largura vl) até o programa ler
     * o evento e redesenhar; a composição mostra o antigo, cortado. */
    j->x = x; j->y = y; j->larg = l; j->alt = a;
    redesenhar_janela(j);
    evento_t e = { .tipo = EV_REDIMENSIONAR, .x = l, .y = a };
    entregar(j, &e);
    klog("[GUI] janela %d redimensionada para %dx%d em (%d,%d)\n", j->id, l, a, x, y);
}

static void alternar_maximizar(janela_t *j)
{
    if (!j->redimensionavel) return;
    if (j->maximizada) {
        j->maximizada = false;
        j->encaixada = false;
        aplicar_tamanho(j, j->antes[0], j->antes[1], j->antes[2], j->antes[3]);
    } else {
        if (!j->encaixada) { j->antes[0] = j->x; j->antes[1] = j->y; j->antes[2] = j->larg; j->antes[3] = j->alt; }
        j->encaixada = false;
        int l, a;
        tamanho_maximo(j, &l, &a);
        j->maximizada = true;
        aplicar_tamanho(j, 0, BARRA_SUPERIOR, l, a);
    }
    klog("[GUI] janela %d %s\n", j->id, j->maximizada ? "maximizada" : "voltou ao tamanho de antes");
}

/* A borda de redimensionar sob o ponto: devolve o índice em ordem[] e o modo. */
static int borda_em(int x, int y, int *modo)
{
    for (int k = qtd - 1; k >= 0; k--) {
        janela_t *j = &janelas[ordem[k]];
        if (j->minimizada || j->escondida) continue;
        int l = moldura_l(j), a = moldura_a(j);
        bool dentro = x >= j->x && x < j->x + l && y >= j->y && y < j->y + a;
        if (j->redimensionavel && !j->maximizada) {
            bool dir = x >= j->x + l - 4 && x < j->x + l + 6 && y >= j->y + BARRA_TITULO && y < j->y + a + 6;
            bool baixo = y >= j->y + a - 4 && y < j->y + a + 6 && x >= j->x && x < j->x + l + 6;
            if (dir || baixo) { *modo = (dir ? 1 : 0) | (baixo ? 2 : 0); return k; }
        }
        if (dentro) return -1;                   /* uma janela por cima tapa as bordas das de baixo */
    }
    return -1;
}

/* H94: encaixe ao arrastar para a borda. Retângulo da moldura de cada zona. */
static void encaixe_ret(int z, int *x, int *y, int *l, int *a)
{
    int W = (int)video_largura(), topo = BARRA_SUPERIOR;
    int base = doca_ligada ? doca_y - 4 : (int)video_altura();
    int h = base - topo, mw = W / 2, mh = h / 2;
    *x = 0; *y = topo; *l = W; *a = h;
    if (z == 2 || z == 4 || z == 6) *l = mw;
    if (z == 3 || z == 5 || z == 7) { *x = mw; *l = W - mw; }
    if (z >= 4) *a = mh;
    if (z == 6 || z == 7) { *y = topo + mh; *a = h - mh; }
}

static int encaixe_em(int mx, int my)
{
    int W = (int)video_largura(), base = doca_ligada ? doca_y - 4 : (int)video_altura();
    bool cima = my < BARRA_SUPERIOR + 80, baixo = my > base - 80;
    if (mx <= 4) return cima ? 4 : baixo ? 6 : 2;
    if (mx >= W - 5) return cima ? 5 : baixo ? 7 : 3;
    if (my <= BARRA_SUPERIOR + 2) return 1;
    return 0;
}

/* Põe a janela na zona z (2 a 7). Quem chama já guardou o tamanho de antes
 * em j->antes, se a janela não estava encaixada. */
static void encaixar(janela_t *j, int z)
{
    int x, y, l, a;
    encaixe_ret(z, &x, &y, &l, &a);
    j->encaixada = true;
    j->maximizada = false;
    aplicar_tamanho(j, x, y, l - 2 * BORDA, a - BARRA_TITULO - BORDA);
    klog("[GUI] janela %d encaixada na zona %d\n", j->id, z);
}

static void desenhar_encaixe(void)
{
    if (!encaixe_zona || arrastando < 0) return;
    int x, y, l, a;
    encaixe_ret(encaixe_zona, &x, &y, &l, &a);
    suave_t sv = { tela.px, tela.larg, tela.cx0, tela.cy0, tela.cx1, tela.cy1 };
    uint32_t cor = tema->filete ? tema->filete : 0x2FB866;
    suave_ret(&sv, x + 6, y + 6, l - 12, a - 12, 12, cor, cor, 60);
    suave_contorno(&sv, x + 6, y + 6, l - 12, a - 12, 12, cor, 220);
}

static void redesenhar_encaixe(int z)
{
    if (!z) return;
    int x, y, l, a;
    encaixe_ret(z, &x, &y, &l, &a);
    gui_redesenhar(x, y, l, a);
}

static void desenhar_redimensionar(void)
{
    if (redimensionando < 0) return;
    janela_t *j = &janelas[redimensionando];
    int l = redim_l + 2 * BORDA, a = redim_a + BARRA_TITULO + BORDA;
    sup_moldura(&tela, j->x, j->y, l, a, 0xFFFFFF);
    sup_moldura(&tela, j->x + 1, j->y + 1, l - 2, a - 2, 0x0B7A3E);
    char t[24];
    snformatar(t, sizeof t, "%d x %d", redim_l, redim_a);
    sup_retangulo(&tela, j->x + l / 2 - 44, j->y + a / 2 - 10, 88, 20, 0x101820);
    sup_texto(&tela, j->x + l / 2 - texto_largura(t) / 2, j->y + a / 2 - 8, t, 0xFFFFFF);
}

/* O vidro precisa do que está atrás dele: se o pedaço a redesenhar encosta
 * num vidro, o pedaço cresce para cobrir o vidro inteiro e mais a margem do
 * desfoque. Repete porque crescer pode encostar em outro vidro. */
static bool cruza(int x, int y, int l, int a) { return x < suj_x1 && x + l > suj_x0 && y < suj_y1 && y + a > suj_y0; }

static void expandir_vidro(void)
{
    if (!vidro_efeitos) return;
    const int R = VIDRO_RAIO;
    for (int volta = 0; volta < 4; volta++) {
        int antes[4] = { suj_x0, suj_y0, suj_x1, suj_y1 };
        if (cruza(0, 0, (int)video_largura(), BARRA_SUPERIOR + R)) gui_redesenhar(0, 0, (int)video_largura(), BARRA_SUPERIOR + R);
        if (doca_ligada && cruza(doca_x - R, doca_y - 30 - R, doca_l + 2 * R, DOCA_A + 30 + 2 * R))
            gui_redesenhar(doca_x - R - DOCA_CELULA, doca_y - 30 - R, doca_l + 2 * R + 2 * DOCA_CELULA, DOCA_A + 30 + 2 * R);
        /* O menu Iniciar não entra aqui (H93): ele é mais alto que
         * VIDRO_ALT_MAX, então o vidro dele é só a cor, sem desfoque, e um
         * pedaço dele pode ser refeito sozinho. Antes, cada célula sob o
         * mouse refazia o menu inteiro e a barra de cima. */
        if (ctx_aberto && cruza(ctx_x - R, ctx_y - R, ctx_l + 2 * R, ctx_qtd * MENU_ITEM + 8 + 2 * R)) redesenhar_ctx();
        for (int k = 0; k < qtd; k++) {
            janela_t *j = &janelas[ordem[k]];
            if (j->minimizada || j->escondida) continue;
            if (cruza(j->x - R, j->y - R, moldura_l(j) + 2 * R, BARRA_TITULO + 2 * R))
                gui_redesenhar(j->x - R, j->y - R, moldura_l(j) + 2 * R, BARRA_TITULO + 2 * R);
        }
        if (antes[0] == suj_x0 && antes[1] == suj_y0 && antes[2] == suj_x1 && antes[3] == suj_y1) break;
    }
}

/* Recompõe o retângulo sujo e mostra. */
void gui_mostrar(void)
{
    if (!ativa || suj_x0 >= suj_x1) return;
    montar_doca();
    expandir_vidro();
    int x = suj_x0, y = suj_y0, l = suj_x1 - suj_x0, a = suj_y1 - suj_y0;
    suj_x0 = suj_x1 = 0;
    sup_recorte(&tela, x, y, l, a);
    if (tela.cx0 >= tela.cx1 || tela.cy0 >= tela.cy1) return;
    area_fundo(&tela);
    desenhar_selecao();
    janela_t *f = focada();
    for (int k = 0; k < qtd; k++) {
        janela_t *j = &janelas[ordem[k]];
        if (j->minimizada || j->escondida) continue;
        desenhar_moldura(j, j == f);
        {
            int vl = j->vl < j->larg ? j->vl : j->larg, va = j->va < j->alt ? j->va : j->alt;
            int cx = j->x + BORDA, cy = j->y + BARRA_TITULO;
            sup_copiar(&tela, cx, cy, j->px, vl, va, j->vl);
            if (vl < j->larg) sup_retangulo(&tela, cx + vl, cy, j->larg - vl, j->alt, 0xF4F4F0);
            if (va < j->alt) sup_retangulo(&tela, cx, cy + va, vl, j->alt - va, 0xF4F4F0);
        }
        vidro_aplicar_cantos(&tela, j->x, j->y, moldura_l(j), moldura_a(j));
    }
    desenhar_anims();
    desenhar_redimensionar();
    desenhar_encaixe();
    desenhar_doca();
    if (tela.cy0 < BARRA_SUPERIOR) {
        if (vidro_efeitos) vidro_pintar(&tela, 0, 0, (int)video_largura(), BARRA_SUPERIOR, tema->topo, tema->k_topo, 0);
        area_barra(&tela, vidro_efeitos, tema->claro, doca_ligada || qtd == 0);
        desenhar_tarefas();
    }
    desenhar_menu();
    desenhar_ctx();
    desenhar_aviso(&tela);
    video_marcar(tela.cx0, tela.cy0, tela.cx1 - tela.cx0, tela.cy1 - tela.cy0);
    video_mostrar();
}

/* ------------------------------------------------------------ eventos */

static void entregar(janela_t *j, evento_t *e)
{
    e->janela = j->id;
    uint32_t prox = (j->fim + 1) % FILA_EVENTOS;
    if (prox == j->ini) {
        /* Fila cheia: movimentos podem ser jogados fora; o resto não. */
        if (e->tipo == EV_MOVER) return;
        j->ini = (j->ini + 1) % FILA_EVENTOS;
    }
    j->fila[j->fim] = *e;
    j->fim = prox;
    acordar(processo_por_pid(j->pid));
}

static int janela_em(int x, int y)   /* índice em ordem[], de cima para baixo */
{
    for (int k = qtd - 1; k >= 0; k--) {
        janela_t *j = &janelas[ordem[k]];
        if (j->minimizada || j->escondida) continue;
        if (x >= j->x && x < j->x + moldura_l(j) && y >= j->y && y < j->y + moldura_a(j))
            return k;
    }
    return -1;
}

static void trazer_para_cima(int k)
{
    int i = ordem[k];
    if (janelas[i].minimizada) {                  /* restaurar (H75): sai da doca */
        janelas[i].minimizada = false;
        janela_t *j = &janelas[i];
        int d = -1;
        for (int t = 0; t < qtd_doca; t++) if (doca[t].janela == i) d = t;
        if (d >= 0) animar(doca_x + 8 + d * DOCA_CELULA, doca_y, DOCA_CELULA, DOCA_A, j->x, j->y, moldura_l(j), moldura_a(j), i);
        klog("[GUI] janela %d restaurada\n", j->id);
        redesenhar_janela(j);
        redesenhar_doca();
    }
    if (k == qtd - 1) return;
    if (!doca_ligada) redesenhar_doca();   /* o botão da janela da frente muda de cor */
    janela_t *antes = focada();
    for (; k < qtd - 1; k++) ordem[k] = ordem[k + 1];
    ordem[qtd - 1] = i;
    if (antes) redesenhar_janela(antes);
    redesenhar_janela(&janelas[i]);
    redesenhar_doca();
}

static void minimizar(janela_t *j)
{
    montar_doca();
    int d = -1;
    for (int t = 0; t < qtd_doca; t++) if (doca[t].janela == (int)(j - janelas)) d = t;
    j->minimizada = true;
    redesenhar_janela(j);
    if (d >= 0) animar(j->x, j->y, moldura_l(j), moldura_a(j), doca_x + 8 + d * DOCA_CELULA, doca_y, DOCA_CELULA, DOCA_A, -1);
    redesenhar_doca();
    if (focada()) redesenhar_janela(focada());
    klog("[GUI] janela %d minimizada\n", j->id);
}

/* Clique num item da doca: traz a janela do programa (restaurando se
 * estiver minimizada) ou abre o programa. */
static void doca_clique(int i)
{
    item_doca_t *it = &doca[i];
    klog("[GUI] doca: %s\n", it->nome);
    if (it->janela >= 0 && janelas[it->janela].usada) {
        for (int k = 0; k < qtd; k++) if (ordem[k] == it->janela) { trazer_para_cima(k); break; }
        return;
    }
    if (it->programa) abrir_programa(it->programa);
}

static janela_t *janela_por_id(int id)
{
    for (int i = 0; i < MAX_JANELAS; i++)
        if (janelas[i].usada && janelas[i].id == id) return &janelas[i];
    return NULL;
}

/* Um item do menu do botão direito foi escolhido. */
static void ctx_escolher(int i)
{
    int acao = ctx[i].acao;
    klog("[GUI] menu: %s\n", ctx[i].nome);
    if (ctx_tipo == CTX_ICONE) { area_icone_acao(ctx_alvo, acao); return; }
    if (ctx_tipo == CTX_PROGRAMA) {
        janela_t *j = janela_por_id(ctx_alvo);
        if (j) { evento_t e = { .tipo = EV_MENU, .tecla = (unsigned)acao }; entregar(j, &e); }
        return;
    }
    if (ctx_tipo == CTX_JANELA) {
        janela_t *j = janela_por_id(ctx_alvo);
        if (!j) return;
        if (acao == J_MINIMIZAR) minimizar(j);
        else if (acao == J_MAXIMIZAR) alternar_maximizar(j);
        else if (acao == J_FECHAR) { evento_t e = { .tipo = EV_FECHAR }; entregar(j, &e); }
        return;
    }
    if (acao == A_TERMINAL) abrir_programa("/bin/terminal");
    else if (acao == A_ARQUIVOS) abrir_programa("/bin/arquivos");
    else if (acao == A_FUNDO) {
        char *argv[] = { "configuracoes", "aparencia", NULL };
        processo_executar("/bin/configuracoes", 2, argv, NULL);
    } else if (acao == A_MOSTRAR_AREA) {
        for (int k = 0; k < MAX_JANELAS; k++) if (janelas[k].usada && !janelas[k].minimizada) minimizar(&janelas[k]);
    }
}

/* ------------------------------------------------------------ barra de tarefas (H81)
 *
 * Opção à doca (Configurações > Aparência, aparencia.barra = tarefas), como
 * no pull request #1 do Robson: um botão por janela na barra de cima, entre
 * o menu do sagui e os ícones da direita. Clicar traz a janela (ou a
 * minimiza, se já estava na frente). */
#define TAREFA_X0    120
#define TAREFA_L     124
#define TAREFA_PASSO (TAREFA_L + 4)

static int tarefas_max(void) { return ((int)video_largura() - 300 - TAREFA_X0) / TAREFA_PASSO; }

static void desenhar_tarefas(void)
{
    if (doca_ligada || tela.cy0 >= BARRA_SUPERIOR) return;
    janela_t *f = focada();
    int n = 0, max = tarefas_max();
    for (int k = 0; k < qtd && n < max; k++) {
        janela_t *j = &janelas[ordem[k]];
        int x = TAREFA_X0 + n++ * TAREFA_PASSO;
        bool frente = j == f;
        vidro_pintar(&tela, x, 3, TAREFA_L, BARRA_SUPERIOR - 6, frente ? tema->destaque : tema->titulo, 220, CANTOS_EM_CIMA | CANTOS_EMBAIXO);
        if (!j->minimizada) sup_retangulo(&tela, x + 8, BARRA_SUPERIOR - 5, TAREFA_L - 16, 1, tema->filete ? tema->filete : 0xFFDF4A);
        char t[15];
        txt_copia_n(t, j->titulo, sizeof t);
        sup_texto(&tela, x + 6, 3, t, j->minimizada ? 0x9AA8B8 : 0xFFFFFF);
    }
}

static janela_t *tarefa_em(int x, int y)
{
    if (doca_ligada || y >= BARRA_SUPERIOR || x < TAREFA_X0) return NULL;
    int n = (x - TAREFA_X0) / TAREFA_PASSO;
    if ((x - TAREFA_X0) % TAREFA_PASSO >= TAREFA_L || n >= tarefas_max() || n >= qtd) return NULL;
    return &janelas[ordem[n]];
}

/* ------------------------------------------------------------ laço da área de trabalho (H90)
 *
 * Arrastar no vazio da área de trabalho desenha um retângulo translúcido;
 * ao soltar, os ícones que ele toca ficam marcados (com Shift, somam aos já
 * marcados). Ideia e primeira versão: commit 0b3c462, do Robson. */
static bool laco_ativo, laco_somar;
static int laco_x0, laco_y0, laco_x1, laco_y1;

static void laco_retangulo(int *x, int *y, int *l, int *a)
{
    *x = laco_x0 < laco_x1 ? laco_x0 : laco_x1;
    *y = laco_y0 < laco_y1 ? laco_y0 : laco_y1;
    *l = (laco_x0 < laco_x1 ? laco_x1 - laco_x0 : laco_x0 - laco_x1) + 1;
    *a = (laco_y0 < laco_y1 ? laco_y1 - laco_y0 : laco_y0 - laco_y1) + 1;
}

static void sujar_laco(void)
{
    int x, y, l, a;
    laco_retangulo(&x, &y, &l, &a);
    gui_redesenhar(x - 1, y - 1, l + 2, a + 2);
}

/* Destaque dos ícones marcados e o laço, por cima do fundo e por baixo das
 * janelas (o laço é da área de trabalho). */
static void desenhar_selecao(void)
{
    suave_t sv = { tela.px, tela.larg, tela.cx0, tela.cy0, tela.cx1, tela.cy1 };
    uint32_t cor = tema->filete ? tema->filete : 0x2FB866;
    for (int i = 0; i < area_qtd_icones(); i++) {
        if (!area_icone_selecionado(i)) continue;
        int x, y, l, a;
        area_icone_retangulo(i, &x, &y, &l, &a);
        suave_ret(&sv, x, y - 4, l, a + 2, 10, cor, cor, 80);
        suave_contorno(&sv, x, y - 4, l, a + 2, 10, cor, 200);
    }
    if (!laco_ativo) return;
    int x, y, l, a;
    laco_retangulo(&x, &y, &l, &a);
    suave_ret(&sv, x, y, l, a, 3, cor, cor, 64);
    suave_contorno(&sv, x, y, l, a, 3, cor, 230);
}

static void tratar_laco(evento_mouse_t *m)
{
    sujar_laco();
    if (m->botoes & BOTAO_ESQUERDO) {
        laco_x1 = m->x;
        laco_y1 = m->y < BARRA_SUPERIOR ? BARRA_SUPERIOR : m->y;
        sujar_laco();
        return;
    }
    laco_ativo = false;
    int x, y, l, a;
    laco_retangulo(&x, &y, &l, &a);
    if (l > 4 || a > 4) area_selecionar(x, y, l, a, laco_somar);
    klog("[GUI] laco: (%d,%d) %dx%d, %d icones marcados\n", x, y, l, a, area_qtd_selecionados());
    gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
}

static void tratar_mouse(evento_mouse_t *m)
{
    if (laco_ativo) { tratar_laco(m); return; }
    bool apertou = (m->mudaram & m->botoes) != 0;
    bool soltou = (m->mudaram & ~m->botoes) != 0;

    if (redimensionando >= 0) {
        janela_t *j = &janelas[redimensionando];
        int sujo_l = redim_l + 2 * BORDA + 2, sujo_a = redim_a + BARRA_TITULO + BORDA + 2;
        if (!j->usada) { redimensionando = -1; return; }
        if (!(m->botoes & BOTAO_ESQUERDO)) {
            gui_redesenhar(j->x - 1, j->y - 1, sujo_l, sujo_a);
            redimensionando = -1;
            aplicar_tamanho(j, j->x, j->y, redim_l, redim_a);
            return;
        }
        gui_redesenhar(j->x - 1, j->y - 1, sujo_l, sujo_a);
        int lmax, amax;
        tamanho_maximo(j, &lmax, &amax);
        if (redim_modo & 1) redim_l = m->x - j->x - 2 * BORDA;
        if (redim_modo & 2) redim_a = m->y - j->y - BARRA_TITULO - BORDA;
        if (redim_l < j->lmin) redim_l = j->lmin;
        if (redim_a < j->amin) redim_a = j->amin;
        if (redim_l > lmax) redim_l = lmax;
        if ((uint32_t)(redim_l * redim_a) > j->cap_px) redim_a = (int)(j->cap_px / (uint32_t)redim_l);
        gui_redesenhar(j->x - 1, j->y - 1, redim_l + 2 * BORDA + 2, redim_a + BARRA_TITULO + BORDA + 2);
        return;
    }

    if (arrastando >= 0) {
        janela_t *j = &janelas[arrastando];
        if (!j->usada || !(m->botoes & BOTAO_ESQUERDO)) {
            int z = encaixe_zona;
            redesenhar_encaixe(z);
            encaixe_zona = 0;
            arrastando = -1;
            if (!j->usada) return;
            if (z == 1) {
                if (!j->encaixada) { j->antes[0] = arr_x0; j->antes[1] = arr_y0; j->antes[2] = j->larg; j->antes[3] = j->alt; }
                j->encaixada = true;                   /* guarda o antes já posto acima */
                alternar_maximizar(j);
            } else if (z) {
                if (!j->encaixada) { j->antes[0] = arr_x0; j->antes[1] = arr_y0; j->antes[2] = j->larg; j->antes[3] = j->alt; }
                encaixar(j, z);
            } else klog("[GUI] janela %d movida para (%d,%d)\n", j->id, j->x, j->y);
            return;
        }
        int z = j->redimensionavel ? encaixe_em(m->x, m->y) : 0;
        if (z != encaixe_zona) { redesenhar_encaixe(encaixe_zona); encaixe_zona = z; redesenhar_encaixe(z); }
        int nx = m->x - arr_dx, ny = m->y - arr_dy;
        if (ny < BARRA_SUPERIOR) ny = BARRA_SUPERIOR;
        if (ny > (int)video_altura() - BARRA_TITULO) ny = (int)video_altura() - BARRA_TITULO;
        if (nx > (int)video_largura() - 40) nx = (int)video_largura() - 40;
        if (nx < 40 - moldura_l(j)) nx = 40 - moldura_l(j);
        if (nx != j->x || ny != j->y) {
            redesenhar_janela(j);
            j->x = nx; j->y = ny;
            redesenhar_janela(j);
        }
        return;
    }

    if (apertou) {
        if (ctx_aberto) {
            int ci = ctx_item_em(m->x, m->y);
            ctx_aberto = false;
            redesenhar_ctx();
            if (ci >= 0) ctx_escolher(ci);
            return;
        }
        /* O menu aberto pega o clique: num item, abre; fora, só fecha. */
        if (menu_aberto) {
            int mi = menu_alvo_em(m->x, m->y);
            if (!(m->mudaram & BOTAO_ESQUERDO) || !(m->botoes & BOTAO_ESQUERDO)) {
                if (mi >= 0) return;              /* soltar o botão dentro do menu */
            } else if (mi >= ALVO_CAT && mi < ALVO_CAT + QTD_CATS) {
                menu_cat = mi - ALVO_CAT; menu_sel = 0;
                klog("[GUI] menu: categoria %s\n", cat_nomes[menu_cat]);
                redesenhar_corpo_menu();
                return;
            } else if (mi == ALVO_BUSCA) return;
            else if (mi == ALVO_SOBRE || mi == ALVO_DESLIGAR) {
                menu_abrir(false);
                abrir_programa(mi == ALVO_SOBRE ? "/bin/sobre-o-sistema" : "/bin/energia");
                return;
            } else if (mi >= 0) { menu_lancar(mi); return; }
            else if (m->x >= MENU_X && m->x < MENU_X + MENU_L && m->y >= MENU_Y && m->y < MENU_Y + MENU_ALT) return;
            menu_abrir(false);
            if (m->y >= BARRA_SUPERIOR) return;
            if (m->x < 110) return;               /* clicou no próprio botão do menu: fecha */
        }
        int di = doca_item_em(m->x, m->y);
        if (di >= 0) {
            if (m->mudaram & BOTAO_ESQUERDO) doca_clique(di);
            captura = -1;
            return;
        }
        int modo;
        int kb = (m->mudaram & BOTAO_ESQUERDO) ? borda_em(m->x, m->y, &modo) : -1;
        if (kb >= 0) {
            trazer_para_cima(kb);
            redimensionando = ordem[qtd - 1];
            redim_modo = modo;
            redim_l = janelas[redimensionando].larg;
            redim_a = janelas[redimensionando].alt;
            captura = -1;
            return;
        }
        int k = janela_em(m->x, m->y);
        if (k < 0 && m->y < BARRA_SUPERIOR) {
            /* Na barra de cima: o menu do sagui, o som e o botão de energia (H36, H75). */
            if (!(m->mudaram & BOTAO_ESQUERDO)) return;
            janela_t *tj = tarefa_em(m->x, m->y);
            if (tj) {
                if (tj == focada()) minimizar(tj);
                else {
                    tj->minimizada = false;
                    int pos = -1;
                    for (int q = 0; q < qtd; q++) if (&janelas[ordem[q]] == tj) pos = q;
                    if (pos >= 0) trazer_para_cima(pos);
                    redesenhar_janela(tj);
                }
                gui_redesenhar(0, 0, (int)video_largura(), BARRA_SUPERIOR);
                klog("[GUI] barra de tarefas: %s\n", tj->titulo);
                return;
            }
            if (m->x < 110) menu_abrir(true);
            else if (area_som_em(m->x, m->y)) {
                char *argv[] = { "configuracoes", "som", NULL };
                processo_executar("/bin/configuracoes", 2, argv, NULL);
            } else if (area_energia_em(m->x, m->y)) area_energia_abrir();
            return;
        }
        if (k < 0) {
            /* Na área de trabalho: duplo clique num ícone abre o programa. */
            int ic = area_icone_em(m->x, m->y);
            uint32_t agora = relogio_tiques();
            bool duplo = ic >= 0 && ult_clique_alvo == 1000 + ic && agora - ult_clique_tique < DUPLO_CLIQUE;
            ult_clique_alvo = ic >= 0 ? 1000 + ic : -999;
            ult_clique_tique = agora;
            if (duplo && (m->mudaram & BOTAO_ESQUERDO)) { area_icone_abrir(ic); ult_clique_alvo = -999; }
            if (m->mudaram & BOTAO_ESQUERDO) {
                bool shift = (teclado_modificadores() & MOD_SHIFT) != 0;
                if (ic >= 0) {
                    /* Um clique marca só este ícone (com Shift, soma). */
                    if (!area_icone_selecionado(ic) || !shift) {
                        int ix, iy, il, ia;
                        area_icone_retangulo(ic, &ix, &iy, &il, &ia);
                        area_selecionar(ix + il / 2, iy + ia / 2, 1, 1, shift);
                        gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
                    }
                } else {
                    if (!shift && area_limpar_selecao()) gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
                    laco_ativo = true;
                    laco_somar = shift;
                    laco_x0 = laco_x1 = m->x;
                    laco_y0 = laco_y1 = m->y;
                }
            }
            if (ic >= 0 && (m->mudaram & BOTAO_DIREITO)) abrir_ctx(ic, m->x, m->y);
            else if (m->mudaram & BOTAO_DIREITO) abrir_ctx_area(m->x, m->y);
            captura = -1;
            return;
        }
        trazer_para_cima(k);
        janela_t *j = focada();
        int idx = ordem[qtd - 1];
        int rx = m->x - j->x, ry = m->y - j->y;
        if (ry < BARRA_TITULO && (m->mudaram & BOTAO_DIREITO)) {
            ctx_novo(CTX_JANELA, j->id);
            ctx_item("Minimizar", J_MINIMIZAR);
            if (j->redimensionavel) ctx_item(j->maximizada ? "Voltar ao tamanho" : "Maximizar", J_MAXIMIZAR);
            ctx_item("Fechar", J_FECHAR);
            ctx_mostrar(m->x, m->y);
            captura = -1;
            return;
        }
        if (ry < BARRA_TITULO) {
            int bx = moldura_l(j) - BORDA - 20;
            if ((m->mudaram & BOTAO_ESQUERDO) && rx >= bx && rx < bx + 16 && ry >= 4 && ry < 20) {
                evento_t e = { .tipo = EV_FECHAR };
                entregar(j, &e);
                klog("[GUI] fechar janela %d (%s)\n", j->id, j->titulo);
            } else if ((m->mudaram & BOTAO_ESQUERDO) && rx >= bx - 24 && rx < bx - 8 && ry >= 4 && ry < 20) {
                minimizar(j);
            } else if ((m->mudaram & BOTAO_ESQUERDO) && j->redimensionavel && rx >= bx - 48 && rx < bx - 32 && ry >= 4 && ry < 20) {
                alternar_maximizar(j);
            } else if ((m->mudaram & BOTAO_ESQUERDO) && j->redimensionavel && ult_clique_alvo == 2000 + j->id &&
                       relogio_tiques() - ult_clique_tique < DUPLO_CLIQUE) {
                ult_clique_alvo = -999;
                alternar_maximizar(j);              /* duplo clique na barra de título */
            } else if (m->mudaram & BOTAO_ESQUERDO) {
                arrastando = idx;
                arr_dx = rx; arr_dy = ry;
                ult_clique_alvo = 2000 + j->id;
                ult_clique_tique = relogio_tiques();
                encaixe_zona = 0;
                if (j->maximizada || j->encaixada) {
                    /* Arrastar uma janela maximizada ou encaixada: ela volta ao tamanho de antes, sob o mouse. */
                    j->maximizada = false;
                    j->encaixada = false;
                    int nl = j->antes[2];
                    arr_dx = rx * (nl + 2 * BORDA) / moldura_l(j);
                    aplicar_tamanho(j, m->x - arr_dx, j->y, nl, j->antes[3]);
                    arr_x0 = j->antes[0]; arr_y0 = j->antes[1];
                } else { arr_x0 = j->x; arr_y0 = j->y; }
            }
            captura = -1;
            return;
        }
        int cx = m->x - j->x - BORDA, cy = m->y - j->y - BARRA_TITULO;
        if (cx < 0 || cy < 0 || cx >= j->larg || cy >= j->alt) return;   /* na borda */
        uint32_t agora = relogio_tiques();
        int dx = cx - ult_clique_x, dy = cy - ult_clique_y;
        bool duplo = ult_clique_alvo == j->id && agora - ult_clique_tique < DUPLO_CLIQUE &&
                     dx > -5 && dx < 5 && dy > -5 && dy < 5;
        evento_t e = { .tipo = EV_CLIQUE, .x = cx, .y = cy, .botoes = m->mudaram & m->botoes,
                       .cliques = duplo ? 2 : 1, .modificadores = teclado_modificadores() & 3 };
        ult_clique_alvo = duplo ? -999 : j->id;
        ult_clique_tique = agora;
        ult_clique_x = cx; ult_clique_y = cy;
        entregar(j, &e);
        captura = idx;
        return;
    }

    if (captura >= 0 && janelas[captura].usada) {
        janela_t *j = &janelas[captura];
        evento_t e = { .tipo = soltou ? EV_SOLTOU : EV_MOVER, .x = m->x - j->x - BORDA,
                       .y = m->y - j->y - BARRA_TITULO, .botoes = soltou ? m->mudaram : m->botoes };
        entregar(j, &e);
        if (soltou) captura = -1;
        return;
    }
    /* Movimento sem botão: o realce da doca e do menu, e a janela com foco. */
    int sob = doca_item_em(m->x, m->y);
    if (sob != doca_sob) { doca_sob = sob; redesenhar_doca(); }
    int ms = menu_alvo_em(m->x, m->y);
    if (ms == ALVO_BUSCA) ms = -1;
    if (ms != menu_sob) { redesenhar_alvo(menu_sob); redesenhar_alvo(ms); menu_sob = ms; }
    int cs = ctx_item_em(m->x, m->y);
    if (cs != ctx_sob) { ctx_sob = cs; redesenhar_ctx(); }
    if (sob >= 0) return;
    janela_t *j = focada();
    if (j) {
        int cx = m->x - j->x - BORDA, cy = m->y - j->y - BARRA_TITULO;
        if (cx >= 0 && cy >= 0 && cx < j->larg && cy < j->alt) {
            evento_t e = { .tipo = EV_MOVER, .x = cx, .y = cy, .botoes = 0 };
            entregar(j, &e);
        }
    }
}

/* H95: atalhos do sistema. Devolve true se a tecla foi usada. */
static int  alt_tab_lista[MAX_JANELAS], alt_tab_qtd, alt_tab_pos;

static bool atalho(int t, uint32_t mods)
{
    if (t == TECLA_SUPER) { menu_abrir(!menu_aberto); return true; }
    janela_t *j = focada();
    if ((mods & MOD_ALT) && t == '\t') {
        if (!alt_tab_qtd) {                      /* começo: a ordem de agora, da frente para trás */
            for (int k = qtd - 1; k >= 0; k--)
                if (!janelas[ordem[k]].escondida) alt_tab_lista[alt_tab_qtd++] = ordem[k];
            alt_tab_pos = 0;
        }
        if (alt_tab_qtd < 2) return true;
        alt_tab_pos = (alt_tab_pos + ((mods & MOD_SHIFT) ? alt_tab_qtd - 1 : 1)) % alt_tab_qtd;
        int i = alt_tab_lista[alt_tab_pos];
        if (menu_aberto) menu_abrir(false);
        for (int k = 0; k < qtd; k++) if (ordem[k] == i) { trazer_para_cima(k); break; }
        klog("[GUI] alt+tab: janela %d\n", janelas[i].id);
        return true;
    }
    if ((mods & MOD_ALT) && t == TECLA_F4) {
        if (j) {
            evento_t e = { .tipo = EV_FECHAR };
            entregar(j, &e);
            klog("[GUI] alt+f4: fechar janela %d (%s)\n", j->id, j->titulo);
        }
        return true;
    }
    if (!(mods & MOD_SUPER)) return false;
    if (t == 'd' || t == 'D') {                  /* Windows+D: minimiza todas */
        for (int k = qtd - 1; k >= 0; k--)
            if (!janelas[ordem[k]].minimizada && !janelas[ordem[k]].escondida) minimizar(&janelas[ordem[k]]);
        return true;
    }
    if (!j || !j->redimensionavel) return t >= TECLA_CIMA && t <= TECLA_DIREITA;
    if (t == TECLA_CIMA) {
        if (!j->maximizada) alternar_maximizar(j);
    } else if (t == TECLA_BAIXO) {
        if (j->maximizada) alternar_maximizar(j);
        else if (j->encaixada) {
            j->encaixada = false;
            aplicar_tamanho(j, j->antes[0], j->antes[1], j->antes[2], j->antes[3]);
            klog("[GUI] janela %d voltou ao tamanho de antes\n", j->id);
        } else minimizar(j);
    } else if (t == TECLA_ESQUERDA || t == TECLA_DIREITA) {
        if (!j->encaixada && !j->maximizada) { j->antes[0] = j->x; j->antes[1] = j->y; j->antes[2] = j->larg; j->antes[3] = j->alt; }
        encaixar(j, t == TECLA_ESQUERDA ? 2 : 3);
    } else return false;
    return true;
}

static void tarefa_gerenciador(void *arg)
{
    (void)arg;
    static const char canal = 0;
    for (;;) {
        int_desliga();
        evento_mouse_t m;
        while (mouse_tentar(&m))
            tratar_mouse(&m);
        int t;
        if (!(teclado_modificadores() & MOD_ALT)) alt_tab_qtd = 0;   /* soltou o Alt: acaba o Alt+Tab */
        while ((t = teclado_tentar()) >= 0) {
            if (t == TECLA_IMPRIMIR) {   /* H87: a captura de tela é do sistema, não da janela em foco */
                char *argv[] = { "captura", NULL };
                int pid = processo_executar("/bin/captura", 1, argv, NULL);
                klog("[GUI] Print Screen: captura pid %d\n", pid);
                continue;
            }
            if (atalho(t, teclado_modificadores())) continue;   /* H95 */
            if (menu_aberto) { menu_tecla(t); continue; }   /* H89: a busca do menu Iniciar */
            janela_t *j = focada();
            if (!j) continue;
            evento_t e = { .tipo = EV_TECLA, .tecla = (unsigned)t, .modificadores = teclado_modificadores() & 3 };
            entregar(j, &e);
        }
        if (area_config_mudou() | area_instalados_mudaram()) {
            ler_tema();
            gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
        }
        anim_tique();
        /* A doca muda quando janelas abrem e fecham. */
        static int qtd_vista = -1;
        if (qtd != qtd_vista) { qtd_vista = qtd; montar_doca(); redesenhar_doca(); }
        if (area_relogio_mudou())
            gui_redesenhar(0, 0, (int)video_largura(), BARRA_SUPERIOR);
        /* Pendrive: avisos do kernel viram notificações; o ícone acompanha. */
        int tipo;
        char texto[96];
        while (disco_proximo_aviso(&tipo, texto, sizeof texto))
            gui_aviso(texto);
        disco_t *pd = NULL;
        for (int k = 0; k < MAX_DISCOS && !pd; k++) { disco_t *d = disco_indice(k); if (d && d->presente && !d->de_boot && (!d->interno || d->montado)) pd = d; }   /* H97: HD sem montar não é pendrive */
        if (area_pendrive(pd != NULL, pd ? (pd->interno ? "Disco" : pd->rotulo[0] ? pd->rotulo : "Pendrive") : NULL))
            gui_redesenhar(0, BARRA_SUPERIOR, 140, (int)video_altura() - BARRA_SUPERIOR);
        if (aviso_ate && (int32_t)(relogio_tiques() - aviso_ate) >= 0) {
            aviso_ate = 0;
            int x, y;
            aviso_retangulo(&x, &y);
            gui_redesenhar(x - 2, y - 2, AVISO_L + 4, AVISO_A + 4);
        }
        gui_mostrar();
        /* Dorme até o próximo tique (o escalonador acorda pelo despertador). */
        atual->acordar_em = relogio_tiques() + 1;
        bloquear(&canal);
        ceder();
    }
}

bool gui_iniciar(void)
{
    if (!video_ativo() || !area_iniciar()) return false;
    sup_iniciar(&tela, video_buffer(), (int)video_largura(), (int)video_altura());
    vidro_iniciar((int)video_largura());
    ativa = true;
    /* Efeitos automáticos (H77): mede quanto custa recompor a tela inteira
     * com o vidro; se passar de 40 ms por quadro, o computador é lento e
     * fica com os efeitos leves (sem desfoque e sem sombra). */
    vidro_efeitos = true;
    uint32_t t0 = relogio_tiques();
    for (int i = 0; i < 4; i++) {
        gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
        gui_mostrar();
    }
    uint32_t ms = (relogio_tiques() - t0) * (1000 / TIQUES_POR_SEGUNDO) / 4;
    efeitos_lento = ms > 40;
    ler_tema();
    klog("[GUI] quadro inteiro com vidro: %u ms; efeitos %s (%s)\n", ms, vidro_efeitos ? "completos" : "leves",
         efeitos_automaticos ? "escolha automatica" : "escolha do usuario");
    gui_redesenhar(0, 0, (int)video_largura(), (int)video_altura());
    gui_mostrar();
    tarefa_kernel("janelas", tarefa_gerenciador, NULL);
    klog("[GUI] area de trabalho pronta\n");
    return true;
}

/* ------------------------------------------------------------ chamadas de sistema */

static janela_t *janela_do_processo(int id)
{
    for (int i = 0; i < MAX_JANELAS; i++)
        if (janelas[i].usada && janelas[i].id == id)
            return janelas[i].pid == atual->pid ? &janelas[i] : NULL;
    return NULL;
}

static void destruir(int i)
{
    janela_t *j = &janelas[i];
    redesenhar_janela(j);
    if (!j->minimizada && !j->escondida)
        animar(j->x, j->y, moldura_l(j), moldura_a(j), j->x + moldura_l(j) / 2 - moldura_l(j) / 8,
               j->y + moldura_a(j) / 2 - moldura_a(j) / 8, moldura_l(j) / 4, moldura_a(j) / 4, -1);
    for (int k = 0; k < MAX_ANIM; k++) if (anims[k].janela == i) anims[k].janela = -1;
    processo_t *dono = processo_por_pid(j->pid);
    if (dono && dono->usuario && dono->espaco != espaco_kernel())
        for (uint32_t k = 0; k < j->paginas; k++)
            espaco_desmapear(dono->espaco, j->virt + k * PAGINA);
    memfisica_liberar_seguidas((uint32_t)j->px, j->paginas);
    j->usada = false;
    int k = 0;
    while (k < qtd && ordem[k] != i) k++;
    for (; k < qtd - 1; k++) ordem[k] = ordem[k + 1];
    if (k < qtd) qtd--;
    if (arrastando == i) arrastando = -1;
    if (captura == i) captura = -1;
    if (focada()) redesenhar_janela(focada());   /* a nova janela de cima ganha o foco */
}

void janelas_do_processo_fechar(processo_t *p)
{
    for (int i = 0; i < MAX_JANELAS; i++)
        if (janelas[i].usada && janelas[i].pid == p->pid)
            destruir(i);
    gui_mostrar();
}

int32_t sis_criar_janela(uint32_t ptitulo, uint32_t dims, uint32_t ppixels, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (!ativa) return ERRO_ACESSO;
    char titulo[48];
    int r = usuario_texto((const char *)ptitulo, titulo, sizeof titulo);
    if (r < 0) return r;
    int l = (int)(dims & 0xFFFF), a = (int)(dims >> 16);
    if (l < 40 || a < 20 || l > JANELA_LARG_MAX || a > JANELA_ALT_MAX) return ERRO_INVALIDO;
    if (!usuario_ok((void *)ppixels, 4, true)) return ERRO_ENDERECO;
    int i = 0;
    while (i < MAX_JANELAS && janelas[i].usada) i++;
    if (i == MAX_JANELAS) return ERRO_LIMITE;
    uint32_t paginas = ((uint32_t)(l * a) * 4 + PAGINA - 1) / PAGINA;
    uint32_t fis = memfisica_alocar_seguidas(paginas);
    if (!fis) return ERRO_SEM_MEMORIA;
    janela_t *j = &janelas[i];
    memset(j, 0, sizeof *j);
    j->usada = true;
    j->id = proximo_id++;
    j->pid = atual->pid;
    j->larg = l; j->alt = a;
    j->vl = l; j->va = a; j->pend_l = 0;
    j->px = (uint32_t *)fis;
    j->paginas = paginas;
    j->virt = JANELAS_USUARIO + (uint32_t)i * ESPACO_JANELA;
    txt_copia_n(j->titulo, titulo, sizeof j->titulo);
    for (uint32_t k = 0; k < (uint32_t)(l * a); k++) j->px[k] = 0xF4F4F0;
    for (uint32_t k = 0; k < paginas; k++)
        espaco_mapear(atual->espaco, j->virt + k * PAGINA, fis + k * PAGINA, PG_USUARIO | PG_ESCRITA | PG_EMPRESTADA);
    /* Posição em cascata, sem passar da tela. */
    int n = j->id % 8;
    j->x = 150 + n * 34;
    j->y = BARRA_SUPERIOR + 30 + n * 28;
    if (j->x + moldura_l(j) > (int)video_largura()) j->x = (int)video_largura() - moldura_l(j);
    if (j->y + moldura_a(j) > (int)video_altura()) j->y = (int)video_altura() - moldura_a(j);
    if (j->x < 0) j->x = 0;
    if (j->y < BARRA_SUPERIOR) j->y = BARRA_SUPERIOR;
    janela_t *antes = focada();
    if (antes) redesenhar_janela(antes);
    ordem[qtd++] = i;
    redesenhar_janela(j);
    animar(j->x + moldura_l(j) / 2 - moldura_l(j) / 8, j->y + moldura_a(j) / 2 - moldura_a(j) / 8, moldura_l(j) / 4,
           moldura_a(j) / 4, j->x, j->y, moldura_l(j), moldura_a(j), i);
    gui_mostrar();
    *(uint32_t *)ppixels = j->virt;
    klog("[GUI] janela %d criada por %d: \"%s\" %dx%d em (%d,%d)\n", j->id, j->pid, j->titulo, l, a, j->x, j->y);
    return j->id;
}

int32_t sis_fechar_janela(uint32_t id, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    janela_t *j = janela_do_processo((int)id);
    if (!j) return ERRO_INVALIDO;
    destruir((int)(j - janelas));
    gui_mostrar();
    return 0;
}

int32_t sis_atualizar_janela(uint32_t id, uint32_t pret, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    janela_t *j = janela_do_processo((int)id);
    if (!j) return ERRO_INVALIDO;
    if (j->pend_l) {                     /* H96: o programa já desenha no tamanho novo */
        j->vl = j->pend_l; j->va = j->pend_a; j->pend_l = 0;
        redesenhar_janela(j);
    }
    int x = 0, y = 0, l = j->larg, a = j->alt;
    if (pret) {
        if (!usuario_ok((void *)pret, sizeof(retangulo_t), false)) return ERRO_ENDERECO;
        retangulo_t *r = (retangulo_t *)pret;
        x = r->x; y = r->y; l = r->largura; a = r->altura;
        if (x < 0) { l += x; x = 0; }
        if (y < 0) { a += y; y = 0; }
        if (x + l > j->larg) l = j->larg - x;
        if (y + a > j->alt) a = j->alt - y;
    }
    gui_redesenhar(j->x + BORDA + x, j->y + BARRA_TITULO + y, l, a);
    gui_mostrar();
    return 0;
}

int32_t sis_proximo_evento(uint32_t id, uint32_t pev, uint32_t esperar, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (!usuario_ok((void *)pev, sizeof(evento_t), true)) return ERRO_ENDERECO;
    bool tem_janela = false;
    for (int i = 0; i < MAX_JANELAS; i++) {
        janela_t *j = &janelas[i];
        if (!j->usada || j->pid != atual->pid || (id && j->id != (int)id)) continue;
        tem_janela = true;
        if (j->ini != j->fim) {
            *(evento_t *)pev = j->fila[j->ini];
            if (j->fila[j->ini].tipo == EV_REDIMENSIONAR) { j->pend_l = j->fila[j->ini].x; j->pend_a = j->fila[j->ini].y; }
            j->ini = (j->ini + 1) % FILA_EVENTOS;
            return 1;
        }
    }
    if (!tem_janela) return ERRO_INVALIDO;
    if (!esperar) return 0;
    bloquear(atual);   /* "entregar" acorda o dono da janela */
    return SIS_REPETIR;
}

/* SIS_TECLAS_SEGURADAS (&mapa[32]): quais teclas estão apertadas agora.
 * Só quem tem a janela em foco recebe o mapa; os outros recebem tudo zerado,
 * para um programa em segundo plano não espiar o que se digita. */
int32_t sis_teclas_seguradas(uint32_t pmapa, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    if (!usuario_ok((void *)pmapa, 32, true)) return ERRO_INVALIDO;
    uint8_t mapa[32];
    memset(mapa, 0, sizeof mapa);
    janela_t *j = focada();
    bool foco = j && j->pid == atual->pid;
    if (foco) teclado_seguradas(mapa);
    memcpy((void *)pmapa, mapa, 32);
    return foco ? 1 : 0;
}

/* SIS_LER_TELA (&pixels, bytes) -> largura | altura << 16 (H87). Copia o
 * buffer de trás (a tela montada, sem o cursor do mouse) se "bytes" couber
 * largura * altura * 4; com bytes = 0 só diz o tamanho. Para a captura de
 * tela: qualquer programa pode ler a tela, como no X11 sem extensões. */
int32_t sis_ler_tela(uint32_t ppx, uint32_t n, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    if (!video_ativo()) return ERRO_NAO_EXISTE;
    uint32_t l = video_largura(), a = video_altura();
    int32_t tamanho = (int32_t)(l | a << 16);
    if (n == 0) return tamanho;
    if (n < l * a * 4) return ERRO_INVALIDO;
    if (!usuario_ok((void *)ppx, l * a * 4, true)) return ERRO_ENDERECO;
    bool i0 = int_salva_desliga();
    memcpy((void *)ppx, video_buffer(), l * a * 4);
    int_restaura(i0);
    return tamanho;
}

int32_t sis_titulo_janela(uint32_t id, uint32_t ptitulo, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    janela_t *j = janela_do_processo((int)id);
    if (!j) return ERRO_INVALIDO;
    int r = usuario_texto((const char *)ptitulo, j->titulo, sizeof j->titulo);
    if (r < 0) return r;
    gui_redesenhar(j->x, j->y, moldura_l(j), BARRA_TITULO);
    gui_mostrar();
    return 0;
}

/* Opções da janela (H78). Redimensionável: a memória da janela passa a ter
 * lugar para o maior tamanho possível (a tela entre a barra de cima e a
 * doca, limitada aos 4 MB que cada janela tem no espaço do programa). O
 * conteúdo atual é copiado; o endereço que o programa vê não muda. */
int32_t sis_janela_opcoes(uint32_t id, uint32_t opcoes, uint32_t minimos, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    janela_t *j = janela_do_processo((int)id);
    if (!j) return ERRO_INVALIDO;
    if (!(opcoes & JAN_REDIMENSIONAVEL) || j->redimensionavel) return 0;
    uint32_t max_px = (uint32_t)(video_largura() * video_altura());
    if (max_px > ESPACO_JANELA / 4) max_px = ESPACO_JANELA / 4;
    uint32_t paginas = (max_px * 4 + PAGINA - 1) / PAGINA;
    uint32_t fis = memfisica_alocar_seguidas(paginas);
    if (!fis) return ERRO_SEM_MEMORIA;
    uint32_t *novo = (uint32_t *)fis;
    for (uint32_t k = 0; k < (uint32_t)(j->larg * j->alt); k++) novo[k] = j->px[k];
    for (uint32_t k = 0; k < j->paginas; k++) espaco_desmapear(atual->espaco, j->virt + k * PAGINA);
    memfisica_liberar_seguidas((uint32_t)j->px, j->paginas);
    j->px = novo;
    j->paginas = paginas;
    for (uint32_t k = 0; k < paginas; k++)
        espaco_mapear(atual->espaco, j->virt + k * PAGINA, fis + k * PAGINA, PG_USUARIO | PG_ESCRITA | PG_EMPRESTADA);
    j->cap_px = max_px;
    j->redimensionavel = true;
    j->lmin = (int)(minimos & 0xFFFF) ? (int)(minimos & 0xFFFF) : 120;
    j->amin = (int)(minimos >> 16) ? (int)(minimos >> 16) : 80;
    redesenhar_janela(j);
    gui_mostrar();
    klog("[GUI] janela %d redimensionavel (ate %u pixels)\n", j->id, max_px);
    return 0;
}

/* SIS_MENU_JANELA (id, x | y << 16, "Item|Item|Item") -> 0. Abre um menu do
 * próprio programa no ponto (x, y) da janela (H81, do pull request #1 do
 * Robson); a escolha volta em EV_MENU com o índice do item em "tecla".
 * Clicar fora fecha o menu sem evento. */
int32_t sis_menu_janela(uint32_t id, uint32_t xy, uint32_t pitens, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    janela_t *j = janela_do_processo((int)id);
    if (!j) return ERRO_INVALIDO;
    char itens[CTX_MAX * 32];
    int r = usuario_texto((const char *)pitens, itens, sizeof itens);
    if (r < 0) return r;
    bool i0 = int_salva_desliga();
    if (ctx_aberto) redesenhar_ctx();
    ctx_novo(CTX_PROGRAMA, j->id);
    char *p = itens;
    for (int n = 0; *p && n < CTX_MAX; n++) {
        char *fim = p;
        while (*fim && *fim != '|') fim++;
        char guarda = *fim;
        *fim = 0;
        ctx_item(p, n);
        p = guarda ? fim + 1 : fim;
    }
    int x = j->x + BORDA + (int)(xy & 0xFFFF), y = j->y + BARRA_TITULO + (int)(xy >> 16);
    if (ctx_qtd) ctx_mostrar(x, y);
    int_restaura(i0);
    gui_mostrar();
    return 0;
}
