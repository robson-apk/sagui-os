/* zip (zipar) - cria um pacote .zip com arquivos e pastas (H93).
 *
 *   zip [-0..-9] [-q] pacote.zip arquivo-ou-pasta...
 * As pastas entram com tudo o que têm dentro. -0 só guarda (sem
 * compactar), -1 a -9 escolhem o nível (6 é o padrão), -q não mostra os
 * nomes. Se o pacote já existir, ele é substituído pelo novo.
 *
 * Formato: PKWARE APPNOTE.TXT ("ZIP File Format Specification", versão
 * 6.3.x), seções 4.3 e 4.4: para cada arquivo, um cabeçalho local
 * (assinatura 0x04034B50) seguido dos dados; no fim, o diretório central
 * (um registro 0x02014B50 por arquivo, com o deslocamento do cabeçalho
 * local) e o registro de fim de diretório (0x06054B50). Métodos: 0
 * (guardado) e 8 (DEFLATE, RFC 1951, da libsagui); cada arquivo vai com o
 * que ficar menor. Sem criptografia e sem ZIP64 (até 4 GB e 65535 itens).
 *
 * Nomes: no SaguiOS são Latin-1. Nomes só com ASCII vão como estão; os
 * outros vão em UTF-8 com o bit 11 das opções ligado ("language encoding
 * flag", APPNOTE apêndice D), que diz isso a quem for abrir. As pastas
 * terminam em '/' e levam o atributo de pasta do MS-DOS (0x10).
 *
 * A organização (guardar as entradas e montar o diretório central no fim)
 * acompanha o fork do Alejandro Anuncibay, reescrita aqui. */
#include "sagui.h"
#include "compactar.h"

typedef struct {
    char *nome;          /* já como vai no pacote (ASCII ou UTF-8) */
    uint32_t crc, compactado, original, deslocamento;
    uint16_t metodo, opcoes;
    bool pasta;
} item_t;

static uint8_t *pacote;
static uint32_t tam_pacote, cap_pacote;
static bool sem_memoria;
static item_t *itens;
static int qtd_itens, cap_itens;
static int nivel = 6;
static bool quieto;
static uint32_t data_dos;
static int erros;
static const char *nome_pacote;

static void somar(const void *dados, uint32_t n)
{
    if (sem_memoria) return;
    if (tam_pacote + n > cap_pacote) {
        uint32_t nova = cap_pacote ? cap_pacote : 65536;
        while (nova < tam_pacote + n) nova *= 2;
        uint8_t *t = nova <= COMP_LIMITE ? realoca(pacote, nova) : NULL;
        if (!t) { sem_memoria = true; return; }
        pacote = t;
        cap_pacote = nova;
    }
    if (n) memcpy(pacote + tam_pacote, dados, n);
    tam_pacote += n;
}

static void poe16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void poe32(uint8_t *p, uint32_t v) { poe16(p, v); poe16(p + 2, v >> 16); }

/* Quanto por cento se economizou, só com contas de 32 bits. */
static uint32_t economia(uint32_t original, uint32_t compactado)
{
    while (original >= (1u << 24)) { original >>= 1; compactado >>= 1; }
    return original && compactado <= original ? (original - compactado) * 100u / original : 0;
}

static int adicionar(const char *nome_latin1, const uint8_t *dados, uint32_t n, bool pasta)
{
    if (qtd_itens >= 65535) { imprimir_em(ERROS, "zip: itens demais\n"); return COMP_ERRO_METODO; }
    if (qtd_itens == cap_itens) {
        int nc = cap_itens ? cap_itens * 2 : 64;
        item_t *t = realoca(itens, sizeof(item_t) * (size_t)nc);
        if (!t) return COMP_ERRO_MEMORIA;
        itens = t;
        cap_itens = nc;
    }
    char nome[600];
    if (nome_latin1_para_utf8(nome_latin1, nome, sizeof nome - 2) < 0) return ERRO_INVALIDO;
    size_t ln = tam_texto(nome);
    if (pasta && nome[ln - 1] != '/') { nome[ln++] = '/'; nome[ln] = 0; }
    item_t *it = &itens[qtd_itens];
    memset(it, 0, sizeof *it);
    it->nome = aloca(ln + 1);
    if (!it->nome) return COMP_ERRO_MEMORIA;
    copia_texto(it->nome, nome, ln + 1);
    it->pasta = pasta;
    it->opcoes = nome_ascii(nome_latin1) ? 0 : 0x0800;   /* bit 11: nome em UTF-8 */
    it->original = pasta ? 0 : n;
    it->crc = pasta ? 0 : crc32_somar(0, dados, n);
    const uint8_t *conteudo = dados;
    uint8_t *compactado = NULL;
    it->metodo = 0;
    it->compactado = it->original;
    if (!pasta && n && nivel > 0) {
        uint32_t cap = deflar_limite(n);
        compactado = aloca(cap);
        if (!compactado) { libera(it->nome); return COMP_ERRO_MEMORIA; }
        int r = deflar(dados, n, compactado, cap, nivel);
        if (r >= 0 && (uint32_t)r < n) { it->metodo = 8; it->compactado = (uint32_t)r; conteudo = compactado; }
    }
    it->deslocamento = tam_pacote;
    /* Cabeçalho local (APPNOTE 4.3.7). */
    uint8_t h[30];
    memset(h, 0, sizeof h);
    poe32(h, 0x04034B50);
    poe16(h + 4, 20);                /* versão necessária: 2.0 (DEFLATE e pastas) */
    poe16(h + 6, it->opcoes);
    poe16(h + 8, it->metodo);
    poe32(h + 10, data_dos);         /* hora e data do MS-DOS */
    poe32(h + 14, it->crc);
    poe32(h + 18, it->compactado);
    poe32(h + 22, it->original);
    poe16(h + 26, (uint32_t)ln);
    somar(h, 30);
    somar(it->nome, (uint32_t)ln);
    if (it->compactado) somar(conteudo, it->compactado);
    libera(compactado);
    if (sem_memoria) { libera(it->nome); return COMP_ERRO_MEMORIA; }
    qtd_itens++;
    if (!quieto) {
        if (pasta) imprimir("  pasta: %s\n", nome_latin1);
        else if (it->metodo == 8) imprimir("  compactado: %s (%u%% menor)\n", nome_latin1, economia(it->original, it->compactado));
        else imprimir("  guardado: %s\n", nome_latin1);
    }
    return 0;
}

static void fechar_pacote(void)
{
    uint32_t inicio = tam_pacote;
    for (int i = 0; i < qtd_itens; i++) {
        item_t *it = &itens[i];
        uint32_t ln = (uint32_t)tam_texto(it->nome);
        /* Registro do diretório central (APPNOTE 4.3.12). */
        uint8_t h[46];
        memset(h, 0, sizeof h);
        poe32(h, 0x02014B50);
        poe16(h + 4, 20);            /* feito por: MS-DOS (0), versão 2.0 */
        poe16(h + 6, 20);
        poe16(h + 8, it->opcoes);
        poe16(h + 10, it->metodo);
        poe32(h + 12, data_dos);
        poe32(h + 16, it->crc);
        poe32(h + 20, it->compactado);
        poe32(h + 24, it->original);
        poe16(h + 28, ln);
        poe32(h + 38, it->pasta ? 0x10 : 0x20);   /* atributos do MS-DOS: pasta ou "arquivo" */
        poe32(h + 42, it->deslocamento);
        somar(h, 46);
        somar(it->nome, ln);
    }
    /* Fim do diretório central (APPNOTE 4.3.16). */
    uint8_t f[22];
    memset(f, 0, sizeof f);
    poe32(f, 0x06054B50);
    poe16(f + 8, (uint32_t)qtd_itens);
    poe16(f + 10, (uint32_t)qtd_itens);
    poe32(f + 12, tam_pacote - inicio);
    poe32(f + 16, inicio);
    somar(f, 22);
}

static int ao_percorrer(const char *real, const char *nome, int tipo, uint32_t tamanho, void *ctx)
{
    (void)tamanho;
    (void)ctx;
    if (tipo == TIPO_PASTA) {
        int r = adicionar(nome, NULL, 0, true);
        if (r < 0) { imprimir_em(ERROS, "zip: %s: %s\n", real, comp_nome_erro(r)); erros++; }
        return r == COMP_ERRO_MEMORIA ? r : 0;
    }
    if (compara_texto(real, nome_pacote) == 0) return 0;   /* o pacote não entra nele mesmo */
    uint8_t *d;
    int n = ler_arquivo_inteiro(real, &d);
    if (n < 0) { imprimir_em(ERROS, "zip: %s: %s\n", real, comp_nome_erro(n)); erros++; return 0; }
    int r = adicionar(nome, d, (uint32_t)n, false);
    libera(d);
    if (r < 0) { imprimir_em(ERROS, "zip: %s: %s\n", real, comp_nome_erro(r)); erros++; }
    return r == COMP_ERRO_MEMORIA ? r : 0;
}

int principal(int argc, char **argv)
{
    int primeiro = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1]) {
            for (const char *c = a + 1; *c; c++) {
                if (*c >= '0' && *c <= '9') nivel = *c - '0';
                else if (*c == 'q') quieto = true;
                else if (*c == 'r') { }   /* as pastas já entram com tudo dentro */
                else { imprimir_em(ERROS, "zip: opção desconhecida -%c\n", *c); primeiro = -1; break; }
            }
            if (primeiro < 0) break;
            continue;
        }
        if (!nome_pacote) nome_pacote = a;
        else if (!primeiro) primeiro = i;
    }
    if (primeiro <= 0 || !nome_pacote) {
        imprimir_em(ERROS, "uso: zip [-0..-9] [-q] pacote.zip arquivo-ou-pasta...\n");
        return 2;
    }
    data_t d;
    data_dos = ler_data(&d) == 0 ? data_para_dos(&d) : (1u << 5 | 1u);   /* sem relógio: 1/1/1980 */
    for (int i = primeiro; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1]) continue;
        if (a == nome_pacote) continue;
        /* No pacote os nomes ficam relativos: sem a barra do começo e sem "./". */
        const char *nome = a;
        while (nome[0] == '/') nome++;
        while (nome[0] == '.' && nome[1] == '/') nome += 2;
        char limpo[256];
        copia_texto(limpo, nome, sizeof limpo);
        size_t l = tam_texto(limpo);
        while (l > 1 && limpo[l - 1] == '/') limpo[--l] = 0;
        int r = percorrer(a, limpo, ao_percorrer, NULL);
        if (r < 0) { imprimir_em(ERROS, "zip: %s: %s\n", a, comp_nome_erro(r)); erros++; }
    }
    fechar_pacote();
    if (sem_memoria) { imprimir_em(ERROS, "zip: sem memória\n"); return 1; }
    int r = gravar_arquivo_inteiro(nome_pacote, pacote, tam_pacote);
    if (r < 0) { imprimir_em(ERROS, "zip: %s: %s\n", nome_pacote, nome_erro(r)); return 1; }
    if (!quieto) imprimir("%s: %d itens, %u bytes\n", nome_pacote, qtd_itens, tam_pacote);
    return erros ? 1 : 0;
}
