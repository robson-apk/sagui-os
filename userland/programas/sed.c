/* sed (editar-fluxo) - edita o texto que passa, linha a linha, seguindo um
 * roteiro de comandos (H91; a ideia veio do fork SaguiOS-Extended, do
 * Alejandro Anuncibay, e o programa foi reescrito sobre a libsagui atual).
 *
 *   sed [-n] [-E] [-e roteiro]... [roteiro] [arquivo...]
 *
 *   sed s/velho/novo/       troca a primeira ocorrência em cada linha;
 *   sed s/velho/novo/g      troca todas; "p" no fim mostra a linha trocada,
 *                           "i" ignora maiúsculas, um número N troca só a
 *                           N-ésima; no novo texto, & é o trecho achado;
 *   sed 3d   sed /^#/d      apaga a linha 3 / as que começam com #;
 *   sed -n 2,4p             só mostra as linhas 2 a 4 (-n: só o que for
 *                           pedido com p);
 *   sed '$!d'               ! inverte o endereço ($ = a última linha);
 *   também: q (para), = (número da linha), y/abc/xyz/ (troca letras),
 *   a, i e c texto (acrescenta depois, insere antes, troca a linha).
 * Vários comandos vão separados por ";" ou em vários -e. Endereços: N, $,
 * /expressão/ e intervalos A,B com qualquer um deles.
 *
 * As expressões (. * [] ^ $, e + ? com -E) são as do motor pequeno de
 * expressao_simples.h, que o expr também usa. Não há grupos \( \) nem \1. */
#include "sagui.h"
#include "expressao_simples.h"

#define MAX_COMANDOS  32
#define LINHA_MAX     8192

/* ------------------------------------------------------------------ roteiro */

enum { END_NADA, END_LINHA, END_ULTIMA, END_EXPR };

typedef struct { int tipo, linha; expressao_t *e; } endereco_t;

typedef struct {
    endereco_t a1, a2;
    bool       negar, dentro;
    char       letra;
    expressao_t *e;           /* s */
    char       *texto;        /* s: o novo texto; a/i/c: o texto; y: as letras de origem */
    char       *texto2;       /* y: as letras de destino */
    bool       global, mostrar;
    int        vez;           /* s: troca só a N-ésima (0 = a primeira, ou todas com g) */
} comando_t;

static comando_t cmd[MAX_COMANDOS];
static int qtd_cmd;
static bool calado;

static int erro_roteiro(const char *msg)
{
    imprimir_em(ERROS, "sed: %s\n", msg);
    return -1;
}

/* Copia até o delimitador (sem escapar), tirando a barra de "\delim".
 * Devolve o texto novo (aloca) e avança *p para depois do delimitador. */
static char *ate_delimitador(const char **p, char d, int *tam)
{
    const char *s = *p;
    char *t = aloca(tam_texto(s) + 1);
    int n = 0;
    while (*s && *s != d) {
        if (*s == '\\' && s[1] == d) { t[n++] = d; s += 2; continue; }
        if (*s == '\\' && s[1]) t[n++] = *s++;
        t[n++] = *s++;
    }
    t[n] = 0;
    if (tam) *tam = n;
    if (*s != d) { libera(t); return NULL; }
    *p = s + 1;
    return t;
}

static bool ler_endereco(const char **p, endereco_t *a)
{
    const char *s = *p;
    if (*s >= '0' && *s <= '9') {
        a->tipo = END_LINHA;
        a->linha = 0;
        while (*s >= '0' && *s <= '9') a->linha = a->linha * 10 + (*s++ - '0');
    } else if (*s == '$') {
        a->tipo = END_ULTIMA;
        s++;
    } else if (*s == '/') {
        s++;
        int n;
        char *t = ate_delimitador(&s, '/', &n);
        if (!t) return false;
        a->e = aloca(sizeof *a->e);
        if (!montar_expressao(a->e, t, n, false)) return false;
        a->tipo = END_EXPR;
    } else {
        a->tipo = END_NADA;
    }
    *p = s;
    return true;
}

/* Lê o roteiro inteiro para a lista de comandos. */
static int compilar(const char *s)
{
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == ';') s++;
        if (!*s) break;
        if (qtd_cmd == MAX_COMANDOS) return erro_roteiro("comandos demais");
        comando_t *c = &cmd[qtd_cmd++];
        if (!ler_endereco(&s, &c->a1)) return erro_roteiro("endereço inválido");
        if (*s == ',') {
            s++;
            if (!ler_endereco(&s, &c->a2) || c->a2.tipo == END_NADA) return erro_roteiro("endereço inválido");
        }
        while (*s == ' ') s++;
        if (*s == '!') { c->negar = true; s++; while (*s == ' ') s++; }
        c->letra = *s;
        if (!*s) return erro_roteiro("falta o comando");
        s++;
        switch (c->letra) {
        case 'd': case 'p': case 'q': case '=':
            break;
        case 'a': case 'i': case 'c': {
            /* "a texto" ou "a\texto": o texto vai até o fim da linha. */
            if (*s == '\\') s++;
            while (*s == ' ') s++;
            const char *f = s;
            while (*f && *f != '\n') f++;
            c->texto = aloca((size_t)(f - s) + 1);
            memcpy(c->texto, s, (size_t)(f - s));
            c->texto[f - s] = 0;
            s = f;
            break;
        }
        case 's': case 'y': {
            char d = *s++;
            if (!d || d == '\\' || d == '\n') return erro_roteiro("delimitador inválido");
            int n;
            char *velho = ate_delimitador(&s, d, &n);
            char *novo = velho ? ate_delimitador(&s, d, NULL) : NULL;
            if (!novo) return erro_roteiro(c->letra == 's' ? "comando s sem fim" : "comando y sem fim");
            if (c->letra == 'y') {
                if (tam_texto(velho) != tam_texto(novo)) return erro_roteiro("y: as duas listas precisam ter o mesmo tamanho");
                c->texto = velho;
                c->texto2 = novo;
                break;
            }
            bool sem_caixa = false;
            for (; *s && *s != ';' && *s != '\n' && *s != ' '; s++) {
                if (*s == 'g') c->global = true;
                else if (*s == 'p') c->mostrar = true;
                else if (*s == 'i' || *s == 'I') sem_caixa = true;
                else if (*s >= '1' && *s <= '9') { c->vez = 0; while (*s >= '0' && *s <= '9') c->vez = c->vez * 10 + (*s++ - '0'); s--; }
                else return erro_roteiro("opção desconhecida no comando s");
            }
            c->e = aloca(sizeof *c->e);
            if (!montar_expressao(c->e, velho, n, sem_caixa)) return erro_roteiro("expressão inválida");
            c->texto = novo;
            break;
        }
        default:
            imprimir_em(ERROS, "sed: comando desconhecido: %c\n", c->letra);
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ execução */

static char espaco[LINHA_MAX];     /* a linha sendo editada ("pattern space") */
static int  tam_espaco;
static int  num_linha;
static bool ultima;

static bool casa_endereco(const endereco_t *a)
{
    if (a->tipo == END_LINHA) return num_linha == a->linha;
    if (a->tipo == END_ULTIMA) return ultima;
    const char *i, *f;
    return procurar(a->e, espaco, espaco, espaco + tam_espaco, &i, &f);
}

/* O comando vale para esta linha? Um intervalo A,B liga quando A casa e
 * desliga depois da linha em que B casa (B é procurado só a partir da
 * linha seguinte à de A; se B for um número já passado, vale uma linha). */
static bool selecionado(comando_t *c)
{
    bool sim;
    if (c->a1.tipo == END_NADA) sim = true;
    else if (c->a2.tipo == END_NADA) sim = casa_endereco(&c->a1);
    else if (!c->dentro) {
        sim = casa_endereco(&c->a1);
        if (sim) c->dentro = !(c->a2.tipo == END_LINHA && c->a2.linha <= num_linha);
    } else {
        sim = true;
        if (c->a2.tipo == END_LINHA ? num_linha >= c->a2.linha : casa_endereco(&c->a2)) c->dentro = false;
    }
    return sim != c->negar;
}

static void escrever_espaco(void)
{
    escrever_tudo(SAIDA, espaco, (unsigned)tam_espaco);
    escrever_tudo(SAIDA, "\n", 1);
}

/* Faz a troca do comando s na linha; devolve true se trocou algo. */
static bool substituir(comando_t *c)
{
    static char novo[LINHA_MAX];
    int n = 0, vez = 0;
    bool trocou = false;
    const char *s = espaco, *fim = espaco + tam_espaco, *ini, *f, *fim_anterior = NULL;
    while (s <= fim && procurar(c->e, espaco, s, fim, &ini, &f)) {
        /* Um trecho vazio colado no fim do trecho anterior não conta (em
         * "a:b" com s/[^:]*\/X/g, depois de "a" não há um vazio antes do
         * ":"): a letra é copiada e a busca anda. */
        if (f == ini && ini == fim_anterior) {
            if (ini < fim && n < LINHA_MAX) novo[n++] = *ini;
            s = ini + 1;
            fim_anterior = NULL;
            continue;
        }
        fim_anterior = f;
        vez++;
        bool esta = c->vez ? vez == c->vez : (c->global || vez == 1);
        /* Copia o que vem antes do trecho achado. */
        for (const char *k = s; k < ini && n < LINHA_MAX; k++) novo[n++] = *k;
        s = ini;
        if (esta) {
            trocou = true;
            for (const char *r = c->texto; *r && n < LINHA_MAX; r++) {
                if (*r == '&') { for (const char *k = ini; k < f && n < LINHA_MAX; k++) novo[n++] = *k; continue; }
                if (*r == '\\' && r[1]) {
                    r++;
                    novo[n++] = *r == 'n' ? '\n' : *r == 't' ? '\t' : *r;
                    continue;
                }
                novo[n++] = *r;
            }
        } else {
            for (const char *k = ini; k < f && n < LINHA_MAX; k++) novo[n++] = *k;
        }
        /* Um trecho vazio (como em s/x*\/-/g) não pode prender o laço: a
         * letra seguinte é copiada e a busca anda uma posição. */
        if (f == ini) { if (f < fim && n < LINHA_MAX) novo[n++] = *f; s = f + 1; }
        else s = f;
        if (!c->global && !c->vez) break;
        if (c->vez && vez >= c->vez) break;
    }
    if (!trocou) return false;
    for (; s < fim && n < LINHA_MAX; s++) novo[n++] = *s;
    memcpy(espaco, novo, (size_t)n);
    tam_espaco = n;
    return true;
}

/* Roda o roteiro numa linha. Devolve false se for para parar (q). */
static bool executar_linha(void)
{
    static char depois[LINHA_MAX];   /* textos de "a", escritos depois da linha */
    int tam_depois = 0;
    bool apagada = false, continuar = true;
    for (int k = 0; k < qtd_cmd && !apagada; k++) {
        comando_t *c = &cmd[k];
        if (!selecionado(c)) continue;
        switch (c->letra) {
        case 'd': apagada = true; break;
        case 'p': escrever_espaco(); break;
        case '=': imprimir("%d\n", num_linha); break;
        case 'q': continuar = false; k = qtd_cmd; break;
        case 'i': imprimir("%s\n", c->texto); break;
        case 'a':
            tam_depois += formatar_texto(depois + tam_depois, sizeof depois - (size_t)tam_depois, "%s\n", c->texto);
            if (tam_depois > (int)sizeof depois - 1) tam_depois = (int)sizeof depois - 1;
            break;
        case 'c':
            /* Num intervalo, o texto sai uma vez só, no fim dele. */
            if (!c->dentro) imprimir("%s\n", c->texto);
            apagada = true;
            break;
        case 'y':
            for (int p = 0; p < tam_espaco; p++) {
                const char *o = acha_char(c->texto, espaco[p]);
                if (o && espaco[p]) espaco[p] = c->texto2[o - c->texto];
            }
            break;
        case 's':
            if (substituir(c) && c->mostrar) escrever_espaco();
            break;
        }
    }
    if (!apagada && !calado) escrever_espaco();
    escrever_tudo(SAIDA, depois, (unsigned)tam_depois);
    return continuar;
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: sed [-n] [-E] [-e roteiro]... [roteiro] [arquivo...]\n");
    return 1;
}

int principal(int argc, char **argv)
{
    static char roteiro[2048];
    bool tem_roteiro = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        for (const char *o = argv[i] + 1; *o; o++) {
            if (*o == 'e') {
                const char *v = o[1] ? o + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!v) return uso();
                junta_texto(roteiro, v, sizeof roteiro);
                junta_texto(roteiro, "\n", sizeof roteiro);
                tem_roteiro = true;
                break;
            }
            if (*o == 'n') calado = true;
            else if (*o == 'E' || *o == 'r') estendida = true;
            else { imprimir_em(ERROS, "sed: opção desconhecida -%c\n", *o); return uso(); }
        }
    }
    if (!tem_roteiro) {
        if (i >= argc) return uso();
        copia_texto(roteiro, argv[i++], sizeof roteiro);
    }
    if (compilar(roteiro) < 0) return 1;

    /* Os arquivos são um fluxo só, um depois do outro (como no sed). */
    int arquivos = argc - i, erros = 0;
    unsigned total = 0;
    char *tudo = NULL;
    for (int k = 0; k < (arquivos ? arquivos : 1); k++) {
        int fd = abrir_para_ler("sed", arquivos ? argv[i + k] : "-");
        if (fd < 0) { erros++; continue; }
        unsigned n;
        char *b = ler_ate_o_fim(fd, &n);
        if (fd != ENTRADA) fechar(fd);
        if (!b) { imprimir_em(ERROS, "sed: memória insuficiente\n"); return 1; }
        n = (unsigned)utf8_para_latin1(b, n, NULL);
        char *junto = realoca(tudo, total + n + 2);
        if (!junto) { imprimir_em(ERROS, "sed: memória insuficiente\n"); return 1; }
        tudo = junto;
        memcpy(tudo + total, b, n);
        total += n;
        if (n && tudo[total - 1] != '\n') tudo[total++] = '\n';
        libera(b);
    }
    for (unsigned p = 0; p < total;) {
        unsigned f = p;
        while (f < total && tudo[f] != '\n') f++;
        unsigned t = f;
        if (t > p && tudo[t - 1] == '\r') t--;
        tam_espaco = (int)(t - p) < LINHA_MAX ? (int)(t - p) : LINHA_MAX;
        memcpy(espaco, tudo + p, (size_t)tam_espaco);
        num_linha++;
        p = f + 1;
        ultima = p >= total;
        if (!executar_linha()) break;
    }
    return erros ? 2 : 0;
}
