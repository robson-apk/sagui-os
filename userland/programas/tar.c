/* tar (empacotar) - junta arquivos e pastas num pacote .tar e desfaz (H93).
 *
 *   tar c[v][z]f pacote.tar arquivo-ou-pasta...   cria o pacote
 *   tar x[v][o]f pacote.tar [-C pasta]            extrai (na pasta atual ou na do -C)
 *   tar t[v]f pacote.tar                          lista o que há dentro
 * As letras podem vir com ou sem '-' ("tar -cvf" também vale). z compacta
 * com gzip (.tar.gz); na extração e na lista o gzip é reconhecido sozinho.
 * v mostra cada nome (na lista, também o tamanho). o deixa substituir
 * arquivos que já existem.
 *
 * Formato: "ustar" da norma POSIX (IEEE Std 1003.1, descrição do
 * utilitário pax, seção "ustar Interchange Format"): para cada arquivo, um
 * cabeçalho de 512 bytes (nome, modo, tamanho e data em octal, soma de
 * conferência, tipo) e os dados completados até múltiplo de 512; no fim,
 * dois blocos de zeros. Nomes que não cabem nos 100 bytes do campo name
 * são partidos numa barra e o começo vai no campo prefix (155 bytes);
 * se nem assim couber, vai um cabeçalho estendido "x" com path= (formato
 * pax, a mesma norma). Na leitura também entendemos o "x" do pax e o "L"
 * do GNU tar, que outros programas usam para nomes longos.
 *
 * Os nomes no SaguiOS são Latin-1; no pacote vão em UTF-8 (como o pax pede
 * e como os outros sistemas esperam), e voltam para Latin-1 ao extrair.
 *
 * Segurança na extração: nomes absolutos ou com ".." são recusados (um
 * pacote malicioso não pode gravar fora da pasta escolhida), ligações
 * simbólicas e especiais são ignoradas, e um arquivo que já existe só é
 * substituído com a letra o.
 *
 * O pacote é montado e lido inteiro na memória: simples, e os arquivos do
 * SaguiOS cabem nela. A divisão do trabalho (montar o pacote numa memória
 * que cresce, ler os pax "x" e os "L") seguiu o fork do Alejandro
 * Anuncibay, reescrita aqui. */
#include "sagui.h"
#include "compactar.h"

typedef struct {
    uint8_t *d;
    uint32_t n, cap;
    bool erro;
} memoria_t;

static void somar(memoria_t *m, const void *dados, uint32_t n)
{
    if (m->erro) return;
    if (m->n + n > m->cap) {
        uint32_t nova = m->cap ? m->cap : 65536;
        while (nova < m->n + n) nova *= 2;
        uint8_t *t = nova <= COMP_LIMITE ? realoca(m->d, nova) : NULL;
        if (!t) { m->erro = true; return; }
        m->d = t;
        m->cap = nova;
    }
    if (n) memcpy(m->d + m->n, dados, n);
    m->n += n;
}

static void completar_bloco(memoria_t *m)
{
    static const uint8_t zeros[512];
    if (m->n % 512) somar(m, zeros, 512 - m->n % 512);
}

static bool verboso, sobrescrever;
static uint32_t agora;
static int erros;

/* ================================================================ criar */

/* Número em octal, com zeros à esquerda, ocupando len-1 dígitos e um zero
 * no fim (como a norma pede). */
static void octal(uint8_t *dst, int len, uint32_t v)
{
    for (int i = len - 2; i >= 0; i--) { dst[i] = (uint8_t)('0' + (v & 7)); v >>= 3; }
    dst[len - 1] = 0;
}

static void cabecalho(memoria_t *m, const char *nome, const char *prefixo, uint32_t modo, uint32_t tam, char tipo)
{
    uint8_t h[512];
    memset(h, 0, sizeof h);
    size_t ln = tam_texto(nome);
    memcpy(h, nome, ln > 100 ? 100 : ln);     /* 100 bytes exatos não levam o zero */
    octal(h + 100, 8, modo);
    octal(h + 108, 8, 0);                     /* dono e grupo: 0 */
    octal(h + 116, 8, 0);
    octal(h + 124, 12, tam);
    octal(h + 136, 12, agora);
    h[156] = (uint8_t)tipo;
    memcpy(h + 257, "ustar", 6);              /* magic "ustar\0" e version "00" */
    memcpy(h + 263, "00", 2);
    memcpy(h + 265, "sagui", 5);              /* uname e gname */
    memcpy(h + 297, "sagui", 5);
    if (prefixo) memcpy(h + 345, prefixo, tam_texto(prefixo));
    /* A soma é de todos os bytes do cabeçalho, contando o próprio campo da
     * soma como 8 espaços. Vai em 6 dígitos octais, um zero e um espaço. */
    memset(h + 148, ' ', 8);
    uint32_t soma = 0;
    for (int i = 0; i < 512; i++) soma += h[i];
    octal(h + 148, 7, soma);
    h[155] = ' ';
    somar(m, h, 512);
}

/* Um registro pax "tamanho chave=valor\n", em que o tamanho conta o
 * próprio número: tentamos com quantos dígitos for preciso. */
static void registro_pax(memoria_t *m, const char *chave, const char *valor)
{
    uint32_t resto = (uint32_t)(tam_texto(chave) + tam_texto(valor) + 3);   /* ' ', '=', '\n' */
    uint32_t total = resto + 1;
    for (;;) {
        char num[12];
        formatar_texto(num, sizeof num, "%u", total);
        if (tam_texto(num) + resto == total) break;
        total = (uint32_t)tam_texto(num) + resto;
    }
    char linha[700];
    formatar_texto(linha, sizeof linha, "%u %s=%s\n", total, chave, valor);
    somar(m, linha, (uint32_t)tam_texto(linha));
}

static void entrada_tar(memoria_t *m, const char *nome_latin1, const uint8_t *dados, uint32_t tam, bool pasta)
{
    char nome[600];
    if (nome_latin1_para_utf8(nome_latin1, nome, sizeof nome - 2) < 0) {
        imprimir_em(ERROS, "tar: %s: nome comprido demais\n", nome_latin1);
        erros++;
        return;
    }
    size_t ln = tam_texto(nome);
    if (pasta && nome[ln - 1] != '/') { nome[ln++] = '/'; nome[ln] = 0; }
    const char *curto = nome;
    char prefixo[156];
    bool tem_prefixo = false;
    if (ln > 100) {
        /* Partir na barra mais à direita que deixe o fim em até 100 bytes
         * e o começo em até 155 (a barra em si não é guardada). */
        int corte = -1;
        for (int i = 0; i < (int)ln - 1; i++)
            if (nome[i] == '/' && i <= 155 && (int)ln - i - 1 <= 100) { corte = i; break; }
        if (corte > 0) {
            memcpy(prefixo, nome, (size_t)corte);
            prefixo[corte] = 0;
            curto = nome + corte + 1;
            tem_prefixo = true;
        } else {
            memoria_t pax = { 0 };
            registro_pax(&pax, "path", nome);
            if (pax.erro) { m->erro = true; return; }
            cabecalho(m, "PaxHeaders/nome", NULL, 0644, pax.n, 'x');
            somar(m, pax.d, pax.n);
            completar_bloco(m);
            libera(pax.d);
        }
    }
    cabecalho(m, curto, tem_prefixo ? prefixo : NULL, pasta ? 0755 : 0644, pasta ? 0 : tam, pasta ? '5' : '0');
    if (!pasta && tam) {
        somar(m, dados, tam);
        completar_bloco(m);
    }
    if (verboso) imprimir("%s%s\n", nome_latin1, pasta ? "/" : "");
}

typedef struct {
    memoria_t *m;
    const char *pacote;
} criar_ctx_t;

static int ao_percorrer(const char *real, const char *nome, int tipo, uint32_t tamanho, void *p)
{
    criar_ctx_t *c = p;
    (void)tamanho;
    if (tipo == TIPO_PASTA) { entrada_tar(c->m, nome, NULL, 0, true); return 0; }
    if (compara_texto(real, c->pacote) == 0) return 0;   /* não pôr o pacote dentro dele mesmo */
    uint8_t *d;
    int n = ler_arquivo_inteiro(real, &d);
    if (n < 0) { imprimir_em(ERROS, "tar: %s: %s\n", real, comp_nome_erro(n)); erros++; return 0; }
    entrada_tar(c->m, nome, d, (uint32_t)n, false);
    libera(d);
    return c->m->erro ? COMP_ERRO_MEMORIA : 0;
}

static bool termina_com(const char *s, const char *fim)
{
    size_t a = tam_texto(s), b = tam_texto(fim);
    return a > b && compara_texto(s + a - b, fim) == 0;
}

static int criar(const char *pacote, char **nomes, int qtd, bool gz)
{
    if (!qtd) { imprimir_em(ERROS, "tar: diga o que vai no pacote\n"); return 1; }
    memoria_t m = { 0 };
    criar_ctx_t c = { &m, pacote };
    for (int i = 0; i < qtd; i++) {
        /* No pacote o nome fica relativo: sem a barra do começo e sem "./". */
        const char *nome = nomes[i];
        while (nome[0] == '/') nome++;
        while (nome[0] == '.' && nome[1] == '/') nome += 2;
        char limpo[256];
        copia_texto(limpo, nome, sizeof limpo);
        size_t l = tam_texto(limpo);
        while (l > 1 && limpo[l - 1] == '/') limpo[--l] = 0;
        int r = percorrer(nomes[i], limpo, ao_percorrer, &c);
        if (r < 0) { imprimir_em(ERROS, "tar: %s: %s\n", nomes[i], comp_nome_erro(r)); erros++; }
    }
    static const uint8_t fim[1024];
    somar(&m, fim, sizeof fim);   /* dois blocos de zeros fecham o pacote */
    if (m.erro) { imprimir_em(ERROS, "tar: sem memória\n"); libera(m.d); return 1; }
    uint8_t *saida = m.d;
    int tam = (int)m.n;
    if (gz || termina_com(pacote, ".gz") || termina_com(pacote, ".tgz")) {
        tam = gzip_compactar(m.d, m.n, NULL, agora, 6, &saida);
        libera(m.d);
        if (tam < 0) { imprimir_em(ERROS, "tar: %s\n", comp_nome_erro(tam)); return 1; }
    }
    int r = compara_texto(pacote, "-") == 0 ? escrever_tudo(SAIDA, saida, (unsigned)tam)
                                            : gravar_arquivo_inteiro(pacote, saida, (uint32_t)tam);
    libera(saida);
    if (r < 0) { imprimir_em(ERROS, "tar: %s: %s\n", pacote, nome_erro(r)); return 1; }
    return erros ? 1 : 0;
}

/* ================================================================ ler */

/* Campos numéricos: octal com espaços ou zeros em volta; o GNU tar usa
 * "base 256" (primeiro bit ligado) para números grandes. */
static int64_t numero(const uint8_t *p, int len)
{
    if (p[0] & 0x80) {
        uint32_t v = 0;
        for (int i = 1; i < len; i++) {
            if (v >> 24) return -1;   /* passa de 4 GB: não é para nós */
            v = v << 8 | p[i];
        }
        return (p[0] & 0x7F) ? -1 : (int64_t)v;
    }
    uint32_t v = 0;
    int i = 0;
    while (i < len && p[i] == ' ') i++;
    for (; i < len && p[i] >= '0' && p[i] <= '7'; i++) {
        if (v >> 29) return -1;
        v = v * 8 + (uint32_t)(p[i] - '0');
    }
    return v;
}

static bool zerado(const uint8_t *h)
{
    for (int i = 0; i < 512; i++) if (h[i]) return false;
    return true;
}

/* Lê os registros pax de um cabeçalho "x" e guarda o path= em longo. */
static void ler_pax(const uint8_t *q, uint32_t tam, char *longo, int max, bool *tem)
{
    const uint8_t *fim = q + tam;
    while (q < fim) {
        const uint8_t *ini = q;
        uint32_t len = 0;
        while (q < fim && *q >= '0' && *q <= '9') len = len * 10 + (uint32_t)(*q++ - '0');
        if (q >= fim || *q != ' ' || len == 0 || len > (uint32_t)(fim - ini)) return;
        q++;
        const uint8_t *fim_reg = ini + len - 1;   /* o '\n' */
        if (fim_reg - q > 5 && memcmp(q, "path=", 5) == 0) {
            int l = (int)(fim_reg - q - 5);
            if (l < max) { memcpy(longo, q + 5, (size_t)l); longo[l] = 0; *tem = true; }
        }
        q = ini + len;
    }
}

typedef struct {
    char nome[600];   /* em Latin-1 */
    char tipo;        /* '0' arquivo, '5' pasta, outro: ignorado */
    uint32_t tamanho;
    const uint8_t *dados;
} entrada_t;

/* A próxima entrada do pacote: 1 achou, 0 acabou, < 0 erro. */
static int proxima(const uint8_t *buf, uint32_t n, uint32_t *pos, entrada_t *e)
{
    char longo[600];
    bool tem_longo = false;
    for (;;) {
        uint32_t p = *pos;
        if (p + 512 > n) return p >= n ? 0 : COMP_ERRO_TRUNCADO;   /* sem os zeros do fim: aceitável */
        const uint8_t *h = buf + p;
        if (zerado(h)) return 0;
        uint32_t soma = 0;
        for (int i = 0; i < 512; i++) soma += (i >= 148 && i < 156) ? ' ' : h[i];
        if ((int64_t)soma != numero(h + 148, 8)) return COMP_ERRO_DADOS;
        int64_t tam = numero(h + 124, 12);
        if (tam < 0) return COMP_ERRO_METODO;
        uint32_t ini = p + 512;
        if ((uint64_t)ini + (uint64_t)tam > n) return COMP_ERRO_TRUNCADO;
        uint32_t ocupa = ((uint32_t)tam + 511) & ~511u;
        *pos = ini + ocupa > n ? n : ini + ocupa;
        char tipo = (char)h[156];
        if (tipo == 'L') {   /* GNU: o nome longo da próxima entrada */
            uint32_t l = (uint32_t)tam < sizeof longo - 1 ? (uint32_t)tam : sizeof longo - 1;
            memcpy(longo, buf + ini, l);
            longo[l] = 0;
            tem_longo = true;
            continue;
        }
        if (tipo == 'x') { ler_pax(buf + ini, (uint32_t)tam, longo, sizeof longo, &tem_longo); continue; }
        if (tipo == 'g') continue;   /* pax global: nada que usemos */
        char nome[600];
        if (tem_longo) copia_texto(nome, longo, sizeof nome);
        else {
            nome[0] = 0;
            if (memcmp(h + 257, "ustar", 5) == 0 && h[345]) {
                char pre[156];
                memcpy(pre, h + 345, 155);
                pre[155] = 0;
                copia_texto(nome, pre, sizeof nome);
                junta_texto(nome, "/", sizeof nome);
            }
            char nm[101];
            memcpy(nm, h, 100);
            nm[100] = 0;
            junta_texto(nome, nm, sizeof nome);
        }
        nome_utf8_para_latin1(nome, (int)tam_texto(nome), e->nome, sizeof e->nome);
        if (tipo == 0 || tipo == '7') tipo = '0';   /* '7' (contíguo) é um arquivo comum */
        size_t l = tam_texto(e->nome);
        if (tipo == '0' && l && e->nome[l - 1] == '/') tipo = '5';   /* tar antigo marca pasta assim */
        e->tipo = tipo;
        e->tamanho = (uint32_t)tam;
        e->dados = buf + ini;
        return 1;
    }
}

static int ler_pacote(const char *pacote, uint8_t **dados, uint32_t *n)
{
    uint8_t *d;
    int r = ler_arquivo_inteiro(pacote, &d);
    if (r < 0) { imprimir_em(ERROS, "tar: %s: %s\n", pacote, comp_nome_erro(r)); return 1; }
    if (eh_gzip(d, (uint32_t)r)) {
        uint8_t *cru;
        int t = gzip_descompactar(d, (uint32_t)r, &cru, NULL, 0);
        libera(d);
        if (t < 0) { imprimir_em(ERROS, "tar: %s: %s\n", pacote, comp_nome_erro(t)); return 1; }
        d = cru;
        r = t;
    }
    *dados = d;
    *n = (uint32_t)r;
    return 0;
}

static int listar_pacote(const char *pacote)
{
    uint8_t *d;
    uint32_t n, pos = 0;
    if (ler_pacote(pacote, &d, &n)) return 1;
    entrada_t *e = aloca(sizeof *e);
    int r;
    while (e && (r = proxima(d, n, &pos, e)) > 0) {
        if (verboso) imprimir("%c %10u  %s\n", e->tipo == '5' ? 'd' : e->tipo == '0' ? '-' : '?', e->tamanho, e->nome);
        else imprimir("%s\n", e->nome);
    }
    if (e && r < 0) { imprimir_em(ERROS, "tar: %s: %s\n", pacote, comp_nome_erro(r)); erros++; }
    libera(e);
    libera(d);
    return erros ? 1 : 0;
}

static int extrair(const char *pacote, const char *destino)
{
    uint8_t *d;
    uint32_t n, pos = 0;
    if (ler_pacote(pacote, &d, &n)) return 1;
    if (destino) {
        int r = criar_pastas(destino);
        if (r < 0) { imprimir_em(ERROS, "tar: %s: %s\n", destino, nome_erro(r)); libera(d); return 1; }
    }
    entrada_t *e = aloca(sizeof *e);
    char caminho[512];
    int r = 0;
    while (e && (r = proxima(d, n, &pos, e)) > 0) {
        if (!caminho_seguro(e->nome, destino, caminho, sizeof caminho)) {
            imprimir_em(ERROS, "tar: %s: caminho perigoso (absoluto ou com \"..\"), recusado\n", e->nome);
            erros++;
            continue;
        }
        if (e->tipo == '5') {
            int q = criar_pastas(caminho);
            if (q < 0) { imprimir_em(ERROS, "tar: %s: %s\n", caminho, nome_erro(q)); erros++; }
            else if (verboso) imprimir("%s\n", e->nome);
            continue;
        }
        if (e->tipo != '0') {
            imprimir_em(ERROS, "tar: %s: ligação ou arquivo especial, ignorado\n", e->nome);
            continue;
        }
        if (existe_arquivo(caminho) && !sobrescrever) {
            imprimir_em(ERROS, "tar: %s já existe (use a letra o para substituir)\n", caminho);
            erros++;
            continue;
        }
        int q = criar_pastas_ate(caminho);
        if (q >= 0) q = gravar_arquivo_inteiro(caminho, e->dados, e->tamanho);
        if (q < 0) { imprimir_em(ERROS, "tar: %s: %s\n", caminho, nome_erro(q)); erros++; continue; }
        if (verboso) imprimir("%s\n", e->nome);
    }
    if (e && r < 0) { imprimir_em(ERROS, "tar: %s: %s\n", pacote, comp_nome_erro(r)); erros++; }
    libera(e);
    libera(d);
    return erros ? 1 : 0;
}

/* ================================================================ principal */

static void uso(void)
{
    imprimir_em(ERROS, "uso: tar c[v][z]f pacote.tar arquivos...   (cria)\n"
                       "     tar x[v][o]f pacote.tar [-C pasta]     (extrai; o = substitui)\n"
                       "     tar t[v]f pacote.tar                   (lista)\n");
}

int principal(int argc, char **argv)
{
    if (argc < 2) { uso(); return 2; }
    char modo = 0;
    bool gz = false;
    const char *pacote = NULL, *destino = NULL;
    char *nomes[64];
    int qtd = 0;
    bool quer_pacote = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (compara_texto(a, "-C") == 0 && i + 1 < argc) { destino = argv[++i]; continue; }
        /* As letras: no primeiro argumento (com ou sem '-') ou em "-x". */
        if (i == 1 || (a[0] == '-' && a[1])) {
            for (const char *c = a[0] == '-' ? a + 1 : a; *c; c++) {
                if (*c == 'c' || *c == 'x' || *c == 't') {
                    if (modo && modo != *c) { imprimir_em(ERROS, "tar: escolha só um de c, x ou t\n"); return 2; }
                    modo = *c;
                } else if (*c == 'v') verboso = true;
                else if (*c == 'z') gz = true;
                else if (*c == 'o') sobrescrever = true;
                else if (*c == 'f') quer_pacote = true;
                else { imprimir_em(ERROS, "tar: letra desconhecida: %c\n", *c); uso(); return 2; }
            }
            continue;
        }
        if (quer_pacote && !pacote) { pacote = a; continue; }
        if (qtd < 64) nomes[qtd++] = argv[i];
    }
    if (!modo || !pacote) { uso(); return 2; }
    data_t d;
    agora = ler_data(&d) == 0 ? data_para_unix(&d) : 0;
    if (modo == 'c') return criar(pacote, nomes, qtd, gz);
    if (modo == 't') return listar_pacote(pacote);
    return extrair(pacote, destino);
}
