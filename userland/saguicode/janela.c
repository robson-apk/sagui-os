/* janela.c - o Sagui Code numa janela (H89).
 *
 * A mesma lógica do Sagui Code do Terminal (o núcleo em agente.c), com uma
 * interface de janela:
 *   - à esquerda, as sessões (conversas guardadas), com "Nova sessão" e um
 *     menu do botão direito para renomear e apagar;
 *   - no meio, a conversa: os pedidos, as respostas e cada ferramenta usada
 *     num bloco compacto ("Lendo x.c" e o resultado numa linha); erros em
 *     vermelho;
 *   - embaixo, a caixa do pedido (Enter manda, Shift+Enter quebra a linha) e
 *     os botões Enviar e Parar;
 *   - em cima, o modelo, o gasto da conversa e o total, e um botão que abre a
 *     pasta dos projetos no Arquivos.
 *
 * A janela não congela: o agente anda um passo de cada vez (agente_passo) e,
 * entre um passo e outro, os eventos da janela são tratados. A chamada ao
 * modelo em si espera a resposta (o texto vai aparecendo enquanto chega);
 * antes dela, a linha de situação mostra "Pensando...". "Parar" faz o pedido
 * terminar antes do próximo passo.
 *
 * As sessões ficam no pendrive, em /usb/.saguios/saguicode/sessoes/NNNN.json,
 * no mesmo formato do /salvar do Terminal, com o título como primeiro campo
 * (a lista lê só o começo de cada arquivo). Sem pendrive, elas ficam só na
 * memória enquanto a janela estiver aberta, e a janela avisa.
 *
 * A tela usa Latin-1 e o modelo escreve em UTF-8: a conversão é feita aqui,
 * pela definição do UTF-8 (RFC 3629), trocando o que não existe no Latin-1
 * (aspas curvas, travessão, reticências) pelo equivalente mais próximo. */
#include "agente.h"
#include "gui.h"

#define TOPO_A       44      /* barra de cima */
#define LATERAL_L    214     /* coluna das sessões */
#define ENTRADA_A    72      /* caixa do pedido */
#define SITUACAO_A   22      /* linha "Pensando..." */
#define LH           17      /* altura de uma linha de texto na conversa */
#define BARRA_L      12      /* barra de rolagem da conversa */
#define MAX_SESSOES  128
#define PASTA_SESSOES "/usb/.saguios/saguicode/sessoes"

#define COR_TOPO      0xE6E9EC
#define COR_LATERAL   0xEDEFF2
#define COR_USUARIO   0xE3F2E8
#define COR_FERRAMENTA 0xF0F2F5
#define COR_FERR_BORDA 0xD3D9DF
#define COR_ERRO_FUNDO 0xFDECEA
#define COR_ERRO      0xB71C1C
#define COR_AVISO_FUNDO 0xFFF4D0
#define COR_AVISO     0x7A5A00
#define COR_AZUL      0x2A6FD8

static jan_t *j;
static agente_t ag;

/* ------------------------------------------------------------ UTF-8 -> Latin-1 */

/* Decodifica aos poucos: um caractere pode chegar partido entre dois pedaços
 * da resposta, então o estado fica guardado de uma chamada para outra. */
typedef struct { unsigned cp; int falta; } utf8_t;

typedef struct { char *d; unsigned n, cap; } texto_t;

static void tx_por(texto_t *t, const char *s, unsigned n)
{
    if (t->n + n + 1 > t->cap) {
        unsigned c = t->cap ? t->cap * 2 : 256;
        while (c < t->n + n + 1) c *= 2;
        char *x = realoca(t->d, c);
        if (!x) return;
        t->d = x;
        t->cap = c;
    }
    memcpy(t->d + t->n, s, n);
    t->n += n;
    t->d[t->n] = 0;
}

static void tx_texto(texto_t *t, const char *s) { tx_por(t, s, (unsigned)tam_texto(s)); }

/* Um caractere (já decodificado) no texto em Latin-1. */
static void por_caractere(texto_t *t, unsigned cp)
{
    char c;
    if (cp == '\r') return;
    if (cp == '\t') { tx_por(t, "    ", 4); return; }
    if (cp < 256 && (cp >= 32 || cp == '\n')) { c = (char)cp; tx_por(t, &c, 1); return; }
    const char *troca = "?";
    if (cp < 32) return;                                     /* outros controles: some */
    if (cp == 0x2018 || cp == 0x2019 || cp == 0x201A) troca = "'";
    else if (cp == 0x201C || cp == 0x201D || cp == 0x201E) troca = "\"";
    else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) troca = "-";
    else if (cp == 0x2026) troca = "...";
    else if (cp == 0x2022 || cp == 0x25CF) troca = "*";
    else if (cp == 0x2192) troca = "->";
    else if (cp == 0x2190) troca = "<-";
    else if (cp == 0x2713 || cp == 0x2714 || cp == 0x2705) troca = "ok";
    else if (cp == 0x00A0 || cp == 0x202F) troca = " ";
    tx_texto(t, troca);
}

static void de_utf8(utf8_t *u, texto_t *t, const char *s, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (u->falta) {
            if ((c & 0xC0) == 0x80) {
                u->cp = (u->cp << 6) | (c & 0x3F);
                if (--u->falta == 0) por_caractere(t, u->cp);
                continue;
            }
            u->falta = 0;                     /* sequência quebrada: marca e segue com este byte */
            tx_texto(t, "?");
        }
        if (c < 0x80) por_caractere(t, c);
        else if ((c & 0xE0) == 0xC0) { u->cp = c & 0x1F; u->falta = 1; }
        else if ((c & 0xF0) == 0xE0) { u->cp = c & 0x0F; u->falta = 2; }
        else if ((c & 0xF8) == 0xF0) { u->cp = c & 0x07; u->falta = 3; }
        else tx_texto(t, "?");
    }
}

/* ------------------------------------------------------------ a conversa na tela */

enum { B_USUARIO, B_ASSISTENTE, B_FERRAMENTA, B_ERRO, B_AVISO, B_INFO };
enum { F_RODANDO, F_OK, F_FALHOU };

typedef struct {
    int     tipo;
    texto_t t;                 /* Latin-1 */
    char    nome[24];          /* ferramenta */
    char    resumo[120];       /* ferramenta: a linha do resultado */
    int     estado;            /* ferramenta: F_* */
    int     y, a;              /* posição na conversa (calculada ao desenhar) */
} bloco_t;

static bloco_t *blocos;
static int qtd_blocos, cap_blocos;
static int rolagem;            /* pixels do começo da conversa escondidos em cima */
static bool seguir = true;     /* rolar sozinho para o fim quando chega texto novo */
static bool assistente_aberto; /* o último bloco é a resposta que está chegando */
static utf8_t dec_resposta;
static unsigned ultimo_desenho;

static bloco_t *novo_bloco(int tipo)
{
    if (qtd_blocos == cap_blocos) {
        int c = cap_blocos ? cap_blocos * 2 : 32;
        bloco_t *n = realoca(blocos, (size_t)c * sizeof(bloco_t));
        if (!n) return NULL;
        blocos = n;
        cap_blocos = c;
    }
    bloco_t *b = &blocos[qtd_blocos++];
    memset(b, 0, sizeof *b);
    b->tipo = tipo;
    return b;
}

static void limpar_blocos(void)
{
    for (int i = 0; i < qtd_blocos; i++) libera(blocos[i].t.d);
    qtd_blocos = 0;
    rolagem = 0;
    seguir = true;
    assistente_aberto = false;
}

static bloco_t *bloco_latin1(int tipo, const char *latin1)
{
    bloco_t *b = novo_bloco(tipo);
    if (b) tx_texto(&b->t, latin1);
    return b;
}

static bloco_t *bloco_utf8(int tipo, const char *utf8)
{
    bloco_t *b = novo_bloco(tipo);
    utf8_t u = { 0, 0 };
    if (b) de_utf8(&u, &b->t, utf8, (unsigned)tam_texto(utf8));
    return b;
}

/* As medidas da janela, recalculadas a cada desenho (ela muda de tamanho). */
static int conv_x, conv_y, conv_l, conv_a;

static void medidas(void)
{
    conv_x = LATERAL_L + 8;
    conv_y = TOPO_A + 8;
    conv_l = j->larg - conv_x - 8;
    conv_a = j->alt - conv_y - 8 - ENTRADA_A - 4 - SITUACAO_A;
}

/* A próxima linha de t[ini..n) que cabe em "cols" letras: devolve quantas
 * letras mostrar e põe em *prox onde começa a linha seguinte. Quebra no
 * último espaço que cabe; uma palavra maior que a linha é cortada. */
static int proxima_linha(const char *t, int ini, int n, int cols, int *prox)
{
    int fim = ini;
    while (fim < n && t[fim] != '\n' && fim - ini < cols) fim++;
    if (fim >= n || t[fim] == '\n') { *prox = fim + 1; return fim - ini; }
    int k = fim;
    while (k > ini && t[k] != ' ') k--;
    if (k == ini) { *prox = fim; return fim - ini; }
    *prox = k + 1;
    return k - ini;
}

static int contar_linhas(const char *t, int n, int cols)
{
    int linhas = 0, p = 0;
    do { int prox; proxima_linha(t, p, n, cols, &prox); linhas++; p = prox; } while (p < n);
    return linhas;
}

/* Desenho cortado na faixa visível da conversa. */
static int vis0, vis1;

static void ret_cortado(int x, int y, int l, int a, uint32_t cor)
{
    int y0 = y < vis0 ? vis0 : y, y1 = y + a > vis1 ? vis1 : y + a;
    if (y1 > y0) g_retangulo(j, x, y0, l, y1 - y0, cor);
}

static void texto_cortado(int x, int y, const char *t, int n, uint32_t cor)
{
    if (y >= vis0 && y + LETRA_A <= vis1) g_texto_n(j, x, y, t, n, cor);
}

static void linhas_desenhar(const char *t, int n, int x, int y, int cols, uint32_t cor)
{
    int p = 0;
    do {
        int prox, k = proxima_linha(t, p, n, cols, &prox);
        if (y >= vis1) return;
        texto_cortado(x, y, t + p, k, cor);
        y += LH;
        p = prox;
    } while (p < n);
}

/* Onde fica o texto de cada tipo de bloco (recuo e largura). */
static void caixa_do_bloco(const bloco_t *b, int *x, int *l)
{
    int x0 = conv_x + 10, l0 = conv_l - 20 - BARRA_L;
    if (b->tipo == B_FERRAMENTA) { x0 += 18; l0 -= 18; }
    *x = x0;
    *l = l0;
}

static int altura_do_bloco(const bloco_t *b)
{
    int x, l;
    caixa_do_bloco(b, &x, &l);
    int cols = (l - 20) / LETRA_L;
    if (cols < 8) cols = 8;
    int linhas = contar_linhas(b->t.d ? b->t.d : "", (int)b->t.n, cols);
    switch (b->tipo) {
    case B_USUARIO:    return LH + linhas * LH + 14;
    case B_ASSISTENTE: return LH + linhas * LH + 8;
    case B_FERRAMENTA: return 2 * LH + 10;
    case B_INFO:       return linhas * LH + 4;
    default:           return linhas * LH + 12;
    }
}

static int altura_total(void)
{
    int y = 8;
    for (int i = 0; i < qtd_blocos; i++) {
        blocos[i].y = y;
        blocos[i].a = altura_do_bloco(&blocos[i]);
        y += blocos[i].a + 8;
    }
    return y;
}

static void desenhar_bloco(const bloco_t *b, int sy)
{
    int x, l;
    caixa_do_bloco(b, &x, &l);
    int cols = (l - 20) / LETRA_L;
    if (cols < 8) cols = 8;
    const char *t = b->t.d ? b->t.d : "";
    int n = (int)b->t.n;
    switch (b->tipo) {
    case B_USUARIO:
        ret_cortado(x, sy, l, b->a, COR_USUARIO);
        ret_cortado(x, sy, 3, b->a, COR_DESTAQUE);
        texto_cortado(x + 10, sy + 6, "Você", 4, COR_DESTAQUE);
        linhas_desenhar(t, n, x + 10, sy + 6 + LH, cols, COR_TEXTO);
        break;
    case B_ASSISTENTE:
        ret_cortado(x, sy, 3, b->a, 0xF2B21C);
        texto_cortado(x + 10, sy + 2, "Sagui Code", 10, COR_TEXTO_FRACO);
        linhas_desenhar(t, n, x + 10, sy + 2 + LH, cols, COR_TEXTO);
        break;
    case B_FERRAMENTA: {
        ret_cortado(x, sy, l, b->a, COR_FERR_BORDA);
        ret_cortado(x + 1, sy + 1, l - 2, b->a - 2, COR_FERRAMENTA);
        uint32_t marca = b->estado == F_FALHOU ? COR_ERRO : b->estado == F_OK ? COR_DESTAQUE : COR_AZUL;
        ret_cortado(x + 8, sy + 8, 8, 8, marca);
        int k = n < cols - 2 ? n : cols - 2;
        texto_cortado(x + 24, sy + 4, t, k, COR_TEXTO);
        const char *r = b->estado == F_RODANDO ? "rodando..." : b->resumo;
        int kr = (int)tam_texto(r);
        if (kr > cols - 2) kr = cols - 2;
        texto_cortado(x + 24, sy + 4 + LH, r, kr, b->estado == F_FALHOU ? COR_ERRO : COR_TEXTO_FRACO);
        break;
    }
    case B_ERRO:
        ret_cortado(x, sy, l, b->a, COR_ERRO_FUNDO);
        linhas_desenhar(t, n, x + 10, sy + 6, cols, COR_ERRO);
        break;
    case B_AVISO:
        ret_cortado(x, sy, l, b->a, COR_AVISO_FUNDO);
        linhas_desenhar(t, n, x + 10, sy + 6, cols, COR_AVISO);
        break;
    default:
        linhas_desenhar(t, n, x + 10, sy + 2, cols, COR_TEXTO_FRACO);
        break;
    }
}

static int max_rolagem(int total)
{
    int m = total - (conv_a - 2);
    return m > 0 ? m : 0;
}

static void desenhar_conversa(void)
{
    medidas();
    int total = altura_total();
    if (seguir) rolagem = max_rolagem(total);
    if (rolagem > max_rolagem(total)) rolagem = max_rolagem(total);
    if (rolagem < 0) rolagem = 0;
    g_retangulo(j, conv_x, conv_y, conv_l, conv_a, COR_BRANCO);
    g_moldura(j, conv_x, conv_y, conv_l, conv_a, COR_BORDA);
    vis0 = conv_y + 1;
    vis1 = conv_y + conv_a - 1;
    for (int i = 0; i < qtd_blocos; i++) {
        int sy = conv_y + 1 + blocos[i].y - rolagem;
        if (sy + blocos[i].a < vis0) continue;
        if (sy >= vis1) break;
        desenhar_bloco(&blocos[i], sy);
    }
    /* Barra de rolagem: o "polegar" mostra que parte da conversa está à vista. */
    int bx = conv_x + conv_l - BARRA_L, trilho = conv_a - 4;
    g_retangulo(j, bx, conv_y + 1, BARRA_L - 1, conv_a - 2, 0xE4E7EA);
    int pol_a = total <= conv_a ? trilho : trilho * conv_a / total;
    if (pol_a < 14) pol_a = 14;
    int m = max_rolagem(total);
    int pol_y = m ? (trilho - pol_a) * rolagem / m : 0;
    g_retangulo(j, bx + 2, conv_y + 2 + pol_y, BARRA_L - 5, pol_a, 0x9AA3AC);
    jan_mostrar_area(j, conv_x, conv_y, conv_l, conv_a);
    ultimo_desenho = tempo_ms();
}

static void rolar(int delta)
{
    medidas();
    int total = altura_total();
    rolagem += delta;
    if (rolagem < 0) rolagem = 0;
    if (rolagem >= max_rolagem(total)) { rolagem = max_rolagem(total); seguir = true; }
    else seguir = false;
    desenhar_conversa();
}

/* O bloco sob o ponto (x, y) da janela, ou -1. */
static int bloco_em(int x, int y)
{
    if (x < conv_x || x >= conv_x + conv_l - BARRA_L || y < conv_y || y >= conv_y + conv_a) return -1;
    altura_total();
    int cy = y - conv_y - 1 + rolagem;
    for (int i = 0; i < qtd_blocos; i++) if (cy >= blocos[i].y && cy < blocos[i].y + blocos[i].a) return i;
    return -1;
}

/* ------------------------------------------------------------ barra de cima e situação */

static long gasto_total;       /* de todas as conversas, guardado nas configurações */
static botao_t b_projeto, b_nova, b_enviar, b_parar;
static char situacao[160];
static uint32_t cor_situacao = COR_TEXTO_FRACO;

static void desenhar_topo(void)
{
    g_retangulo(j, 0, 0, j->larg, TOPO_A, COR_TOPO);
    g_retangulo(j, 0, TOPO_A - 1, j->larg, 1, COR_BORDA);
    g_texto(j, 14, 14, "Sagui Code", COR_DESTAQUE);
    char d1[32], d2[32], gasto[96];
    agente_dinheiro(ag.gasto_conversa, d1, sizeof d1);
    agente_dinheiro(gasto_total, d2, sizeof d2);
    formatar_texto(gasto, sizeof gasto, "Conversa: %s   Total: %s", d1, d2);
    b_projeto = (botao_t){ j->larg - 142, 9, 130, 26, "Abrir projeto", false };
    int gx = b_projeto.x - 16 - g_largura_texto(gasto);
    g_texto(j, gx, 14, gasto, COR_TEXTO);
    /* O modelo ocupa o que sobrar entre o nome e o gasto. */
    char modelo[128];
    formatar_texto(modelo, sizeof modelo, "Modelo: %s", ag.ia.modelo);
    int cabe = (gx - 16 - 120) / LETRA_L;
    if (cabe > 0) g_texto_n(j, 120, 14, modelo, cabe, COR_TEXTO_FRACO);
    botao_desenhar(j, &b_projeto);
    jan_mostrar_area(j, 0, 0, j->larg, TOPO_A);
}

static void desenhar_situacao(void)
{
    medidas();
    int y = conv_y + conv_a + 2;
    g_retangulo(j, conv_x, y, conv_l, SITUACAO_A, COR_FUNDO);
    g_texto_n(j, conv_x + 4, y + 3, situacao, (conv_l - 8) / LETRA_L, cor_situacao);
    jan_mostrar_area(j, conv_x, y, conv_l, SITUACAO_A);
}

static void mudar_situacao(const char *t, uint32_t cor)
{
    copia_texto(situacao, t, sizeof situacao);
    cor_situacao = cor;
    desenhar_situacao();
}

/* ------------------------------------------------------------ o que o agente conta */

static void ui_pensando(void *ctx)
{
    (void)ctx;
    assistente_aberto = false;
    dec_resposta = (utf8_t){ 0, 0 };
    mudar_situacao("Pensando...", COR_DESTAQUE);
    desenhar_conversa();
}

static void ui_texto(void *ctx, const char *s, unsigned n)
{
    (void)ctx;
    if (!assistente_aberto) {
        if (!novo_bloco(B_ASSISTENTE)) return;
        assistente_aberto = true;
        mudar_situacao("Escrevendo...", COR_DESTAQUE);
    }
    de_utf8(&dec_resposta, &blocos[qtd_blocos - 1].t, s, n);
    /* Redesenha no máximo umas 8 vezes por segundo: a conversa inteira é
     * medida de novo a cada desenho. */
    if (tempo_ms() - ultimo_desenho > 120) desenhar_conversa();
}

static void ui_resposta(void *ctx, const char *utf8)
{
    (void)ctx;
    if (!assistente_aberto && utf8[0]) bloco_utf8(B_ASSISTENTE, utf8);
    assistente_aberto = false;
    desenhar_conversa();
    registrar("saguicode-janela: resposta mostrada (%u letras)", (unsigned)tam_texto(utf8));
}

static void ui_ferramenta(void *ctx, const char *nome, const char *args, const char *descricao)
{
    (void)ctx; (void)args;
    assistente_aberto = false;
    bloco_t *b = bloco_latin1(B_FERRAMENTA, descricao);
    if (b) { copia_texto(b->nome, nome, sizeof b->nome); b->estado = F_RODANDO; }
    char s[200];
    formatar_texto(s, sizeof s, "%s...", descricao);
    mudar_situacao(s, COR_AZUL);
    desenhar_conversa();
    registrar("saguicode-janela: ferramenta mostrada: %s", descricao);
}

static void ui_ferramenta_fim(void *ctx, const char *nome, bool ok, const char *resumo)
{
    (void)ctx;
    for (int i = qtd_blocos - 1; i >= 0; i--) {
        bloco_t *b = &blocos[i];
        if (b->tipo != B_FERRAMENTA || b->estado != F_RODANDO) continue;
        b->estado = ok ? F_OK : F_FALHOU;
        copia_texto(b->resumo, resumo, sizeof b->resumo);
        break;
    }
    desenhar_conversa();
    registrar("saguicode-janela: ferramenta %s terminou: %s", nome, resumo);
}

static void ui_aviso(void *ctx, int tipo, const char *t)
{
    (void)ctx;
    assistente_aberto = false;
    if (tipo == AV_CONTEXTO) bloco_latin1(B_INFO, "(a conversa ficou grande: os resultados antigos foram resumidos)");
    else if (tipo == AV_ERRO_API) {
        /* O erro pode trazer a mensagem do servidor, em UTF-8. */
        bloco_utf8(B_ERRO, t);
    } else bloco_latin1(tipo == AV_LIMITE_GASTO ? B_ERRO : B_AVISO, t);
    desenhar_conversa();
}

static bool ui_confirmar(void *ctx, const char *pergunta)
{
    (void)ctx;
    char p[400];
    formatar_texto(p, sizeof p, "O Sagui Code pede licença:\n\n%s", pergunta);
    bool sim = confirmar("Sagui Code", p);
    registrar("saguicode-janela: licenca %s", sim ? "dada" : "negada");
    return sim;
}

static void salvar_sessao(void);

static void ui_fim(void *ctx)
{
    (void)ctx;
    gasto_total += ag.gasto_pedido;
    char v[24];
    formatar_texto(v, sizeof v, "%ld", gasto_total);
    config_gravar("saguicode.gasto_total", v);
    desenhar_topo();
    char d[32], s[160];
    agente_dinheiro(ag.gasto_pedido, d, sizeof d);
    formatar_texto(s, sizeof s, "Pronto. Último pedido: entrada %d tokens (%d do cache), saída %d; %s.", ag.entrada, ag.cache, ag.saida, d);
    mudar_situacao(s, COR_TEXTO_FRACO);
    salvar_sessao();
}

/* ------------------------------------------------------------ sessões */

typedef struct {
    int      id;
    char     titulo[64];       /* Latin-1 */
    char    *json;             /* sem pendrive: a conversa guardada aqui */
    unsigned tam;
} sessao_t;

static sessao_t sessoes[MAX_SESSOES];
static int qtd_sessoes;
static char nomes_lista[MAX_SESSOES][64];
static lista_t lista;
static int id_atual;           /* 0 = conversa nova, ainda não guardada */

static void caminho_sessao(int id, char *c, int max)
{
    formatar_texto(c, (size_t)max, "%s/%04d.json", PASTA_SESSOES, id);
}

/* O título está no começo do arquivo: {"titulo":"...", ...}. Lê só ele
 * (com os escapes do JSON, RFC 8259), sem montar a conversa inteira. */
static void titulo_do_json(const char *s, unsigned n, char *titulo, int max)
{
    static const char chave[] = "{\"titulo\":\"";
    titulo[0] = 0;
    if (n < sizeof chave || memcmp(s, chave, sizeof chave - 1) != 0) return;
    texto_t t = { 0, 0, 0 };
    char bruto[256];
    int k = 0;
    for (unsigned i = sizeof chave - 1; i < n && s[i] != '"' && k < (int)sizeof bruto - 1; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            i++;
            bruto[k++] = s[i] == 'n' || s[i] == 't' ? ' ' : s[i];
        } else bruto[k++] = s[i];
    }
    utf8_t u = { 0, 0 };
    de_utf8(&u, &t, bruto, (unsigned)k);
    copia_texto(titulo, t.d ? t.d : "", (size_t)max);
    libera(t.d);
}

/* Troca o título no começo de um JSON de sessão (devolve memória nova). */
static char *trocar_titulo(const char *s, unsigned n, const char *novo, unsigned *tam)
{
    static const char chave[] = "{\"titulo\":\"";
    unsigned resto;
    if (n >= sizeof chave && memcmp(s, chave, sizeof chave - 1) == 0) {
        unsigned i = sizeof chave - 1;
        while (i < n && s[i] != '"') i += s[i] == '\\' ? 2 : 1;
        resto = i + 1;                          /* depois das aspas: ",..." */
    } else resto = 0;
    json_escritor_t e;                          /* o título com os escapes do JSON */
    je_iniciar(&e);
    je_texto_latin1(&e, novo);
    texto_t t = { 0, 0, 0 };
    tx_texto(&t, "{\"titulo\":");
    tx_por(&t, e.d ? e.d : "\"\"", e.d ? e.n : 2);
    je_liberar(&e);
    if (resto == 0) { tx_texto(&t, ","); resto = 1; }   /* não tinha título: entra antes do "modelo" */
    if (resto < n) tx_por(&t, s + resto, n - resto);
    *tam = t.n;
    return t.d;
}

static int ler_arquivo(const char *c, texto_t *t, unsigned max)
{
    int fd = abrir(c, ABRIR_LER);
    if (fd < 0) return fd;
    char buf[1024];
    int n;
    while (t->n < max && (n = ler(fd, buf, sizeof buf)) > 0) tx_por(t, buf, (unsigned)n);
    fechar(fd);
    return 0;
}

static int gravar_arquivo(const char *c, const char *d, unsigned n)
{
    int fd = abrir(c, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0) return fd;
    int r = escrever_tudo(fd, d, n);
    fechar(fd);
    return r < 0 ? r : 0;
}

static void montar_lista(void)
{
    /* As mais novas primeiro. */
    for (int a = 1; a < qtd_sessoes; a++)
        for (int b = a; b > 0 && sessoes[b].id > sessoes[b - 1].id; b--) {
            sessao_t x = sessoes[b]; sessoes[b] = sessoes[b - 1]; sessoes[b - 1] = x;
        }
    lista.sel = -1;
    for (int i = 0; i < qtd_sessoes; i++) {
        if (sessoes[i].titulo[0]) copia_texto(nomes_lista[i], sessoes[i].titulo, sizeof nomes_lista[i]);
        else formatar_texto(nomes_lista[i], sizeof nomes_lista[i], "Sessão %d", sessoes[i].id);
        if (sessoes[i].id == id_atual) lista.sel = i;
    }
    lista.qtd = qtd_sessoes;
}

static void carregar_sessoes(void)
{
    if (!ag.pendrive) return;
    qtd_sessoes = 0;
    entrada_pasta_t e;
    for (int i = 0; listar(PASTA_SESSOES, i, &e) == 1 && qtd_sessoes < MAX_SESSOES; i++) {
        int id = texto_para_int(e.nome);
        if (id <= 0 || e.tipo == TIPO_PASTA) continue;
        sessao_t *s = &sessoes[qtd_sessoes++];
        memset(s, 0, sizeof *s);
        s->id = id;
        char c[96];
        caminho_sessao(id, c, sizeof c);
        texto_t t = { 0, 0, 0 };
        if (ler_arquivo(c, &t, 512) == 0 && t.d) titulo_do_json(t.d, t.n, s->titulo, sizeof s->titulo);
        libera(t.d);
    }
    montar_lista();
}

static int sessao_por_id(int id)
{
    for (int i = 0; i < qtd_sessoes; i++) if (sessoes[i].id == id) return i;
    return -1;
}

static void desenhar_lateral(void)
{
    int alt = j->alt - TOPO_A;
    g_retangulo(j, 0, TOPO_A, LATERAL_L, alt, COR_LATERAL);
    g_retangulo(j, LATERAL_L - 1, TOPO_A, 1, alt, 0xD3D9DF);
    g_texto(j, 12, TOPO_A + 10, "Sessões", COR_TEXTO);
    b_nova = (botao_t){ 10, TOPO_A + 32, LATERAL_L - 20, 26, "Nova sessão", false };
    botao_desenhar(j, &b_nova);
    int rodape = ag.pendrive ? 10 : 50;
    lista.x = 8;
    lista.y = TOPO_A + 68;
    lista.l = LATERAL_L - 16;
    lista.a = j->alt - lista.y - rodape;
    lista_desenhar(j, &lista);
    if (!ag.pendrive) {
        g_texto(j, 12, j->alt - 44, "Sem pendrive: as", COR_AVISO);
        g_texto(j, 12, j->alt - 26, "sessões ficam só na memória.", COR_AVISO);
    }
    jan_mostrar_area(j, 0, TOPO_A, LATERAL_L, alt);
}

static void salvar_sessao(void)
{
    if (id_atual == 0) {
        int maior = 0;
        for (int i = 0; i < qtd_sessoes; i++) if (sessoes[i].id > maior) maior = sessoes[i].id;
        id_atual = maior + 1;
    }
    int i = sessao_por_id(id_atual);
    if (i < 0) {
        if (qtd_sessoes == MAX_SESSOES) return;
        i = qtd_sessoes++;
        memset(&sessoes[i], 0, sizeof sessoes[i]);
        sessoes[i].id = id_atual;
    }
    sessao_t *s = &sessoes[i];
    copia_texto(s->titulo, ag.titulo, sizeof s->titulo);
    int r;
    if (ag.pendrive) {
        char c[96];
        caminho_sessao(id_atual, c, sizeof c);
        r = agente_salvar(&ag, c);
    } else {
        libera(s->json);
        s->json = agente_para_json(&ag, &s->tam);
        r = s->json ? 0 : -1;
    }
    registrar("saguicode-janela: sessao %04d salva (%s, r=%d)", id_atual, ag.pendrive ? "pendrive" : "memoria", r);
    if (r < 0) { bloco_latin1(B_ERRO, "Não consegui guardar esta sessão."); desenhar_conversa(); }
    montar_lista();
    desenhar_lateral();
}

/* Primeira linha (ou a última, se for o fim de uma saída) do resultado de
 * uma ferramenta guardado na conversa, para o bloco ao reabrir a sessão. */
static void resumo_guardado(const char *nome, const char *latin1, char *resumo, int max, int *estado)
{
    *estado = F_OK;
    if (comeca_com(latin1, "Erro") || comeca_com(latin1, "O usuário não permitiu")) *estado = F_FALHOU;
    if (compara_texto(nome, "compilar") == 0) {
        bool ok = false;
        for (const char *p = latin1; *p; p++) if (comeca_com(p, "Compilou sem erros")) ok = true;
        *estado = ok ? F_OK : F_FALHOU;
        copia_texto(resumo, ok ? "compilou sem erros" : "erro de compilação", (size_t)max);
        return;
    }
    if (compara_texto(nome, "ler_arquivo") == 0 && *estado == F_OK) {
        formatar_texto(resumo, (size_t)max, "%u bytes lidos", (unsigned)tam_texto(latin1));
        return;
    }
    /* A última linha com texto (os comandos terminam em "(código de saída N)"). */
    const char *ini = latin1, *p = latin1;
    if (*estado == F_OK)
        for (; *p; p++) if (*p == '\n' && p[1] && p[1] != '\n') ini = p + 1;
    int n = 0;
    while (ini[n] && ini[n] != '\n' && n < max - 1) n++;
    memcpy(resumo, ini, (size_t)n);
    resumo[n] = 0;
}

/* Refaz os blocos da tela a partir da conversa do agente (ao abrir uma sessão). */
static int reconstruir_blocos(void)
{
    limpar_blocos();
    int pendente = qtd_blocos;          /* o próximo bloco de ferramenta sem resultado */
    for (int i = 1; i < ag.ia.qtd; i++) {
        ia_mensagem_t *m = &ag.ia.msgs[i];
        if (compara_texto(m->papel, "user") == 0) bloco_utf8(B_USUARIO, m->conteudo ? m->conteudo : "");
        else if (compara_texto(m->papel, "assistant") == 0) {
            if (m->conteudo && m->conteudo[0]) bloco_utf8(B_ASSISTENTE, m->conteudo);
            json_t *ch = m->chamadas ? json_ler(m->chamadas, (unsigned)tam_texto(m->chamadas), NULL, 0) : NULL;
            for (json_t *c = ch ? ch->filho : NULL; c; c = c->prox) {
                json_t *f = json_campo(c, "function");
                char d[160];
                agente_descrever(json_texto_de(f, "name"), json_texto_de(f, "arguments"), d, sizeof d);
                bloco_t *b = bloco_latin1(B_FERRAMENTA, d);
                if (b) { copia_texto(b->nome, json_texto_de(f, "name"), sizeof b->nome); b->estado = F_OK; }
            }
            json_liberar(ch);
        } else if (compara_texto(m->papel, "tool") == 0) {
            while (pendente < qtd_blocos && (blocos[pendente].tipo != B_FERRAMENTA || blocos[pendente].resumo[0])) pendente++;
            if (pendente >= qtd_blocos) continue;
            texto_t t = { 0, 0, 0 };
            utf8_t u = { 0, 0 };
            de_utf8(&u, &t, m->conteudo ? m->conteudo : "", m->conteudo ? (unsigned)tam_texto(m->conteudo) : 0);
            bloco_t *b = &blocos[pendente];
            resumo_guardado(b->nome, t.d ? t.d : "", b->resumo, sizeof b->resumo, &b->estado);
            if (!b->resumo[0]) copia_texto(b->resumo, "(sem saída)", sizeof b->resumo);
            libera(t.d);
        }
    }
    return qtd_blocos;
}

static void boas_vindas(void);

static void nova_sessao(void)
{
    agente_nova_conversa(&ag);
    ag.permitir_tudo = false;
    id_atual = 0;
    limpar_blocos();
    boas_vindas();
    montar_lista();
    desenhar_lateral();
    desenhar_topo();
    desenhar_conversa();
    mudar_situacao("Sessão nova.", COR_TEXTO_FRACO);
    registrar("saguicode-janela: sessao nova");
}

static void abrir_sessao(int i)
{
    if (i < 0 || i >= qtd_sessoes) return;
    sessao_t *s = &sessoes[i];
    char erro[96] = "";
    int n;
    if (ag.pendrive) {
        char c[96];
        caminho_sessao(s->id, c, sizeof c);
        n = agente_abrir(&ag, c, erro, sizeof erro);
    } else n = s->json ? agente_de_json(&ag, s->json, s->tam, erro, sizeof erro) : -1;
    if (n < 0) {
        char m[200];
        formatar_texto(m, sizeof m, "Não consegui abrir esta sessão.\n%s", erro);
        mensagem("Sagui Code", m);
        montar_lista();
        desenhar_lateral();
        return;
    }
    ag.permitir_tudo = false;
    id_atual = s->id;
    int nb = reconstruir_blocos();
    montar_lista();
    desenhar_lateral();
    desenhar_topo();
    desenhar_conversa();
    mudar_situacao("Sessão aberta. Continue a conversa quando quiser.", COR_TEXTO_FRACO);
    registrar("saguicode-janela: sessao %04d aberta (%d mensagens, %d blocos)", id_atual, n, nb);
}

static void renomear_sessao(int i)
{
    if (i < 0 || i >= qtd_sessoes) return;
    sessao_t *s = &sessoes[i];
    char novo[60];
    copia_texto(novo, s->titulo, sizeof novo);
    if (!pedir_texto("Renomear sessão", "Novo nome da sessão:", novo, sizeof novo) || !novo[0]) return;
    if (s->id == id_atual) {
        copia_texto(ag.titulo, novo, sizeof ag.titulo);
        salvar_sessao();
    } else if (ag.pendrive) {
        char c[96];
        caminho_sessao(s->id, c, sizeof c);
        texto_t t = { 0, 0, 0 };
        if (ler_arquivo(c, &t, 4 * 1024 * 1024) == 0 && t.d) {
            unsigned n;
            char *d = trocar_titulo(t.d, t.n, novo, &n);
            if (d) gravar_arquivo(c, d, n);
            libera(d);
        }
        libera(t.d);
    } else if (s->json) {
        unsigned n;
        char *d = trocar_titulo(s->json, s->tam, novo, &n);
        if (d) { libera(s->json); s->json = d; s->tam = n; }
    }
    copia_texto(s->titulo, novo, sizeof s->titulo);
    registrar("saguicode-janela: sessao %04d renomeada", s->id);
    montar_lista();
    desenhar_lateral();
}

static void apagar_sessao(int i)
{
    if (i < 0 || i >= qtd_sessoes) return;
    sessao_t *s = &sessoes[i];
    char p[160];
    formatar_texto(p, sizeof p, "Apagar a sessão\n\"%s\"?\nA conversa não volta mais.", nomes_lista[i]);
    if (!confirmar("Apagar sessão", p)) return;
    int id = s->id;
    if (ag.pendrive) {
        char c[96];
        caminho_sessao(id, c, sizeof c);
        apagar(c);
    }
    libera(s->json);
    memmove(&sessoes[i], &sessoes[i + 1], sizeof(sessao_t) * (size_t)(qtd_sessoes - i - 1));
    qtd_sessoes--;
    registrar("saguicode-janela: sessao %04d apagada", id);
    if (id == id_atual) nova_sessao();
    else { montar_lista(); desenhar_lateral(); }
}

/* ------------------------------------------------------------ pedido */

static area_t entrada;

static void boas_vindas(void)
{
    char t[300];
    formatar_texto(t, sizeof t, "Peça em português o que quer programar, por exemplo: \"faça um programa que mostra a tabuada do 7\". "
                   "Os arquivos ficam em %s%s.", ag.pasta_trabalho, ag.pendrive ? "" : " (sem pendrive: somem ao desligar)");
    bloco_latin1(B_INFO, t);
    if (!ag.ia.chave[0])
        bloco_latin1(B_AVISO, "Falta a chave do OpenRouter. Abra Configurações > IA, cole a sua chave e grave; "
                              "ela fica guardada no pendrive. Depois é só mandar o pedido aqui.");
}

static void desenhar_entrada(void)
{
    medidas();
    int y = j->alt - 8 - ENTRADA_A;
    entrada.x = conv_x;
    entrada.y = y;
    entrada.l = conv_l - 104;
    entrada.a = ENTRADA_A;
    area_desenhar(j, &entrada);
    b_enviar = (botao_t){ conv_x + conv_l - 96, y + 2, 96, 30, "Enviar", false };
    b_parar = (botao_t){ conv_x + conv_l - 96, y + ENTRADA_A - 32, 96, 30, "Parar", false };
    botao_desenhar(j, &b_enviar);
    botao_desenhar(j, &b_parar);
}

static void enviar(void)
{
    if (ag.ocupado) { mudar_situacao("Espere este pedido terminar (ou clique em Parar).", COR_AVISO); return; }
    /* O texto sem espaços nem linhas em branco nas pontas. */
    int ini = 0, fim = entrada.tam;
    while (ini < fim && (entrada.buf[ini] == ' ' || entrada.buf[ini] == '\n')) ini++;
    while (fim > ini && (entrada.buf[fim - 1] == ' ' || entrada.buf[fim - 1] == '\n')) fim--;
    if (fim == ini) return;
    if (!ag.ia.chave[0]) agente_ler_chave(&ag);       /* pode ter acabado de ser gravada */
    if (!ag.ia.chave[0]) {
        bloco_latin1(B_AVISO, "Ainda não há chave do OpenRouter. Abra Configurações > IA, cole a chave e grave; depois mande o pedido de novo.");
        seguir = true;
        desenhar_conversa();
        mudar_situacao("Falta a chave do OpenRouter.", COR_AVISO);
        registrar("saguicode-janela: sem chave");
        return;
    }
    char *p = aloca((size_t)(fim - ini) + 1);
    if (!p) return;
    memcpy(p, entrada.buf + ini, (size_t)(fim - ini));
    p[fim - ini] = 0;
    if (!ag.titulo[0]) {
        /* O título da sessão: o começo do primeiro pedido, numa linha. */
        int k = 0;
        for (; p[k] && k < 40; k++) ag.titulo[k] = p[k] == '\n' ? ' ' : p[k];
        ag.titulo[k] = 0;
        if (p[k]) junta_texto(ag.titulo, "...", sizeof ag.titulo);
    }
    /* Tira os avisos da sessão vazia: a conversa começa aqui. */
    if (ag.ia.qtd <= 1) limpar_blocos();
    bloco_latin1(B_USUARIO, p);
    area_definir(&entrada, "", 0);
    entrada.foco = true;
    desenhar_entrada();
    seguir = true;
    desenhar_conversa();
    registrar("saguicode-janela: pedido enviado (%u letras)", (unsigned)(fim - ini));
    agente_pedir(&ag, p);
    libera(p);
}

/* ------------------------------------------------------------ copiar */

static const char *rotulo_do_bloco(int tipo)
{
    return tipo == B_USUARIO ? "Você: " : tipo == B_ASSISTENTE ? "Sagui Code: " : tipo == B_FERRAMENTA ? "> " : "";
}

static void copiar_blocos(int de, int ate)
{
    texto_t t = { 0, 0, 0 };
    for (int i = de; i < ate; i++) {
        bloco_t *b = &blocos[i];
        if (i > de) tx_texto(&t, "\n\n");
        if (ate - de > 1) tx_texto(&t, rotulo_do_bloco(b->tipo));
        tx_por(&t, b->t.d ? b->t.d : "", b->t.n);
        if (b->tipo == B_FERRAMENTA) { tx_texto(&t, " - "); tx_texto(&t, b->resumo); }
    }
    area_copia_gravar(t.d ? t.d : "", t.n);
    registrar("saguicode-janela: copiou %u letras", t.n);
    mudar_situacao("Copiado. Cole com Ctrl+V.", COR_TEXTO_FRACO);
    libera(t.d);
}

/* ------------------------------------------------------------ janela */

static void desenhar_tudo(void)
{
    g_retangulo(j, 0, 0, j->larg, j->alt, COR_FUNDO);
    desenhar_topo();
    desenhar_lateral();
    desenhar_conversa();
    desenhar_situacao();
    desenhar_entrada();
    jan_mostrar(j);
}

static void abrir_projeto(void)
{
    char *argv[] = { "arquivos", ag.pasta_trabalho, NULL };
    int pid = executar("/bin/arquivos", argv);
    registrar("saguicode-janela: abrindo %s no Arquivos (pid %d)", ag.pasta_trabalho, pid);
    mudar_situacao(pid > 0 ? "Abrindo a pasta do projeto no Arquivos..." : "Não consegui abrir o Arquivos.", COR_TEXTO_FRACO);
}

enum { MENU_NENHUM, MENU_SESSAO, MENU_CONVERSA };

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;
    ag.ui = (agente_ui_t){
        .texto = ui_texto, .pensando = ui_pensando, .resposta = ui_resposta, .ferramenta = ui_ferramenta,
        .ferramenta_fim = ui_ferramenta_fim, .aviso = ui_aviso, .confirmar = ui_confirmar, .fim = ui_fim,
    };
    agente_iniciar(&ag);
    char v[32];
    if (config_ler("saguicode.gasto_total", v, sizeof v) > 0) gasto_total = texto_para_int(v);
    if (ag.pendrive) {
        criar_pasta("/usb/.saguios/saguicode");
        criar_pasta(PASTA_SESSOES);
    }
    /* 920x540: na tela de 1024x768, a caixa do pedido fica acima da doca. */
    j = jan_abrir("Sagui Code", 920, 540);
    if (!j) return 1;
    jan_redimensionavel(j, 660, 440);
    lista = (lista_t){ 8, TOPO_A + 68, LATERAL_L - 16, 100, nomes_lista, 0, -1, 0, NULL };
    if (!area_iniciar(&entrada, 0, 0, 100, ENTRADA_A)) return 1;
    entrada.foco = true;
    agente_nova_conversa(&ag);
    carregar_sessoes();
    boas_vindas();
    copia_texto(situacao, ag.ia.chave[0] ? "Pronto." : "Falta a chave do OpenRouter (Configurações > IA).", sizeof situacao);
    cor_situacao = ag.ia.chave[0] ? COR_TEXTO_FRACO : COR_AVISO;
    desenhar_tudo();
    registrar("saguicode-janela: pronta (modelo %s, %d sessoes, chave %s, %s)", ag.ia.modelo, qtd_sessoes,
              ag.ia.chave[0] ? "sim" : "nao", ag.pendrive ? "pendrive" : "sem pendrive");

    int menu = MENU_NENHUM, menu_alvo = -1;
    evento_t e;
    for (;;) {
        bool teve = false;
        while (proximo_evento(j->id, &e, false) == 1) {
            teve = true;
            int c;
            while (recolher(&c) > 0) ;          /* o Arquivos aberto daqui, quando fecha */
            if (e.tipo == EV_FECHAR) goto fim;
            if (e.tipo == EV_REDIMENSIONAR) { jan_novo_tamanho(j, &e); desenhar_tudo(); continue; }
            if (e.tipo == EV_MENU) {
                registrar("saguicode-janela: menu %u", e.tecla);
                if (menu == MENU_SESSAO && !ag.ocupado) {
                    if (e.tecla == 0) abrir_sessao(menu_alvo);
                    else if (e.tecla == 1) renomear_sessao(menu_alvo);
                    else if (e.tecla == 2) apagar_sessao(menu_alvo);
                } else if (menu == MENU_CONVERSA) {
                    if (e.tecla == 0 && menu_alvo >= 0 && menu_alvo < qtd_blocos) copiar_blocos(menu_alvo, menu_alvo + 1);
                    else if (e.tecla == 1 && qtd_blocos) copiar_blocos(0, qtd_blocos);
                }
                menu = MENU_NENHUM;
                continue;
            }
            if (botao_tratar(j, &b_projeto, &e)) abrir_projeto();
            if (botao_tratar(j, &b_nova, &e) && !ag.ocupado) nova_sessao();
            if (botao_tratar(j, &b_enviar, &e)) enviar();
            if (botao_tratar(j, &b_parar, &e) && ag.ocupado) {
                agente_parar(&ag);
                mudar_situacao("Parando depois deste passo...", COR_AVISO);
                registrar("saguicode-janela: parar pedido");
            }
            if (e.tipo == EV_TECLA) {
                if (e.tecla == '\n' && !(e.modificadores & 1)) { enviar(); continue; }
                if (e.tecla == TECLA_PGUP) { rolar(-(conv_a - 3 * LH)); continue; }
                if (e.tecla == TECLA_PGDN) { rolar(conv_a - 3 * LH); continue; }
                if (e.tecla == TECLA_ESC && ag.ocupado) { agente_parar(&ag); mudar_situacao("Parando depois deste passo...", COR_AVISO); continue; }
                if (!entrada.foco) entrada.foco = true;      /* a caixa do pedido é quem recebe o teclado */
                area_tratar(j, &entrada, &e);
                continue;
            }
            if (e.tipo == EV_CLIQUE) {
                /* Sessões: clique abre; botão direito, o menu. */
                if (e.x >= lista.x && e.x < lista.x + lista.l && e.y >= lista.y && e.y < lista.y + lista.a) {
                    int i = lista.topo + (e.y - lista.y - 2) / LISTA_ALTURA_ITEM;
                    if (i < 0 || i >= lista.qtd) continue;
                    if (e.botoes & 2) {
                        menu = MENU_SESSAO;
                        menu_alvo = i;
                        lista.sel = i;
                        lista_desenhar(j, &lista);
                        jan_menu(j, e.x, e.y, "Abrir|Renomear...|Apagar...");
                    } else if (ag.ocupado) {
                        mudar_situacao("Espere o pedido terminar para trocar de sessão.", COR_AVISO);
                    } else if (sessoes[i].id != id_atual) abrir_sessao(i);
                    continue;
                }
                /* Conversa: a barra de rolagem pagina; botão direito, copiar. */
                if (e.x >= conv_x + conv_l - BARRA_L && e.x < conv_x + conv_l && e.y >= conv_y && e.y < conv_y + conv_a) {
                    rolar(e.y < conv_y + conv_a / 2 ? -(conv_a - 3 * LH) : conv_a - 3 * LH);
                    continue;
                }
                int bi = bloco_em(e.x, e.y);
                if ((e.botoes & 2) && bi >= 0) {
                    menu = MENU_CONVERSA;
                    menu_alvo = bi;
                    jan_menu(j, e.x, e.y, "Copiar esta mensagem|Copiar a conversa inteira");
                    continue;
                }
                if (e.x >= entrada.x && e.x < entrada.x + entrada.l && e.y >= entrada.y && e.y < entrada.y + entrada.a)
                    area_tratar(j, &entrada, &e);
                continue;
            }
            area_tratar(j, &entrada, &e);       /* arrastar para selecionar e soltar */
        }
        if (ag.ocupado) {
            /* Um passo do agente (a chamada ao modelo e as ferramentas); os
             * eventos que chegarem enquanto isso esperam a próxima volta. */
            agente_passo(&ag);
            continue;
        }
        if (!teve) dormir(30);
    }
fim:
    agente_liberar(&ag);
    jan_fechar(j);
    return 0;
}
