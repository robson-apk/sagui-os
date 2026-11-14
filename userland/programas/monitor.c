/* monitor - Sagui Monitor, inspirado no btop (pull request #7, de Gabriel).
 *
 * Janela gráfica em ring 3 que mostra:
 *   - uso de CPU (média entre duas amostras)
 *   - uso de memória (total, livre, usada)
 *   - lista de processos com PID, CPU%, MEM%, estado e nome
 *   - histórico curto de CPU como barrinhas laterais
 *
 * Teclas:
 *   P - ordenar por CPU   M - ordenar por memória   N - ordenar por nome
 *   Q ou ESC - sair
 *
 * Tudo desenhado pelo próprio programa na área de pixels da janela,
 * usando apenas as chamadas de sistema do SaguiOS (int 0x80) e a libsagui.
 * Sem libc, sem código de outros sistemas. */

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

    const char *ordem_txt = modo_ordem == 0 ? "CPU" :
                            modo_ordem == 1 ? "MEM" : "NOME";
    char rot[96];
    formatar_texto(rot, sizeof rot, "ordenar: [P]CPU [M]MEM [N]NOME  (atual: %s)", ordem_txt);
    desenhar_texto(j, LARG_JAN - 16 - g_largura_texto(rot), y + 200, rot, TOPO_TEXTO_FR);

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

int principal(int argc, char **argv)
{
    (void)argc; (void)argv;

    jan_t *j = jan_abrir("Sagui Monitor", LARG_JAN, ALT_JAN);
    if (!j) return 1;

    g_retangulo(j, 0, 32, LARG_JAN, ALT_JAN - 32, TOPO_FUNDO);
    jan_mostrar(j);

    info_versao_t iv;
    info_sistema(INFO_VERSAO, &iv, sizeof iv);

    processo_snapshot_t amostra[2];

    /* Primeira amostra. */
    capturar_processos(&amostra[0]);
    unsigned t0 = tempo_ms();
    dormir(500);

    int modo_ordem = 0;   /* 0 = CPU, 1 = MEM, 2 = NOME */
    int sel = 0;
    bool rodando = true;

    while (rodando) {
        /* Segunda amostra. */
        amostra[1] = amostra[0];   /* estrutura cópia; preenche qtd e lista abaixo */
        capturar_processos(&amostra[1]);
        unsigned t1 = tempo_ms();
        unsigned delta_ms = t1 - t0;
        if (delta_ms < 100) delta_ms = 100;

        /* Calcula percentuais diretamente em amostra[1].lista[].cpu_val. */
        unsigned total_cpu = calcular_cpu(&amostra[0], &amostra[1], delta_ms);
        if (total_cpu > 1000) total_cpu = 1000;

        /* Desloca histórico. */
        for (int i = HIST_MAX - 1; i > 0; i--) hist_cpu[i] = hist_cpu[i - 1];
        hist_cpu[0] = total_cpu;
        if (hist_qtd < HIST_MAX) hist_qtd++;

        /* Memória. */
        memoria_t mem;
        capturar_memoria(&mem);
        unsigned pct_mem = mem.gerida_kb ? (mem.usada_kb * 1000) / mem.gerida_kb : 0;

        /* Redesenha. */
        g_retangulo(j, 0, 32, LARG_JAN, ALT_JAN - 32, TOPO_FUNDO);
        info_sistema(INFO_VERSAO, &iv, sizeof iv);   /* tempo ligado a cada volta */
        desenhar_cabecalho(j, iv.versao, iv.segundos_ligado);
        desenhar_linha_cpu_mem(j, 70, total_cpu, pct_mem);

        /* Ordena a cópia para exibição (percentual viaja com o processo). */
        processo_snapshot_t exib = amostra[1];
        ordenar_por(&exib, modo_ordem);
        desenhar_lista(j, 140, &exib, sel, modo_ordem);

        /* Rodapé com ajuda. */
        const char *ajuda = "Q/ESC: sair   setas: mover seleção";
        desenhar_texto(j, 16, ALT_JAN - 24, ajuda, TOPO_TEXTO_FR);
        jan_mostrar(j);

        /* Troca amostras. */
        amostra[0] = amostra[1];
        t0 = t1;

        evento_t e;
        int r = proximo_evento(j->id, &e, false);
        if (r == 1) {
            if (e.tipo == EV_FECHAR) rodando = false;
            else if (e.tipo == EV_TECLA) {
                unsigned t = e.tecla;
                if (t == TECLA_ESC || t == 'q' || t == 'Q') rodando = false;
                else if (t == 'p' || t == 'P') { modo_ordem = 0; sel = 0; }
                else if (t == 'm' || t == 'M') { modo_ordem = 1; sel = 0; }
                else if (t == 'n' || t == 'N') { modo_ordem = 2; sel = 0; }
                else if (t == TECLA_CIMA) { if (sel > 0) sel--; }
                else if (t == TECLA_BAIXO) { if (sel < amostra[1].qtd - 1) sel++; }
            }
        }

        dormir(400);
    }

    jan_fechar(j);
    return 0;
}
