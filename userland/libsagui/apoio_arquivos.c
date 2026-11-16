/* apoio_arquivos.c - pequenas ajudas para os programas de arquivos (H93):
 * ler um arquivo inteiro para a memória, converter a data do relógio para
 * os formatos do Unix (tar, gzip) e do MS-DOS (zip), os nomes dos erros
 * da compactação, e o que o tar e o zip têm em comum: percorrer pastas,
 * criar as que faltam, converter nomes entre Latin-1 e UTF-8 e conferir que
 * um nome vindo de um pacote não sai da pasta de destino. Ficam fora de compactar.c para que aquele arquivo dependa
 * só de memória e possa ser testado no computador de desenvolvimento. */
#include "compactar.h"

const char *comp_nome_erro(int erro)
{
    switch (erro) {
    case COMP_ERRO_DADOS:    return "dados compactados corrompidos";
    case COMP_ERRO_CHEIO:    return "o resultado não cabe";
    case COMP_ERRO_CRC:      return "a soma de conferência (CRC) não bate";
    case COMP_ERRO_MEMORIA:  return "sem memória";
    case COMP_ERRO_TRUNCADO: return "o arquivo acaba antes da hora";
    case COMP_ERRO_METODO:   return "método não suportado";
    }
    return nome_erro(erro);
}


/* Segundos desde 1/1/1970 (sem fuso: o relógio da placa-mãe vale como está). */
uint32_t data_para_unix(const data_t *d)
{
    static const uint16_t antes_do_mes[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    if (d->ano < 1970 || d->mes < 1 || d->mes > 12) return 0;
    uint32_t dias = 0;
    for (uint32_t a = 1970; a < d->ano; a++) dias += (a % 4 == 0 && (a % 100 != 0 || a % 400 == 0)) ? 366 : 365;
    dias += antes_do_mes[d->mes - 1] + d->dia - 1u;
    uint32_t a = d->ano;
    if (d->mes > 2 && a % 4 == 0 && (a % 100 != 0 || a % 400 == 0)) dias++;
    return dias * 86400u + d->hora * 3600u + d->minuto * 60u + d->segundo;
}

/* MS-DOS: data = (ano - 1980) << 9 | mês << 5 | dia; hora = h << 11 |
 * min << 5 | seg / 2 (PKWARE APPNOTE, seção 4.4.6). */
uint32_t data_para_dos(const data_t *d)
{
    uint32_t ano = d->ano < 1980 ? 0 : d->ano - 1980u;
    uint32_t data = ano << 9 | (uint32_t)d->mes << 5 | d->dia;
    uint32_t hora = (uint32_t)d->hora << 11 | (uint32_t)d->minuto << 5 | d->segundo / 2u;
    return hora << 16 | data;
}

int ler_arquivo_inteiro(const char *caminho, uint8_t **dados)
{
    info_arquivo_t inf;
    *dados = NULL;
    int r = info_arquivo(caminho, &inf);
    if (r < 0) return r;
    if (inf.tipo == TIPO_PASTA) return ERRO_E_PASTA;
    if (inf.tamanho > COMP_LIMITE) return COMP_ERRO_CHEIO;
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) return fd;
    uint8_t *b = aloca(inf.tamanho + 1);
    if (!b) { fechar(fd); return COMP_ERRO_MEMORIA; }
    uint32_t k = 0;
    while (k < inf.tamanho) {
        int m = ler(fd, b + k, inf.tamanho - k);
        if (m < 0) { fechar(fd); libera(b); return m; }
        if (m == 0) break;
        k += (uint32_t)m;
    }
    fechar(fd);
    *dados = b;
    return (int)k;
}

int gravar_arquivo_inteiro(const char *caminho, const void *dados, uint32_t n)
{
    int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0) return fd;
    int r = n ? escrever_tudo(fd, dados, n) : 0;
    int f = fechar(fd);
    return r < 0 ? r : f < 0 ? f : 0;
}

bool existe_arquivo(const char *caminho)
{
    info_arquivo_t inf;
    return info_arquivo(caminho, &inf) == 0;
}

/* ---------------------------------------------------------------- caminhos */

bool caminho_seguro(const char *nome, const char *destino, char *saida, int max)
{
    char rel[512];
    int k = 0;
    /* Absoluto: "/x" ou, vindo do Windows, "C:\x" ou "\x". Recusado: quem
     * extrai escolhe a pasta, o pacote não. */
    if (nome[0] == '/' || nome[0] == '\\' || (nome[0] && nome[1] == ':')) return false;
    const char *p = nome;
    while (*p) {
        while (*p == '/' || *p == '\\') p++;
        const char *ini = p;
        while (*p && *p != '/' && *p != '\\') p++;
        int n = (int)(p - ini);
        if (n == 0 || (n == 1 && ini[0] == '.')) continue;
        if (n == 2 && ini[0] == '.' && ini[1] == '.') return false;   /* subiria para fora do destino */
        if (k + n + 2 >= (int)sizeof rel) return false;
        if (k) rel[k++] = '/';
        memcpy(rel + k, ini, (size_t)n);
        k += n;
    }
    if (k == 0) return false;
    rel[k] = 0;
    saida[0] = 0;
    if (destino && destino[0] && compara_texto(destino, ".") != 0) {
        size_t d = tam_texto(destino);
        if ((int)(d + (size_t)k + 2) > max) return false;
        copia_texto(saida, destino, (size_t)max);
        if (destino[d - 1] != '/') junta_texto(saida, "/", (size_t)max);
    } else if (k + 1 > max) return false;
    junta_texto(saida, rel, (size_t)max);
    return true;
}

int criar_pastas(const char *caminho)
{
    char t[512];
    size_t n = tam_texto(caminho);
    if (n == 0 || n >= sizeof t) return ERRO_INVALIDO;
    copia_texto(t, caminho, sizeof t);
    for (size_t i = 1; i <= n; i++) {
        if (t[i] != '/' && t[i] != 0) continue;
        char c = t[i];
        t[i] = 0;
        info_arquivo_t inf;
        if (info_arquivo(t, &inf) == 0) {
            if (inf.tipo != TIPO_PASTA) return ERRO_NAO_E_PASTA;
        } else {
            int r = criar_pasta(t);
            if (r < 0 && r != ERRO_JA_EXISTE) return r;
        }
        t[i] = c;
    }
    return 0;
}

int criar_pastas_ate(const char *caminho)
{
    char t[512];
    copia_texto(t, caminho, sizeof t);
    char *b = acha_ultimo_char(t, '/');
    if (!b || b == t) return 0;
    *b = 0;
    return criar_pastas(t);
}

/* ---------------------------------------------------------------- nomes */

bool nome_ascii(const char *s)
{
    for (; *s; s++) if ((uint8_t)*s >= 0x80) return false;
    return true;
}

/* Latin-1 é o começo do Unicode: cada byte >= 0x80 vira dois em UTF-8
 * (110xxxxx 10xxxxxx). */
int nome_latin1_para_utf8(const char *src, char *dst, int max)
{
    int k = 0;
    for (const uint8_t *p = (const uint8_t *)src; *p; p++) {
        if (*p < 0x80) { if (k + 1 >= max) return -1; dst[k++] = (char)*p; }
        else {
            if (k + 2 >= max) return -1;
            dst[k++] = (char)(0xC0 | *p >> 6);
            dst[k++] = (char)(0x80 | (*p & 0x3F));
        }
    }
    dst[k] = 0;
    return k;
}

/* Quantos bytes de continuação o primeiro byte de um caractere UTF-8
 * anuncia (-1: não pode começar um caractere). */
static int continuacoes(uint8_t c)
{
    if (c < 0x80) return 0;
    if (c >= 0xC2 && c <= 0xDF) return 1;
    if ((c & 0xF0) == 0xE0) return 2;
    if (c >= 0xF0 && c <= 0xF4) return 3;
    return -1;
}

/* Se o nome não for UTF-8 válido, ele já devia estar em Latin-1 (pacotes
 * antigos): fica como está. Caracteres fora do Latin-1 viram '_'. */
int nome_utf8_para_latin1(const char *src, int n, char *dst, int max)
{
    const uint8_t *p = (const uint8_t *)src;
    bool valido = true;
    for (int i = 0; i < n && valido;) {
        int extra = continuacoes(p[i]);
        if (extra < 0 || i + extra >= n + (extra ? 0 : 1)) { valido = false; break; }
        for (int j = 1; j <= extra; j++) if ((p[i + j] & 0xC0) != 0x80) valido = false;
        i += extra + 1;
    }
    int k = 0;
    for (int i = 0; i < n;) {
        if (k + 1 >= max) return -1;
        int extra = valido ? continuacoes(p[i]) : 0;
        if (extra == 0) { dst[k++] = (char)p[i++]; continue; }
        if (extra == 1) {
            uint32_t c = (uint32_t)(p[i] & 0x1F) << 6 | (p[i + 1] & 0x3F);
            dst[k++] = c < 0x100 ? (char)c : '_';
        } else dst[k++] = '_';
        i += extra + 1;
    }
    dst[k] = 0;
    return k;
}

/* ---------------------------------------------------------------- percorrer pastas */

static int percorrer_fundo(const char *real, const char *nome, percorrer_fn fn, void *ctx, int fundo)
{
    info_arquivo_t inf;
    int r = info_arquivo(real, &inf);
    if (r < 0) return r;
    r = fn(real, nome, (int)inf.tipo, inf.tamanho, ctx);
    if (r < 0 || inf.tipo != TIPO_PASTA) return r;
    if (fundo > 32) return ERRO_LIMITE;   /* pastas dentro de pastas demais */
    /* A pilha dos programas é pequena: os caminhos vão para a memória. */
    char *cr = aloca(512), *cn = aloca(512);
    entrada_pasta_t *e = aloca(sizeof *e);
    r = (cr && cn && e) ? 0 : ERRO_SEM_MEMORIA;
    for (int i = 0; r >= 0; i++) {
        int q = listar(real, i, e);
        if (q <= 0) { if (q < 0) r = q; break; }
        if (compara_texto(e->nome, ".") == 0 || compara_texto(e->nome, "..") == 0) continue;
        size_t lr = tam_texto(real), ln = tam_texto(nome);
        formatar_texto(cr, 512, "%s%s%s", real, lr && real[lr - 1] == '/' ? "" : "/", e->nome);
        formatar_texto(cn, 512, "%s%s%s", nome, ln && nome[ln - 1] != '/' ? "/" : "", e->nome);
        r = percorrer_fundo(cr, cn, fn, ctx, fundo + 1);
    }
    libera(cr);
    libera(cn);
    libera(e);
    return r;
}

int percorrer(const char *real, const char *nome, percorrer_fn fn, void *ctx)
{
    return percorrer_fundo(real, nome, fn, ctx, 0);
}
