/* principal.c - ponto de entrada em C do kernel do SaguiOS.
 *
 * Confere que o carregador fez o trabalho dele (assinatura e boot_info),
 * mostra a saudação e a memória, e liga as partes do kernel em ordem:
 * interrupções e relógio, memória física e alocador, teclado, sistema de
 * arquivos e, por fim, o terminal. Tudo cabe em poucas linhas na tela: no
 * G470 o mapa de memória grande empurrava a saudação para fora (erro 1). */
#include "hda.h"
#include "rede.h"
#include "bootinfo.h"
#include "console.h"
#include "texto.h"
#include "cpu.h"
#include "relogio.h"
#include "autoteste.h"
#include "memoria.h"
#include "teclado.h"
#include "sgfs.h"
#include "paginacao.h"
#include "processo.h"
#include "sistema.h"
#include "vfs.h"
#include "mouse.h"
#include "i8042.h"
#include "gui.h"
#include "usb.h"
#include "ata.h"
#include "video.h"

#define SAGUI_VERSAO "2.1"

info_boot_t info_boot;
const char *sagui_versao = SAGUI_VERSAO;
uint32_t imagem_bytes;   /* tamanho útil da imagem, lido do cabeçalho de boot (setores x 512) */
regiao_e820_t mapa_memoria[MAPA_MAX];

static const char *nome_tipo_e820(uint32_t t)
{
    switch (t) {
    case 1: return "livre";
    case 2: return "reservada";
    case 3: return "ACPI (recuperavel)";
    case 4: return "ACPI NVS";
    case 5: return "defeituosa";
    default: return "outra";
    }
}

static void parar_com_erro(const char *msg)
{
    console_cor(COR_BRANCO, COR_VERMELHO);
    kprintf("\n SaguiOS: %s \n", msg);
    for (;;)
        __asm__ volatile("cli; hlt");
}

/* Soma a RAM livre (tipo 1) do mapa E820. Entradas com o atributo estendido
 * presente mas com o bit 0 desligado devem ser ignoradas (ACPI 3.0).
 * Com abaixo_de_4g, conta só a parte que um kernel de 32 bits sem PAE
 * consegue endereçar. */
static uint64_t memoria_livre(bool abaixo_de_4g)
{
    const uint64_t limite = 0x100000000ull;
    uint64_t total = 0;
    for (uint32_t i = 0; i < info_boot.mapa_qtd; i++) {
        regiao_e820_t *r = &mapa_memoria[i];
        if (r->tipo != E820_LIVRE || !(r->extra & 1))
            continue;
        uint64_t ini = r->base, fim = r->base + r->tamanho;
        if (abaixo_de_4g) {
            if (ini >= limite) continue;
            if (fim > limite) fim = limite;
        }
        total += fim - ini;
    }
    return total;
}

/* O mapa completo vai só para a serial. No G470 ele tem ~20 regiões, e
 * mostrá-lo na tela de 25 linhas empurrava a saudação para fora (erro 1). */
static void registrar_mapa(void)
{
    klog("Mapa de memoria (E820), %u regioes:\n", info_boot.mapa_qtd);
    for (uint32_t i = 0; i < info_boot.mapa_qtd; i++) {
        regiao_e820_t *r = &mapa_memoria[i];
        uint64_t fim = r->base + r->tamanho - 1;
        klog("  %08x%08x - %08x%08x  %s\n",
             (uint32_t)(r->base >> 32), (uint32_t)r->base,
             (uint32_t)(fim >> 32), (uint32_t)fim, nome_tipo_e820(r->tipo));
    }
}

/* Confere o cabeçalho do SGFS (o leitor completo vem na H08). */
static void conferir_ramdisk(void)
{
    const uint8_t *rd = (const uint8_t *)info_boot.ramdisk_inicio;
    if (info_boot.ramdisk_bytes < 32 || memcmp(rd, "SGFS", 4) != 0) {
        kprintf("Ramdisk: NAO encontrado em %p\n", rd);
        return;
    }
    uint32_t entradas = *(const uint32_t *)(rd + 8);
    klog("Ramdisk SGFS: %u bytes em %p, %u entradas\n", info_boot.ramdisk_bytes, rd, entradas);
}

/* Imagens de teste podem pedir um programa inicial com argumentos, ex.:
 * "/bin/ola um dois". O kernel o executa e espera ele terminar. */
static void rodar_programa_de_teste(char *linha)
{
    char *argv[16];
    int argc = 0;
    for (char *p = linha; *p && argc < 15;) {
        while (*p == ' ') *p++ = 0;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    argv[argc] = NULL;
    int pid = processo_executar(argv[0], argc, argv, NULL);
    klog("[TESTE] programa %s iniciado: %d\n", argv[0], pid);
    if (pid < 0) return;
    while (processo_por_pid(pid))
        cpu_espera();
    klog("[TESTE] programa terminou\n");
}

/* Uma linha de status da inicialização: "[ ok ] texto". */
static void etapa(const char *texto)
{
    console_cor(COR_VERDE_CLARO, COR_PRETO);
    kprintf("[ ok ] ");
    console_cor(COR_CINZA, COR_PRETO);
    kprintf("%s\n", texto);
}

void inicio_kernel(uint32_t magico, info_boot_t *bi)
{
    serial_iniciar();
    console_iniciar();
    klog("\n[SAGUI] kernel iniciado\n");

    if (magico != BOOT_MAGICO || bi->assinatura != BOOTINFO_ASSIN)
        parar_com_erro("nao fui carregado pelo estagio 2 do SaguiOS");

    /* A boot_info e o mapa estão na memória baixa, que mais tarde vira área
     * livre. Copiamos para dentro do kernel antes de qualquer outra coisa. */
    info_boot = *bi;
    imagem_bytes = *(const uint32_t *)(0x7E00 + 48) * 512;   /* CAB_IMG_SETORES, ainda na memória baixa */
    info_boot.teste[63] = 0;
    if (info_boot.mapa_qtd > MAPA_MAX)
        info_boot.mapa_qtd = MAPA_MAX;
    memcpy(mapa_memoria, (const void *)info_boot.mapa_end, info_boot.mapa_qtd * sizeof(regiao_e820_t));

    /* H05, H06, H10: interrupções, relógio, memória e paginação. Vêm antes
     * de qualquer texto porque o terminal gráfico (H18) precisa do kmalloc e
     * da janela do kernel para o framebuffer. Até lá, só a serial registra. */
    gdt_iniciar();
    idt_iniciar();
    pic_iniciar();
    relogio_iniciar();
    int_liga();
    memfisica_iniciar();
    paginacao_iniciar();
    void energia_iniciar(void);
    energia_iniciar();   /* tabelas ACPI: desligar e reiniciar (H36) */

    /* H17/H18: terminal gráfico com a fonte própria. Se não houver modo
     * gráfico, o console continua no modo texto e avisa. */
    bool grafico = console_grafico();

    int largura = console_colunas();
    console_cor(COR_AMARELO, COR_VERDE);
    kprintf("%*s", largura, "");
    kprintf("  %-*s%20s  ", largura - 24, "Ola! Eu sou o SaguiOS.", "versão " SAGUI_VERSAO);
    kprintf("%*s", largura, "");
    console_cor(COR_CINZA, COR_PRETO);

    uint64_t livre = memoria_livre(false);
    uint64_t usavel = memoria_livre(true);
    console_cor(COR_BRANCO, COR_PRETO);
    kprintf("Memoria detectada: %u MB livres", (uint32_t)(livre >> 20));
    console_cor(COR_CINZA, COR_PRETO);
    kprintf(" | usável (abaixo de 4 GB): %u MB\n", (uint32_t)(usavel >> 20));
    registrar_mapa();
    static const char *metodos_a20[] = { "ja ligada", "BIOS", "porta 0x92", "teclado" };
    kprintf("E820: %u regioes | A20: %s | disco 0x%x | VESA: ", info_boot.mapa_qtd,
            info_boot.a20_metodo < 4 ? metodos_a20[info_boot.a20_metodo] : "?", info_boot.disco);
    if (info_boot.vesa_ok)
        kprintf("%ux%ux%u\n", info_boot.vesa_largura, info_boot.vesa_altura, info_boot.vesa_bpp);
    else
        kprintf("nenhum\n");
    klog("[SAGUI] memoria_mb=%u usavel_mb=%u\n", (uint32_t)(livre >> 20), (uint32_t)(usavel >> 20));

    etapa("Interrupções: GDT e IDT próprias, PIC remapeado, relógio a 100 Hz");
    {
        char linha[96];
        snformatar(linha, sizeof linha, "Memória física: %u MB gerenciados em páginas de 4 KB, kmalloc pronto",
                   memfisica_livres() / 256);
        etapa(linha);
    }
    etapa("Paginação: páginas de 4 KB, um espaço de endereço por processo");
    if (grafico) {
        char linha[96];
        snformatar(linha, sizeof linha, "Vídeo: %ux%u, %u bits, fonte própria 8x16 com acentos",
                   info_boot.vesa_largura, info_boot.vesa_altura, info_boot.vesa_bpp);
        etapa(linha);
    } else {
        console_cor(COR_AMARELO, COR_PRETO);
        kprintf("[aviso] Modo gráfico indisponível: o sistema continua em modo texto.\n");
        console_cor(COR_CINZA, COR_PRETO);
        klog("[SAGUI] sem modo grafico, modo texto\n");
    }

    /* H11: a partir daqui o relógio troca de processo (round-robin). */
    processos_iniciar();
    sistema_iniciar();
    etapa("Processos: round-robin a cada 20 ms, programas no anel 3, int 0x80");

    /* H07: teclado. */
    teclado_iniciar();
    etapa("Teclado PS/2 com layout ABNT2 (acentos com teclas mortas, ç)");

    /* H19: mouse / touchpad na porta auxiliar do mesmo controlador. */
    if (mouse_iniciar())
        etapa("Mouse/touchpad PS/2: cursor desenhado pelo SaguiOS, IRQ 12");
    else {
        /* Os números ajudam a entender o hardware a partir de uma foto:
         * configuração do controlador e respostas do touchpad (FA = ok). */
        const i8042_diag_t *d = i8042_diagnostico();
        console_cor(COR_AMARELO, COR_PRETO);
        kprintf("[aviso] Mouse/touchpad PS/2 não respondeu (cfg %x->%x, F6=%x FF=%x F4=%x). Teclado segue normal.\n",
                (uint32_t)d->config_lida & 0xFFF, (uint32_t)d->config_final, (uint32_t)d->resposta_padrao & 0xFFF,
                (uint32_t)d->resposta_reset & 0xFFF, (uint32_t)d->resposta_ligar & 0xFFF);
        console_cor(COR_CINZA, COR_PRETO);
    }

    /* H08: sistema de arquivos do ramdisk. */
    conferir_ramdisk();
    if (sgfs_iniciar((const void *)info_boot.ramdisk_inicio, info_boot.ramdisk_bytes)) {
        char linha[96];
        vfs_iniciar();
        snformatar(linha, sizeof linha, "Arquivos: SGFS com %u entradas (%u KB) + camada gravável na RAM",
                   sgfs_quantidade(), (info_boot.ramdisk_bytes + 1023) / 1024);
        etapa(linha);
    } else {
        console_cor(COR_VERMELHO_CLARO, COR_PRETO);
        kprintf("[ERRO] ramdisk SGFS invalido: o sistema segue sem arquivos\n");
        console_cor(COR_CINZA, COR_PRETO);
    }
    /* H29-H31: USB 2.0 (EHCI), hubs e pendrives de dados. */
    usb_iniciar();
    /* H97: discos rígidos IDE/ATA (o instalador grava neles). */
    {
        int n = ata_iniciar();
        if (n) { char linha[64]; snformatar(linha, sizeof linha, "Discos: %d disco(s) IDE/ATA", n); etapa(linha); }
    }
    {
        char linha[96];
        int n = ehci_quantidade();
        if (n) {
            snformatar(linha, sizeof linha, "USB: %d controlador(es) EHCI, pendrives em /usb (comandos discos, montar)", n);
            etapa(linha);
        } else {
            console_cor(COR_AMARELO, COR_PRETO);
            kprintf("[aviso] Nenhum controlador USB 2.0 (EHCI) encontrado.\n");
            console_cor(COR_CINZA, COR_PRETO);
        }
    }
    /* H40: som pela placa Intel HD Audio, se houver (o bip continua de reserva). */
    if (hda_iniciar()) {
        misturador_iniciar();
        char linha[128];
        snformatar(linha, sizeof linha, "Som: Intel HD Audio, %s", hda_descricao());
        etapa(linha);
    } else {
        klog("[SOM] sem placa HD Audio: so o bip\n");
    }
    /* H50: gerador de números aleatórios (precisa do relógio rodando). */
    void aleatorio_iniciar(void);
    aleatorio_iniciar();
    /* Fase 3 da 2.0: rede com cabo (DHCP na tarefa da rede). */
    rede_iniciar();
    if (rede_tem_placa()) {
        char linha[128], r[80];
        rede_relatorio(r, sizeof r);
        snformatar(linha, sizeof linha, "Rede: %s (comandos rede e ping)", r);
        etapa(linha);
    }
    /* H84: resolução nativa do painel pela placa Intel do G470, se a
     * configuração (lida do pendrive, já montado pelo USB acima) pedir. */
    if (grafico && video_nativo_tentar()) {
        char linha[96];
        snformatar(linha, sizeof linha, "Vídeo: resolução nativa do painel, %ux%u", video_largura(), video_altura());
        etapa(linha);
    }
    klog("[SAGUI] boot ok\n");

    bool vai_ter_gui = grafico && !(info_boot.opcoes & 2);
    if (info_boot.teste[0] == '/' && !vai_ter_gui)
        rodar_programa_de_teste(info_boot.teste);
    else if (info_boot.teste[0] && info_boot.teste[0] != '/')
        autoteste_executar(info_boot.teste);

    /* H20: com vídeo e sem a opção --console, a área de trabalho assume a
     * tela; o shell passa a rodar dentro do programa Terminal (H24). */
    if (grafico && !(info_boot.opcoes & 2) && gui_iniciar()) {
        console_esconder(true);
        klog("[SAGUI] area de trabalho no ar\n");
        misturador_som_sistema(SOM_INICIO);
        if (info_boot.teste[0] == '/') {
            rodar_programa_de_teste(info_boot.teste);   /* testes da fase 5: programa gráfico */
        } else {
            char *argv_t[] = { "terminal", NULL };
            int pid = processo_executar("/bin/terminal", 1, argv_t, NULL);
            klog("[SAGUI] terminal: %d\n", pid);
        }
        for (;;)
            cpu_espera();   /* o processo 0 só descansa; o resto são processos e a tarefa das janelas */
    }

    /* H16: o primeiro processo é o shell, um programa comum no anel 3. O
     * terminal provisório da H09 foi removido. Daqui em diante este código
     * é só o processo 0 (ocioso). */
    for (;;) {
        char *argv[] = { "sagsh", NULL };
        int pid = processo_executar("/bin/sagsh", 1, argv, NULL);
        if (pid < 0) {
            console_cor(COR_VERMELHO_CLARO, COR_PRETO);
            kprintf("Não consegui iniciar /bin/sagsh (erro %d).\n", pid);
            console_cor(COR_CINZA, COR_PRETO);
            for (;;) cpu_espera();
        }
        klog("[SAGUI] shell iniciado: processo %d\n", pid);
        while (processo_por_pid(pid))
            cpu_espera();
        kprintf("\n[SaguiOS] O shell foi fechado. Abrindo um novo...\n");
    }
}
