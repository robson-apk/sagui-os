/* captura - salva a tela num arquivo PNG (H87).
 *
 * A tecla Print Screen abre este programa (o gerenciador de janelas cuida da
 * tecla). Também roda pelo terminal: "captura" ou "captura arquivo.png".
 * Sem nome, salva em /usb/Capturas se houver pendrive montado (fica guardado),
 * senão em /capturas (na memória: some ao desligar), com a data no nome.
 *
 * PNG (RFC 2083): assinatura, IHDR (largura, altura, 8 bits, RGB), IDAT com as
 * linhas comprimidas pelo deflate da libsagui (cada linha começa com o filtro
 * 0, "nenhum") e IEND; cada pedaço termina com o CRC-32. */
#include "sagui.h"
#include "gui.h"
#include "compactar.h"

static void por32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* Grava um pedaço do PNG: tamanho, tipo, dados e CRC (do tipo e dos dados). */
static int pedaco(int fd, const char *tipo, const uint8_t *dados, uint32_t n)
{
    uint8_t cab[8], fim[4];
    por32(cab, n);
    memcpy(cab + 4, tipo, 4);
    uint32_t c = crc32(0, tipo, 4);
    c = crc32(c, dados, n);
    por32(fim, c);
    if (escrever_tudo(fd, cab, 8) < 0) return -1;
    if (n && escrever_tudo(fd, dados, n) < 0) return -1;
    return escrever_tudo(fd, fim, 4) < 0 ? -1 : 0;
}

int gravar_png(const char *caminho, const uint32_t *px, int l, int a)
{
    uint32_t linha = 1 + (uint32_t)l * 3, bruto_n = linha * (uint32_t)a;
    uint8_t *bruto = aloca(bruto_n);
    uint8_t *z = bruto ? aloca(DEFLAR_MAX(bruto_n) + 8) : NULL;
    if (!bruto || !z) { libera(bruto); return ERRO_SEM_MEMORIA; }
    for (int y = 0; y < a; y++) {
        uint8_t *p = bruto + (uint32_t)y * linha;
        *p++ = 0;
        const uint32_t *s = px + (uint32_t)y * (uint32_t)l;
        for (int x = 0; x < l; x++) { *p++ = (uint8_t)(s[x] >> 16); *p++ = (uint8_t)(s[x] >> 8); *p++ = (uint8_t)s[x]; }
    }
    int zn = deflar_zlib(bruto, bruto_n, z, DEFLAR_MAX(bruto_n) + 8);
    libera(bruto);
    if (zn < 0) { libera(z); return ERRO_SEM_MEMORIA; }
    int fd = abrir(caminho, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd < 0) { libera(z); return fd; }
    static const uint8_t assinatura[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    uint8_t ihdr[13];
    por32(ihdr, (uint32_t)l);
    por32(ihdr + 4, (uint32_t)a);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    int r = escrever_tudo(fd, assinatura, 8) < 0 ? -1 : 0;
    if (!r) r = pedaco(fd, "IHDR", ihdr, 13);
    if (!r) r = pedaco(fd, "IDAT", z, (uint32_t)zn);
    if (!r) r = pedaco(fd, "IEND", NULL, 0);
    fechar(fd);
    libera(z);
    return r < 0 ? ERRO_LIMITE : 8 + 25 + 12 + zn + 12;
}

static bool tem_pendrive(void)
{
    info_arquivo_t inf;
    entrada_pasta_t e;
    return info_arquivo("/usb", &inf) == 0 && inf.tipo == TIPO_PASTA && listar("/usb", 0, &e) >= 0;
}

/* Janela pequena por 2 segundos dizendo onde ficou (depois da captura, então
 * não aparece nela). */
static void avisar(const char *texto)
{
    int l = g_largura_texto(texto) + 32;
    if (l < 260) l = 260;
    jan_t *j = jan_abrir("Captura de tela", l, 56);
    if (!j) return;
    g_retangulo(j, 0, 0, j->larg, j->alt, COR_FUNDO);
    g_texto(j, 16, 20, texto, COR_TEXTO);
    jan_mostrar(j);
    unsigned ate = tempo_ms() + 2000;
    evento_t e;
    while (tempo_ms() < ate) {
        while (proximo_evento(j->id, &e, false) == 1)
            if (e.tipo == EV_FECHAR || e.tipo == EV_TECLA || e.tipo == EV_CLIQUE) ate = 0;
        dormir(50);
    }
    jan_fechar(j);
}

int principal(int argc, char **argv)
{
    int tam = ler_tela(NULL, 0);
    if (tam < 0) { imprimir_em(ERROS, "captura: sem tela gráfica (%s)\n", nome_erro(tam)); return 1; }
    int l = tam & 0xFFFF, a = (int)((unsigned)tam >> 16);
    uint32_t *px = aloca((uint32_t)l * (uint32_t)a * 4);
    if (!px) { imprimir_em(ERROS, "captura: sem memória\n"); return 1; }
    int r = ler_tela(px, (unsigned)l * (unsigned)a * 4);
    if (r < 0) { imprimir_em(ERROS, "captura: %s\n", nome_erro(r)); return 1; }

    char caminho[128];
    if (argc > 1) {
        copia_texto(caminho, argv[1], sizeof caminho);
    } else {
        const char *pasta = tem_pendrive() ? "/usb/Capturas" : "/capturas";
        criar_pasta(pasta);
        data_t d;
        ler_data(&d);
        formatar_texto(caminho, sizeof caminho, "%s/captura-%u-%02u-%02u-%02u%02u%02u.png", pasta,
                       d.ano, d.mes, d.dia, d.hora, d.minuto, d.segundo);
        info_arquivo_t inf;
        for (int n = 2; info_arquivo(caminho, &inf) == 0 && n < 100; n++)   /* duas no mesmo segundo */
            formatar_texto(caminho, sizeof caminho, "%s/captura-%u-%02u-%02u-%02u%02u%02u-%d.png", pasta,
                           d.ano, d.mes, d.dia, d.hora, d.minuto, d.segundo, n);
    }
    r = gravar_png(caminho, px, l, a);
    libera(px);
    if (r < 0) {
        imprimir_em(ERROS, "captura: %s: %s\n", caminho, nome_erro(r));
        registrar("captura: falhou em %s (%d)", caminho, r);
        return 1;
    }
    registrar("captura: salva %s (%dx%d, %d bytes)", caminho, l, a, r);
    imprimir("Tela salva em %s (%dx%d, %d KB)\n", caminho, l, a, (r + 1023) / 1024);
    if (argc <= 1) {
        char aviso[160];
        formatar_texto(aviso, sizeof aviso, "Salva em %s", caminho);
        avisar(aviso);
    }
    return 0;
}
