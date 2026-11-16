/* ata.c - discos rígidos IDE/ATA pelas portas clássicas (H97).
 *
 * Até aqui o disco de onde o sistema ligou só era lido pela BIOS, no boot;
 * o kernel não enxergava discos internos. Este driver fala com os dois
 * canais IDE das portas de sempre (0x1F0 e 0x170, controle em 0x3F6 e
 * 0x376), mestre e escravo, em PIO, sem interrupções (nIEN ligado): cada
 * comando espera o disco ficar pronto lendo o registrador de estado.
 * Endereços LBA de 28 bits até 128 GB e de 48 bits acima disso.
 *
 * As controladoras SATA que estão no modo IDE (compatível) aparecem aqui
 * também; o modo AHCI precisa de outro driver.
 *
 * Referência: ATA/ATAPI-6 (T13/1410D): IDENTIFY DEVICE (0xEC), READ
 * SECTORS (0x20) / EXT (0x24), WRITE SECTORS (0x30) / EXT (0x34), FLUSH
 * CACHE (0xE7) / EXT (0xEA); registradores de comando e de controle. */
#include "ata.h"
#include "portas.h"
#include "console.h"
#include "disco.h"
#include "cpu.h"
#include "texto.h"

#define ATA_DADOS     0
#define ATA_ERRO      1
#define ATA_QTD       2
#define ATA_LBA0      3
#define ATA_LBA1      4
#define ATA_LBA2      5
#define ATA_UNIDADE   6
#define ATA_ESTADO    7    /* leitura */
#define ATA_COMANDO   7    /* escrita */

#define EST_ERR  0x01
#define EST_DRQ  0x08
#define EST_DF   0x20
#define EST_DRDY 0x40
#define EST_BSY  0x80

typedef struct {
    uint16_t base, ctrl;
    int      escravo;
    bool     lba48;
    uint32_t setores;
} ata_t;

static ata_t unidades[4];
static int qtd;

static void esperar_400ns(const ata_t *u)
{
    for (int i = 0; i < 4; i++) (void)porta_le8(u->ctrl);   /* cada leitura do estado alternativo leva ~100 ns */
}

/* Espera BSY sair; com drq, espera também DRQ (ou erro). -1 em erro ou tempo esgotado. */
static int esperar(const ata_t *u, bool drq)
{
    for (uint32_t volta = 0; volta < 4000000; volta++) {
        uint8_t e = porta_le8(u->base + ATA_ESTADO);
        if (e & EST_BSY) continue;
        if (e & (EST_ERR | EST_DF)) return -1;
        if (!drq || (e & EST_DRQ)) return 0;
    }
    return -1;
}

static void selecionar(const ata_t *u, uint32_t lba, uint32_t n, bool escrever)
{
    if (u->lba48) {
        porta_escreve8(u->base + ATA_UNIDADE, (uint8_t)(0x40 | (u->escravo << 4)));
        esperar_400ns(u);
        porta_escreve8(u->base + ATA_QTD, (uint8_t)(n >> 8));
        porta_escreve8(u->base + ATA_LBA0, (uint8_t)(lba >> 24));
        porta_escreve8(u->base + ATA_LBA1, 0);
        porta_escreve8(u->base + ATA_LBA2, 0);
        porta_escreve8(u->base + ATA_QTD, (uint8_t)n);
        porta_escreve8(u->base + ATA_LBA0, (uint8_t)lba);
        porta_escreve8(u->base + ATA_LBA1, (uint8_t)(lba >> 8));
        porta_escreve8(u->base + ATA_LBA2, (uint8_t)(lba >> 16));
        porta_escreve8(u->base + ATA_COMANDO, escrever ? 0x34 : 0x24);
    } else {
        porta_escreve8(u->base + ATA_UNIDADE, (uint8_t)(0xE0 | (u->escravo << 4) | ((lba >> 24) & 0x0F)));
        esperar_400ns(u);
        porta_escreve8(u->base + ATA_QTD, (uint8_t)n);
        porta_escreve8(u->base + ATA_LBA0, (uint8_t)lba);
        porta_escreve8(u->base + ATA_LBA1, (uint8_t)(lba >> 8));
        porta_escreve8(u->base + ATA_LBA2, (uint8_t)(lba >> 16));
        porta_escreve8(u->base + ATA_COMANDO, escrever ? 0x30 : 0x20);
    }
}

/* Até 128 setores por comando (cabe no contador de 8 bits do LBA28 e é
 * pouco tempo com as interrupções desligadas). */
#define POR_VEZ 128

static int ler(disco_t *d, uint32_t lba, uint32_t n, void *buf)
{
    ata_t *u = d->dono;
    uint16_t *p = buf;
    while (n) {
        uint32_t k = n > POR_VEZ ? POR_VEZ : n;
        if (lba + k > u->setores) return -1;
        bool i = int_salva_desliga();
        selecionar(u, lba, k, false);
        for (uint32_t s = 0; s < k; s++) {
            if (esperar(u, true) < 0) { int_restaura(i); klog("[ATA] erro lendo lba %u\n", lba + s); return -1; }
            for (int w = 0; w < 256; w++) *p++ = porta_le16(u->base + ATA_DADOS);
            esperar_400ns(u);
        }
        int_restaura(i);
        lba += k; n -= k;
    }
    return 0;
}

static int escrever(disco_t *d, uint32_t lba, uint32_t n, const void *buf)
{
    ata_t *u = d->dono;
    const uint16_t *p = buf;
    while (n) {
        uint32_t k = n > POR_VEZ ? POR_VEZ : n;
        if (lba + k > u->setores) return -1;
        bool i = int_salva_desliga();
        selecionar(u, lba, k, true);
        for (uint32_t s = 0; s < k; s++) {
            if (esperar(u, true) < 0) { int_restaura(i); klog("[ATA] erro gravando lba %u\n", lba + s); return -1; }
            for (int w = 0; w < 256; w++) porta_escreve16(u->base + ATA_DADOS, *p++);
            esperar_400ns(u);
        }
        /* O disco pode guardar na própria memória: FLUSH CACHE garante que
         * os dados estão no disco antes de dizer que gravou. */
        if (esperar(u, false) < 0) { int_restaura(i); return -1; }
        porta_escreve8(u->base + ATA_COMANDO, u->lba48 ? 0xEA : 0xE7);
        int r = esperar(u, false);
        int_restaura(i);
        if (r < 0) return -1;
        lba += k; n -= k;
    }
    return 0;
}

/* IDENTIFY: true se há um disco ATA (não ATAPI) nessa posição. */
static bool identificar(ata_t *u, char *modelo, int max)
{
    if (porta_le8(u->base + ATA_ESTADO) == 0xFF) return false;     /* canal sem nada (barramento flutuando) */
    porta_escreve8(u->ctrl, 0x02);                                      /* nIEN: sem interrupções */
    porta_escreve8(u->base + ATA_UNIDADE, (uint8_t)(0xA0 | (u->escravo << 4)));
    esperar_400ns(u);
    porta_escreve8(u->base + ATA_QTD, 0);
    porta_escreve8(u->base + ATA_LBA0, 0);
    porta_escreve8(u->base + ATA_LBA1, 0);
    porta_escreve8(u->base + ATA_LBA2, 0);
    porta_escreve8(u->base + ATA_COMANDO, 0xEC);
    if (porta_le8(u->base + ATA_ESTADO) == 0) return false;        /* não existe */
    for (uint32_t v = 0; v < 1000000; v++) {
        uint8_t e = porta_le8(u->base + ATA_ESTADO);
        if (!(e & EST_BSY)) break;
    }
    if (porta_le8(u->base + ATA_LBA1) || porta_le8(u->base + ATA_LBA2)) return false;   /* ATAPI ou SATA de outro tipo */
    if (esperar(u, true) < 0) return false;
    uint16_t id[256];
    for (int w = 0; w < 256; w++) id[w] = porta_le16(u->base + ATA_DADOS);
    u->lba48 = (id[83] & (1 << 10)) != 0;
    uint32_t s28 = (uint32_t)id[60] | (uint32_t)id[61] << 16;
    uint32_t s48 = (uint32_t)id[100] | (uint32_t)id[101] << 16;
    if (id[102] || id[103]) s48 = 0xFFFFFFFFu;                /* mais de 2 TB: só os primeiros 2 TB */
    u->setores = u->lba48 && s48 ? s48 : s28;
    if (!u->setores) return false;
    int k = 0;
    for (int w = 27; w < 47 && k + 2 < max; w++) {           /* o modelo vem com os bytes trocados */
        modelo[k++] = (char)(id[w] >> 8);
        modelo[k++] = (char)(id[w] & 0xFF);
    }
    while (k > 0 && modelo[k - 1] == ' ') k--;
    modelo[k] = 0;
    return true;
}

int ata_iniciar(void)
{
    static const uint16_t bases[2][2] = { { 0x1F0, 0x3F6 }, { 0x170, 0x376 } };
    for (int c = 0; c < 2; c++)
        for (int e = 0; e < 2; e++) {
            ata_t *u = &unidades[qtd];
            u->base = bases[c][0]; u->ctrl = bases[c][1]; u->escravo = e;
            char modelo[48];
            if (!identificar(u, modelo, sizeof modelo)) continue;
            klog("[ATA] canal %d %s: %s, %u setores%s\n", c, e ? "escravo" : "mestre", modelo, u->setores, u->lba48 ? " (LBA48)" : "");
            if (disco_registrar_interno(modelo[0] ? modelo : "Disco ATA", u->setores, u, ler, escrever)) qtd++;
        }
    return qtd;
}
