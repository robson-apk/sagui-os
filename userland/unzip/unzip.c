/* unzip pacote.zip [-d pasta] [-l] - extrai (ou lista, com -l) um .zip (H87).
 *
 * Lê o registro de fim (no fim do arquivo, depois de um comentário de até
 * 64 KB), e por ele o diretório central: cada entrada diz o método, os
 * tamanhos, o CRC-32 e onde está o cabeçalho local. Aceita os métodos 0
 * (guardado) e 8 (deflate, pelo descompressor do Cipó, ligado pelo Makefile);
 * confere o CRC de cada arquivo. Sem ZIP64 nem criptografia. Nomes com ".."
 * ou começando por "/" são recusados (não escrevem fora da pasta). */
#include "sagui.h"
#include "compactar.h"

int inflar(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max);   /* navegador/descompactar.c */

#define CAMINHO_MAX 256
#define MAX_BYTES (64u * 1024 * 1024)

static unsigned g16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t g32(const uint8_t *p) { return g16(p) | (uint32_t)g16(p + 2) << 16; }

static void criar_pastas(const char *caminho)
{
    char p[CAMINHO_MAX];
    copia_texto(p, caminho, sizeof p);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') { *s = 0; criar_pasta(p); *s = '/'; }
}

static bool nome_seguro(const char *s)
{
    if (s[0] == '/' || s[0] == '\\') return false;
    for (const char *p = s; *p; p++) {
        if (*p == '\\') return false;
        if (p[0] == '.' && p[1] == '.' && (p == s || p[-1] == '/') && (p[2] == '/' || p[2] == 0)) return false;
    }
    return true;
}

int principal(int argc, char **argv)
{
    const char *pacote = NULL, *destino = NULL;
    bool listar_so = false;
    for (int i = 1; i < argc; i++) {
        if (compara_texto(argv[i], "-l") == 0) listar_so = true;
        else if (compara_texto(argv[i], "-d") == 0 && i + 1 < argc) destino = argv[++i];
        else if (!pacote) pacote = argv[i];
        else { imprimir_em(ERROS, "uso: unzip pacote.zip [-d pasta] [-l]\n"); return 1; }
    }
    if (!pacote) { imprimir_em(ERROS, "uso: unzip pacote.zip [-d pasta] [-l]\n"); return 1; }
    info_arquivo_t inf;
    int r = info_arquivo(pacote, &inf);
    if (r < 0) { imprimir_em(ERROS, "unzip: %s: %s\n", pacote, nome_erro(r)); return 1; }
    if (inf.tamanho > MAX_BYTES) { imprimir_em(ERROS, "unzip: arquivo grande demais\n"); return 1; }
    uint8_t *d = aloca(inf.tamanho + 1);
    if (!d) { imprimir_em(ERROS, "unzip: sem memória\n"); return 1; }
    int fd = abrir(pacote, ABRIR_LER);
    if (fd < 0) { imprimir_em(ERROS, "unzip: %s: %s\n", pacote, nome_erro(fd)); return 1; }
    uint32_t n = 0;
    while (n < inf.tamanho) { int k = ler(fd, d + n, inf.tamanho - n); if (k <= 0) break; n += (uint32_t)k; }
    fechar(fd);

    /* registro de fim: procura de trás para a frente */
    int64_t fimc = -1;
    for (int64_t p = (int64_t)n - 22; p >= 0 && p >= (int64_t)n - 22 - 65535; p--)
        if (g32(d + p) == 0x06054b50) { fimc = p; break; }
    if (fimc < 0) { imprimir_em(ERROS, "unzip: %s não é um .zip (sem o registro de fim)\n", pacote); return 1; }
    unsigned total = g16(d + fimc + 10);
    uint32_t central = g32(d + fimc + 16);
    if (total == 0xFFFF || central == 0xFFFFFFFFu) { imprimir_em(ERROS, "unzip: ZIP64 não suportado\n"); return 1; }
    if (central > n) { imprimir_em(ERROS, "unzip: diretório central fora do arquivo\n"); return 1; }
    if (destino && !listar_so) { criar_pastas(destino); criar_pasta(destino); }

    int erros = 0, feitos = 0;
    uint32_t p = central, soma = 0;
    if (listar_so) imprimir("   tamanho  comprimido  nome\n");
    for (unsigned i = 0; i < total; i++) {
        if (p + 46 > n || g32(d + p) != 0x02014b50) { imprimir_em(ERROS, "unzip: diretório central estragado\n"); erros++; break; }
        unsigned flags = g16(d + p + 8), metodo = g16(d + p + 10);
        uint32_t crc = g32(d + p + 16), comp = g32(d + p + 20), tam = g32(d + p + 24);
        unsigned nl = g16(d + p + 28), el = g16(d + p + 30), cl = g16(d + p + 32);
        uint32_t local = g32(d + p + 42);
        char nome[CAMINHO_MAX];
        unsigned k = nl < sizeof nome - 1 ? nl : sizeof nome - 1;
        if (p + 46 + nl > n) { erros++; break; }
        memcpy(nome, d + p + 46, k);
        nome[k] = 0;
        if (flags & 0x800) { size_t sobra; utf8_para_latin1(nome, k, &sobra); }   /* nome em UTF-8 */
        p += 46 + nl + el + cl;
        bool pasta = k > 0 && nome[k - 1] == '/';
        soma += tam;
        if (listar_so) { imprimir("%10u  %10u  %s\n", tam, comp, nome); continue; }
        if (!nome_seguro(nome)) { imprimir_em(ERROS, "unzip: pulei %s (caminho fora da pasta)\n", nome); erros++; continue; }
        char saida[CAMINHO_MAX];
        if (destino) formatar_texto(saida, sizeof saida, "%s/%s", destino, nome);
        else copia_texto(saida, nome, sizeof saida);
        if (pasta) {
            saida[tam_texto(saida) - 1] = 0;
            criar_pastas(saida);
            criar_pasta(saida);
            continue;
        }
        if (flags & 1) { imprimir_em(ERROS, "unzip: %s está protegido por senha (não suportado)\n", nome); erros++; continue; }
        if (local + 30 > n || g32(d + local) != 0x04034b50) { imprimir_em(ERROS, "unzip: %s: cabeçalho local estragado\n", nome); erros++; continue; }
        uint32_t dados = local + 30 + g16(d + local + 26) + g16(d + local + 28);
        if (dados + comp > n || tam > MAX_BYTES) { imprimir_em(ERROS, "unzip: %s: dados fora do arquivo\n", nome); erros++; continue; }
        uint8_t *out = aloca(tam + 1);
        if (!out) { imprimir_em(ERROS, "unzip: %s: sem memória\n", nome); erros++; continue; }
        int got;
        if (metodo == 0) { got = comp == tam ? (int)tam : -1; if (got >= 0) memcpy(out, d + dados, tam); }
        else if (metodo == 8) got = inflar(d + dados, comp, out, tam);
        else { imprimir_em(ERROS, "unzip: %s: método %u não suportado\n", nome, metodo); libera(out); erros++; continue; }
        if (got != (int)tam || crc32(0, out, tam) != crc) {
            imprimir_em(ERROS, "unzip: %s: dados estragados (CRC não confere)\n", nome);
            libera(out); erros++; continue;
        }
        criar_pastas(saida);
        int o = abrir(saida, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
        if (o < 0) { imprimir_em(ERROS, "unzip: %s: %s\n", saida, nome_erro(o)); libera(out); erros++; continue; }
        if (escrever_tudo(o, out, tam) < 0) erros++;
        fechar(o);
        libera(out);
        feitos++;
        imprimir("  %s (%u bytes)\n", saida, tam);
    }
    if (listar_so) imprimir("%u itens, %u bytes\n", total, soma);
    else imprimir("%d arquivos extraídos%s\n", feitos, erros ? ", com erros" : "");
    registrar("unzip: %s %s, %d arquivos, %d erros", listar_so ? "listou" : "extraiu", pacote, listar_so ? (int)total : feitos, erros);
    return erros ? 1 : 0;
}
