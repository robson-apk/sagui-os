/* instalar - grava o SaguiOS num disco, com uma partição para os seus
 * arquivos (H97).
 *
 * O disco de destino fica com:
 *   - a mesma partição do sistema (tipo 0xDA) do disco de onde o SaguiOS
 *     ligou, copiada setor por setor (estágio 1 e 2, kernel e ramdisk);
 *   - uma partição FAT32 (tipo 0x0C) no resto do disco, alinhada em 1 MB,
 *     com o rótulo SAGUIOS e o arquivo de configurações do sistema. No boot
 *     pelo disco, o kernel reconhece essa FAT32 como "do SaguiOS" e a monta
 *     em /usb sozinho: configurações, programas instalados, capturas e o
 *     que você guardar ali continuam depois de desligar.
 *
 * Uso: instalar               (janela)
 *      instalar hd1 --sim     (sem perguntar; para os testes)
 *
 * A FAT32 é formatada aqui, pela especificação da Microsoft ("FAT32 File
 * System Specification", 2000): setor de boot com o BPB, FSInfo, cópia do
 * setor de boot no setor 6, duas FATs e a pasta raiz no cluster 2. */
#include "sagui.h"
#include "gui.h"
#include "icones.h"

#define SETOR      512
#define POR_VEZ    128                    /* setores por chamada */
#define DADOS_MIN  (64u * 2048u)          /* 64 MB para os arquivos, no mínimo */

static uint8_t *buf;                      /* POR_VEZ setores */
static char erro[120];
static void (*avisar_progresso)(int pct, const char *etapa);

static int setores(const char *disco, uint32_t lba, uint32_t n, void *b, bool gravar)
{
    pedido_setores_t p;
    memset(&p, 0, sizeof p);
    copia_texto(p.nome, disco, sizeof p.nome);
    p.lba = lba; p.n = n; p.escrever = gravar; p.buf = b;
    int r = disco_setores(&p);
    if (r < 0) formatar_texto(erro, sizeof erro, "%s %s no setor %u: %s", gravar ? "gravando" : "lendo", disco, lba, nome_erro(r));
    return r;
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void poe16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void poe32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* CHS de 3 bytes da tabela de partições (255 cabeças, 63 setores). */
static void chs(uint8_t *p, uint32_t lba)
{
    uint32_t c = lba / (255 * 63), h = (lba / 63) % 255, s = lba % 63 + 1;
    if (c > 1023) { c = 1023; h = 254; s = 63; }
    p[0] = (uint8_t)h; p[1] = (uint8_t)(s | ((c >> 2) & 0xC0)); p[2] = (uint8_t)c;
}

/* A partição do sistema (0xDA) de um disco: true e o fim (primeiro setor depois dela). */
static bool sistema_em(const char *disco, uint32_t *fim, uint8_t *mbr)
{
    if (setores(disco, 0, 1, mbr, false) < 0 || mbr[510] != 0x55 || mbr[511] != 0xAA) return false;
    for (int k = 0; k < 4; k++) {
        const uint8_t *e = mbr + 446 + k * 16;
        if (e[4] == 0xDA) { *fim = le32(e + 8) + le32(e + 12); return *fim > 1; }
    }
    return false;
}

/* Grava n setores de zeros a partir de lba. */
static int zerar(const char *disco, uint32_t lba, uint32_t n)
{
    memset(buf, 0, POR_VEZ * SETOR);
    while (n) {
        uint32_t k = n > POR_VEZ ? POR_VEZ : n;
        if (setores(disco, lba, k, buf, true) < 0) return -1;
        lba += k; n -= k;
    }
    return 0;
}

/* Formata a FAT32 em [ini, ini + n). */
static int formatar_fat32(const char *disco, uint32_t ini, uint32_t n)
{
    /* Setores por cluster pela tabela da especificação. */
    uint32_t spc = n <= 532480 ? 1 : n <= 16777216 ? 8 : n <= 33554432 ? 16 : n <= 67108864 ? 32 : 64;
    const uint32_t reservados = 32, nfats = 2;
    uint32_t t1 = n - reservados, t2 = (256 * spc + nfats) / 2;
    uint32_t fatsz = (t1 + t2 - 1) / t2;
    uint32_t dados = n - reservados - nfats * fatsz, clusters = dados / spc;
    if (clusters < 65525) { copia_texto(erro, "o espaço é pequeno demais para uma FAT32", sizeof erro); return -1; }

    uint8_t s[SETOR];
    memset(s, 0, sizeof s);
    s[0] = 0xEB; s[1] = 0x58; s[2] = 0x90;
    memcpy(s + 3, "SAGUIOS ", 8);
    poe16(s + 11, SETOR);
    s[13] = (uint8_t)spc;
    poe16(s + 14, reservados);
    s[16] = (uint8_t)nfats;
    s[21] = 0xF8;                                    /* disco fixo */
    poe16(s + 24, 63);
    poe16(s + 26, 255);
    poe32(s + 28, ini);                              /* setores antes da partição */
    poe32(s + 32, n);
    poe32(s + 36, fatsz);
    poe32(s + 44, 2);                                /* pasta raiz no cluster 2 */
    poe16(s + 48, 1);                                /* FSInfo */
    poe16(s + 50, 6);                                /* cópia do setor de boot */
    s[64] = 0x80;
    s[66] = 0x29;
    poe32(s + 67, tempo_ms() ^ 0x5A6E1D05u);         /* número de série */
    memcpy(s + 71, "SAGUIOS    ", 11);
    memcpy(s + 82, "FAT32   ", 8);
    s[510] = 0x55; s[511] = 0xAA;

    uint8_t fsinfo[SETOR];
    memset(fsinfo, 0, sizeof fsinfo);
    poe32(fsinfo, 0x41615252);
    poe32(fsinfo + 484, 0x61417272);
    poe32(fsinfo + 488, clusters - 1);               /* livres (o 2 é a raiz) */
    poe32(fsinfo + 492, 3);                          /* próximo livre */
    poe32(fsinfo + 508, 0xAA550000);

    /* Área reservada e as FATs zeradas; depois os setores com conteúdo. */
    if (avisar_progresso) avisar_progresso(80, "Formatando a partição dos arquivos");
    if (zerar(disco, ini, reservados + nfats * fatsz) < 0) return -1;
    if (zerar(disco, ini + reservados + nfats * fatsz, spc) < 0) return -1;   /* a pasta raiz */
    if (setores(disco, ini, 1, s, true) < 0 || setores(disco, ini + 6, 1, s, true) < 0) return -1;
    if (setores(disco, ini + 1, 1, fsinfo, true) < 0 || setores(disco, ini + 7, 1, fsinfo, true) < 0) return -1;
    uint8_t fat[SETOR];
    memset(fat, 0, sizeof fat);
    poe32(fat, 0x0FFFFFF8);
    poe32(fat + 4, 0x0FFFFFFF);
    poe32(fat + 8, 0x0FFFFFFF);                      /* a raiz: um cluster só */
    for (uint32_t f = 0; f < nfats; f++)
        if (setores(disco, ini + reservados + f * fatsz, 1, fat, true) < 0) return -1;
    uint8_t raiz[SETOR];
    memset(raiz, 0, sizeof raiz);
    memcpy(raiz, "SAGUIOS    ", 11);
    raiz[11] = 0x08;                                 /* rótulo do volume */
    if (setores(disco, ini + reservados + nfats * fatsz, 1, raiz, true) < 0) return -1;
    return 0;
}

/* A instalação inteira. 0 = pronto; -1 com o motivo em erro. */
static int instalar(const char *origem, const char *destino, uint32_t total)
{
    uint8_t mbr[SETOR];
    uint32_t fim;
    if (!sistema_em(origem, &fim, mbr)) { formatar_texto(erro, sizeof erro, "%s não tem a partição do SaguiOS", origem); return -1; }
    uint32_t dados_ini = (fim + 2047) / 2048 * 2048;
    if (total < dados_ini + DADOS_MIN) { formatar_texto(erro, sizeof erro, "%s é pequeno demais (precisa de %u MB)", destino, (dados_ini + DADOS_MIN) / 2048 + 1); return -1; }
    uint32_t dados_n = total - dados_ini;
    registrar("instalar: %s -> %s: sistema %u setores, arquivos em %u (%u MB)", origem, destino, fim, dados_ini, dados_n / 2048);

    /* 1. O sistema, setor por setor (o setor 0 vai por último, com a tabela nova). */
    for (uint32_t lba = 1; lba < fim; ) {
        uint32_t k = fim - lba > POR_VEZ ? POR_VEZ : fim - lba;
        if (setores(origem, lba, k, buf, false) < 0 || setores(destino, lba, k, buf, true) < 0) return -1;
        lba += k;
        if (avisar_progresso) avisar_progresso((int)(lba * 70 / fim), "Copiando o sistema");
    }
    /* Apaga o começo da área dos arquivos (um sistema de arquivos antigo ali confundiria). */
    if (zerar(destino, dados_ini, 64) < 0) return -1;

    /* 2. A FAT32 dos arquivos. */
    if (formatar_fat32(destino, dados_ini, dados_n) < 0) return -1;

    /* 3. A tabela de partições: a do sistema como estava, e a FAT32 depois. */
    for (int k = 0; k < 4; k++) {
        uint8_t *e = mbr + 446 + k * 16;
        if (e[4] != 0xDA) memset(e, 0, 16);
    }
    int livre = 0;
    while (livre < 4 && mbr[446 + livre * 16 + 4]) livre++;
    uint8_t *e = mbr + 446 + livre * 16;
    e[0] = 0;
    chs(e + 1, dados_ini);
    e[4] = 0x0C;                                     /* FAT32 com LBA */
    chs(e + 5, total - 1);
    poe32(e + 8, dados_ini);
    poe32(e + 12, dados_n);
    if (avisar_progresso) avisar_progresso(90, "Gravando a tabela de partições");
    if (setores(destino, 0, 1, mbr, true) < 0) return -1;

    /* 4. O arquivo de configurações: é ele que faz o sistema montar a
     * partição sozinho no boot. Se outro disco está em /usb, ele é liberado
     * antes (só um fica montado por vez). */
    info_disco_t d;
    for (int i = 0; listar_discos(i, &d) == 1; i++)
        if (d.montado) desmontar(d.nome);
    if (disco_reler(destino) < 0 || montar(destino) < 0) { formatar_texto(erro, sizeof erro, "a partição nova de %s não montou", destino); return -1; }
    criar_pasta("/usb/.saguios");
    int fd = abrir("/usb/.saguios/configuracoes.txt", ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    if (fd >= 0) {
        const char *t = "# SaguiOS instalado neste disco\nsistema.instalado = sim\n";
        escrever(fd, t, (unsigned)tam_texto(t));
        fechar(fd);
    }
    desmontar(destino);
    if (fd < 0) { copia_texto(erro, "não deu para criar o arquivo de configurações", sizeof erro); return -1; }
    if (avisar_progresso) avisar_progresso(100, "Pronto");
    registrar("instalar: pronto em %s", destino);
    return 0;
}

/* O disco de onde o sistema ligou: o primeiro com a partição 0xDA. */
static bool achar_origem(char *nome, int max)
{
    info_disco_t d;
    uint8_t mbr[SETOR];
    uint32_t fim;
    for (int i = 0; listar_discos(i, &d) == 1; i++)
        if (sistema_em(d.nome, &fim, mbr)) { copia_texto(nome, d.nome, (size_t)max); return true; }
    return false;
}

/* ------------------------------------------------------------ linha de comando */

static void progresso_texto(int pct, const char *etapa)
{
    static int ultimo = -1;
    if (pct / 10 == ultimo / 10 && pct != 100) return;
    ultimo = pct;
    imprimir("  %3d%%  %s\n", pct, etapa);
}

static int linha_de_comando(const char *destino)
{
    char origem[8];
    if (!achar_origem(origem, sizeof origem)) { imprimir("instalar: não achei o disco do SaguiOS\n"); return 1; }
    if (compara_texto(origem, destino) == 0) { imprimir("instalar: %s é o disco de onde o sistema ligou\n", destino); return 1; }
    info_disco_t d;
    uint32_t total = 0;
    for (int i = 0; listar_discos(i, &d) == 1; i++)
        if (compara_texto(d.nome, destino) == 0) total = d.setores;
    if (!total) { imprimir("instalar: não existe o disco %s\n", destino); return 1; }
    avisar_progresso = progresso_texto;
    imprimir("Instalando o SaguiOS de %s em %s...\n", origem, destino);
    if (instalar(origem, destino, total) < 0) { imprimir("instalar: %s\n", erro); registrar("instalar: erro: %s", erro); return 1; }
    imprimir("Pronto. Desligue, tire o disco de instalação e ligue pelo %s.\n", destino);
    return 0;
}

/* ------------------------------------------------------------ janela */

#define L 520
#define A 380

static jan_t *j;
static int pct_atual = -1;

static void progresso_janela(int pct, const char *etapa)
{
    if (pct == pct_atual) return;
    pct_atual = pct;
    g_retangulo(j, 20, 280, L - 40, 50, COR_FUNDO);
    g_texto(j, 20, 280, etapa, COR_TEXTO);
    g_ret_redondo(j, 20, 304, L - 40, 14, 7, 0xDDE2E6, 0xDDE2E6);
    if (pct > 0) g_ret_redondo(j, 20, 304, (L - 40) * pct / 100, 14, 7, COR_DESTAQUE, COR_DESTAQUE);
    jan_mostrar_area(j, 20, 280, L - 40, 50);
}

int principal(int argc, char **argv)
{
    buf = aloca(POR_VEZ * SETOR);
    if (!buf) return 1;
    if (argc >= 3 && compara_texto(argv[2], "--sim") == 0) return linha_de_comando(argv[1]);
    if (argc >= 2) { imprimir("uso: instalar          (janela)\n      instalar hd1 --sim\n"); return 1; }

    j = jan_abrir("Instalar o SaguiOS", L, A);
    if (!j) return 1;
    char origem[8];
    bool tem_origem = achar_origem(origem, sizeof origem);
    /* Os discos onde dá para instalar: todos menos o de origem. */
    info_disco_t discos[4];
    botao_t botoes[4];
    char rotulos[4][80];
    int qtd = 0;
    info_disco_t d;
    for (int i = 0; qtd < 4 && listar_discos(i, &d) == 1; i++) {
        if (tem_origem && compara_texto(d.nome, origem) == 0) continue;
        discos[qtd] = d;
        uint32_t mb = d.setores / 2048;
        if (mb >= 1024) formatar_texto(rotulos[qtd], sizeof rotulos[qtd], "%s: %s (%u,%u GB)", d.nome, d.produto, mb / 1024, mb % 1024 * 10 / 1024);
        else formatar_texto(rotulos[qtd], sizeof rotulos[qtd], "%s: %s (%u MB)", d.nome, d.produto, mb);
        botao_t b = { 20, 108 + qtd * 40, L - 40, 32, rotulos[qtd], false, icone_programas, false, true };
        botoes[qtd++] = b;
    }
    g_retangulo(j, 0, 0, L, A, COR_FUNDO);
    g_icone(j, 20, 18, icone_programas, 20);
    g_texto(j, 50, 20, "Instalar o SaguiOS num disco", COR_TEXTO);
    g_texto(j, 20, 52, "O sistema e uma partição para os seus arquivos vão", COR_TEXTO_FRACO);
    g_texto(j, 20, 70, "para o disco escolhido. Tudo o que há nele é apagado.", COR_TEXTO_FRACO);
    if (!tem_origem) g_texto(j, 20, 116, "Não achei o disco de onde o SaguiOS ligou.", COR_TEXTO);
    else if (!qtd) g_texto(j, 20, 116, "Nenhum outro disco encontrado.", COR_TEXTO);
    for (int i = 0; i < qtd; i++) botao_desenhar(j, &botoes[i]);
    botao_t fechar_b = { L - 116, A - 44, 96, 32, "Fechar", false };
    botao_desenhar(j, &fechar_b);
    jan_mostrar(j);
    registrar("instalar: janela aberta (%d discos)", qtd);
    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR || botao_tratar(j, &fechar_b, &e) || (e.tipo == EV_TECLA && e.tecla == TECLA_ESC)) break;
        for (int i = 0; i < qtd && tem_origem; i++) {
            if (!botao_tratar(j, &botoes[i], &e)) continue;
            char pergunta[160];
            formatar_texto(pergunta, sizeof pergunta, "Apagar tudo em %s e instalar o SaguiOS?", discos[i].nome);
            if (!confirmar("Instalar", pergunta)) break;
            avisar_progresso = progresso_janela;
            pct_atual = -1;
            if (instalar(origem, discos[i].nome, discos[i].setores) < 0) {
                registrar("instalar: erro: %s", erro);
                mensagem("Não deu para instalar", erro);
            } else {
                char fim[160];
                formatar_texto(fim, sizeof fim, "Pronto. Desligue, tire o disco de instalação e ligue pelo %s.", discos[i].nome);
                mensagem("SaguiOS instalado", fim);
            }
            break;
        }
    }
    jan_fechar(j);
    return 0;
}
