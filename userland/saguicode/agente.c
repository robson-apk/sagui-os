/* agente.c - o núcleo do Sagui Code (H70 a H72; separado da interface no H89).
 *
 * O laço do agente:
 *   1. o pedido do usuário entra na conversa (agente_pedir);
 *   2. a conversa vai para o modelo (ia.c), e a resposta chega aos pedaços
 *      para a interface mostrar enquanto é gerada;
 *   3. se o modelo pediu ferramentas, cada uma é executada aqui (pedindo
 *      licença à interface antes de sobrescrever arquivos ou rodar
 *      programas) e o resultado volta para a conversa; volta ao passo 2;
 *   4. sem pedidos de ferramenta, o pedido acabou: a interface mostra o gasto.
 * Cada volta (2 e 3) é um agente_passo. Há um limite de passos por pedido e
 * um limite de gasto por sessão.
 *
 * As ferramentas são operações do próprio SaguiOS: arquivos, o shell
 * (sagsh -c), o compilador (cc) e os programas. Os arquivos do SaguiOS são em
 * Latin-1; a conversa é em UTF-8: a conversão é feita na entrada e na saída de
 * cada ferramenta.
 *
 * Contexto: o texto de instruções, o manual do C do Sagui e os cabeçalhos da
 * libsagui vão na primeira mensagem (marcada para o cache de prompt). Quando
 * a conversa passa do limite configurado, os resultados antigos de
 * ferramentas viram um resumo de uma linha e, se ainda não bastar, os pedidos
 * mais antigos saem inteiros.
 *
 * A chave fica em /usb/.saguios/chaves.txt (linha "openrouter = ...") e nunca
 * vai para a tela, o log ou as conversas salvas. */
#include "agente.h"

#define MAX_RESULTADO  12000      /* o que uma ferramenta devolve ao modelo, no máximo */

/* ------------------------------------------------------------ texto */

char *agente_utf8(const char *s, unsigned n)
{
    char *d = aloca(2 * n + 1);
    if (!d) return NULL;
    unsigned k = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) d[k++] = (char)c;
        else { d[k++] = (char)(0xC0 | (c >> 6)); d[k++] = (char)(0x80 | (c & 63)); }
    }
    d[k] = 0;
    return d;
}

unsigned agente_latin1(char *s)
{
    size_t sobra = 0;
    unsigned n = (unsigned)utf8_para_latin1(s, tam_texto(s), &sobra);
    s[n] = 0;                    /* a conversão encurta o texto e não põe o zero no fim */
    return n;
}

typedef struct { char *d; unsigned n, cap; } texto_t;

static void tx_por(texto_t *t, const char *s, unsigned n)
{
    if (t->n + n + 1 > t->cap) {
        unsigned c = t->cap ? t->cap * 2 : 1024;
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

static void tx_formatar(texto_t *t, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = formatar_lista(buf, sizeof buf, fmt, ap);
    va_end(ap);
    tx_por(t, buf, (unsigned)(n < (int)sizeof buf ? n : (int)sizeof buf - 1));
}

void agente_dinheiro(long micro, char *saida, int max)
{
    /* US$ com 4 casas: 1234 micro = 0,0012 */
    long dec = (micro + 50) / 100;           /* décimos de milésimo */
    formatar_texto(saida, (size_t)max, "US$ %ld,%04ld", dec / 10000, dec % 10000);
}

/* ------------------------------------------------------------ avisos para a interface */

static void nota(agente_t *ag, int tipo, const char *fmt, ...)
{
    if (!ag->ui.nota) return;
    char buf[300];
    va_list ap;
    va_start(ap, fmt);
    formatar_lista(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ag->ui.nota(ag->ui.ctx, tipo, buf);
}

static void aviso(agente_t *ag, int tipo, const char *fmt, ...)
{
    if (!ag->ui.aviso) return;
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    formatar_lista(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ag->ui.aviso(ag->ui.ctx, tipo, buf);
}

static bool licenca(agente_t *ag, const char *pergunta)
{
    if (ag->permitir_tudo) return true;
    return ag->ui.confirmar && ag->ui.confirmar(ag->ui.ctx, pergunta);
}

static void ao_texto(void *ctx, const char *s, unsigned n)
{
    agente_t *ag = ctx;
    if (ag->ui.texto) ag->ui.texto(ag->ui.ctx, s, n);
}

/* ------------------------------------------------------------ rodar programas */

/* Roda argv com a saída (e os erros) num pipe. Espera até "segundos";
 * depois disso, se matar_no_fim, encerra o programa; se não, deixa rodando
 * (programas com janela). Devolve o código de saída, ou -1000 se continua. */
static int rodar_capturando(const char *caminho, char *const argv[], int segundos, bool matar_no_fim, texto_t *saida)
{
    int p[2], e[2];
    if (criar_pipe(p) < 0) { tx_texto(saida, "(não consegui criar o pipe)"); return -1; }
    if (criar_pipe(e) < 0) { fechar(p[0]); fechar(p[1]); tx_texto(saida, "(não consegui criar o pipe)"); return -1; }
    int g0 = duplicar(ENTRADA, 14), g1 = duplicar(SAIDA, 15), g2 = duplicar(ERROS, 16);
    duplicar(e[0], ENTRADA);              /* a entrada é um pipe vazio que já acabou */
    duplicar(p[1], SAIDA);
    duplicar(p[1], ERROS);
    fechar(e[1]);
    int pid = executar(caminho, argv);
    duplicar(g0, ENTRADA);
    duplicar(g1, SAIDA);
    duplicar(g2, ERROS);
    fechar(g0); fechar(g1); fechar(g2);
    fechar(p[1]);
    fechar(e[0]);
    if (pid < 0) {
        fechar(p[0]);
        tx_formatar(saida, "(não consegui rodar %s: %s)", caminho, nome_erro(pid));
        return pid;
    }
    unsigned fim = tempo_ms() + (unsigned)segundos * 1000;
    int codigo = -1000;
    bool terminou = false;
    char buf[512];
    for (;;) {
        int d = disponivel(p[0]);
        if (d > 0) {
            int n = ler(p[0], buf, sizeof buf);
            if (n > 0 && saida->n < 200000) tx_por(saida, buf, (unsigned)n);
            continue;
        }
        if (d < 0 && terminou) break;            /* pipe quebrado: ninguém mais escreve */
        if (!terminou) {
            int c, r = recolher(&c);
            if (r == pid) { terminou = true; codigo = c; continue; }
        }
        if (d < 0) {                             /* fechou a saída mas ainda roda */
            if (!terminou) { int c; esperar(pid, &c); codigo = c; terminou = true; }
            break;
        }
        if ((int)(tempo_ms() - fim) >= 0) break;
        dormir(10);
    }
    fechar(p[0]);
    if (!terminou) {
        if (matar_no_fim) { matar(pid); int c; esperar(pid, &c); tx_formatar(saida, "\n(encerrado depois de %d s)", segundos); codigo = -999; }
        else tx_formatar(saida, "\n(o programa continua rodando, processo %d)", pid);
    }
    return codigo;
}

/* ------------------------------------------------------------ ferramentas */

static const char FERRAMENTAS[] =
    "["
    "{\"type\":\"function\",\"function\":{\"name\":\"ler_arquivo\",\"description\":\"Lê um arquivo de texto do SaguiOS.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"caminho\":{\"type\":\"string\"}},\"required\":[\"caminho\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"escrever_arquivo\",\"description\":\"Cria ou sobrescreve um arquivo de texto com o conteúdo dado.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"caminho\":{\"type\":\"string\"},\"conteudo\":{\"type\":\"string\"}},\"required\":[\"caminho\",\"conteudo\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"editar_arquivo\",\"description\":\"Troca um trecho de um arquivo por outro. O trecho antigo tem de aparecer exatamente uma vez.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"caminho\":{\"type\":\"string\"},\"trecho_antigo\":{\"type\":\"string\"},\"trecho_novo\":{\"type\":\"string\"}},\"required\":[\"caminho\",\"trecho_antigo\",\"trecho_novo\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"listar_pasta\",\"description\":\"Lista os arquivos e pastas de uma pasta.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"caminho\":{\"type\":\"string\"}},\"required\":[\"caminho\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"executar_comando\",\"description\":\"Roda uma linha de comando no shell do SaguiOS (sagsh) e devolve a saída. Pede licença ao usuário.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"comando\":{\"type\":\"string\"}},\"required\":[\"comando\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"compilar\",\"description\":\"Compila um programa em C do SaguiOS com o cc. Devolve as mensagens do compilador (erros com linha, coluna e dica).\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"arquivo\":{\"type\":\"string\"},\"saida\":{\"type\":\"string\",\"description\":\"nome do programa gerado\"}},\"required\":[\"arquivo\",\"saida\"]}}},"
    "{\"type\":\"function\",\"function\":{\"name\":\"rodar\",\"description\":\"Roda um programa e devolve o que ele escreveu nos primeiros segundos. Programas com janela continuam abertos. Pede licença ao usuário.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"programa\":{\"type\":\"string\"},\"argumentos\":{\"type\":\"string\"}},\"required\":[\"programa\"]}}}"
    "]";

/* Um argumento em Latin-1 (caminhos e conteúdo de arquivos), em memória nova. */
static char *arg_latin1(json_t *a, const char *nome)
{
    json_t *c = json_campo(a, nome);
    const char *s = json_texto(c);
    unsigned n = c && c->tipo == JSON_TEXTO ? c->tam : 0;
    char *d = aloca(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = 0;
    agente_latin1(d);
    return d;
}

static int ler_inteiro(const char *caminho, texto_t *t, unsigned max)
{
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) return fd;
    char buf[1024];
    int n;
    while ((n = ler(fd, buf, sizeof buf)) > 0 && t->n < max) tx_por(t, buf, (unsigned)n);
    fechar(fd);
    return 0;
}

static int gravar_inteiro(const char *caminho, const char *d, unsigned n)
{
    int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0) return fd;
    int r = escrever_tudo(fd, d, n);
    fechar(fd);
    return r < 0 ? r : 0;
}

/* Divide "a b 'c d'" em argv (no lugar). */
static int separar_argumentos(char *s, char **argv, int max)
{
    int n = 0;
    while (*s && n < max - 1) {
        while (*s == ' ') s++;
        if (!*s) break;
        char aspas = (*s == '"' || *s == '\'') ? *s++ : 0;
        argv[n++] = s;
        while (*s && (aspas ? *s != aspas : *s != ' ')) s++;
        if (*s) *s++ = 0;
    }
    argv[n] = NULL;
    return n;
}

static void caminho_programa(agente_t *ag, const char *nome, char *out, int max)
{
    info_arquivo_t i;
    if (acha_char(nome, '/')) { copia_texto(out, nome, (size_t)max); return; }
    formatar_texto(out, (size_t)max, "%s/%s", ag->pasta_trabalho, nome);
    if (info_arquivo(out, &i) >= 0) return;
    formatar_texto(out, (size_t)max, "/bin/%s", nome);
    if (info_arquivo(out, &i) >= 0) return;
    formatar_texto(out, (size_t)max, "/usb/programas/%s", nome);
}

/* Executa uma ferramenta; o resultado (Latin-1, vira UTF-8 depois) vai em
 * "res"; "ok" e "resumo" são para a interface mostrar numa linha. */
static void executar_ferramenta(agente_t *ag, const char *nome, const char *argumentos, texto_t *res, bool *ok, char *resumo, int max_resumo)
{
    char erro[96];
    *ok = true;
    resumo[0] = 0;
    json_t *a = json_ler(argumentos, (unsigned)tam_texto(argumentos), erro, sizeof erro);
    if (!a) {
        tx_formatar(res, "Erro: os argumentos não são um JSON válido (%s).", erro);
        *ok = false;
        copia_texto(resumo, "os argumentos vieram estragados", (size_t)max_resumo);
        return;
    }
    texto_t t = { 0, 0, 0 };
    if (compara_texto(nome, "ler_arquivo") == 0) {
        char *c = arg_latin1(a, "caminho");
        nota(ag, NOTA_INFO, "lendo %s", c);
        int r = ler_inteiro(c, &t, 48 * 1024);
        if (r < 0) {
            tx_formatar(res, "Erro: não consegui ler %s (%s).", c, nome_erro(r));
            *ok = false;
            formatar_texto(resumo, (size_t)max_resumo, "não consegui ler (%s)", nome_erro(r));
        } else {
            tx_texto(res, t.d ? t.d : "");
            if (t.n >= 48 * 1024) tx_texto(res, "\n[arquivo cortado em 48 KB]");
            formatar_texto(resumo, (size_t)max_resumo, "%u bytes lidos", t.n);
        }
        libera(c);
    } else if (compara_texto(nome, "escrever_arquivo") == 0 || compara_texto(nome, "editar_arquivo") == 0) {
        bool editar = nome[0] == 'e' && nome[1] == 'd';
        char *c = arg_latin1(a, "caminho");
        info_arquivo_t inf;
        bool existe = info_arquivo(c, &inf) >= 0;
        char *novo = NULL;
        unsigned tam_novo = 0;
        *ok = false;
        if (editar) {
            char *antigo = arg_latin1(a, "trecho_antigo"), *trocar = arg_latin1(a, "trecho_novo");
            if (ler_inteiro(c, &t, 512 * 1024) < 0 || !t.d) {
                tx_formatar(res, "Erro: não consegui ler %s.", c);
                copia_texto(resumo, "não consegui ler o arquivo", (size_t)max_resumo);
            } else {
                unsigned ta = (unsigned)tam_texto(antigo), vezes = 0;
                char *onde = NULL;
                for (unsigned i = 0; ta && i + ta <= t.n; i++) if (memcmp(t.d + i, antigo, ta) == 0) { if (!onde) onde = t.d + i; vezes++; }
                if (vezes != 1) {
                    tx_formatar(res, "Erro: o trecho antigo aparece %u vezes em %s (tem de ser exatamente 1). Leia o arquivo e mande um trecho maior.", vezes, c);
                    formatar_texto(resumo, (size_t)max_resumo, "o trecho aparece %u vezes (tem de ser 1)", vezes);
                } else {
                    unsigned tn = (unsigned)tam_texto(trocar), antes = (unsigned)(onde - t.d);
                    tam_novo = t.n - ta + tn;
                    novo = aloca(tam_novo + 1);
                    if (novo) {
                        memcpy(novo, t.d, antes);
                        memcpy(novo + antes, trocar, tn);
                        memcpy(novo + antes + tn, onde + ta, t.n - antes - ta);
                    }
                }
            }
            libera(antigo);
            libera(trocar);
        } else {
            novo = arg_latin1(a, "conteudo");
            tam_novo = novo ? (unsigned)tam_texto(novo) : 0;
        }
        if (novo) {
            char pergunta[200];
            formatar_texto(pergunta, sizeof pergunta, "%s %s (%u bytes)?", editar ? "Editar" : existe ? "Sobrescrever" : "Criar", c, tam_novo);
            bool pode = (!existe && !editar) ? true : licenca(ag, pergunta);
            if (!existe && !editar) nota(ag, NOTA_INFO, "criando %s (%u bytes)", c, tam_novo);
            if (!pode) {
                tx_texto(res, "O usuário não permitiu mudar o arquivo.");
                copia_texto(resumo, "sem licença: o arquivo não mudou", (size_t)max_resumo);
            } else {
                int r = gravar_inteiro(c, novo, tam_novo);
                if (r < 0) {
                    tx_formatar(res, "Erro: não consegui gravar %s (%s).", c, nome_erro(r));
                    formatar_texto(resumo, (size_t)max_resumo, "não consegui gravar (%s)", nome_erro(r));
                } else {
                    tx_formatar(res, "Arquivo %s %s (%u bytes).", c, editar ? "editado" : "gravado", tam_novo);
                    formatar_texto(resumo, (size_t)max_resumo, "%s (%u bytes)", editar ? "editado" : "gravado", tam_novo);
                    *ok = true;
                }
                registrar("saguicode: %s %s (%u bytes)", editar ? "editou" : "gravou", c, tam_novo);
            }
            libera(novo);
        }
        libera(c);
    } else if (compara_texto(nome, "listar_pasta") == 0) {
        char *c = arg_latin1(a, "caminho");
        entrada_pasta_t e;
        int i = 0;
        for (; listar(c[0] ? c : ".", i, &e) == 1; i++) {
            tx_formatar(res, "%s%s  %u\n", e.nome, e.tipo == TIPO_PASTA ? "/" : "", e.tamanho);
        }
        if (i == 0) tx_formatar(res, "(pasta vazia ou inexistente: %s)", c);
        formatar_texto(resumo, (size_t)max_resumo, i == 1 ? "%d item" : "%d itens", i);
        libera(c);
    } else if (compara_texto(nome, "executar_comando") == 0) {
        char *cmd = arg_latin1(a, "comando");
        char pergunta[300];
        formatar_texto(pergunta, sizeof pergunta, "Rodar \"%s\"?", cmd);
        if (!licenca(ag, pergunta)) {
            tx_texto(res, "O usuário não permitiu rodar o comando.");
            *ok = false;
            copia_texto(resumo, "sem licença: o comando não rodou", (size_t)max_resumo);
        } else {
            char *argv[] = { "sagsh", "-c", cmd, NULL };
            int cod = rodar_capturando("/bin/sagsh", argv, 30, true, &t);
            tx_texto(res, t.d ? t.d : "");
            tx_formatar(res, "\n(código de saída %d)", cod);
            *ok = cod == 0;
            formatar_texto(resumo, (size_t)max_resumo, "código de saída %d", cod);
            registrar("saguicode: comando %s (codigo %d)", cmd, cod);
        }
        libera(cmd);
    } else if (compara_texto(nome, "compilar") == 0) {
        char *arq = arg_latin1(a, "arquivo"), *sai = arg_latin1(a, "saida");
        nota(ag, NOTA_INFO, "cc %s -o %s", arq, sai);
        char *argv[] = { "cc", arq, "-o", sai, NULL };
        int cod = rodar_capturando("/bin/cc", argv, 60, true, &t);
        tx_texto(res, t.d ? t.d : "");
        if (cod == 0) tx_formatar(res, "\nCompilou sem erros: %s.", sai);
        else tx_formatar(res, "\nA compilação falhou (código %d).", cod);
        if (cod == 0) nota(ag, NOTA_OK, "compilou");
        else nota(ag, NOTA_ERRO, "erro de compilação");
        *ok = cod == 0;
        if (cod == 0) formatar_texto(resumo, (size_t)max_resumo, "compilou sem erros: %s", sai);
        else copia_texto(resumo, "erro de compilação", (size_t)max_resumo);
        registrar("saguicode: compilar %s -> %s (codigo %d)", arq, sai, cod);
        libera(arq);
        libera(sai);
    } else if (compara_texto(nome, "rodar") == 0) {
        char *prog = arg_latin1(a, "programa"), *args = arg_latin1(a, "argumentos");
        char pergunta[300];
        formatar_texto(pergunta, sizeof pergunta, "Rodar o programa %s %s?", prog, args ? args : "");
        if (!licenca(ag, pergunta)) {
            tx_texto(res, "O usuário não permitiu rodar o programa.");
            *ok = false;
            copia_texto(resumo, "sem licença: o programa não rodou", (size_t)max_resumo);
        } else {
            char caminho[128];
            caminho_programa(ag, prog, caminho, sizeof caminho);
            char *argv[16];
            argv[0] = prog;
            int n = args ? separar_argumentos(args, argv + 1, 15) : 0;
            argv[n + 1] = NULL;
            int cod = rodar_capturando(caminho, argv, 8, false, &t);
            tx_texto(res, t.d ? t.d : "");
            if (cod != -1000) tx_formatar(res, "\n(código de saída %d)", cod);
            *ok = cod == 0 || cod == -1000;
            if (cod == -1000) copia_texto(resumo, "continua rodando (programa com janela)", (size_t)max_resumo);
            else formatar_texto(resumo, (size_t)max_resumo, "código de saída %d", cod);
            registrar("saguicode: rodou %s (codigo %d)", caminho, cod);
        }
        libera(prog);
        libera(args);
    } else {
        tx_formatar(res, "Erro: não existe a ferramenta %s.", nome);
        *ok = false;
        copia_texto(resumo, "ferramenta desconhecida", (size_t)max_resumo);
    }
    libera(t.d);
    json_liberar(a);
    if (res->n > MAX_RESULTADO) {
        res->n = MAX_RESULTADO;
        res->d[res->n] = 0;
        tx_texto(res, "\n[resultado cortado]");
    }
}

void agente_descrever(const char *nome, const char *args_utf8, char *saida, int max)
{
    static const struct { const char *nome, *verbo, *campo, *campo2; } tabela[] = {
        { "ler_arquivo", "Lendo", "caminho", NULL },
        { "escrever_arquivo", "Escrevendo", "caminho", NULL },
        { "editar_arquivo", "Editando", "caminho", NULL },
        { "listar_pasta", "Listando", "caminho", NULL },
        { "executar_comando", "Rodando o comando", "comando", NULL },
        { "compilar", "Compilando", "arquivo", NULL },
        { "rodar", "Rodando", "programa", "argumentos" },
    };
    json_t *a = args_utf8 ? json_ler(args_utf8, (unsigned)tam_texto(args_utf8), NULL, 0) : NULL;
    formatar_texto(saida, (size_t)max, "Usando %s", nome);
    for (unsigned i = 0; i < sizeof tabela / sizeof tabela[0]; i++) {
        if (compara_texto(nome, tabela[i].nome) != 0) continue;
        char *v = a ? arg_latin1(a, tabela[i].campo) : NULL;
        char *v2 = a && tabela[i].campo2 ? arg_latin1(a, tabela[i].campo2) : NULL;
        formatar_texto(saida, (size_t)max, "%s %s%s%s", tabela[i].verbo, v && v[0] ? v : ".", v2 && v2[0] ? " " : "", v2 ? v2 : "");
        libera(v);
        libera(v2);
    }
    for (char *p = saida; *p; p++) if (*p == '\n' || *p == '\t') *p = ' ';
    json_liberar(a);
}

/* ------------------------------------------------------------ contexto */

int agente_estimar_tokens(agente_t *ag)
{
    unsigned letras = 0;
    for (int i = 0; i < ag->ia.qtd; i++) {
        if (ag->ia.msgs[i].conteudo) letras += (unsigned)tam_texto(ag->ia.msgs[i].conteudo);
        if (ag->ia.msgs[i].chamadas) letras += (unsigned)tam_texto(ag->ia.msgs[i].chamadas);
    }
    letras += sizeof FERRAMENTAS;
    return (int)(letras / 4);
}

/* Diminui a conversa até caber em max_contexto: primeiro resume os
 * resultados antigos de ferramentas, depois tira os pedidos mais antigos
 * (cada pedido: da mensagem do usuário até antes da próxima). A primeira
 * mensagem (as instruções) e o pedido atual ficam sempre. */
static void cortar_contexto(agente_t *ag)
{
    ia_t *ia = &ag->ia;
    int antes = agente_estimar_tokens(ag);
    if (antes <= ag->max_contexto) return;
    int ultimo_usuario = -1;
    for (int i = ia->qtd - 1; i >= 0; i--) if (compara_texto(ia->msgs[i].papel, "user") == 0) { ultimo_usuario = i; break; }
    for (int i = 1; i < ultimo_usuario && agente_estimar_tokens(ag) > ag->max_contexto; i++) {
        ia_mensagem_t *m = &ia->msgs[i];
        if (compara_texto(m->papel, "tool") == 0 && m->conteudo && tam_texto(m->conteudo) > 200) {
            libera(m->conteudo);
            /* O texto do programa é Latin-1; a conversa é UTF-8. */
            static const char resumo[] = "[resultado antigo removido para economizar espaço]";
            m->conteudo = agente_utf8(resumo, sizeof resumo - 1);
        }
    }
    while (agente_estimar_tokens(ag) > ag->max_contexto) {
        /* Acha o primeiro pedido (depois das instruções) que não é o atual. */
        int ini = -1, fim = -1;
        for (int i = 1; i < ia->qtd; i++) if (compara_texto(ia->msgs[i].papel, "user") == 0) { ini = i; break; }
        if (ini < 0) break;
        for (int i = ini + 1; i < ia->qtd; i++) if (compara_texto(ia->msgs[i].papel, "user") == 0) { fim = i; break; }
        if (fim < 0) break;                   /* só sobrou o pedido atual */
        for (int k = ini; k < fim; k++) ia_apagar_mensagem(ia, ini);
    }
    aviso(ag, AV_CONTEXTO, "[conversa resumida: de ~%d para ~%d tokens]", antes, agente_estimar_tokens(ag));
    registrar("saguicode: contexto cortado de %d para %d tokens", antes, agente_estimar_tokens(ag));
}

/* ------------------------------------------------------------ instruções */

static const char INSTRUCOES[] =
    "Você é o Sagui Code, um agente de programação que roda DENTRO do SaguiOS, um sistema operacional x86 de 32 bits "
    "escrito do zero por IA. Você ajuda o usuário a criar e consertar programas em C para o SaguiOS.\n\n"
    "Regras:\n"
    "- Responda em português do Brasil, curto e direto.\n"
    "- Os programas usam só a libsagui (não existe libc: nada de printf, malloc, stdio.h). A função principal é "
    "`int principal(int argc, char **argv)`. Use imprimir() para escrever no terminal.\n"
    "- Não existe ponto flutuante: use inteiros (por exemplo, décimos ou centésimos).\n"
    "- Programas com janela usam gui.h: jan_abrir, g_retangulo, g_texto, proximo_evento etc.\n"
    "- Trabalhe com as ferramentas: escreva o arquivo .c, compile com `compilar`, leia os erros (eles têm linha, coluna "
    "e uma dica), corrija com `editar_arquivo` e compile de novo até passar. Depois rode com `rodar` para conferir.\n"
    "- Antes de mudar um arquivo que já existe, leia-o.\n"
    "- A pasta de trabalho é %s; use caminhos dentro dela.\n"
    "- Quando terminar, diga em uma ou duas frases o que fez e como o usuário roda o programa.\n";

static void acrescentar_arquivo(texto_t *t, const char *titulo, const char *caminho)
{
    texto_t a = { 0, 0, 0 };
    if (ler_inteiro(caminho, &a, 200 * 1024) < 0 || !a.d) return;
    tx_formatar(t, "\n\n# %s (%s)\n\n", titulo, caminho);
    tx_texto(t, a.d);
    libera(a.d);
}

void agente_nova_conversa(agente_t *ag)
{
    ia_liberar(&ag->ia);
    ag->gasto_conversa = 0;
    ag->titulo[0] = 0;
    texto_t t = { 0, 0, 0 };
    tx_formatar(&t, INSTRUCOES, ag->pasta_trabalho);
    acrescentar_arquivo(&t, "Manual do C do SaguiOS", "/sistema/ajuda/c-do-sagui.md");
    acrescentar_arquivo(&t, "Cabeçalho sagui.h (a libsagui)", "/sistema/inc/sagui.h");
    acrescentar_arquivo(&t, "Cabeçalho gui.h (janelas)", "/sistema/inc/gui.h");
    char *u = agente_utf8(t.d ? t.d : "", t.n);
    int i = ia_acrescentar(&ag->ia, "system", u ? u : "", NULL, NULL);
    libera(u);
    if (i >= 0) ag->ia.msgs[i].guardar_cache = true;
    libera(t.d);
}

/* ------------------------------------------------------------ um pedido */

static void montar_chamadas(const ia_resposta_t *r, json_escritor_t *e)
{
    je_iniciar(e);
    je_lista(e);
    for (int i = 0; i < r->n_chamadas; i++) {
        const ia_chamada_t *c = &r->chamadas[i];
        je_objeto(e);
        je_chave(e, "id"); je_texto(e, c->id);
        je_chave(e, "type"); je_texto(e, "function");
        je_chave(e, "function"); je_objeto(e);
        je_chave(e, "name"); je_texto(e, c->nome);
        je_chave(e, "arguments"); je_texto(e, c->argumentos ? c->argumentos : "{}");
        je_fim_objeto(e);
        je_fim_objeto(e);
    }
    je_fim_lista(e);
}

static void resumo_args(const char *args, char *out, int max)
{
    int k = 0;
    for (const char *p = args; *p && k < max - 4; p++) out[k++] = *p == '\n' ? ' ' : *p;
    if (args && (int)tam_texto(args) > max - 4) { out[k++] = '.'; out[k++] = '.'; out[k++] = '.'; }
    out[k] = 0;
}

void agente_pedir(agente_t *ag, const char *texto_latin1)
{
    char *u = agente_utf8(texto_latin1, (unsigned)tam_texto(texto_latin1));
    ia_acrescentar(&ag->ia, "user", u ? u : "", NULL, NULL);
    libera(u);
    ag->ocupado = true;
    ag->parar = false;
    ag->passo = 0;
    ag->gasto_pedido = 0;
    ag->entrada = ag->saida = ag->cache = 0;
}

void agente_parar(agente_t *ag) { if (ag->ocupado) ag->parar = true; }

static void terminar_pedido(agente_t *ag)
{
    ag->ocupado = false;
    ag->parar = false;
    if (ag->ui.fim) ag->ui.fim(ag->ui.ctx);
    registrar("saguicode: pedido terminado: entrada %d (cache %d), saida %d, custo %ld micro-dolares, sessao %ld",
              ag->entrada, ag->cache, ag->saida, ag->gasto_pedido, ag->gasto_sessao);
}

/* Uma chamada ao modelo e as ferramentas que ele pedir. false = o pedido acabou. */
static bool um_passo(agente_t *ag)
{
    if (ag->gasto_sessao >= ag->limite_micro) {
        char d[32];
        agente_dinheiro(ag->limite_micro, d, sizeof d);
        aviso(ag, AV_LIMITE_GASTO, "Limite de gasto da sessão atingido (%s).", d);
        return false;
    }
    cortar_contexto(ag);
    ia_resposta_t r;
    if (ag->ui.pensando) ag->ui.pensando(ag->ui.ctx);
    int ok = ia_perguntar(&ag->ia, &r);
    if (ok < 0) {
        aviso(ag, AV_ERRO_API, "Erro: %s", ag->ia.erro);
        registrar("saguicode: erro da API: %s", ag->ia.erro);
        ia_resposta_liberar(&r);
        return false;
    }
    ag->gasto_pedido += r.custo_micro;
    ag->gasto_sessao += r.custo_micro;
    ag->gasto_conversa += r.custo_micro;
    ag->entrada += r.tokens_entrada;
    ag->saida += r.tokens_saida;
    ag->cache += r.tokens_cache;
    ag->tokens_sessao_entrada += r.tokens_entrada;
    ag->tokens_sessao_saida += r.tokens_saida;
    if (r.n_chamadas == 0) {
        ia_acrescentar(&ag->ia, "assistant", r.texto ? r.texto : "", NULL, NULL);
        registrar("saguicode: resposta final (%u letras, fim %s)", r.tam, r.fim);
        if (ag->ui.resposta) ag->ui.resposta(ag->ui.ctx, r.texto ? r.texto : "");
        ia_resposta_liberar(&r);
        return false;
    }
    json_escritor_t e;
    montar_chamadas(&r, &e);
    ia_acrescentar(&ag->ia, "assistant", r.texto, e.d, NULL);
    je_liberar(&e);
    for (int i = 0; i < r.n_chamadas; i++) {
        ia_chamada_t *c = &r.chamadas[i];
        const char *args = c->argumentos ? c->argumentos : "{}";
        char resumo[100], descricao[160], fim[120];
        resumo_args(c->argumentos ? c->argumentos : "", resumo, sizeof resumo);
        agente_descrever(c->nome, args, descricao, sizeof descricao);
        if (ag->ui.ferramenta) ag->ui.ferramenta(ag->ui.ctx, c->nome, resumo, descricao);
        registrar("saguicode: ferramenta %s", c->nome);
        texto_t res = { 0, 0, 0 };
        bool deu_certo;
        executar_ferramenta(ag, c->nome, args, &res, &deu_certo, fim, sizeof fim);
        if (ag->ui.ferramenta_fim) ag->ui.ferramenta_fim(ag->ui.ctx, c->nome, deu_certo, fim);
        /* O resultado foi montado em Latin-1 (arquivos e textos do SaguiOS): vai em UTF-8. */
        char *u = agente_utf8(res.d ? res.d : "", res.n);
        ia_acrescentar(&ag->ia, "tool", u ? u : "", NULL, c->id);
        libera(u);
        libera(res.d);
    }
    ia_resposta_liberar(&r);
    return true;
}

bool agente_passo(agente_t *ag)
{
    if (!ag->ocupado) return false;
    bool continuar;
    /* O pedido de parar chega entre um passo e outro (a janela trata os
     * eventos depois de cada passo): confere antes de chamar o modelo de novo. */
    if (ag->parar) {
        aviso(ag, AV_PARADO, "Parado a pedido. Diga \"continue\" para seguir.");
        registrar("saguicode: parado a pedido no passo %d", ag->passo);
        continuar = false;
    } else {
        continuar = um_passo(ag);
        ag->passo++;
        if (continuar && ag->passo >= ag->max_passos) {
            aviso(ag, AV_LIMITE_PASSOS, "Limite de %d passos neste pedido. Diga \"continue\" para seguir.", ag->max_passos);
            continuar = false;
        }
    }
    if (!continuar) terminar_pedido(ag);
    return continuar;
}

/* ------------------------------------------------------------ conversas salvas */

char *agente_para_json(agente_t *ag, unsigned *tam)
{
    json_escritor_t e;
    je_iniciar(&e);
    je_objeto(&e);
    /* O título vem primeiro: a lista de sessões da janela lê só o começo do
     * arquivo para mostrá-lo, sem ler a conversa inteira. */
    if (ag->titulo[0]) { je_chave(&e, "titulo"); je_texto_latin1(&e, ag->titulo); }
    je_chave(&e, "modelo"); je_texto(&e, ag->ia.modelo);
    je_chave(&e, "gasto_micro"); je_int(&e, (int)ag->gasto_conversa);
    je_chave(&e, "mensagens");
    je_lista(&e);
    for (int i = 1; i < ag->ia.qtd; i++) {                /* as instruções não: são refeitas ao abrir */
        ia_mensagem_t *m = &ag->ia.msgs[i];
        je_objeto(&e);
        je_chave(&e, "role"); je_texto(&e, m->papel);
        je_chave(&e, "content"); if (m->conteudo) je_texto(&e, m->conteudo); else je_nulo(&e);
        if (m->chamadas) { je_chave(&e, "tool_calls"); je_bruto(&e, m->chamadas, (unsigned)tam_texto(m->chamadas)); }
        if (m->id_chamada[0]) { je_chave(&e, "tool_call_id"); je_texto(&e, m->id_chamada); }
        je_fim_objeto(&e);
    }
    je_fim_lista(&e);
    je_fim_objeto(&e);
    if (e.erro) { je_liberar(&e); return NULL; }
    *tam = e.n;
    return e.d;
}

int agente_de_json(agente_t *ag, const char *json, unsigned tam, char *erro, int max_erro)
{
    json_t *j = json_ler(json, tam, erro, max_erro);
    if (!j) return -1;
    agente_nova_conversa(ag);
    const char *m = json_texto_de(j, "modelo");
    if (*m) copia_texto(ag->ia.modelo, m, sizeof ag->ia.modelo);
    copia_texto(ag->titulo, json_texto_de(j, "titulo"), sizeof ag->titulo);
    agente_latin1(ag->titulo);
    ag->gasto_conversa = json_int(json_campo(j, "gasto_micro"), 0);
    int n = 0;
    for (json_t *x = json_campo(j, "mensagens") ? json_campo(j, "mensagens")->filho : NULL; x; x = x->prox, n++) {
        json_t *ch = json_campo(x, "tool_calls");
        char *chamadas = NULL;
        if (ch) {
            json_escritor_t e;
            je_iniciar(&e);
            /* reescreve o pedaço: simples, refaz a lista */
            je_lista(&e);
            for (json_t *c = ch->filho; c; c = c->prox) {
                json_t *f = json_campo(c, "function");
                je_objeto(&e);
                je_chave(&e, "id"); je_texto(&e, json_texto_de(c, "id"));
                je_chave(&e, "type"); je_texto(&e, "function");
                je_chave(&e, "function"); je_objeto(&e);
                je_chave(&e, "name"); je_texto(&e, json_texto_de(f, "name"));
                je_chave(&e, "arguments"); je_texto(&e, json_texto_de(f, "arguments"));
                je_fim_objeto(&e);
                je_fim_objeto(&e);
            }
            je_fim_lista(&e);
            chamadas = e.d;
        }
        json_t *conteudo = json_campo(x, "content");
        ia_acrescentar(&ag->ia, json_texto_de(x, "role"), conteudo && conteudo->tipo == JSON_TEXTO ? conteudo->texto : NULL,
                       chamadas, *json_texto_de(x, "tool_call_id") ? json_texto_de(x, "tool_call_id") : NULL);
        libera(chamadas);
    }
    json_liberar(j);
    return n;
}

int agente_salvar(agente_t *ag, const char *caminho)
{
    unsigned n = 0;
    char *d = agente_para_json(ag, &n);
    int r = d ? gravar_inteiro(caminho, d, n) : -1;
    libera(d);
    return r;
}

int agente_abrir(agente_t *ag, const char *caminho, char *erro, int max_erro)
{
    texto_t t = { 0, 0, 0 };
    if (ler_inteiro(caminho, &t, 4 * 1024 * 1024) < 0 || !t.d) return -1;
    int n = agente_de_json(ag, t.d, t.n, erro, max_erro);
    libera(t.d);
    return n < 0 ? -2 : n;
}

/* ------------------------------------------------------------ configuração */

void agente_ler_chave(agente_t *ag)
{
    char caminho[96];
    formatar_texto(caminho, sizeof caminho, "%s/chaves.txt", ag->pasta_config);
    texto_t t = { 0, 0, 0 };
    if (ler_inteiro(caminho, &t, 8192) < 0 || !t.d) return;
    for (char *p = t.d; *p;) {
        char *fim = acha_char(p, '\n');
        if (fim) *fim = 0;
        while (*p == ' ') p++;
        if (comeca_com(p, "openrouter")) {
            char *v = acha_char(p, '=');
            if (v) {
                v++;
                while (*v == ' ') v++;
                int n = (int)tam_texto(v);
                while (n > 0 && (v[n - 1] == ' ' || v[n - 1] == '\r')) v[--n] = 0;
                copia_texto(ag->ia.chave, v, sizeof ag->ia.chave);
            }
        }
        if (!fim) break;
        p = fim + 1;
    }
    memset(t.d, 0, t.n);
    libera(t.d);
}

static void ler_configuracoes(agente_t *ag)
{
    char v[200];
    if (config_ler("ia.modelo", v, sizeof v) > 0) copia_texto(ag->ia.modelo, v, sizeof ag->ia.modelo);
    else copia_texto(ag->ia.modelo, "anthropic/claude-sonnet-4.5", sizeof ag->ia.modelo);
    if (config_ler("ia.servidor", v, sizeof v) > 0) copia_texto(ag->ia.servidor, v, sizeof ag->ia.servidor);
    if (config_ler("ia.limite_centavos", v, sizeof v) > 0) ag->limite_micro = (long)texto_para_int(v) * 10000;
    if (config_ler("ia.max_passos", v, sizeof v) > 0 && texto_para_int(v) > 0) ag->max_passos = texto_para_int(v);
    if (config_ler("ia.max_contexto", v, sizeof v) > 0 && texto_para_int(v) > 1000) ag->max_contexto = texto_para_int(v);
}

void agente_iniciar(agente_t *ag)
{
    agente_ui_t ui = ag->ui;          /* a interface pode ter sido posta antes */
    memset(ag, 0, sizeof *ag);
    ag->ui = ui;
    ag->max_passos = 25;
    ag->limite_micro = 1000000;       /* US$ 1,00 por sessão */
    ag->max_contexto = 60000;
    ia_iniciar(&ag->ia);
    ag->ia.ferramentas = agente_utf8(FERRAMENTAS, sizeof FERRAMENTAS - 1);   /* as descrições estão em Latin-1 no programa */
    ag->ia.ao_texto = ao_texto;
    ag->ia.ctx = ag;
    info_arquivo_t inf;
    ag->pendrive = info_arquivo("/usb/.saguios", &inf) >= 0;
    copia_texto(ag->pasta_config, ag->pendrive ? "/usb/.saguios" : "/tmp", sizeof ag->pasta_config);
    copia_texto(ag->pasta_trabalho, ag->pendrive ? "/usb/projetos" : "/tmp/projetos", sizeof ag->pasta_trabalho);
    criar_pasta(ag->pasta_trabalho);
    mudar_pasta(ag->pasta_trabalho);
    ler_configuracoes(ag);
    agente_ler_chave(ag);
}

void agente_liberar(agente_t *ag)
{
    memset(ag->ia.chave, 0, sizeof ag->ia.chave);
    ia_liberar(&ag->ia);
}
