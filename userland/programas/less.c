/* less (paginar) - mostra um arquivo grande uma tela por vez.
 * less arquivo (ou: comando | less, que então só imprime, sem teclado).
 *
 * Teclas: espaço ou PgDn desce uma tela; b ou PgUp sobe; setas, j e k uma
 * linha; g ou Home vai ao começo; G ou End ao fim; /texto procura (n acha o
 * próximo); q ou Esc sai.
 *
 * Usa a tela alternativa do Terminal (ESC [ ? 1049 h), sem quebra de linha
 * (ESC [ ? 7 l), e pede o tamanho da janela (ESC [ ? 7727 h: o Terminal
 * responde ESC [ 8 ; linhas ; colunas t, e de novo quando ela muda). */
#include "sagui.h"

static char *dados;
static unsigned tam;
static unsigned *inicio;      /* começo de cada linha */
static int qtd;
static int linhas = 28, colunas = 80;
static int topo;
static char busca[64];
static char *tela;
static unsigned tela_cap, tela_n;

static void por(const char *s, unsigned n)
{
    if (tela_n + n > tela_cap) return;
    memcpy(tela + tela_n, s, n);
    tela_n += n;
}

static void por_t(const char *s) { por(s, (unsigned)tam_texto(s)); }

static int paginas(void) { return linhas - 1; }

static int topo_max(void)
{
    int m = qtd - paginas();
    return m < 0 ? 0 : m;
}

static void desenhar(const char *nome, const char *aviso)
{
    tela_n = 0;
    por_t("\x1b[H\x1b[J");
    for (int l = 0; l < paginas(); l++) {
        int i = topo + l;
        if (i < qtd) {
            unsigned a = inicio[i], b = i + 1 < qtd ? inicio[i + 1] - 1 : tam;
            if (b > a && dados[b - 1] == '\r') b--;
            unsigned n = b - a;
            if (n > (unsigned)colunas - 1) n = (unsigned)colunas - 1;
            for (unsigned k = 0; k < n; k++) {
                char c = dados[a + k];
                if (c == '\t') c = ' ';
                else if ((unsigned char)c < 32) c = '.';
                por(&c, 1);
            }
        } else {
            por_t("~");
        }
        por_t("\r\n");
    }
    char st[160];
    int fim = topo + paginas();
    if (fim > qtd) fim = qtd;
    int pct = qtd ? fim * 100 / qtd : 100;
    if (aviso) formatar_texto(st, sizeof st, "\x1b[7m %s \x1b[0m", aviso);
    else formatar_texto(st, sizeof st, "\x1b[7m %s  linhas %d-%d de %d  %d%%  (q sai, / procura) \x1b[0m",
                        nome, qtd ? topo + 1 : 0, fim, qtd, pct);
    por_t(st);
    escrever_tudo(SAIDA, tela, tela_n);
}

static bool linha_tem(int i, const char *t)
{
    unsigned a = inicio[i], b = i + 1 < qtd ? inicio[i + 1] : tam;
    size_t n = tam_texto(t);
    for (unsigned k = a; k + n <= b; k++)
        if (memcmp(dados + k, t, n) == 0) return true;
    return false;
}

static bool procurar(int de)
{
    for (int i = de; i < qtd; i++)
        if (linha_tem(i, busca)) { topo = i > topo_max() ? topo_max() : i; return true; }
    return false;
}

static int tecla(void)
{
    unsigned char c;
    if (ler(ENTRADA, &c, 1) <= 0) return -1;
    return c;
}

/* Lê o que vem depois de ESC. Devolve a tecla equivalente ('j', 'k', ' ', 'b',
 * 'g', 'G', 'q') ou 0. A resposta de tamanho ESC [ 8 ; L ; C t muda o tamanho. */
static int depois_do_esc(void)
{
    int c = tecla();
    switch (c) {
    case 27: return 'q';
    case TECLA_B_CIMA: return 'k';
    case TECLA_B_BAIXO: return 'j';
    case TECLA_B_PGUP: return 'b';
    case TECLA_B_PGDN: return ' ';
    case TECLA_B_HOME: return 'g';
    case TECLA_B_END: return 'G';
    case '[': {
        int v[3] = { 0, 0, 0 }, k = 0;
        for (;;) {
            c = tecla();
            if (c < 0) return 0;
            if (c >= '0' && c <= '9') v[k] = v[k] * 10 + (c - '0');
            else if (c == ';') { if (k < 2) k++; }
            else break;
        }
        if (c == 't' && v[0] == 8 && v[1] > 2 && v[2] > 10) {
            linhas = v[1];
            colunas = v[2];
            return 'R';
        }
        return 0;
    }
    default: return 0;
    }
}

static void ler_busca(const char *nome)
{
    int n = 0;
    busca[0] = 0;
    for (;;) {
        char aviso[96];
        formatar_texto(aviso, sizeof aviso, "/%s", busca);
        desenhar(nome, aviso);
        int c = tecla();
        if (c < 0 || c == '\r' || c == '\n') break;
        if (c == 27) { tecla(); busca[0] = 0; return; }
        if ((c == 8 || c == 127) && n > 0) busca[--n] = 0;
        else if (c >= 32 && n < (int)sizeof busca - 1) { busca[n++] = (char)c; busca[n] = 0; }
    }
}

int principal(int argc, char **argv)
{
    if (argc > 2) { imprimir_em(ERROS, "uso: less [arquivo]\n"); return 1; }
    const char *nome = argc == 2 ? argv[1] : "(entrada)";
    int fd = ENTRADA;
    if (argc == 2) {
        fd = abrir(argv[1], ABRIR_LER);
        if (fd < 0) { imprimir_em(ERROS, "less: %s: %s\n", argv[1], nome_erro(fd)); return 1; }
    }
    unsigned cap = 8192;
    dados = aloca(cap);
    int r;
    while (dados) {
        if (tam == cap) { dados = realoca(dados, cap * 2); cap *= 2; if (!dados) break; }
        r = ler(fd, dados + tam, cap - tam);
        if (r <= 0) break;
        tam += (unsigned)r;
    }
    if (!dados) { imprimir_em(ERROS, "less: sem memória\n"); return 1; }
    if (argc < 2) {   /* veio de um pipe: o teclado não chega aqui, só mostra */
        escrever_tudo(SAIDA, dados, tam);
        return 0;
    }
    fechar(fd);
    qtd = 0;
    for (unsigned k = 0; k < tam; k++) if (dados[k] == '\n') qtd++;
    if (tam && dados[tam - 1] != '\n') qtd++;
    inicio = aloca((size_t)(qtd + 1) * sizeof *inicio);
    tela_cap = 64 * 1024;
    tela = aloca(tela_cap);
    if (!inicio || !tela) { imprimir_em(ERROS, "less: sem memória\n"); return 1; }
    int n = 0;
    if (tam) inicio[n++] = 0;
    for (unsigned k = 0; k + 1 < tam; k++) if (dados[k] == '\n') inicio[n++] = k + 1;
    qtd = n;

    escrever_texto(SAIDA, "\x1b[?1049h\x1b[?7l\x1b[?25l\x1b[?7727h");
    registrar("less: %s, %d linhas", nome, qtd);
    const char *aviso = NULL;
    char msg[96];
    for (;;) {
        desenhar(nome, aviso);
        aviso = NULL;
        int c = tecla();
        if (c < 0) break;
        if (c == 27) c = depois_do_esc();
        if (c == 'q' || c == 'Q') break;
        switch (c) {
        case ' ': case 'f': topo += paginas(); break;
        case 'b': topo -= paginas(); break;
        case 'j': case '\r': case '\n': topo++; break;
        case 'k': topo--; break;
        case 'g': topo = 0; break;
        case 'G': topo = topo_max(); break;
        case '/':
            ler_busca(nome);
            if (busca[0] && !procurar(topo + 1 < qtd ? topo : 0)) {
                formatar_texto(msg, sizeof msg, "não achei \"%s\"", busca);
                aviso = msg;
            }
            if (busca[0]) registrar("less: procurou \"%s\", topo %d", busca, topo + 1);
            break;
        case 'n':
            if (busca[0] && !procurar(topo + 1)) {
                formatar_texto(msg, sizeof msg, "não há mais \"%s\"", busca);
                aviso = msg;
            }
            break;
        default: break;
        }
        if (topo > topo_max()) topo = topo_max();
        if (topo < 0) topo = 0;
    }
    escrever_texto(SAIDA, "\x1b[?7727l\x1b[?25h\x1b[?7h\x1b[?1049l");
    registrar("less: saiu");
    return 0;
}
