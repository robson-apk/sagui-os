/* chamadas_processos.c - chamadas dos grupos "processos" e "diversas". */
#include "pci.h"
#include "sistema.h"
#include "console.h"
#include "memoria.h"
#include "processo.h"
#include "relogio.h"
#include "texto.h"
#include "bootinfo.h"
#include "vfs.h"
#include "fat32.h"

SEM_RETORNO void maquina_reiniciar(void);

int32_t sis_sair(uint32_t codigo, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    processo_encerrar(atual, (int)codigo, NULL);
    return 0;
}

int32_t sis_pid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return atual->pid;
}

int32_t sis_matar(uint32_t pid, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    processo_t *p = processo_por_pid((int)pid);
    if (!p || p->estado == P_ZUMBI) return ERRO_SEM_PROCESSO;
    if (!p->usuario) return ERRO_ACESSO;   /* tarefas do kernel e o ocioso não */
    processo_encerrar(p, SAIDA_ENCERRADO_PELO_KERNEL, "encerrado pelo comando matar");
    return 0;
}

/* esperar(pid ou -1, &codigo, sem_bloquear): recolhe um filho que terminou.
 * Se nenhum terminou ainda, bloqueia e a chamada é repetida quando um filho
 * terminar (processo_encerrar acorda o pai); com sem_bloquear, devolve 0. */
int32_t sis_esperar(uint32_t pid, uint32_t ptr_codigo, uint32_t sem_bloquear, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    int *codigo = (int *)ptr_codigo;
    if (codigo && !usuario_ok(codigo, sizeof(int), true)) return ERRO_ENDERECO;
    bool tem_filho = false;
    for (int i = 1; i < MAX_PROCESSOS; i++) {
        processo_t *p = processo_indice(i);
        if (p->estado == P_LIVRE || p->pai != atual->pid) continue;
        if ((int)pid != -1 && p->pid != (int)pid) continue;
        tem_filho = true;
        if (p->estado == P_ZUMBI) {
            int r = p->pid;
            if (codigo) *codigo = p->codigo_saida;
            processo_liberar(p);
            return r;
        }
    }
    if (!tem_filho) return ERRO_SEM_PROCESSO;
    if (sem_bloquear) return 0;   /* ninguém terminou ainda; quem chamou não quer esperar */
    bloquear(atual);
    return SIS_REPETIR;
}

static const char canal_relogio = 0;

int32_t sis_dormir(uint32_t ms, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    uint32_t tiques = (ms * TIQUES_POR_SEGUNDO + 999) / 1000;
    if (tiques == 0) tiques = 1;
    atual->acordar_em = relogio_tiques() + tiques;
    bloquear(&canal_relogio);
    return 0;   /* resultado já pronto: não repete ao acordar */
}

int32_t sis_tempo(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return (int32_t)(relogio_tiques() * (1000 / TIQUES_POR_SEGUNDO));
}

int32_t sis_data(uint32_t ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    data_t *dt = (data_t *)ptr;
    if (!usuario_ok(dt, sizeof *dt, true)) return ERRO_ENDERECO;
    data_hora_t h;
    cmos_ler_data(&h);
    dt->ano = h.ano; dt->mes = h.mes; dt->dia = h.dia;
    dt->hora = h.hora; dt->minuto = h.minuto; dt->segundo = h.segundo;
    dt->dia_semana = (uint8_t)dia_da_semana(h.ano, h.mes, h.dia);
    return 0;
}

/* SIS_AJUSTAR_DATA (&data): acerta o relógio da placa-mãe (H38). */
int32_t sis_ajustar_data(uint32_t ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    const data_t *dt = (const data_t *)ptr;
    if (!usuario_ok(dt, sizeof *dt, false)) return ERRO_ENDERECO;
    static const uint8_t dias[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (dt->ano < 2000 || dt->ano > 2099 || dt->mes < 1 || dt->mes > 12 || dt->dia < 1 || dt->dia > dias[dt->mes - 1] ||
        dt->hora > 23 || dt->minuto > 59 || dt->segundo > 59)
        return ERRO_INVALIDO;
    data_hora_t h = { dt->ano, dt->mes, dt->dia, dt->hora, dt->minuto, dt->segundo };
    cmos_gravar_data(&h);
    klog("[RELOGIO] acertado para %02u/%02u/%u %02u:%02u:%02u\n", h.dia, h.mes, h.ano, h.hora, h.minuto, h.segundo);
    return 0;
}

int32_t sis_info_sistema(uint32_t tipo, uint32_t ptr, uint32_t n, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (tipo == INFO_MEMORIA) {
        info_memoria_t *m = (info_memoria_t *)ptr;
        if (!usuario_ok(m, sizeof *m, true)) return ERRO_ENDERECO;
        estat_memoria_t est;
        memoria_estatisticas(&est);
        m->ram_total_kb = (uint32_t)(est.ram_total >> 10);
        m->gerida_kb = est.paginas_geridas * 4;
        m->livre_kb = est.paginas_livres * 4;
        m->usada_kb = m->gerida_kb - m->livre_kb;
        return 0;
    }
    if (tipo == INFO_PROCESSOS) {
        info_processo_t *v = (info_processo_t *)ptr;
        if (n > MAX_PROCESSOS) n = MAX_PROCESSOS;
        if (!usuario_ok(v, n * sizeof *v, true)) return ERRO_ENDERECO;
        uint32_t k = 0;
        for (int i = 0; i < MAX_PROCESSOS && k < n; i++) {
            processo_t *p = processo_indice(i);
            if (p->estado == P_LIVRE) continue;
            v[k].pid = p->pid;
            v[k].pai = p->pai;
            v[k].estado = p->estado;
            v[k].memoria_kb = (p->usuario && p->estado != P_ZUMBI ? espaco_paginas_usuario(p->espaco) * 4 : 0)
                              + (p->pid ? PILHA_KERNEL / 1024 : 0);
            v[k].cpu_ms = p->tiques_cpu * (1000 / TIQUES_POR_SEGUNDO);
            v[k].usuario = p->usuario;
            txt_copia_n(v[k].nome, p->nome, sizeof v[k].nome);
            k++;
        }
        return (int32_t)k;
    }
    if (tipo == INFO_USB) {
        if (!usuario_ok((void *)ptr, n, true) || n < 2) return ERRO_ENDERECO;
        int usb_relatorio(char *s, int max);
        return usb_relatorio((char *)ptr, (int)n);
    }
    if (tipo == INFO_REGISTRO) {
        if (!usuario_ok((void *)ptr, n, true) || n < 2) return ERRO_ENDERECO;
        extern int diario_copiar(char *destino, int max);
        return diario_copiar((char *)ptr, (int)n);
    }
    if (tipo == INFO_PCI) {
        if (!usuario_ok((void *)ptr, n, true) || n < 2) return ERRO_ENDERECO;
        return pci_relatorio((char *)ptr, (int)n);
    }
    if (tipo == INFO_DISCOS_ESPACO) {
        /* "/" é a árvore na memória (ramdisk + o que foi gravado na RAM): o
         * usado é a soma dos arquivos, e o livre é a memória livre, que é
         * onde um arquivo novo vai parar. "/usb" vem da FAT do pendrive. */
        info_espaco_t *v = (info_espaco_t *)ptr;
        if (n > 2) n = 2;
        if (n == 0 || !usuario_ok(v, n * sizeof *v, true)) return ERRO_ENDERECO;
        memset(v, 0, n * sizeof *v);
        estat_memoria_t est;
        memoria_estatisticas(&est);
        txt_copia_n(v[0].ponto, "/", sizeof v[0].ponto);
        txt_copia_n(v[0].tipo, "ramdisk + RAM", sizeof v[0].tipo);
        v[0].usado_kb = (vfs_bytes_usados() + 1023) / 1024;
        v[0].livre_kb = est.paginas_livres * 4;
        v[0].total_kb = v[0].usado_kb + v[0].livre_kb;
        uint32_t k = 1;
        disco_t *d = disco_montado();
        uint32_t total, livre;
        if (k < n && d && fat_espaco(&total, &livre) == 0) {
            txt_copia_n(v[k].ponto, "/usb", sizeof v[k].ponto);
            txt_copia_n(v[k].tipo, "FAT32", sizeof v[k].tipo);
            txt_copia_n(v[k].rotulo, d->rotulo, sizeof v[k].rotulo);
            v[k].total_kb = total;
            v[k].livre_kb = livre;
            v[k].usado_kb = total - livre;
            k++;
        }
        return (int32_t)k;
    }
    if (tipo == INFO_VERSAO) {
        info_versao_t *v = (info_versao_t *)ptr;
        /* Programas antigos pedem a estrutura sem o campo "bits" (H94):
         * escreve só o que cabe no tamanho que o programa pediu. */
        uint32_t tam = n >= sizeof *v ? sizeof *v : sizeof *v - sizeof v->bits;
        if (!usuario_ok(v, tam, true)) return ERRO_ENDERECO;
        extern const char *sagui_versao;
        extern uint32_t imagem_bytes;
        txt_copia_n(v->versao, sagui_versao, sizeof v->versao);
        v->imagem_bytes = imagem_bytes;
        v->segundos_ligado = relogio_segundos();
        v->largura = info_boot.vesa_ativo ? info_boot.vesa_largura : 80;
        v->altura = info_boot.vesa_ativo ? info_boot.vesa_altura : 25;
        /* O tamanho de um ponteiro do próprio kernel diz a arquitetura: 4
         * bytes no kernel de 32 bits, 8 num kernel de 64 bits, sem precisar
         * de uma constante a mais para lembrar de trocar. */
        if (tam == sizeof *v) v->bits = (uint32_t)(sizeof(void *) * 8);
        return 0;
    }
    return ERRO_INVALIDO;
}

/* aumentar_memoria(n): estende a área de dados do programa em n bytes e
 * devolve onde ela terminava. É a base do malloc da libsagui. */
int32_t sis_aumentar_memoria(uint32_t n, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    uint32_t antigo = atual->dados_fim;
    if (n == 0) return (int32_t)antigo;
    if (n > DADOS_LIMITE - antigo) return ERRO_SEM_MEMORIA;
    if (!espaco_alocar(atual->espaco, antigo, n, PG_ESCRITA | PG_USUARIO)) return ERRO_SEM_MEMORIA;
    atual->dados_fim = antigo + n;
    return (int32_t)antigo;
}

int32_t sis_reiniciar(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    maquina_reiniciar();
}

/* registrar(texto): uma linha no log do sistema (a serial), com o nome e o
 * PID do programa. Os programas gráficos registram o que fazem por aqui,
 * sem sujar o terminal de quem os abriu. */
int32_t sis_registrar(uint32_t ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    char t[256];
    int r = usuario_texto((const char *)ptr, t, sizeof t);
    if (r < 0) return r;
    klog("[%s %d] %s\n", atual->nome, atual->pid, t);
    return 0;
}

/* sis_executar fica em kernel/nucleo/carregador.c (H13). */
