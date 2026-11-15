/* armazenamento.c - pendrives: Bulk-Only Transport + SCSI (H31).
 *
 * Um pendrive USB "fala" SCSI dentro de um envelope simples (Bulk-Only):
 *   1. o sistema manda um CBW de 31 bytes pelo endpoint bulk de saída, com
 *      o comando SCSI dentro;
 *   2. os dados vão ou vêm pelos endpoints bulk;
 *   3. o pendrive responde um CSW de 13 bytes com o resultado.
 * Comandos usados: INQUIRY (quem é), TEST UNIT READY, REQUEST SENSE (por
 * que não está pronto), READ CAPACITY(10) (tamanho), READ(10) e WRITE(10).
 * Se um endpoint parar (STALL), é destravado com CLEAR_FEATURE; se o
 * pendrive se perder no protocolo, faz-se o "reset de recuperação" da
 * especificação Bulk-Only e tenta-se de novo. */
#include "usb.h"
#include "console.h"
#include "disco.h"
#include "memoria.h"
#include "texto.h"

#define CBW_ASSINATURA 0x43425355u
#define CSW_ASSINATURA 0x53425355u
#define MAX_SETORES_POR_COMANDO 128   /* 64 KB: o tamanho do buffer do EHCI */

static uint32_t etiqueta = 1;

static void destravar(usb_disp_t *d, bool entrada)
{
    uint8_t ep = entrada ? (uint8_t)(d->ep_entrada | 0x80) : d->ep_saida;
    usb_controle(d, 0x02, 1, 0, ep, NULL, 0);         /* CLEAR_FEATURE(ENDPOINT_HALT) */
    if (entrada) d->alterna_entrada = 0; else d->alterna_saida = 0;
}

static void recuperar(usb_disp_t *d)
{
    usb_controle(d, 0x21, 0xFF, 0, d->interface, NULL, 0);   /* Bulk-Only Mass Storage Reset */
    destravar(d, true);
    destravar(d, false);
}

/* Um comando SCSI completo. Devolve bytes de dados, ou <0. */
static int scsi(usb_disp_t *d, const uint8_t *cdb, uint8_t tam_cdb, void *dados, uint32_t tam, bool entrada)
{
    for (int tentativa = 0; tentativa < 3; tentativa++) {
        uint8_t cbw[31];
        memset(cbw, 0, sizeof cbw);
        uint32_t tag = etiqueta++;
        *(uint32_t *)&cbw[0] = CBW_ASSINATURA;
        *(uint32_t *)&cbw[4] = tag;
        *(uint32_t *)&cbw[8] = tam;
        cbw[12] = entrada ? 0x80 : 0x00;
        cbw[13] = 0;   /* LUN 0 */
        cbw[14] = tam_cdb;
        memcpy(&cbw[15], cdb, tam_cdb);
        int r = ehci_bulk(d->ctl, d->endereco, d->ep_saida, d->mps_saida, false, &d->alterna_saida, cbw, 31);
        if (r == USB_ERRO_PAROU) { recuperar(d); continue; }
        if (r < 0) return r;
        int feito = 0;
        if (tam) {
            feito = ehci_bulk(d->ctl, d->endereco, entrada ? d->ep_entrada : d->ep_saida,
                              entrada ? d->mps_entrada : d->mps_saida, entrada,
                              entrada ? &d->alterna_entrada : &d->alterna_saida, dados, tam);
            if (feito == USB_ERRO_PAROU) { destravar(d, entrada); feito = 0; }
            else if (feito < 0) return feito;
        }
        uint8_t csw[13];
        r = ehci_bulk(d->ctl, d->endereco, d->ep_entrada, d->mps_entrada, true, &d->alterna_entrada, csw, 13);
        if (r == USB_ERRO_PAROU) {
            destravar(d, true);
            r = ehci_bulk(d->ctl, d->endereco, d->ep_entrada, d->mps_entrada, true, &d->alterna_entrada, csw, 13);
        }
        if (r < 13 || *(uint32_t *)&csw[0] != CSW_ASSINATURA || *(uint32_t *)&csw[4] != tag) {
            if (r < 0 && r != USB_ERRO_PAROU) return r;
            recuperar(d);
            continue;
        }
        if (csw[12] == 0) return feito;
        if (csw[12] == 1) return USB_ERRO;   /* falhou: quem chamou decide (REQUEST SENSE) */
        recuperar(d);                        /* 2 = erro de fase */
    }
    return USB_ERRO;
}

static int ler(disco_t *k, uint32_t lba, uint32_t n, void *buf)
{
    usb_disp_t *d = k->dono;
    if (!k->presente) return USB_ERRO;
    uint8_t *p = buf;
    while (n) {
        uint32_t parte = n > MAX_SETORES_POR_COMANDO ? MAX_SETORES_POR_COMANDO : n;
        uint8_t cdb[10] = { 0x28, 0, (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
                            0, (uint8_t)(parte >> 8), (uint8_t)parte, 0 };
        /* Pendrives de verdade às vezes falham numa transferência e acertam
         * na seguinte (no G470, uma música do pendrive parava no meio): até 3
         * tentativas, anotadas no diário. */
        int r = -1;
        for (int t = 0; t < 3; t++) {
            r = scsi(d, cdb, 10, p, parte * k->tam_setor, true);
            if (r >= (int)(parte * k->tam_setor)) break;
            klog("[USB] leitura de %u setores em %u falhou (%d), tentativa %d\n", parte, lba, r, t + 1);
        }
        if (r < (int)(parte * k->tam_setor)) return r < 0 ? r : USB_ERRO;
        lba += parte; n -= parte; p += parte * k->tam_setor;
    }
    return 0;
}

static int escrever(disco_t *k, uint32_t lba, uint32_t n, const void *buf)
{
    usb_disp_t *d = k->dono;
    if (!k->presente) return USB_ERRO;
    const uint8_t *p = buf;
    while (n) {
        uint32_t parte = n > MAX_SETORES_POR_COMANDO ? MAX_SETORES_POR_COMANDO : n;
        uint8_t cdb[10] = { 0x2A, 0, (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
                            0, (uint8_t)(parte >> 8), (uint8_t)parte, 0 };
        int r = scsi(d, cdb, 10, (void *)p, parte * k->tam_setor, false);
        if (r < 0) return r;
        lba += parte; n -= parte; p += parte * k->tam_setor;
    }
    return 0;
}

bool armaz_conectar(usb_disp_t *d)
{
    if (!d->ep_entrada || !d->ep_saida) return false;
    d->alterna_entrada = d->alterna_saida = 0;
    uint8_t buf[64];
    uint8_t inquiry[6] = { 0x12, 0, 0, 0, 36, 0 };
    if (scsi(d, inquiry, 6, buf, 36, true) < 0) { klog("[USB] INQUIRY falhou\n"); return false; }
    /* Espera o pendrive ficar pronto (alguns levam um tempo depois de ligados). */
    bool pronto = false;
    for (int i = 0; i < 20 && !pronto; i++) {
        uint8_t tur[6] = { 0 };
        if (scsi(d, tur, 6, NULL, 0, false) == 0) { pronto = true; break; }
        uint8_t sense[6] = { 0x03, 0, 0, 0, 18, 0 };
        scsi(d, sense, 6, buf, 18, true);
        ehci_esperar_ms(d->ctl, 100);
    }
    uint8_t cap[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (scsi(d, cap, 10, buf, 8, true) < 8) { klog("[USB] READ CAPACITY falhou\n"); return false; }
    uint32_t ultimo = (uint32_t)buf[0] << 24 | (uint32_t)buf[1] << 16 | (uint32_t)buf[2] << 8 | buf[3];
    uint32_t tam_setor = (uint32_t)buf[4] << 24 | (uint32_t)buf[5] << 16 | (uint32_t)buf[6] << 8 | buf[7];
    if (tam_setor != 512) { klog("[USB] setor de %u bytes nao suportado\n", tam_setor); return false; }
    disco_t *k = disco_registrar(d->fabricante, d->produto, ultimo + 1, tam_setor, d, ler, escrever);
    d->disco = k;
    klog("[USB] armazenamento: %u setores de %u bytes (%u MB)%s\n", ultimo + 1, tam_setor,
         (ultimo + 1) / 2048, pronto ? "" : ", ainda nao estava pronto");
    return k != NULL;
}

void armaz_desconectar(usb_disp_t *d)
{
    if (d->disco) disco_removido(d->disco);
    d->disco = NULL;
}
