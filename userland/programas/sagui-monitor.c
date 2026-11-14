/* sagui-monitor - Sagui Monitor, janela no estilo do btop (pull request #7,
 * de Gabriel). O "monitor" do Terminal é o top; esta é a versão em janela.
 *
 * Mostra o uso de CPU (tudo menos o processo ocioso), a memória, um histórico
 * curto da CPU e a lista de processos com PID, %CPU, memória, estado e nome.
 *
 * Teclas: P ordena por CPU, M por memória, N por nome; setas escolhem um
 * processo (a escolha segue o processo, não a linha); Q ou Esc fecham.
 *
 * Tudo desenhado pelo próprio programa, só com as chamadas do SaguiOS e a
 * libsagui. */

#include "sagui.h"
#include "gui.h"

#define LARG_JAN 720
#define ALT_JAN  560

#define MAX_PROC  64
#define HIST_MAX  40      /* quantas amostras de CPU guardar */

/* ------------------------------------------------------------ cores do tema top */
#define TOPO_FUNDO     0x1E1E2E
#define TOPO_PAINEL    0x313244
#define TOPO_TEXTO     0xCDD6F4
#define TOPO_TEXTO_FR  0x6C7086
#define TOPO_CPU       0xF38BA8   /* vermelho */
#define TOPO_MEM       0x89DCEB   /* ciano */
#define TOPO_SEL       0x45475A
#define TOPO_BORDA     0x585B70

/* ------------------------------------------------------------ dados do sistema */

typedef struct {
    int    pid;
    int    pai;
    int    estado;      /* 1 pronto, 2 executando, 3 bloqueado, 4 zumbi */
    unsigned memoria_kb;
    unsigned cpu_val;   /* bruto: ms acumulados; depois da conta: percentual * 10 */
    int    usuario;
    char   nome[32];
} proc_t;

typedef struct {
    proc_t  lista[MAX_PROC];
    int     qtd;
} processo_snapshot_t;

typedef struct {
    unsigned ram_total_kb;
    unsigned gerida_kb;
    unsigned livre_kb;
    unsigned usada_kb;
} memoria_t;

/* Histórico de uso total de CPU (0-1000 = 0.0% - 100.0%). */
static unsigned hist_cpu[HIST_MAX];
static int    hist_qtd = 0;

/* Protótipo (definida depois de desenhar_lista). */
static void texto_para_int_n(int v, char *buf, unsigned max);

/* ------------------------------------------------------------ captura de amostras */

static void capturar_processos(processo_snapshot_t *s)
{
    int n = info_sistema(INFO_PROCESSOS, s->lista, MAX_PROC);   /* n = quantos itens cabem, não bytes */
    if (n < 0) n = 0;
    if (n > MAX_PROC) n = MAX_PROC;
    s->qtd = n;
}

static void capturar_memoria(memoria_t *m)
{
    info_sistema(INFO_MEMORIA, m, sizeof *m);
}

/* Calcula a porcentagem de CPU de cada processo entre duas amostras.
 * Escreve o resultado (0-1000) direto em b->lista[i].cpu_val.
 * Devolve o percentual total médio (0-1000). */
static unsigned calcular_cpu(const processo_snapshot_t *a,
                             processo_snapshot_t *b,
                             unsigned delta_ms)
{
    if (delta_ms == 0) delta_ms = 1;

    /* Delta de CPU ms por processo (pid -> delta). */
    unsigned delta_ms_por_proc[MAX_PROC];
    unsigned total_delta = 0;
    for (int i = 0; i < b->qtd; i++) {
        unsigned delta = 0;
        for (int j = 0; j < a->qtd; j++) {
            if (a->lista[j].pid == b->lista[i].pid) {
                delta = b->lista[i].cpu_val - a->lista[j].cpu_val;
                break;
            }
        }
        delta_ms_por_proc[i] = delta;
        total_delta += delta;
    }

    /* Percentual de cada processo (0-1000 = 0.0% - 100.0%). */
    for (int i = 0; i < b->qtd; i++) {
        b->lista[i].cpu_val = (delta_ms_por_proc[i] * 1000) /
                              (total_delta ? total_delta : 1);
    }

    /* Uso total = tudo o que não foi do processo ocioso (PID 0). */
    for (int i = 0; i < b->qtd; i++)
        if (b->lista[i].pid == 0) return 1000 - b->lista[i].cpu_val;
    return total_delta ? 1000 : 0;
}

/* ------------------------------------------------------------ ordenação */

static void ordenar_por(processo_snapshot_t *s, int modo)
{
    for (int i = 1; i < s->qtd; i++) {
        proc_t chave = s->lista[i];
        int j = i - 1;
        while (j >= 0) {
            int trocar = 0;
            if (modo == 0) {   /* CPU */
                if (chave.cpu_val > s->lista[j].cpu_val) trocar = 1;
                else if (chave.cpu_val == s->lista[j].cpu_val &&
                         compara_texto(chave.nome, s->lista[j].nome) < 0) trocar = 1;
            } else if (modo == 1) {   /* MEM */
                if (chave.memoria_kb > s->lista[j].memoria_kb) trocar = 1;
                else if (chave.memoria_kb == s->lista[j].memoria_kb &&
                         compara_texto(chave.nome, s->lista[j].nome) < 0) trocar = 1;
            } else {   /* NOME */
                if (compara_texto(chave.nome, s->lista[j].nome) < 0) trocar = 1;
            }
            if (!trocar) break;
            s->lista[j + 1] = s->lista[j];
            j--;
        }
        s->lista[j + 1] = chave;
    }
}

/* ------------------------------------------------------------ desenho */

static const char *nome_estado(int e)
{
    switch (e) {
    case 1: return "PRONTO";
    case 2: return "EXEC";
    case 3: return "BLOQ";
    case 4: return "ZUMBI";
    default: return "?";
    }
}

static void desenhar_texto(jan_t *j, int x, int y, const char *t, uint32_t cor)
{
    g_texto(j, x, y, t, cor);
    jan_mostrar_area(j, x, y, g_largura_texto(t), LETRA_A);
}

static void desenhar_barra(jan_t *j, int x, int y, int larg, int altura,
                           unsigned pct, uint32_t cor)
{
    g_retangulo(j, x, y, larg, altura, TOPO_PAINEL);
    int preenchido = (larg * pct) / 1000;
    if (preenchido > larg) preenchido = larg;
    if (preenchido > 0) g_retangulo(j, x, y, preenchido, altura, cor);
    g_moldura(j, x, y, larg, altura, TOPO_BORDA);
    jan_mostrar_area(j, x, y, larg, altura);
}

static void desenhar_cabecalho(jan_t *j, const char *versao, unsigned segundos)
{
    int y = 14;
    g_retangulo(j, 0, 0, LARG_JAN, 32, TOPO_PAINEL);
    g_moldura(j, 0, 0, LARG_JAN, 32, TOPO_BORDA);
    desenhar_texto(j, 16, y, "Sagui Monitor", TOPO_TEXTO);

    char uptime[64];
    unsigned h = segundos / 3600;
    unsigned m = (segundos % 3600) / 60;
    unsigned s = segundos % 60;
    formatar_texto(uptime, sizeof uptime, "ligado a %02u:%02u:%02u   v%s",
                   h, m, s, versao);
    desenhar_texto(j, LARG_JAN - 16 - g_largura_texto(uptime), y, uptime, TOPO_TEXTO_FR);
}

static void desenhar_linha_cpu_mem(jan_t *j, int y, unsigned pct_cpu_total, unsigned pct_mem)
{
    desenhar_texto(j, 16, y, "CPU", TOPO_TEXTO);

    int barra_x = 100, barra_l = LARG_JAN - 260, barra_a = 18;
    desenhar_barra(j, barra_x, y, barra_l, barra_a, pct_cpu_total, TOPO_CPU);

    char txt[32];
    formatar_texto(txt, sizeof txt, "%u.%u%%", pct_cpu_total / 10, pct_cpu_total % 10);
    desenhar_texto(j, barra_x + barra_l + 8, y - 2, txt, TOPO_CPU);

    /* Histórico como barrinhas verticais à direita. */
    int hist_x = LARG_JAN - 90, hist_l = 6, hist_a = 16, esp = 4;
    for (int i = 0; i < hist_qtd && i < 8; i++) {
        int ha = (hist_cpu[i] * hist_a) / 1000;
        if (ha < 2) ha = 2;
        g_retangulo(j, hist_x + i * (hist_l + esp), y + (barra_a - ha),
                    hist_l, ha, TOPO_CPU);
    }
    jan_mostrar_area(j, hist_x, y, 8 * (hist_l + esp), barra_a);

    y += 30;

    desenhar_texto(j, 16, y, "MEM", TOPO_TEXTO);
    desenhar_barra(j, barra_x, y, barra_l, barra_a, pct_mem, TOPO_MEM);
    formatar_texto(txt, sizeof txt, "%u.%u%%", pct_mem / 10, pct_mem % 10);
    desenhar_texto(j, barra_x + barra_l + 8, y - 2, txt, TOPO_MEM);
    jan_mostrar_area(j, barra_x, y, barra_l + 120, barra_a);
}

static void desenhar_lista(jan_t *j, int y, const processo_snapshot_t *s,
                           int sel, int modo_ordem)
{
    int col_pid = 16, col_cpu = 110, col_mem = 170, col_estado = 230, col_nome = 300;

    g_retangulo(j, col_pid - 4, y - 2, LARG_JAN - col_pid - 12, 20, TOPO_PAINEL);
    desenhar_texto(j, col_pid, y, "PID", TOPO_TEXTO_FR);
    desenhar_texto(j, col_cpu, y, "CPU%", TOPO_TEXTO_FR);
    desenhar_texto(j, col_mem, y, "MEM KB", TOPO_TEXTO_FR);
    desenhar_texto(j, col_estado, y, "ESTADO", TOPO_TEXTO_FR);
    desenhar_texto(j, col_nome, y, "NOME", TOPO_TEXTO_FR);
    y += 22;

    int vis = (ALT_JAN - y - 30) / 18;
    if (vis < 1) vis = 1;
    int topo = 0;
    if (sel >= topo + vis) topo = sel - vis + 1;
    if (sel < topo) topo = sel;
    if (topo < 0) topo = 0;

    (void)modo_ordem;

    for (int k = 0; k < vis && topo + k < s->qtd; k++) {
        const proc_t *p = &s->lista[topo + k];
        int yy = y + k * 18;
        bool linha_sel = (topo + k == sel);

        if (linha_sel)
            g_retangulo(j, col_pid - 4, yy - 1, LARG_JAN - col_pid - 12, 18, TOPO_SEL);

        char buf[16];
        texto_para_int_n(p->pid, buf, sizeof buf);
        desenhar_texto(j, col_pid, yy, buf, linha_sel ? 0xFFFFFF : TOPO_TEXTO);

        formatar_texto(buf, sizeof buf, "%u.%u", p->cpu_val / 10, p->cpu_val % 10);
        desenhar_texto(j, col_cpu, yy, buf, linha_sel ? 0xFFFFFF : TOPO_TEXTO);

        formatar_texto(buf, sizeof buf, "%u", p->memoria_kb);
        desenhar_texto(j, col_mem, yy, buf, linha_sel ? 0xFFFFFF : TOPO_TEXTO);

        desenhar_texto(j, col_estado, yy, nome_estado(p->estado),
                       linha_sel ? 0xFFFFFF : TOPO_TEXTO_FR);

        desenhar_texto(j, col_nome, yy, p->nome,
                       linha_sel ? 0xFFFFFF : TOPO_TEXTO);
    }
    jan_mostrar_area(j, col_pid - 4, y - 2, LARG_JAN - col_pid - 12, vis * 18 + 4);
}

/* Versão sem sprintf de %d. */
static void texto_para_int_n(int v, char *buf, unsigned max)
{
    if (v == 0) { copia_texto(buf, "0", max); return; }
    char tmp[16];
    int n = 0, neg = v < 0;
    unsigned u = neg ? (unsigned)(-v) : (unsigned)v;
    while (u && n < 15) { tmp[n++] = (char)('0' + u % 10); u /= 10; }
    if (neg) tmp[n++] = '-';
    size_t k = 0;
    while (n) { if (k < max - 1) buf[k++] = tmp[--n]; }
    buf[k] = 0;
}

/* ------------------------------------------------------------ loop principal */

#define AMOSTRA_MS 1000   /* uma amostra nova por segundo */
#define PASSO_MS   50     /* de quanto em quanto tempo olhar o teclado */

static const char *const nomes_ordem[] = { "CPU", "MEM", "NOME" };

static void desenhar_tudo(jan_t *j, const info_versao_t *iv, unsigned total_cpu, unsigned pct_mem,
                          const processo_snapshot_t *exib, int sel, int modo_ordem)
{
    g_retangulo(j, 0, 32, LARG_JAN, ALT_JAN - 32, TOPO_FUNDO);
    desenhar_cabecalho(j, iv->versao, iv->segundos_ligado);
    desenhar_linha_cpu_mem(j, 70, total_cpu, pct_mem);
    desenhar_lista(j, 140, exib, sel, modo_ordem);
    desenhar_texto(j, 16, ALT_JAN - 24, "Q/ESC: sair   setas: escolher", TOPO_TEXTO_FR);
    char rot[64];
    formatar_texto(rot, sizeof rot, "ordem [P]CPU [M]MEM [N]NOME: %s", nomes_ordem[modo_ordem]);
    desenhar_texto(j, LARG_JAN - 16 - g_largura_texto(rot), ALT_JAN - 24, rot, TOPO_TEXTO_FR);
    jan_mostrar(j);
}

static int indice_do_pid(const processo_snapshot_t *s, int pid)
{
    for (int i = 0; i < s->qtd; i++) if (s->lista[i].pid == pid) return i;
    return -1;
}

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;

    jan_t *j = jan_abrir("Sagui Monitor", LARG_JAN, ALT_JAN);
    if (!j) return 1;
    g_retangulo(j, 0, 32, LARG_JAN, ALT_JAN - 32, TOPO_FUNDO);
    jan_mostrar(j);

    /* cru[0]: amostra anterior, com os ms de CPU acumulados de cada processo.
     * A conta do percentual é feita numa cópia, para a próxima amostra
     * comparar ms com ms. */
    static processo_snapshot_t cru, agora, exib;
    capturar_processos(&cru);
    dormir(300);

    info_versao_t iv;
    int modo_ordem = 0, sel = 0, pid_sel = -1;
    unsigned total_cpu = 0, pct_mem = 0;
    bool rodando = true, primeira = true;
    unsigned prox_amostra = 0;

    while (rodando) {
        unsigned t = tempo_ms();
        bool redesenhar = false;
        if (primeira || (int)(t - prox_amostra) >= 0) {
            capturar_processos(&agora);
            processo_snapshot_t pct = agora;
            total_cpu = calcular_cpu(&cru, &pct, AMOSTRA_MS);
            if (total_cpu > 1000) total_cpu = 1000;
            cru = agora;
            for (int i = HIST_MAX - 1; i > 0; i--) hist_cpu[i] = hist_cpu[i - 1];
            hist_cpu[0] = total_cpu;
            if (hist_qtd < HIST_MAX) hist_qtd++;
            memoria_t mem;
            capturar_memoria(&mem);
            pct_mem = mem.gerida_kb ? (mem.usada_kb * 1000) / mem.gerida_kb : 0;
            info_sistema(INFO_VERSAO, &iv, sizeof iv);
            exib = pct;
            if (primeira)
                registrar("sagui-monitor: %d processos, CPU %u.%u%%, memória %u.%u%%",
                          exib.qtd, total_cpu / 10, total_cpu % 10, pct_mem / 10, pct_mem % 10);
            primeira = false;
            prox_amostra = t + AMOSTRA_MS;
            redesenhar = true;
        }

        /* todas as teclas que chegaram, não só uma por volta */
        evento_t e;
        bool mudou_ordem = false, mudou_sel = false;
        while (proximo_evento(j->id, &e, false) == 1) {
            if (e.tipo == EV_FECHAR) rodando = false;
            else if (e.tipo == EV_TECLA) {
                unsigned k = e.tecla;
                if (k == TECLA_ESC || k == 'q' || k == 'Q') rodando = false;
                else if (k == 'p' || k == 'P') { modo_ordem = 0; mudou_ordem = true; }
                else if (k == 'm' || k == 'M') { modo_ordem = 1; mudou_ordem = true; }
                else if (k == 'n' || k == 'N') { modo_ordem = 2; mudou_ordem = true; }
                else if (k == TECLA_CIMA) { if (sel > 0) sel--; mudou_sel = true; pid_sel = -1; }
                else if (k == TECLA_BAIXO) { if (sel < exib.qtd - 1) sel++; mudou_sel = true; pid_sel = -1; }
            }
        }
        if (!rodando) break;

        if (redesenhar || mudou_ordem || mudou_sel) {
            ordenar_por(&exib, modo_ordem);
            /* a escolha segue o processo depois de reordenar ou de nova amostra */
            if (pid_sel >= 0) { int i = indice_do_pid(&exib, pid_sel); if (i >= 0) sel = i; }
            if (sel >= exib.qtd) sel = exib.qtd ? exib.qtd - 1 : 0;
            if (exib.qtd) pid_sel = exib.lista[sel].pid;
            desenhar_tudo(j, &iv, total_cpu, pct_mem, &exib, sel, modo_ordem);
            if (mudou_ordem && exib.qtd)
                registrar("sagui-monitor: ordem %s, primeiro %s", nomes_ordem[modo_ordem], exib.lista[0].nome);
            if (mudou_sel && exib.qtd)
                registrar("sagui-monitor: escolhido PID %d (%s)", exib.lista[sel].pid, exib.lista[sel].nome);
        }
        dormir(PASSO_MS);
    }

    jan_fechar(j);
    return 0;
}
