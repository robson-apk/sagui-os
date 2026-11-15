/* arquivos [pasta] - o gerenciador de arquivos do SaguiOS (H23).
 *
 * Mostra uma pasta como lista com ícones (pastas primeiro). Duplo clique:
 * pasta entra, arquivo de texto abre no Editor, programa executa. Botões
 * para voltar, criar pasta e apagar (com confirmação).
 *
 * Lixeira (H87): "Apagar" move para a lixeira em vez de apagar de vez. Cada
 * disco tem a sua, porque mover entre a memória e o pendrive não é renomear:
 * /.lixeira e /usb/.lixeira. O arquivo .origens de cada lixeira guarda, por
 * linha, "nome na lixeira<TAB>caminho original", para restaurar. Nomes que
 * começam com ponto não aparecem na lista. Na lixeira, os botões viram
 * Restaurar, Apagar de vez e Esvaziar. Shift+Delete apaga de vez. */
#include "sagui.h"
#include "gui.h"
#include "icones.h"

static int larg_jan = 720, alt_jan = 440;   /* a janela pode mudar de tamanho (H78) */
#define LARG larg_jan
#define LATERAL 140   /* lateral com "SaguiOS" e "Pendrive" (H34) */
#define ALT  alt_jan
#define MAX_ITENS 128

static jan_t *j;
static char pasta[160] = "/";
static char nomes[MAX_ITENS][64];
static uint8_t tipos[MAX_ITENS];
static uint32_t tamanhos[MAX_ITENS];
static lista_t lista;
static info_disco_t pendrive;
static bool tem_pendrive;
static botao_t bt_lado_raiz = { 8, 76, LATERAL - 16, 28, "SaguiOS", false, icone_sobre, false, true };
static botao_t bt_lado_pen = { 8, 112, LATERAL - 16, 28, "Pendrive", false, icone_pendrive, false, true };
static botao_t bt_remover = { 8, 148, LATERAL - 16, 44, "", false };
static botao_t bt_lixeira = { 8, 0, LATERAL - 16, 28, "Lixeira", false, icone_lixeira, false, true };   /* y no pé da lateral */
/* H88: botões do topo com ícone. */
static botao_t bt[4] = {
    { 8, 7, 96, 26, "Voltar", false, icone_voltar }, { 112, 7, 130, 26, "Nova pasta", false, icone_nova_pasta },
    { 250, 7, 100, 26, "Apagar", false, icone_lixeira }, { 408, 7, 110, 26, "Esvaziar", false, icone_apagar },
};

/* Ícones 16x16 em pixel art. */
static const char *const ic_pasta[] = {
    "................", "................", ".PPPPP..........", "PYYYYYP.........",
    "PYYYYYYPPPPPPPP.", "PYYYYYYYYYYYYYYP", "PPPPPPPPPPPPPPPP", "PAAAAAAAAAAAAAAP",
    "PAAAAAAAAAAAAAAP", "PAAAAAAAAAAAAAAP", "PAAAAAAAAAAAAAAP", "PAAAAAAAAAAAAAAP",
    "PAAAAAAAAAAAAAAP", "PPPPPPPPPPPPPPPP", "................", "................",
};
static const char *const ic_arquivo[] = {
    "..PPPPPPPPP.....", "..PBBBBBBBPP....", "..PBBBBBBBPBP...", "..PBBBBBBBPPPP..",
    "..PBCCCCCBBBBP..", "..PBBBBBBBBBBP..", "..PBCCCCCCCCBP..", "..PBBBBBBBBBBP..",
    "..PBCCCCCCCBBP..", "..PBBBBBBBBBBP..", "..PBCCCCCCCCBP..", "..PBBBBBBBBBBP..",
    "..PBCCCCCBBBBP..", "..PBBBBBBBBBBP..", "..PPPPPPPPPPPP..", "................",
};
static const char *const ic_programa[] = {
    "................", ".PPPPPPPPPPPPPP.", ".PVVVVVVVVVVVVP.", ".PPPPPPPPPPPPPP.",
    ".PDDDDDDDDDDDDP.", ".PDGDDDDDDDDDDP.", ".PDDGDDDDDDDDDP.", ".PDDDGDDDDDDDDP.",
    ".PDDGDDDDDDDDDP.", ".PDGDDGGGGDDDDP.", ".PDDDDDDDDDDDDP.", ".PDDDDDDDDDDDDP.",
    ".PPPPPPPPPPPPPP.", "................", "................", "................",
};

static uint32_t paleta(char c)
{
    switch (c) {
    case 'P': return 0x202428;
    case 'Y': return 0xFFE070;
    case 'A': return 0xF2C230;
    case 'B': return 0xFFFFFF;
    case 'C': return 0x9AA3AC;
    case 'V': return 0x2A6FD8;
    case 'D': return 0x182030;
    case 'G': return 0x3CDC6E;
    default:  return 0xFF000000;
    }
}

static void icone(jan_t *jj, int x, int y, int i)
{
    const char *const *d = tipos[i] == TIPO_PASTA ? ic_pasta : tipos[i] == TIPO_PROGRAMA ? ic_programa : ic_arquivo;
    g_desenho(jj, x, y, d, 16, 16, paleta, 1);
}

static void caminho_de(int i, char *saida, size_t max)
{
    copia_texto(saida, pasta, max);
    if (compara_texto(pasta, "/") != 0) junta_texto(saida, "/", max);
    junta_texto(saida, nomes[i], max);
}

static void desenhar_topo(void)
{
    g_retangulo(j, 0, 40, LARG, 26, COR_FUNDO);
    char t[200];
    formatar_texto(t, sizeof t, "Pasta: %s", pasta);
    g_texto_n(j, 10, 46, t, (LARG - 20) / LETRA_L, COR_TEXTO);
    jan_mostrar_area(j, 0, 40, LARG, 26);
}

static void desenhar_rodape(const char *msg)
{
    g_retangulo(j, 0, ALT - 24, LARG, 24, 0xE6E9EC);
    g_retangulo(j, 0, ALT - 24, LARG, 1, COR_BORDA);
    g_texto_n(j, 10, ALT - 20, msg, (LARG - 20) / LETRA_L, COR_TEXTO_FRACO);
    jan_mostrar_area(j, 0, ALT - 24, LARG, 24);
}

static void desenhar_botoes(void);

/* Lê a pasta: primeiro as pastas, depois o resto (o kernel já devolve em
 * ordem alfabética). */
static void carregar(void)
{
    entrada_pasta_t e;
    int n = 0;
    for (int passo = 0; passo < 2; passo++)
        for (int i = 0; n < MAX_ITENS && listar(pasta, i, &e) == 1; i++) {
            if (e.nome[0] == '.') continue;   /* escondidos: a lixeira e o .origens */
            bool eh_pasta = e.tipo == TIPO_PASTA;
            if ((passo == 0) != eh_pasta) continue;
            copia_texto(nomes[n], e.nome, 64);
            tipos[n] = (uint8_t)e.tipo;
            tamanhos[n] = e.tamanho;
            n++;
        }
    lista.qtd = n;
    lista.sel = n ? 0 : -1;
    desenhar_botoes();
    lista.topo = 0;
    desenhar_topo();
    lista_desenhar(j, &lista);
    char msg[64];
    formatar_texto(msg, sizeof msg, "%d itens. Duplo clique para abrir.", n);
    desenhar_rodape(msg);
    registrar("arquivos: pasta %s (%d itens)", pasta, n);
}

static void entrar(const char *nova)
{
    info_arquivo_t inf;
    if (info_arquivo(nova, &inf) < 0 || inf.tipo != TIPO_PASTA) return;
    copia_texto(pasta, nova, sizeof pasta);
    carregar();
}

/* A lateral mostra o pendrive (se houver) e, montado, o botão de remover. */
static void desenhar_lateral(void)
{
    g_retangulo(j, 0, 66, LATERAL, ALT - 66 - 24, 0xEAEDF0);
    g_retangulo(j, LATERAL - 1, 66, 1, ALT - 66 - 24, COR_BORDA);
    botao_desenhar(j, &bt_lado_raiz);
    if (tem_pendrive) {
        static char rotulo[24];
        copia_texto(rotulo, pendrive.rotulo[0] ? pendrive.rotulo : "Pendrive", sizeof rotulo);
        bt_lado_pen.texto = rotulo;
        botao_desenhar(j, &bt_lado_pen);
        if (pendrive.montado) {
            bt_remover.texto = "";
            botao_desenhar(j, &bt_remover);
            g_texto(j, 8 + 12, 148 + 6, "Remover com", COR_TEXTO);
            g_texto(j, 8 + 20, 148 + 22, "segurança", COR_TEXTO);
        }
    }
    bt_lixeira.y = ALT - 24 - 38;
    botao_desenhar(j, &bt_lixeira);
    jan_mostrar_area(j, 0, 66, LATERAL, ALT - 66 - 24);
}

/* Confere os pendrives; devolve true se algo mudou. */
static bool atualizar_pendrive(void)
{
    info_disco_t d;
    bool achou = false;
    for (int i = 0; listar_discos(i, &d) == 1; i++)
        if (d.suportado) { achou = true; break; }
    bool mudou = achou != tem_pendrive || (achou && (d.montado != pendrive.montado || compara_texto(d.nome, pendrive.nome) != 0));
    tem_pendrive = achou;
    if (achou) pendrive = d;
    return mudou;
}

static void voltar(void)
{
    if (compara_texto(pasta, "/") == 0) return;
    char *b = acha_ultimo_char(pasta, '/');
    if (b == pasta) pasta[1] = 0; else *b = 0;
    carregar();
}

/* "a.PNG" tem ".png" de "lista" (extensões separadas por |)? */
static bool tem_extensao(const char *nome, const char *lista_ext)
{
    const char *ponto = acha_ultimo_char(nome, '.');
    if (!ponto) return false;
    size_t n = tam_texto(ponto);
    const char *p = lista_ext;
    while (*p) {
        const char *fim = p;
        while (*fim && *fim != '|') fim++;
        if ((size_t)(fim - p) == n) {
            size_t k = 0;
            for (; k < n; k++) {
                char a = ponto[k], b = p[k];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (a != b) break;
            }
            if (k == n) return true;
        }
        p = *fim ? fim + 1 : fim;
    }
    return false;
}

/* Duplo clique num .zip: extrai numa pasta com o nome dele (pelo unzip). */
static void extrair_zip(int i, const char *c)
{
    char destino[200];
    copia_texto(destino, c, sizeof destino);
    char *ponto = acha_ultimo_char(destino, '.');
    if (ponto) *ponto = 0;
    char pergunta[260];
    formatar_texto(pergunta, sizeof pergunta, "Extrair %s\npara a pasta %s?", nomes[i], acha_ultimo_char(destino, '/') + 1);
    if (!confirmar("Extrair", pergunta)) return;
    char *argv[] = { "unzip", (char *)c, "-d", destino, NULL };
    int pid = executar("/bin/unzip", argv), cod = -1;
    if (pid > 0) esperar(pid, &cod);
    registrar("arquivos: extraido %s em %s (codigo %d)", c, destino, cod);
    if (cod != 0) mensagem("Arquivos", "Não consegui extrair tudo (veja o registro).");
    carregar();
}

static void abrir_item(int i)
{
    if (i < 0 || i >= lista.qtd) return;
    char c[200];
    caminho_de(i, c, sizeof c);
    if (tipos[i] == TIPO_PASTA) { entrar(c); return; }
    char *argv[3];
    int pid;
    if (tipos[i] == TIPO_PROGRAMA) {
        argv[0] = nomes[i]; argv[1] = NULL;
        pid = executar(c, argv);
        registrar("arquivos: executando %s (pid %d)", c, pid);
    } else if (tem_extensao(nomes[i], ".png|.jpg|.jpeg|.gif|.webp|.bmp")) {   /* H87 */
        argv[0] = "visualizador"; argv[1] = c; argv[2] = NULL;
        pid = executar("/bin/visualizador", argv);
        registrar("arquivos: abrindo no visualizador %s (pid %d)", c, pid);
    } else if (tem_extensao(nomes[i], ".wav")) {
        argv[0] = "musica"; argv[1] = c; argv[2] = NULL;
        pid = executar("/bin/musica", argv);
        registrar("arquivos: abrindo no tocador %s (pid %d)", c, pid);
    } else if (tem_extensao(nomes[i], ".zip")) {
        extrair_zip(i, c);
        return;
    } else {
        argv[0] = "editor"; argv[1] = c; argv[2] = NULL;
        pid = executar("/bin/editor", argv);
        registrar("arquivos: abrindo no editor %s (pid %d)", c, pid);
    }
    char msg[200];
    formatar_texto(msg, sizeof msg, pid > 0 ? "Abrindo %s..." : "Não consegui abrir %s", nomes[i]);
    desenhar_rodape(msg);
}

static void nova_pasta(void)
{
    char nome[64] = "";
    if (!pedir_texto("Nova pasta", "Nome da nova pasta:", nome, sizeof nome) || !nome[0]) return;
    char c[200];
    copia_texto(c, pasta, sizeof c);
    if (compara_texto(pasta, "/") != 0) junta_texto(c, "/", sizeof c);
    junta_texto(c, nome, sizeof c);
    int r = criar_pasta(c);
    if (r < 0) { char m[200]; formatar_texto(m, sizeof m, "Não consegui criar a pasta:\n%s", nome_erro(r)); mensagem("Arquivos", m); return; }
    registrar("arquivos: pasta criada %s", c);
    carregar();
}

/* ------------------------------------------------------------ lixeira (H87) */

/* A lixeira do disco de "caminho": /usb/.lixeira no pendrive, /.lixeira no resto. */
static const char *lixeira_de(const char *caminho)
{
    return comeca_com(caminho, "/usb/") || compara_texto(caminho, "/usb") == 0 ? "/usb/.lixeira" : "/.lixeira";
}

static bool na_lixeira(void)
{
    return compara_texto(pasta, "/.lixeira") == 0 || compara_texto(pasta, "/usb/.lixeira") == 0;
}

/* Botões do topo: na lixeira são outros. */
static void desenhar_botoes(void)
{
    g_retangulo(j, 0, 0, LARG, 40, 0xE6E9EC);
    g_retangulo(j, 0, 39, LARG, 1, COR_BORDA);
    bool lx = na_lixeira();
    bt[1].texto = lx ? "Restaurar" : "Nova pasta";
    bt[1].icone = lx ? icone_restaurar : icone_nova_pasta;
    bt[2].texto = lx ? "Apagar de vez" : "Apagar";
    bt[2].icone = lx ? icone_apagar : icone_lixeira;
    bt[2].l = lx ? 150 : 100;
    bt[3].x = bt[2].x + bt[2].l + 8;
    int n = lx ? 4 : 3;
    for (int i = 0; i < n; i++) botao_desenhar(j, &bt[i]);
    jan_mostrar_area(j, 0, 0, LARG, 40);
}

/* Apaga um arquivo ou uma pasta com tudo dentro. */
static int apagar_tudo(const char *c, int nivel)
{
    info_arquivo_t inf;
    int r = info_arquivo(c, &inf);
    if (r < 0) return r;
    if (inf.tipo != TIPO_PASTA) return apagar(c);
    if (nivel > 32) return ERRO_LIMITE;
    entrada_pasta_t e;
    char filho[200];
    while (listar(c, 0, &e) == 1) {   /* sempre o primeiro: a lista encolhe */
        copia_texto(filho, c, sizeof filho);
        junta_texto(filho, "/", sizeof filho);
        junta_texto(filho, e.nome, sizeof filho);
        r = apagar_tudo(filho, nivel + 1);
        if (r < 0) return r;
    }
    return remover_pasta(c);
}

/* Procura "nome" no .origens da lixeira; devolve o caminho original. */
static bool origem_de(const char *lix, const char *nome, char *orig, int max)
{
    char arq[200], linha[260];
    formatar_texto(arq, sizeof arq, "%s/.origens", lix);
    int fd = abrir(arq, ABRIR_LER);
    if (fd < 0) return false;
    bool achou = false;
    while (ler_linha(fd, linha, sizeof linha) >= 0) {
        char *tab = acha_char(linha, '\t');
        if (!tab) continue;
        *tab = 0;
        if (compara_texto(linha, nome) == 0) { copia_texto(orig, tab + 1, (size_t)max); achou = true; }   /* a última vale */
    }
    fechar(fd);
    return achou;
}

static void mover_para_lixeira(int i, const char *c)
{
    const char *lix = lixeira_de(c);
    criar_pasta(lix);
    char destino[200];
    info_arquivo_t inf;
    formatar_texto(destino, sizeof destino, "%s/%s", lix, nomes[i]);
    for (int n = 2; info_arquivo(destino, &inf) == 0 && n < 1000; n++)   /* já tem um com esse nome */
        formatar_texto(destino, sizeof destino, "%s/%s~%d", lix, nomes[i], n);
    int r = renomear(c, destino);
    if (r < 0) {
        char m[240];
        formatar_texto(m, sizeof m, "Não consegui pôr na lixeira:\n%s", nome_erro(r));
        mensagem("Arquivos", m);
        registrar("arquivos: lixeira falhou %s (%d)", c, r);
        return;
    }
    char arq[200], linha[300];
    formatar_texto(arq, sizeof arq, "%s/.origens", lix);
    int fd = abrir(arq, ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_ACRESCENTAR);
    if (fd >= 0) {
        formatar_texto(linha, sizeof linha, "%s\t%s\n", acha_ultimo_char(destino, '/') + 1, c);
        escrever_texto(fd, linha);
        fechar(fd);
    }
    registrar("arquivos: apagado %s (para a lixeira: %s)", c, destino);
}

static void apagar_item_modo(bool de_vez)
{
    int i = lista.sel;
    if (i < 0 || i >= lista.qtd) return;
    char c[200], pergunta[240];
    caminho_de(i, c, sizeof c);
    de_vez = de_vez || na_lixeira();
    if (de_vez) formatar_texto(pergunta, sizeof pergunta, "Apagar de vez\n%s%s?\nNão dá para desfazer.", nomes[i], tipos[i] == TIPO_PASTA ? "/" : "");
    else formatar_texto(pergunta, sizeof pergunta, "Tem certeza que quer apagar\n%s%s?\nVai para a Lixeira.", nomes[i], tipos[i] == TIPO_PASTA ? "/" : "");
    if (!confirmar("Apagar", pergunta)) return;
    if (!de_vez) { mover_para_lixeira(i, c); carregar(); return; }
    int r = apagar_tudo(c, 0);
    if (r < 0) { char m[200]; formatar_texto(m, sizeof m, "Não consegui apagar:\n%s", nome_erro(r)); mensagem("Arquivos", m); return; }
    registrar("arquivos: apagado de vez %s", c);
    carregar();
}

static void apagar_item(void) { apagar_item_modo(false); }

static void restaurar_item(void)
{
    int i = lista.sel;
    if (!na_lixeira() || i < 0 || i >= lista.qtd) return;
    char c[200], orig[200];
    caminho_de(i, c, sizeof c);
    if (!origem_de(pasta, nomes[i], orig, sizeof orig)) {   /* sem registro: volta para a raiz do disco */
        formatar_texto(orig, sizeof orig, "%s/%s", compara_texto(pasta, "/.lixeira") == 0 ? "" : "/usb", nomes[i]);
    }
    info_arquivo_t inf;
    if (info_arquivo(orig, &inf) == 0) {
        char m[260];
        formatar_texto(m, sizeof m, "Já existe %s.\nApague ou renomeie antes de restaurar.", orig);
        mensagem("Arquivos", m);
        return;
    }
    char pai[200];   /* a pasta de origem pode ter sido apagada: recria */
    copia_texto(pai, orig, sizeof pai);
    for (char *k = pai + 1; *k; k++) if (*k == '/') { *k = 0; criar_pasta(pai); *k = '/'; }
    int r = renomear(c, orig);
    if (r < 0) { char m[200]; formatar_texto(m, sizeof m, "Não consegui restaurar:\n%s", nome_erro(r)); mensagem("Arquivos", m); return; }
    registrar("arquivos: restaurado %s -> %s", c, orig);
    carregar();
}

static void esvaziar_lixeira(void)
{
    if (!na_lixeira() || lista.qtd == 0) return;
    if (!confirmar("Esvaziar lixeira", "Apagar de vez tudo o que está na lixeira?")) return;
    char lix[32];
    copia_texto(lix, pasta, sizeof lix);
    int r = apagar_tudo(lix, 0);
    criar_pasta(lix);
    registrar("arquivos: lixeira esvaziada %s (%d)", lix, r);
    carregar();
}

static void abrir_lixeira(void)
{
    const char *lix = comeca_com(pasta, "/usb") && !na_lixeira() ? "/usb/.lixeira" : "/.lixeira";
    if (na_lixeira()) lix = pasta;
    criar_pasta(lix);
    copia_texto(pasta, lix, sizeof pasta);
    carregar();
}

int principal(int argc, char **argv)
{
    if (argc > 1) copia_texto(pasta, argv[1], sizeof pasta);
    j = jan_abrir("Arquivos", LARG, ALT);
    if (!j) return 1;
    jan_redimensionavel(j, 480, 300);
    lista = (lista_t){ LATERAL + 6, 70, LARG - LATERAL - 14, ALT - 70 - 30, nomes, 0, -1, 0, icone };
    atualizar_pendrive();
    desenhar_lateral();
    carregar();
    jan_mostrar(j);
    registrar("arquivos: pronto");

    evento_t e;
    unsigned ultimo = tempo_ms();
    for (;;) {
        if (proximo_evento(j->id, &e, false) != 1) {
            /* Sem evento: de vez em quando confere se um pendrive entrou ou saiu. */
            if (tempo_ms() - ultimo > 700) {
                ultimo = tempo_ms();
                if (atualizar_pendrive()) {
                    desenhar_lateral();
                    info_arquivo_t inf;
                    if (comeca_com(pasta, "/usb") && info_arquivo(pasta, &inf) < 0) { copia_texto(pasta, "/", sizeof pasta); carregar(); }
                }
            }
            dormir(50);
            continue;
        }
        int c;
        while (recolher(&c) > 0) ;   /* programas abertos daqui que já terminaram */
        if (e.tipo == EV_FECHAR) break;
        if (e.tipo == EV_REDIMENSIONAR) {                   /* H78: a lista acompanha a janela */
            jan_novo_tamanho(j, &e);
            larg_jan = j->larg;
            alt_jan = j->alt;
            g_retangulo(j, 0, 0, LARG, ALT, COR_FUNDO);
            lista.l = LARG - LATERAL - 14;
            lista.a = ALT - 70 - 30;
            desenhar_lateral();
            carregar();
            jan_mostrar(j);
            registrar("arquivos: tamanho %dx%d", LARG, ALT);
            continue;
        }
        if (botao_tratar(j, &bt_lado_raiz, &e)) { copia_texto(pasta, "/", sizeof pasta); carregar(); }
        if (tem_pendrive && botao_tratar(j, &bt_lado_pen, &e)) {
            int r = pendrive.montado ? 0 : montar(pendrive.nome);
            if (r < 0) { char m[120]; formatar_texto(m, sizeof m, "Não consegui montar o pendrive:\n%s", nome_erro(r)); mensagem("Arquivos", m); }
            else { atualizar_pendrive(); desenhar_lateral(); copia_texto(pasta, "/usb", sizeof pasta); carregar(); registrar("arquivos: pendrive aberto"); }
        }
        if (tem_pendrive && pendrive.montado && botao_tratar(j, &bt_remover, &e)) {
            int r = desmontar(pendrive.nome);
            registrar("arquivos: remover com seguranca -> %d", r);
            if (comeca_com(pasta, "/usb")) { copia_texto(pasta, "/", sizeof pasta); carregar(); }
            atualizar_pendrive();
            desenhar_lateral();
            mensagem("Arquivos", r < 0 ? "Não consegui desmontar o pendrive." : "Pode remover o pendrive.");
        }
        if (botao_tratar(j, &bt_lixeira, &e)) abrir_lixeira();
        bool lx = na_lixeira();
        if (botao_tratar(j, &bt[0], &e)) voltar();
        if (botao_tratar(j, &bt[1], &e)) { if (lx) restaurar_item(); else nova_pasta(); }
        if (botao_tratar(j, &bt[2], &e)) apagar_item();
        if (lx && botao_tratar(j, &bt[3], &e)) esvaziar_lixeira();
        if (e.tipo == EV_TECLA && e.tecla == '\b') { voltar(); continue; }
        if (e.tipo == EV_TECLA && e.tecla == TECLA_DELETE) { apagar_item_modo(e.modificadores & 1); continue; }
        if (e.tipo == EV_MENU) {                            /* menu do botão direito (H81) */
            registrar("arquivos: menu %u", e.tecla);
            if (lx) {
                if (e.tecla == 0) restaurar_item();
                else if (e.tecla == 1) apagar_item();
                else if (e.tecla == 2) esvaziar_lixeira();
                else if (e.tecla == 3) voltar();
            } else {
                if (e.tecla == 0 && lista.sel >= 0) abrir_item(lista.sel);
                else if (e.tecla == 1) apagar_item();
                else if (e.tecla == 2) nova_pasta();
                else if (e.tecla == 3) voltar();
            }
            continue;
        }
        int r = lista_tratar(j, &lista, &e);
        if (e.tipo == EV_CLIQUE && (e.botoes & 2)) {
            jan_menu(j, e.x, e.y, lx ? "Restaurar|Apagar de vez...|Esvaziar lixeira...|Voltar"
                                     : "Abrir|Apagar...|Nova pasta...|Voltar");
            continue;
        }
        if (r == LISTA_ATIVOU) abrir_item(lista.sel);
        else if (r == LISTA_SELECIONOU && lista.sel >= 0) {
            char msg[100];
            if (tipos[lista.sel] == TIPO_PASTA) formatar_texto(msg, sizeof msg, "%s/ (pasta)", nomes[lista.sel]);
            else formatar_texto(msg, sizeof msg, "%s: %u bytes%s", nomes[lista.sel], tamanhos[lista.sel],
                                tipos[lista.sel] == TIPO_PROGRAMA ? " (programa)" : "");
            desenhar_rodape(msg);
        }
    }
    jan_fechar(j);
    return 0;
}
