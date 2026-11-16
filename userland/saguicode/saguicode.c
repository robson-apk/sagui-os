/* saguicode.c - o Sagui Code no Terminal (H70 a H72).
 *
 * Esta é só a interface de texto. O laço do agente, as ferramentas, o resumo
 * da conversa, a conta do gasto e as conversas guardadas ficam no núcleo
 * (agente.c, H89), que também serve à janela (janela.c). Aqui ficam a
 * leitura do teclado, as cores do Terminal, as perguntas de licença
 * ("(s)im, (n)ão, (t)udo") e os comandos que começam com barra.
 *
 * A chave fica em /usb/.saguios/chaves.txt (linha "openrouter = ...") e nunca
 * vai para a tela, o log ou as conversas salvas. */
#include "agente.h"

#define MAX_LINHA      2000

static agente_t ag;

/* ------------------------------------------------------------ tela */

static void cor(const char *c) { escrever_texto(SAIDA, c); }
#define CINZA    "\x1b[90m"
#define VERDE    "\x1b[32m"
#define AMARELO  "\x1b[33m"
#define VERMELHO "\x1b[31m"
#define AZUL     "\x1b[36m"
#define NEGRITO  "\x1b[1m"
#define NORMAL   "\x1b[0m"

/* Lê uma linha do teclado (modo bruto do Terminal), com Backspace e o
 * histórico nas setas para cima e para baixo. */
static char historico[16][MAX_LINHA];
static int hist_qtd;

static int ler_linha_teclado(const char *prompt, char *buf, int max)
{
    escrever_texto(SAIDA, prompt);
    int n = 0, h = hist_qtd;
    buf[0] = 0;
    for (;;) {
        char c;
        if (ler(ENTRADA, &c, 1) <= 0) return -1;
        if (c == '\n' || c == '\r') break;
        if (c == 3) { escrever_texto(SAIDA, "^C\n"); buf[0] = 0; return 0; }
        if (c == 4 && n == 0) return -1;                       /* Ctrl+D */
        if (c == 8 || c == 127) { if (n > 0) { n--; escrever_texto(SAIDA, "\b \b"); } continue; }
        if (c == 27) {
            char l;
            if (ler(ENTRADA, &l, 1) <= 0) return -1;
            if ((l == TECLA_B_CIMA && h > 0) || (l == TECLA_B_BAIXO && h < hist_qtd)) {
                h += l == TECLA_B_CIMA ? -1 : 1;
                while (n > 0) { escrever_texto(SAIDA, "\b \b"); n--; }
                if (h < hist_qtd) { copia_texto(buf, historico[h % 16], (size_t)max); n = (int)tam_texto(buf); escrever_tudo(SAIDA, buf, (unsigned)n); }
            }
            continue;
        }
        if ((unsigned char)c < 32) continue;
        if (n < max - 1) { buf[n++] = c; escrever(SAIDA, &c, 1); }
    }
    buf[n] = 0;
    escrever_texto(SAIDA, "\n");
    if (n > 0) { copia_texto(historico[hist_qtd % 16], buf, MAX_LINHA); hist_qtd++; }
    return n;
}

static void mostrar_dinheiro(long micro)
{
    char d[32];
    agente_dinheiro(micro, d, sizeof d);
    imprimir("%s", d);
}

/* ------------------------------------------------------------ o que o agente conta */

static void ui_texto(void *ctx, const char *s, unsigned n)
{
    (void)ctx;
    escrever_tudo(SAIDA, s, n);     /* o Terminal entende UTF-8 */
}

static void ui_pensando(void *ctx)
{
    (void)ctx;
    imprimir("\n");
    cor(NORMAL);
}

static void ui_resposta(void *ctx, const char *utf8)
{
    (void)ctx; (void)utf8;          /* o texto já apareceu aos pedaços */
    imprimir("\n");
}

static void ui_ferramenta(void *ctx, const char *nome, const char *args, const char *descricao)
{
    (void)ctx; (void)descricao;
    imprimir(AZUL "\n> %s" CINZA " %s\n" NORMAL, nome, args);
}

static void ui_nota(void *ctx, int tipo, const char *t)
{
    (void)ctx;
    imprimir("%s  %s\n" NORMAL, tipo == NOTA_OK ? VERDE : tipo == NOTA_ERRO ? VERMELHO : CINZA, t);
}

static void ui_aviso(void *ctx, int tipo, const char *t)
{
    (void)ctx;
    if (tipo == AV_LIMITE_GASTO) {
        imprimir(VERMELHO "\nLimite de gasto da sessão atingido (" NORMAL);
        mostrar_dinheiro(ag.limite_micro);
        imprimir(VERMELHO "). Use /limite <dólares> para aumentar.\n" NORMAL);
    } else if (tipo == AV_ERRO_API) imprimir(VERMELHO "\n%s\n" NORMAL, t);
    else if (tipo == AV_CONTEXTO) imprimir(CINZA "%s\n" NORMAL, t);
    else imprimir(AMARELO "\n%s\n" NORMAL, t);
}

static bool ui_confirmar(void *ctx, const char *pergunta)
{
    (void)ctx;
    char p[360], r[8];
    formatar_texto(p, sizeof p, "  %s (s)im, (n)ão, (t)udo nesta sessão: ", pergunta);
    cor(AMARELO);
    int n = ler_linha_teclado(p, r, sizeof r);
    cor(NORMAL);
    if (n > 0 && (r[0] == 't' || r[0] == 'T')) { ag.permitir_tudo = true; return true; }
    return n > 0 && (r[0] == 's' || r[0] == 'S');
}

static void ui_fim(void *ctx)
{
    (void)ctx;
    imprimir(CINZA "[entrada %d tokens (%d do cache), saída %d; este pedido ", ag.entrada, ag.cache, ag.saida);
    mostrar_dinheiro(ag.gasto_pedido);
    imprimir(", sessão ");
    mostrar_dinheiro(ag.gasto_sessao);
    imprimir("]\n" NORMAL);
}

static void pedido(const char *texto_latin1)
{
    agente_pedir(&ag, texto_latin1);
    while (agente_passo(&ag)) ;
}

/* ------------------------------------------------------------ conversas salvas */

static void salvar_conversa(const char *nome)
{
    char caminho[128];
    formatar_texto(caminho, sizeof caminho, "%s/conversas", ag.pasta_config);
    criar_pasta(caminho);
    formatar_texto(caminho, sizeof caminho, "%s/conversas/%s.json", ag.pasta_config, nome);
    if (agente_salvar(&ag, caminho) < 0) imprimir(VERMELHO "Não consegui gravar %s.\n" NORMAL, caminho);
    else imprimir("Conversa salva em %s.\n", caminho);
}

static void abrir_conversa(const char *nome)
{
    char caminho[128], erro[96];
    formatar_texto(caminho, sizeof caminho, "%s/conversas/%s.json", ag.pasta_config, nome);
    int n = agente_abrir(&ag, caminho, erro, sizeof erro);
    if (n == -1) imprimir(VERMELHO "Não achei %s.\n" NORMAL, caminho);
    else if (n < 0) imprimir(VERMELHO "Conversa estragada: %s\n" NORMAL, erro);
    else imprimir("Conversa %s aberta (%d mensagens).\n", nome, n);
}

/* ------------------------------------------------------------ comandos */

static void ajuda(void)
{
    imprimir("Comandos:\n"
             "  /modelo [nome]     mostra ou troca o modelo (ex.: /modelo anthropic/claude-sonnet-4.5)\n"
             "  /gasto             tokens e dinheiro gastos nesta sessão\n"
             "  /limite <dólares>  muda o limite de gasto da sessão (ex.: /limite 2)\n"
             "  /limpar            começa uma conversa nova\n"
             "  /salvar <nome>     guarda a conversa no pendrive\n"
             "  /abrir <nome>      abre uma conversa guardada\n"
             "  /sair              sai do Sagui Code\n"
             "Qualquer outra coisa é um pedido para a IA.\n");
}

int principal(int argc, char **argv)
{
    ag.ui = (agente_ui_t){
        .texto = ui_texto, .pensando = ui_pensando, .resposta = ui_resposta, .ferramenta = ui_ferramenta,
        .nota = ui_nota, .aviso = ui_aviso, .confirmar = ui_confirmar, .fim = ui_fim,
    };
    agente_iniciar(&ag);
    imprimir(NEGRITO VERDE "Sagui Code" NORMAL " - agente de programação do SaguiOS. Modelo: %s. /ajuda para os comandos.\n", ag.ia.modelo);
    imprimir(CINZA "Pasta de trabalho: %s%s\n" NORMAL, ag.pasta_trabalho, ag.pendrive ? "" : " (sem pendrive: some ao desligar)");
    if (!ag.ia.chave[0]) {
        imprimir(AMARELO "Não achei a chave do OpenRouter em %s/chaves.txt (linha \"openrouter = sk-or-...\").\n" NORMAL, ag.pasta_config);
        char chave[200];
        imprimir("Cole a chave aqui (não aparece na tela; fica só até sair): ");
        int n = 0;
        for (;;) {
            char c;
            if (ler(ENTRADA, &c, 1) <= 0) return 1;
            if (c == '\n' || c == '\r') break;
            if (c == 8 || c == 127) { if (n) n--; continue; }
            if ((unsigned char)c >= 32 && n < (int)sizeof chave - 1) chave[n++] = c;
        }
        chave[n] = 0;
        imprimir("\n");
        copia_texto(ag.ia.chave, chave, sizeof ag.ia.chave);
        memset(chave, 0, sizeof chave);
        if (!ag.ia.chave[0]) return 1;
    }
    registrar("saguicode: iniciado, modelo %s, servidor %s", ag.ia.modelo, ag.ia.servidor);
    agente_nova_conversa(&ag);
    /* Um pedido direto na linha de comando: saguicode "faça um relógio" */
    if (argc > 1) {
        char p[MAX_LINHA] = "";
        for (int i = 1; i < argc; i++) { if (i > 1) junta_texto(p, " ", sizeof p); junta_texto(p, argv[i], sizeof p); }
        imprimir(NEGRITO "> " NORMAL "%s\n", p);
        pedido(p);
    }
    char linha[MAX_LINHA];
    for (;;) {
        int n = ler_linha_teclado(NEGRITO VERDE "\n> " NORMAL, linha, sizeof linha);
        if (n < 0) break;
        if (n == 0) continue;
        if (linha[0] != '/') { pedido(linha); continue; }
        char *resto = acha_char(linha, ' ');
        if (resto) { *resto++ = 0; while (*resto == ' ') resto++; }
        if (compara_texto(linha, "/sair") == 0) break;
        else if (compara_texto(linha, "/ajuda") == 0) ajuda();
        else if (compara_texto(linha, "/limpar") == 0) { agente_nova_conversa(&ag); ag.permitir_tudo = false; imprimir("Conversa nova.\n"); }
        else if (compara_texto(linha, "/modelo") == 0) {
            if (resto && *resto) { copia_texto(ag.ia.modelo, resto, sizeof ag.ia.modelo); config_gravar("ia.modelo", ag.ia.modelo); }
            imprimir("Modelo: %s\n", ag.ia.modelo);
        } else if (compara_texto(linha, "/gasto") == 0) {
            imprimir("Sessão: entrada %d tokens, saída %d tokens, ", ag.tokens_sessao_entrada, ag.tokens_sessao_saida);
            mostrar_dinheiro(ag.gasto_sessao);
            imprimir(" de ");
            mostrar_dinheiro(ag.limite_micro);
            imprimir(". Conversa atual: ~%d tokens (limite %d).\n", agente_estimar_tokens(&ag), ag.max_contexto);
        } else if (compara_texto(linha, "/limite") == 0 && resto && *resto) {
            /* "2" ou "0,50" ou "0.50" dólares */
            long inteiro = 0, cent = 0;
            const char *p = resto;
            while (*p >= '0' && *p <= '9') inteiro = inteiro * 10 + (*p++ - '0');
            if (*p == ',' || *p == '.') { p++; if (*p >= '0' && *p <= '9') cent = (*p++ - '0') * 10; if (*p >= '0' && *p <= '9') cent += *p - '0'; }
            ag.limite_micro = inteiro * 1000000 + cent * 10000;
            imprimir("Limite da sessão: ");
            mostrar_dinheiro(ag.limite_micro);
            imprimir("\n");
        } else if (compara_texto(linha, "/salvar") == 0 && resto && *resto) salvar_conversa(resto);
        else if (compara_texto(linha, "/abrir") == 0 && resto && *resto) abrir_conversa(resto);
        else imprimir("Comando desconhecido. /ajuda mostra a lista.\n");
    }
    agente_liberar(&ag);
    imprimir("Até logo!\n");
    return 0;
}
