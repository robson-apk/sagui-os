/* sobre-o-sistema - a janela "Sobre o SaguiOS" (H28, refeita na H94).
 *
 * Sem argumentos: o mascote, a versão, a arquitetura, a memória, a tela, há
 * quanto tempo o sistema está ligado e os créditos, atualizados a cada
 * segundo. Dois botões abrem outras janelas, que são este mesmo programa com
 * um argumento (um programa só, e cada janela num processo próprio, para a
 * janela Sobre continuar viva e atualizando):
 *
 *   sobre-o-sistema creditos    "Créditos e licenças": a lista à esquerda e o
 *                               texto à direita, dos arquivos que o build põe
 *                               em /sistema/licencas (tools/mklicencas.py);
 *   sobre-o-sistema historico   "Histórico de versões": o VERSOES.md, que o
 *                               build copia para /sistema/ajuda/versoes.md.
 *
 * Os textos já chegam em Latin-1 (o mksgfs.py converte os .txt e .md). Como
 * a área de texto da libsagui não quebra linhas sozinha, as linhas mais
 * largas que a área são quebradas aqui, num espaço, antes de mostrar. */
#include "sagui.h"
#include "gui.h"

static const char *const cabeca[16] = {
    "...........EEEEEEEEEE...........",
    ".........EEEEEEEEEEEEEE.........",
    "..BBB...EEEEEBBBBBEEEEEE..BBB...",
    ".BBBBB.EEEEEEEBBBEEEEEEEEBBBBB..",
    "BBBBBBBEEEEEEEEBEEEEEEEEEBBBBBB.",
    "BBBBBBEEERRRRRRRRRRRRRREEEBBBBB.",
    ".BBBBBEERRPPPRRRRRRPPPRREEBBBBB.",
    "..BBBBEERRPbPRRRRRRPbPRREEBBBB..",
    "...BBBEERRPPPRRRRRRPPPRREEBBB...",
    ".....EEERRRRRRRRRRRRRRRREEE.....",
    "......EERRRRRRPPPRRRRRRREE......",
    "......EEERRRRRRRRRRRRRREEE......",
    ".......EEERRRRPPPPRRRREEE.......",
    "........EEEERRRRRRRREEEE........",
    ".........EEEEEEEEEEEEEE.........",
    "................................",
};

static uint32_t paleta(char c)
{
    switch (c) {
    case 'E': return 0x3C2D23; case 'B': return 0xF5F5F0; case 'R': return 0xAA7864;
    case 'P': return 0x141210; case 'b': return 0xFFFFFF;
    default: return 0xFF000000;
    }
}

/* Os créditos, em linhas que cabem na janela. A mesma lista vai para o
 * registro (serial), para os testes conferirem o texto. */
static const char *const creditos[] = {
    "SaguiOS, um projeto de Eli Rigobeli.",
    "Escrito por inteligência artificial (Claude, da",
    "Anthropic), sob a direção do Eli.",
    "Desde a versão 2, com a colaboração da comunidade.",
};
#define QTD_CREDITOS 4

/* ------------------------------------------------------------ textos */

/* Quebra as linhas mais largas que "cols" letras, de preferência num espaço,
 * para o texto inteiro aparecer sem rolagem para o lado. Devolve um texto
 * novo (alocado) e o tamanho em *n. */
static char *quebrar_linhas(const char *t, int tam, int cols, int *n)
{
    /* Cada quebra acrescenta no máximo um '\n' a cada "cols" letras. */
    char *s = aloca((size_t)(tam + tam / (cols > 1 ? cols - 1 : 1) + 2));
    if (!s) { *n = 0; return NULL; }
    int k = 0, i = 0;
    while (i < tam) {
        int fim = i;
        while (fim < tam && t[fim] != '\n') fim++;
        int ini = i;
        while (fim - ini > cols) {
            int corte = ini + cols;
            while (corte > ini && t[corte] != ' ') corte--;
            if (corte == ini) {                  /* palavra maior que a linha: corta no meio */
                corte = ini + cols;
                memcpy(s + k, t + ini, (size_t)(corte - ini)); k += corte - ini;
                ini = corte;
            } else {
                memcpy(s + k, t + ini, (size_t)(corte - ini)); k += corte - ini;
                ini = corte + 1;                 /* o espaço vira a quebra */
            }
            s[k++] = '\n';
        }
        memcpy(s + k, t + ini, (size_t)(fim - ini)); k += fim - ini;
        if (fim < tam) s[k++] = '\n';
        i = fim + 1;
    }
    s[k] = 0;
    *n = k;
    return s;
}

/* Carrega um arquivo de texto na área, já com as linhas quebradas. Devolve o
 * tamanho do arquivo, ou o erro. */
static int carregar_texto(area_t *a, const char *caminho)
{
    uint8_t *dados;
    int tam = ler_arquivo_inteiro(caminho, &dados);
    if (tam < 0) {
        char aviso[160];
        int n = formatar_texto(aviso, sizeof aviso, "Não consegui abrir %s (%s).", caminho, nome_erro(tam));
        area_definir(a, aviso, n);
        return tam;
    }
    int cols = (a->l - 12 - 10) / LETRA_L, n;   /* 12 da barra de rolagem, 10 das margens */
    char *q = quebrar_linhas((const char *)dados, tam, cols, &n);
    if (q) { area_definir(a, q, n); libera(q); }
    else area_definir(a, (const char *)dados, tam);
    libera(dados);
    return tam;
}

/* Primeira linha não vazia do texto (para o registro). */
static void primeira_linha(const char *t, char *saida, int max)
{
    while (*t == '\n') t++;
    int k = 0;
    while (t[k] && t[k] != '\n' && k < max - 1) { saida[k] = t[k]; k++; }
    saida[k] = 0;
}

/* ------------------------------------------------------------ Créditos e licenças */

#define MAX_ITENS 48
static char itens[MAX_ITENS][64];
static char arquivos[MAX_ITENS][48];

/* Lê /sistema/licencas/indice.txt, linhas "arquivo|nome". O índice é feito
 * no build a partir das pastas de terceiros/, então um componente novo
 * aparece aqui sem mudar este programa. */
static int ler_indice(void)
{
    uint8_t *dados;
    int tam = ler_arquivo_inteiro("/sistema/licencas/indice.txt", &dados);
    if (tam < 0) return 0;
    int qtd = 0, i = 0;
    while (i < tam && qtd < MAX_ITENS) {
        int ini = i, barra = -1;
        while (i < tam && dados[i] != '\n') { if (dados[i] == '|' && barra < 0) barra = i; i++; }
        if (barra > ini) {
            int na = barra - ini, nn = i - barra - 1;
            if (na > 47) na = 47;
            if (nn > 63) nn = 63;
            memcpy(arquivos[qtd], dados + ini, (size_t)na); arquivos[qtd][na] = 0;
            memcpy(itens[qtd], dados + barra + 1, (size_t)nn); itens[qtd][nn] = 0;
            qtd++;
        }
        i++;
    }
    libera(dados);
    return qtd;
}

static void mostrar_item(jan_t *j, area_t *a, lista_t *l)
{
    char caminho[96], linha[72];
    formatar_texto(caminho, sizeof caminho, "/sistema/licencas/%s", arquivos[l->sel]);
    int tam = carregar_texto(a, caminho);
    area_desenhar(j, a);
    primeira_linha(a->buf, linha, sizeof linha);
    registrar("licencas: mostrando %s (%d bytes): %s", itens[l->sel], tam, linha);
}

static int janela_creditos(void)
{
    enum { L = 940, A = 600 };
    jan_t *j = jan_abrir("Créditos e licenças", L, A);
    if (!j) return 1;
    g_retangulo(j, 0, 0, L, A, COR_FUNDO);
    int qtd = ler_indice();
    lista_t lista = { 10, 10, 270, A - 60, itens, qtd, 0, 0, NULL };
    area_t area;
    if (!area_iniciar(&area, 290, 10, L - 300, A - 60)) return 1;
    area.somente_leitura = true;   /* dá para selecionar e copiar, não para mudar */
    botao_t fechar = { L - 100, A - 40, 90, 28, "Fechar", false };
    if (qtd == 0) {
        static const char aviso[] = "A pasta /sistema/licencas não foi encontrada nesta imagem.";
        area_definir(&area, aviso, (int)sizeof aviso - 1);
    }
    lista_desenhar(j, &lista);
    area_desenhar(j, &area);
    botao_desenhar(j, &fechar);
    rotulo(j, 10, A - 34, "Os textos estão também em /sistema/licencas.", COR_TEXTO_FRACO);
    jan_mostrar(j);
    registrar("licencas: pronto, %d itens", qtd);
    if (qtd > 0) mostrar_item(j, &area, &lista);

    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        if (botao_tratar(j, &fechar, &e)) break;
        if (e.tipo == EV_TECLA && e.tecla == TECLA_ESC) break;
        area_tratar(j, &area, &e);
        if (e.tipo == EV_TECLA && area.foco) continue;   /* as setas rolam o texto que tem o foco */
        int anterior = lista.sel, r = lista_tratar(j, &lista, &e);
        if ((r == LISTA_SELECIONOU || r == LISTA_ATIVOU) && lista.sel != anterior) mostrar_item(j, &area, &lista);
    }
    jan_fechar(j);
    return 0;
}

/* ------------------------------------------------------------ Histórico */

static int janela_historico(void)
{
    enum { L = 720, A = 560 };
    jan_t *j = jan_abrir("Histórico de versões", L, A);
    if (!j) return 1;
    g_retangulo(j, 0, 0, L, A, COR_FUNDO);
    area_t area;
    if (!area_iniciar(&area, 10, 10, L - 20, A - 60)) return 1;
    area.somente_leitura = true;
    int tam = carregar_texto(&area, "/sistema/ajuda/versoes.md");
    botao_t fechar = { L - 100, A - 40, 90, 28, "Fechar", false };
    area_desenhar(j, &area);
    botao_desenhar(j, &fechar);
    jan_mostrar(j);
    char linha[72];
    primeira_linha(area.buf, linha, sizeof linha);
    registrar("historico: pronto (%d bytes): %s", tam, linha);
    evento_t e;
    for (;;) {
        if (proximo_evento(j->id, &e, true) != 1) continue;
        if (e.tipo == EV_FECHAR) break;
        if (botao_tratar(j, &fechar, &e)) break;
        if (e.tipo == EV_TECLA && e.tecla == TECLA_ESC) break;
        area_tratar(j, &area, &e);
    }
    jan_fechar(j);
    return 0;
}

/* ------------------------------------------------------------ Sobre */

static void informacoes(jan_t *j)
{
    info_versao_t v;
    info_memoria_t mem;
    static info_processo_t p[64];
    memset(&v, 0, sizeof v);   /* um kernel antigo não preenche "bits" */
    info_sistema(INFO_VERSAO, &v, sizeof v);
    info_sistema(INFO_MEMORIA, &mem, sizeof mem);
    int procs = info_sistema(INFO_PROCESSOS, p, 64);
    /* A arquitetura vem do kernel (o tamanho dos ponteiros dele): o mesmo
     * programa diz "64 bits" quando roda num kernel de 64 bits. */
    unsigned bits = v.bits ? v.bits : 32;
    char t[96];
    g_retangulo(j, 20, 124, 440, 120, COR_FUNDO);
    formatar_texto(t, sizeof t, "Versão:              %s", v.versao);
    g_texto(j, 20, 124, t, COR_TEXTO);
    formatar_texto(t, sizeof t, "Arquitetura:         %s, %u bits", bits == 64 ? "x86-64" : "x86", bits);
    g_texto(j, 20, 144, t, COR_TEXTO);
    formatar_texto(t, sizeof t, "Memória livre:       %u MB de %u MB", mem.livre_kb / 1024, mem.gerida_kb / 1024);
    g_texto(j, 20, 164, t, COR_TEXTO);
    formatar_texto(t, sizeof t, "Tela:                %ux%u", v.largura, v.altura);
    g_texto(j, 20, 184, t, COR_TEXTO);
    formatar_texto(t, sizeof t, "Processos ativos:    %d", procs);
    g_texto(j, 20, 204, t, COR_TEXTO);
    unsigned s = v.segundos_ligado;
    if (s >= 3600) formatar_texto(t, sizeof t, "Ligado há:           %u h %02u min", s / 3600, s / 60 % 60);
    else formatar_texto(t, sizeof t, "Ligado há:           %u min %02u s", s / 60, s % 60);
    g_texto(j, 20, 224, t, COR_TEXTO);
    jan_mostrar_area(j, 20, 124, 440, 120);
    static bool registrado;
    if (!registrado)
        registrar("sobre: versao %s, imagem %u bytes, %d processos, %u bits", v.versao, v.imagem_bytes, procs, bits);
    registrado = true;
}

static void abrir_janela(const char *modo)
{
    char *argv[] = { "sobre-o-sistema", (char *)modo, NULL };
    int pid = executar("/bin/sobre-o-sistema", argv);
    registrar("sobre: abrindo %s (pid %d)", modo, pid);
}

int principal(int argc, char **argv)
{
    if (argc > 1 && compara_texto(argv[1], "creditos") == 0) return janela_creditos();
    if (argc > 1 && compara_texto(argv[1], "historico") == 0) return janela_historico();

    enum { L = 480, A = 420 };
    jan_t *j = jan_abrir("Sobre o SaguiOS", L, A);
    if (!j) return 1;
    g_retangulo(j, 0, 110, L, A - 110, COR_FUNDO);
    g_retangulo(j, 0, 0, L, 110, 0x0B7A3E);
    g_desenho(j, 20, 14, cabeca, 32, 16, paleta, 5);
    g_texto(j, 200, 30, "SaguiOS", 0xFFDF4A);
    g_texto(j, 200, 52, "um sistema operacional", 0xFFFFFF);
    g_texto(j, 200, 70, "escrito do zero", 0xFFFFFF);
    g_texto(j, 20, 256, "Créditos", COR_DESTAQUE);
    for (int i = 0; i < QTD_CREDITOS; i++) {
        g_texto(j, 20, 278 + i * 18, creditos[i], COR_TEXTO);
        registrar("sobre: creditos: %s", creditos[i]);
    }
    botao_t licencas = { 20, A - 44, 170, 28, "Créditos e licenças", false };
    botao_t historico = { 200, A - 44, 110, 28, "Histórico", false };
    botao_t fechar = { L - 110, A - 44, 90, 28, "Fechar", false };
    botao_desenhar(j, &licencas);
    botao_desenhar(j, &historico);
    botao_desenhar(j, &fechar);
    registrar("sobre: botoes: %s | %s | %s", licencas.texto, historico.texto, fechar.texto);
    informacoes(j);
    jan_mostrar(j);
    evento_t e;
    unsigned ultimo = tempo_ms();
    for (;;) {
        if (proximo_evento(j->id, &e, false) == 1) {
            if (e.tipo == EV_FECHAR) break;
            if (botao_tratar(j, &fechar, &e)) break;
            if (botao_tratar(j, &licencas, &e)) abrir_janela("creditos");
            if (botao_tratar(j, &historico, &e)) abrir_janela("historico");
            if (e.tipo == EV_TECLA && (e.tecla == '\n' || e.tecla == TECLA_ESC)) break;
            continue;
        }
        if (tempo_ms() - ultimo >= 1000) { ultimo = tempo_ms(); informacoes(j); }
        dormir(50);
    }
    jan_fechar(j);
    return 0;
}
