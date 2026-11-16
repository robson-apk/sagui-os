/* gzip (compactar) e gunzip (descompactar) - arquivos .gz (H93).
 *
 *   gzip [-d] [-c] [-k] [-f] [-l] [-t] [-1..-9] [arquivo...]
 *     sem -d: compacta cada arquivo em arquivo.gz e apaga o original;
 *     -d descompacta (o mesmo que gunzip), -c manda o resultado para a saída
 *     (e não apaga nada), -k guarda o original, -f deixa substituir um
 *     arquivo que já existe, -l lista o que há dentro de cada .gz, -t só
 *     confere, -1 a -9 escolhem o nível (1 mais rápido, 9 menor; 6 padrão).
 *     Sem arquivos (ou com "-"), lê da entrada e escreve na saída.
 *
 * O formato é o da RFC 1952; a compactação em si (DEFLATE, RFC 1951) está
 * na libsagui (compactar.c). As opções seguem as do gzip de sempre, para
 * quem já conhece; a organização (um só arquivo para gzip e gunzip, a
 * extensão .tgz virando .tar) veio do fork do Alejandro Anuncibay.
 *
 * Segurança: o arquivo de destino nunca é substituído sem -f, e o original
 * só é apagado depois que o novo foi gravado inteiro. */
#include "sagui.h"
#include "compactar.h"

#ifndef MODO_GUNZIP
#define MODO_GUNZIP 0
#endif

static const char *prog = MODO_GUNZIP ? "gunzip" : "gzip";
static bool descompactar = MODO_GUNZIP, para_saida, manter, forcar, listar_gz, testar;
static int nivel = 6;
static bool cabecalho_lista;

static void uso(void)
{
    imprimir_em(ERROS, "uso: %s [-d] [-c] [-k] [-f] [-l] [-t] [-1..-9] [arquivo...]\n"
                       "  -d descompacta  -c para a saída  -k guarda o original  -f substitui\n"
                       "  -l lista  -t confere  -1 rápido ... -9 menor (padrão 6)\n", prog);
}

/* Lê tudo da entrada (um pipe ou um arquivo redirecionado). */
static int ler_entrada(uint8_t **dados)
{
    uint32_t cap = 65536, n = 0;
    uint8_t *d = aloca(cap);
    if (!d) return COMP_ERRO_MEMORIA;
    for (;;) {
        if (n == cap) {
            if (cap >= COMP_LIMITE) { libera(d); return COMP_ERRO_CHEIO; }
            uint8_t *t = realoca(d, cap * 2);
            if (!t) { libera(d); return COMP_ERRO_MEMORIA; }
            d = t;
            cap *= 2;
        }
        int r = ler(ENTRADA, d + n, cap - n);
        if (r < 0) { libera(d); return r; }
        if (r == 0) break;
        n += (uint32_t)r;
    }
    *dados = d;
    return (int)n;
}

static bool termina_com(const char *s, const char *fim)
{
    size_t a = tam_texto(s), b = tam_texto(fim);
    return a > b && compara_texto(s + a - b, fim) == 0;
}

/* -l: compactado, original, taxa e nome (o ISIZE do fim do arquivo é o
 * tamanho original módulo 2^32; para arquivos de vários membros, gzip -l
 * de sempre também só mostra o do último). */
static int listar_um(const char *caminho, const uint8_t *d, uint32_t n)
{
    if (!eh_gzip(d, n)) { imprimir_em(ERROS, "%s: %s: não é um arquivo gzip\n", prog, caminho); return 1; }
    char nome[NOME_MAX * 2];
    nome[0] = 0;
    if (d[3] & 8) {   /* FNAME, depois do FEXTRA se houver */
        uint32_t p = 10;
        if (d[3] & 4) p += 2u + (uint32_t)(d[10] | d[11] << 8);
        uint32_t k = 0;
        while (p < n && d[p] && k < sizeof nome - 1) nome[k++] = (char)d[p++];
        nome[k] = 0;
    }
    if (!nome[0]) {   /* sem nome guardado: o do arquivo, sem o .gz */
        const char *b = acha_ultimo_char(caminho, '/');
        copia_texto(nome, b ? b + 1 : caminho, sizeof nome);
        if (termina_com(nome, ".gz")) nome[tam_texto(nome) - 3] = 0;
    }
    uint32_t original = (uint32_t)d[n - 4] | (uint32_t)d[n - 3] << 8 | (uint32_t)d[n - 2] << 16 | (uint32_t)d[n - 1] << 24;
    /* Taxa em décimos de porcento, só com inteiros de 32 bits (dividir em
     * 64 bits pediria a libgcc): diminuímos os dois números juntos até a
     * conta "vezes 1000" caber. */
    uint32_t o = original, c = n;
    while (o >= (1u << 21) || c >= (1u << 21)) { o >>= 1; c >>= 1; }
    int taxa = o ? ((int)o - (int)c) * 1000 / (int)o : 0;
    int abs = taxa < 0 ? -taxa : taxa;
    if (!cabecalho_lista) { imprimir("  compactado     original   taxa  nome\n"); cabecalho_lista = true; }
    imprimir("%12u %12u %s%3d,%d%%  %s\n", n, original, taxa < 0 ? "-" : " ", abs / 10, abs % 10, nome);
    return 0;
}

static int trabalhar(const char *caminho)
{
    bool entrada = caminho == NULL;
    const char *mostrado = entrada ? "(entrada)" : caminho;
    uint8_t *d;
    int n = entrada ? ler_entrada(&d) : ler_arquivo_inteiro(caminho, &d);
    if (n < 0) { imprimir_em(ERROS, "%s: %s: %s\n", prog, mostrado, comp_nome_erro(n)); return 1; }
    if (listar_gz) { int r = listar_um(mostrado, d, (uint32_t)n); libera(d); return r; }

    char destino[256];
    destino[0] = 0;
    bool na_saida = entrada || para_saida;
    uint8_t *res;
    int tam;
    if (descompactar || testar) {
        if (!entrada && !na_saida && !testar) {
            copia_texto(destino, caminho, sizeof destino);
            size_t l = tam_texto(destino);
            if (termina_com(caminho, ".gz")) destino[l - 3] = 0;
            else if (termina_com(caminho, ".tgz")) { destino[l - 4] = 0; junta_texto(destino, ".tar", sizeof destino); }
            else { imprimir_em(ERROS, "%s: %s: não termina em .gz; nada feito\n", prog, caminho); libera(d); return 1; }
        }
        tam = gzip_descompactar(d, (uint32_t)n, &res, NULL, 0);
        libera(d);
        if (tam < 0) { imprimir_em(ERROS, "%s: %s: %s\n", prog, mostrado, comp_nome_erro(tam)); return 1; }
        if (testar) { libera(res); imprimir("%s: ok\n", mostrado); return 0; }
    } else {
        if (!na_saida) {
            if (termina_com(caminho, ".gz") || termina_com(caminho, ".tgz")) {
                imprimir_em(ERROS, "%s: %s já está compactado; nada feito\n", prog, caminho);
                libera(d);
                return 1;
            }
            formatar_texto(destino, sizeof destino, "%s.gz", caminho);
        }
        /* O nome original vai no cabeçalho (sem as pastas), para o gunzip
         * de outros sistemas saber como chamar o arquivo. */
        const char *base = entrada ? NULL : acha_ultimo_char(caminho, '/');
        if (!entrada) base = base ? base + 1 : caminho;
        data_t agora;
        uint32_t mtime = ler_data(&agora) == 0 ? data_para_unix(&agora) : 0;
        tam = gzip_compactar(d, (uint32_t)n, base, mtime, nivel, &res);
        libera(d);
        if (tam < 0) { imprimir_em(ERROS, "%s: %s: %s\n", prog, mostrado, comp_nome_erro(tam)); return 1; }
    }
    int r;
    if (na_saida) r = escrever_tudo(SAIDA, res, (unsigned)tam);
    else {
        if (existe_arquivo(destino) && !forcar) {
            imprimir_em(ERROS, "%s: %s já existe (use -f para substituir)\n", prog, destino);
            libera(res);
            return 1;
        }
        r = gravar_arquivo_inteiro(destino, res, (uint32_t)tam);
        if (r >= 0 && !manter) apagar(caminho);
    }
    libera(res);
    if (r < 0) { imprimir_em(ERROS, "%s: %s: %s\n", prog, destino[0] ? destino : "(saída)", nome_erro(r)); return 1; }
    return 0;
}

int principal(int argc, char **argv)
{
    int erros = 0, arquivos = 0;
    bool fim_opcoes = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (compara_texto(a, "--") == 0) break;
        if (a[0] != '-' || !a[1]) continue;
        for (const char *c = a + 1; *c; c++) {
            if (*c == 'd') descompactar = true;
            else if (*c == 'c') para_saida = true;
            else if (*c == 'k') manter = true;
            else if (*c == 'f') forcar = true;
            else if (*c == 'l') listar_gz = true;
            else if (*c == 't') testar = true;
            else if (*c >= '1' && *c <= '9') nivel = *c - '0';
            else if (*c == 'h') { uso(); return 0; }
            else { imprimir_em(ERROS, "%s: opção desconhecida -%c\n", prog, *c); uso(); return 2; }
        }
    }
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!fim_opcoes && compara_texto(a, "--") == 0) { fim_opcoes = true; continue; }
        if (!fim_opcoes && a[0] == '-' && a[1]) continue;
        arquivos++;
        erros += trabalhar(compara_texto(a, "-") == 0 ? NULL : a);
    }
    if (!arquivos) erros += trabalhar(NULL);
    return erros ? 1 : 0;
}
