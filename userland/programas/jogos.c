/* jogos - o lançador de jogos do SaguiOS (H92).
 *
 * O item "Jogos" do menu do sagui abre esta janela: uma grade com o ícone e
 * o nome de cada jogo. Um clique escolhe, duplo clique (ou Enter) abre; as
 * setas andam pela grade. Só aparecem os jogos que existem em /bin, então a
 * mesma tabela serve para uma imagem com menos programas.
 *
 * Os ícones são os mesmos da área de trabalho e da doca: o C gerado por
 * tools/mkarte.py também é ligado a este programa (só os vetores usados
 * ficam, graças ao --gc-sections), e g_imagem os desenha com transparência. */
#include "sagui.h"
#include "gui.h"

extern const uint32_t arte_px_banana[], arte_px_sagui3d[], arte_px_cacabananas[], arte_px_paciencia[],
                      arte_px_chip8[], arte_px_jclic[];

typedef struct {
    const char     *nome;
    const char     *descricao;
    const char     *programa;
    const uint32_t *icone;      /* 64x64 ARGB */
} jogo_t;

static const jogo_t todos[] = {
    { "Banana", "Pegue as bananas", "/bin/banana", arte_px_banana },
    { "Sagui 3D", "Labirinto em 3D", "/bin/sagui3d", arte_px_sagui3d },
    { "Caça-Bananas", "Ache as bananas-bomba", "/bin/cacabananas", arte_px_cacabananas },
    { "Paciência Banana", "Cartas em ordem", "/bin/paciencia", arte_px_paciencia },
    { "CHIP-8", "Jogos de 8 bits", "/bin/chip8", arte_px_chip8 },
    { "Atividades JClic", "Quebra-cabeças e mais", "/bin/jclic", arte_px_jclic },
};
#define QTD_TODOS (int)(sizeof todos / sizeof todos[0])

#define COLUNAS 3
#define CELULA_L 168
#define CELULA_A 124
#define MARGEM  12
#define TOPO    44
#define RODAPE  28

static jan_t *j;
static const jogo_t *jogos[QTD_TODOS];
static int qtd, sel;

static int linhas(void) { return (qtd + COLUNAS - 1) / COLUNAS; }
static int cel_x(int i) { return MARGEM + (i % COLUNAS) * CELULA_L; }
static int cel_y(int i) { return TOPO + 4 + (i / COLUNAS) * CELULA_A; }

static void texto_centro(int cx, int y, const char *t, uint32_t cor)
{
    g_texto(j, cx - g_largura_texto(t) / 2, y, t, cor);
}

static void desenhar_celula(int i)
{
    int x = cel_x(i), y = cel_y(i);
    g_retangulo(j, x, y, CELULA_L - 8, CELULA_A - 8, i == sel ? 0xFFF3B0 : COR_FUNDO);
    if (i == sel) g_moldura(j, x, y, CELULA_L - 8, CELULA_A - 8, 0xE8A812);
    int cx = x + (CELULA_L - 8) / 2;
    g_imagem(j, cx - 32, y + 8, jogos[i]->icone, 64, 64);
    texto_centro(cx, y + 76, jogos[i]->nome, COR_TEXTO);
    texto_centro(cx, y + 94, jogos[i]->descricao, COR_TEXTO_FRACO);
}

static void desenhar(void)
{
    g_retangulo(j, 0, 0, j->larg, j->alt, COR_FUNDO);
    g_retangulo(j, 0, 0, j->larg, TOPO, COR_DESTAQUE);
    g_retangulo(j, 0, TOPO - 3, j->larg, 3, 0xFFD52E);      /* uma faixa amarelo-banana */
    g_texto(j, MARGEM, 14, "Jogos do SaguiOS", COR_BRANCO);
    for (int i = 0; i < qtd; i++) desenhar_celula(i);
    g_texto(j, MARGEM, j->alt - RODAPE + 6, "Duplo clique ou Enter para jogar.", COR_TEXTO_FRACO);
    jan_mostrar(j);
}

static void escolher(int i)
{
    if (i < 0 || i >= qtd || i == sel) return;
    int antes = sel;
    sel = i;
    desenhar_celula(antes);
    desenhar_celula(sel);
    jan_mostrar(j);
}

static void abrir_jogo(int i)
{
    if (i < 0 || i >= qtd) return;
    const char *prog = jogos[i]->programa;
    char *argv[2] = { (char *)acha_ultimo_char(prog, '/') + 1, NULL };
    int pid = executar(prog, argv);
    registrar("jogos: abrir %s (pid %d)", prog, pid);
    if (pid < 0) mensagem("Jogos", "Não consegui abrir o jogo.");
}

static int celula_em(int x, int y)
{
    if (y < TOPO + 4 || x < MARGEM) return -1;
    int c = (x - MARGEM) / CELULA_L, l = (y - TOPO - 4) / CELULA_A;
    if (c >= COLUNAS) return -1;
    int i = l * COLUNAS + c;
    return i < qtd ? i : -1;
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (int i = 0; i < QTD_TODOS; i++) {
        info_arquivo_t inf;
        if (info_arquivo(todos[i].programa, &inf) == 0) jogos[qtd++] = &todos[i];
    }
    j = jan_abrir("Jogos", 2 * MARGEM + COLUNAS * CELULA_L - 8, TOPO + 4 + linhas() * CELULA_A + RODAPE);
    if (!j) return 1;
    desenhar();
    registrar("jogos: pronto, %d jogos", qtd);

    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        int c;
        while (recolher(&c) > 0) ;   /* jogos abertos daqui que já terminaram */
        if (e.tipo == EV_FECHAR) break;
        if (e.tipo == EV_CLIQUE) {
            int i = celula_em(e.x, e.y);
            if (i < 0) continue;
            escolher(i);
            if (e.cliques >= 2) abrir_jogo(i);
        } else if (e.tipo == EV_TECLA) {
            if (e.tecla == TECLA_ESQUERDA) escolher(sel - 1);
            else if (e.tecla == TECLA_DIREITA) escolher(sel + 1);
            else if (e.tecla == TECLA_CIMA) escolher(sel - COLUNAS);
            else if (e.tecla == TECLA_BAIXO) escolher(sel + COLUNAS);
            else if (e.tecla == '\n' || e.tecla == ' ') abrir_jogo(sel);
            else if (e.tecla == TECLA_ESC) break;
        }
    }
    jan_fechar(j);
    return 0;
}
