/* tar (pacote) - junta arquivos e pastas num .tar, lista e extrai.
 *   tar cf pacote.tar arquivo|pasta...   cria
 *   tar tf pacote.tar                    lista
 *   tar xf pacote.tar                    extrai na pasta atual
 * "v" (cvf, xvf) mostra cada arquivo. Formato ustar (POSIX.1-1988): blocos de
 * 512 bytes, um cabeçalho por entrada, tamanhos em octal, dois blocos zerados
 * no fim. Sem compressão. */
#include "sagui.h"

#define BLOCO 512
#define CAMINHO_MAX 256

typedef struct {
    char nome[100], modo[8], dono[8], grupo[8], tamanho[12], data[12], soma[8];
    char tipo, ligado[100], magia[6], versao[2], nome_dono[32], nome_grupo[32];
    char dev_maior[8], dev_menor[8], prefixo[155], resto[12];
} cabecalho_t;

static bool falar;
static int saida_fd;
static unsigned char bloco[BLOCO];

static void octal(char *dst, int largura, unsigned v)
{
    dst[largura - 1] = 0;
    for (int i = largura - 2; i >= 0; i--) { dst[i] = (char)('0' + (v & 7)); v >>= 3; }
}

static unsigned ler_octal(const char *s, int n)
{
    unsigned v = 0;
    for (int i = 0; i < n && s[i]; i++) {
        if (s[i] == ' ') continue;
        if (s[i] < '0' || s[i] > '7') break;
        v = v * 8 + (unsigned)(s[i] - '0');
    }
    return v;
}

static unsigned soma(const cabecalho_t *c)
{
    const unsigned char *p = (const unsigned char *)c;
    unsigned s = 0;
    for (int i = 0; i < BLOCO; i++) s += (i >= 148 && i < 156) ? ' ' : p[i];
    return s;
}

/* ---------------------------------------------------------------- criar */

static int gravar_cabecalho(const char *nome, unsigned tamanho, char tipo)
{
    cabecalho_t c;
    memset(&c, 0, sizeof c);
    size_t n = tam_texto(nome);
    if (n >= sizeof c.nome) {   /* nome longo: o começo vai para o prefixo, cortando numa barra */
        const char *barra = NULL;
        for (const char *p = nome; *p; p++)
            if (*p == '/' && (size_t)(p - nome) < sizeof c.prefixo && tam_texto(p + 1) < sizeof c.nome) { barra = p; break; }
        if (!barra) { imprimir_em(ERROS, "tar: nome longo demais: %s\n", nome); return -1; }
        memcpy(c.prefixo, nome, (size_t)(barra - nome));
        copia_texto(c.nome, barra + 1, sizeof c.nome);
    } else {
        memcpy(c.nome, nome, n);
    }
    octal(c.modo, 8, tipo == '5' ? 0755 : 0644);
    octal(c.dono, 8, 0);
    octal(c.grupo, 8, 0);
    octal(c.tamanho, 12, tamanho);
    octal(c.data, 12, 0);
    c.tipo = tipo;
    memcpy(c.magia, "ustar", 6);
    c.versao[0] = c.versao[1] = '0';
    copia_texto(c.nome_dono, "sagui", sizeof c.nome_dono);
    copia_texto(c.nome_grupo, "sagui", sizeof c.nome_grupo);
    octal(c.soma, 7, soma(&c));
    c.soma[7] = ' ';
    return escrever_tudo(saida_fd, &c, BLOCO) < 0 ? -1 : 0;
}

static int juntar_arquivo(const char *caminho, unsigned tamanho)
{
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) { imprimir_em(ERROS, "tar: %s: %s\n", caminho, nome_erro(fd)); return 1; }
    if (gravar_cabecalho(caminho, tamanho, '0') < 0) { fechar(fd); return 1; }
    unsigned faltam = tamanho;
    while (faltam > 0) {
        memset(bloco, 0, BLOCO);
        unsigned quer = faltam < BLOCO ? faltam : BLOCO, tem = 0;
        while (tem < quer) {
            int r = ler(fd, bloco + tem, quer - tem);
            if (r <= 0) break;
            tem += (unsigned)r;
        }
        escrever_tudo(saida_fd, bloco, BLOCO);   /* um arquivo que encolheu sai com zeros */
        faltam -= quer;
    }
    fechar(fd);
    if (falar) imprimir("%s\n", caminho);
    return 0;
}

static int juntar(const char *caminho, int nivel)
{
    info_arquivo_t inf;
    int r = info_arquivo(caminho, &inf);
    if (r < 0) { imprimir_em(ERROS, "tar: %s: %s\n", caminho, nome_erro(r)); return 1; }
    if (inf.tipo != TIPO_PASTA) return juntar_arquivo(caminho, inf.tamanho);
    char nome[CAMINHO_MAX];
    copia_texto(nome, caminho, sizeof nome);
    size_t n = tam_texto(nome);
    if (n == 0 || nome[n - 1] != '/') junta_texto(nome, "/", sizeof nome);
    if (gravar_cabecalho(nome, 0, '5') < 0) return 1;
    if (falar) imprimir("%s\n", nome);
    if (nivel > 32) return 0;
    entrada_pasta_t e;
    int i = 0, erros = 0;
    char filho[CAMINHO_MAX];
    while (listar(caminho, i++, &e) == 1) {
        copia_texto(filho, nome, sizeof filho);
        junta_texto(filho, e.nome, sizeof filho);
        erros += juntar(filho, nivel + 1);
    }
    return erros;
}

static int criar(const char *pacote, int n, char **itens)
{
    if (n == 0) { imprimir_em(ERROS, "tar: diga o que pôr no pacote\n"); return 1; }
    saida_fd = abrir(pacote, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (saida_fd < 0) { imprimir_em(ERROS, "tar: %s: %s\n", pacote, nome_erro(saida_fd)); return 1; }
    int erros = 0;
    for (int i = 0; i < n; i++) {
        const char *s = itens[i];
        while (s[0] == '/' && s[1]) s++;   /* caminhos relativos no pacote, como o tar do Unix */
        if (s != itens[i]) { /* mudar_pasta para / e usar o relativo */
            char atual[CAMINHO_MAX];
            pasta_atual(atual, sizeof atual);
            mudar_pasta("/");
            erros += juntar(s, 0);
            mudar_pasta(atual);
        } else {
            erros += juntar(s, 0);
        }
    }
    memset(bloco, 0, BLOCO);
    escrever_tudo(saida_fd, bloco, BLOCO);
    escrever_tudo(saida_fd, bloco, BLOCO);
    fechar(saida_fd);
    registrar("tar: criou %s (%d erros)", pacote, erros);
    return erros ? 1 : 0;
}

/* ---------------------------------------------------------------- ler */

static int ler_bloco(int fd)
{
    unsigned tem = 0;
    while (tem < BLOCO) {
        int r = ler(fd, bloco + tem, BLOCO - tem);
        if (r <= 0) return tem == 0 ? 0 : -1;
        tem += (unsigned)r;
    }
    return 1;
}

/* Cria as pastas do caminho (sem o último pedaço). */
static void criar_pastas(const char *caminho)
{
    char p[CAMINHO_MAX];
    copia_texto(p, caminho, sizeof p);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') { *s = 0; criar_pasta(p); *s = '/'; }
}

static bool nome_seguro(const char *s)
{
    if (s[0] == '/') return false;
    for (const char *p = s; *p; p++)
        if (p[0] == '.' && p[1] == '.' && (p == s || p[-1] == '/') && (p[2] == '/' || p[2] == 0)) return false;
    return true;
}

static int ler_pacote(const char *pacote, bool extrair)
{
    int fd = abrir(pacote, ABRIR_LER);
    if (fd < 0) { imprimir_em(ERROS, "tar: %s: %s\n", pacote, nome_erro(fd)); return 1; }
    int erros = 0, entradas = 0, r;
    char nome[CAMINHO_MAX];
    while ((r = ler_bloco(fd)) == 1) {
        cabecalho_t *c = (cabecalho_t *)bloco;
        if (c->nome[0] == 0) break;   /* bloco zerado: fim */
        if (ler_octal(c->soma, 8) != soma(c)) { imprimir_em(ERROS, "tar: cabeçalho estragado (não é um .tar?)\n"); erros++; break; }
        nome[0] = 0;
        if (c->prefixo[0] && !memcmp(c->magia, "ustar", 5)) {
            char pre[156];
            memcpy(pre, c->prefixo, 155); pre[155] = 0;
            copia_texto(nome, pre, sizeof nome);
            junta_texto(nome, "/", sizeof nome);
        }
        char so[101];
        memcpy(so, c->nome, 100); so[100] = 0;
        junta_texto(nome, so, sizeof nome);
        unsigned tamanho = ler_octal(c->tamanho, 12);
        char tipo = c->tipo ? c->tipo : '0';
        unsigned blocos = (tamanho + BLOCO - 1) / BLOCO;
        entradas++;
        if (!extrair) {
            if (falar) imprimir("%c %8u %s\n", tipo == '5' ? 'd' : '-', tamanho, nome);
            else imprimir("%s\n", nome);
        }
        int out = -1;
        if (extrair) {
            if (!nome_seguro(nome)) { imprimir_em(ERROS, "tar: pulei %s (caminho fora da pasta)\n", nome); erros++; }
            else if (tipo == '5') { criar_pastas(nome); size_t n = tam_texto(nome); if (n && nome[n - 1] == '/') nome[n - 1] = 0; criar_pasta(nome); }
            else if (tipo == '0' || tipo == '7') {
                criar_pastas(nome);
                out = abrir(nome, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
                if (out < 0) { imprimir_em(ERROS, "tar: %s: %s\n", nome, nome_erro(out)); erros++; }
            } else {
                imprimir_em(ERROS, "tar: pulei %s (tipo '%c' não suportado)\n", nome, tipo);
            }
            if (falar) imprimir("%s\n", nome);
        }
        unsigned faltam = tamanho;
        for (unsigned b = 0; b < blocos; b++) {
            if (ler_bloco(fd) != 1) { imprimir_em(ERROS, "tar: o pacote acaba no meio de %s\n", nome); erros++; break; }
            unsigned n = faltam < BLOCO ? faltam : BLOCO;
            if (out >= 0) escrever_tudo(out, bloco, n);
            faltam -= n;
        }
        if (out >= 0) fechar(out);
    }
    if (r < 0) { imprimir_em(ERROS, "tar: o pacote acaba no meio de um bloco\n"); erros++; }
    fechar(fd);
    registrar("tar: %s %s, %d entradas, %d erros", extrair ? "extraiu" : "listou", pacote, entradas, erros);
    return erros ? 1 : 0;
}

int principal(int argc, char **argv)
{
    if (argc < 3) {
        imprimir_em(ERROS, "uso: tar cf pacote.tar coisas... | tar tf pacote.tar | tar xf pacote.tar\n");
        return 1;
    }
    const char *op = argv[1];
    if (op[0] == '-') op++;
    char modo = 0;
    bool tem_f = false;
    for (const char *o = op; *o; o++) {
        if (*o == 'c' || *o == 't' || *o == 'x') modo = *o;
        else if (*o == 'v') falar = true;
        else if (*o == 'f') tem_f = true;
        else { imprimir_em(ERROS, "tar: opção desconhecida '%c'\n", *o); return 1; }
    }
    if (!modo || !tem_f) { imprimir_em(ERROS, "tar: use c, t ou x junto com f (ex.: tar cf a.tar pasta)\n"); return 1; }
    if (modo == 'c') return criar(argv[2], argc - 3, argv + 3);
    return ler_pacote(argv[2], modo == 'x');
}
