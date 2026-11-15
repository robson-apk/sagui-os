/* zip pacote.zip arquivo|pasta... - comprime arquivos e pastas num .zip (H87).
 *
 * Formato ZIP (APPNOTE da PKWARE, a parte básica, sem ZIP64): para cada
 * arquivo, um cabeçalho local e os dados; no fim, o diretório central (uma
 * entrada por arquivo, com onde está o cabeçalho local) e o registro de fim.
 * Os dados vão comprimidos com o deflate da libsagui (método 8); se não
 * diminuírem, vão como estão (método 0). Pastas viram entradas "nome/". */
#include "sagui.h"
#include "compactar.h"

#define CAMINHO_MAX 256
#define MAX_ENTRADAS 1024

typedef struct {
    char     nome[CAMINHO_MAX];
    uint32_t crc, comprimido, tamanho, local;
    uint16_t metodo;
    bool     pasta;
} entrada_t;

static entrada_t *ent;
static int qtd;
static int fd;
static uint32_t escrito;
static uint16_t hora_dos, data_dos;
static bool calado;

static void p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *p, uint32_t v) { p16(p, v & 0xFFFF); p16(p + 2, v >> 16); }

static int gravar(const void *d, uint32_t n)
{
    if (escrever_tudo(fd, d, n) < 0) return -1;
    escrito += n;
    return 0;
}

static int cabecalho_local(entrada_t *e)
{
    uint8_t h[30];
    size_t n = tam_texto(e->nome);
    p32(h, 0x04034b50);
    p16(h + 4, 20);
    p16(h + 6, 0);
    p16(h + 8, e->metodo);
    p16(h + 10, hora_dos);
    p16(h + 12, data_dos);
    p32(h + 14, e->crc);
    p32(h + 18, e->comprimido);
    p32(h + 22, e->tamanho);
    p16(h + 26, (unsigned)n);
    p16(h + 28, 0);
    e->local = escrito;
    if (gravar(h, 30) < 0 || gravar(e->nome, (uint32_t)n) < 0) return -1;
    return 0;
}

static int juntar_arquivo(const char *caminho, const char *nome_zip, uint32_t tamanho)
{
    if (qtd >= MAX_ENTRADAS) { imprimir_em(ERROS, "zip: arquivos demais (máximo %d)\n", MAX_ENTRADAS); return 1; }
    uint8_t *d = aloca(tamanho + 1);
    if (!d) { imprimir_em(ERROS, "zip: %s: sem memória\n", caminho); return 1; }
    int f = abrir(caminho, ABRIR_LER);
    if (f < 0) { libera(d); imprimir_em(ERROS, "zip: %s: %s\n", caminho, nome_erro(f)); return 1; }
    uint32_t tem = 0;
    while (tem < tamanho) { int r = ler(f, d + tem, tamanho - tem); if (r <= 0) break; tem += (uint32_t)r; }
    fechar(f);
    entrada_t *e = &ent[qtd];
    copia_texto(e->nome, nome_zip, sizeof e->nome);
    e->pasta = false;
    e->tamanho = tem;
    e->crc = crc32(0, d, tem);
    uint8_t *z = aloca(DEFLAR_MAX(tem));
    int zn = z ? deflar(d, tem, z, DEFLAR_MAX(tem)) : -1;
    if (zn >= 0 && (uint32_t)zn < tem) { e->metodo = 8; e->comprimido = (uint32_t)zn; }
    else { e->metodo = 0; e->comprimido = tem; }
    int r = cabecalho_local(e);
    if (!r) r = gravar(e->metodo == 8 ? z : d, e->comprimido);
    libera(z);
    libera(d);
    if (r < 0) { imprimir_em(ERROS, "zip: erro ao gravar\n"); return 1; }
    qtd++;
    if (!calado) imprimir("  %s (%u -> %u bytes)\n", nome_zip, e->tamanho, e->comprimido);
    return 0;
}

static int juntar(const char *caminho, int nivel)
{
    info_arquivo_t inf;
    int r = info_arquivo(caminho, &inf);
    if (r < 0) { imprimir_em(ERROS, "zip: %s: %s\n", caminho, nome_erro(r)); return 1; }
    const char *nome = caminho;
    while (*nome == '/') nome++;   /* caminhos relativos dentro do zip */
    if (inf.tipo != TIPO_PASTA) return juntar_arquivo(caminho, nome, inf.tamanho);
    if (qtd >= MAX_ENTRADAS) return 1;
    entrada_t *e = &ent[qtd];
    formatar_texto(e->nome, sizeof e->nome, "%s/", nome);
    e->pasta = true; e->metodo = 0; e->crc = 0; e->comprimido = 0; e->tamanho = 0;
    if (nome[0] && cabecalho_local(e) == 0) qtd++;
    if (nivel > 32) return 0;
    entrada_pasta_t ep;
    char filho[CAMINHO_MAX];
    int erros = 0;
    for (int i = 0; listar(caminho, i, &ep) == 1; i++) {
        formatar_texto(filho, sizeof filho, "%s%s%s", caminho, compara_texto(caminho, "/") ? "/" : "", ep.nome);
        erros += juntar(filho, nivel + 1);
    }
    return erros;
}

int principal(int argc, char **argv)
{
    int primeiro = 1;
    if (argc > 1 && compara_texto(argv[1], "-q") == 0) { calado = true; primeiro = 2; }
    if (argc - primeiro < 2) { imprimir_em(ERROS, "uso: zip [-q] pacote.zip arquivo|pasta...\n"); return 1; }
    const char *pacote = argv[primeiro];
    ent = aloca(MAX_ENTRADAS * sizeof *ent);
    if (!ent) { imprimir_em(ERROS, "zip: sem memória\n"); return 1; }
    data_t dt;
    ler_data(&dt);
    hora_dos = (uint16_t)(dt.hora << 11 | dt.minuto << 5 | dt.segundo / 2);
    data_dos = (uint16_t)((dt.ano > 1980 ? dt.ano - 1980 : 0) << 9 | dt.mes << 5 | dt.dia);
    fd = abrir(pacote, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0) { imprimir_em(ERROS, "zip: %s: %s\n", pacote, nome_erro(fd)); return 1; }
    int erros = 0;
    for (int i = primeiro + 1; i < argc; i++) erros += juntar(argv[i], 0);
    /* diretório central */
    uint32_t inicio_central = escrito;
    for (int i = 0; i < qtd; i++) {
        entrada_t *e = &ent[i];
        uint8_t h[46];
        size_t n = tam_texto(e->nome);
        p32(h, 0x02014b50);
        p16(h + 4, 20); p16(h + 6, 20); p16(h + 8, 0); p16(h + 10, e->metodo);
        p16(h + 12, hora_dos); p16(h + 14, data_dos);
        p32(h + 16, e->crc); p32(h + 20, e->comprimido); p32(h + 24, e->tamanho);
        p16(h + 28, (unsigned)n); p16(h + 30, 0); p16(h + 32, 0); p16(h + 34, 0); p16(h + 36, 0);
        p32(h + 38, e->pasta ? 0x10 : 0);
        p32(h + 42, e->local);
        gravar(h, 46);
        gravar(e->nome, (uint32_t)n);
    }
    uint8_t fimc[22];
    p32(fimc, 0x06054b50);
    p16(fimc + 4, 0); p16(fimc + 6, 0);
    p16(fimc + 8, (unsigned)qtd); p16(fimc + 10, (unsigned)qtd);
    p32(fimc + 12, escrito - inicio_central);
    p32(fimc + 16, inicio_central);
    p16(fimc + 20, 0);
    int r = gravar(fimc, 22);
    fechar(fd);
    if (r < 0) { imprimir_em(ERROS, "zip: erro ao gravar %s\n", pacote); return 1; }
    imprimir("%s: %d itens, %u bytes\n", pacote, qtd, escrito);
    registrar("zip: %s, %d itens, %u bytes, %d erros", pacote, qtd, escrito, erros);
    return erros ? 1 : 0;
}
