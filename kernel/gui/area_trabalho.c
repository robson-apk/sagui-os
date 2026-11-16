/* area_trabalho.c - o fundo, o mascote, os ícones e a barra superior (H20, H28).
 *
 * O fundo não muda: é desenhado uma vez numa superfície guardada (degradê
 * verde, amarelo e azul, o sagui no canto e os ícones), e a cada
 * redesenho só a parte necessária é copiada. A barra superior é desenhada
 * por cima, porque o relógio muda. */
#include "gui.h"
#include "hda.h"
#include "vfs.h"
#include "abi.h"
#include "arte.h"
#include "console.h"
#include "memoria.h"
#include "processo.h"
#include "relogio.h"
#include "configuracoes.h"
#include "rede.h"
#include "fat32.h"
#include "texto.h"
#include "texto.h"
#include "video.h"
#include "disco.h"
#include "sistema.h"

static uint32_t *fundo;
static int larg, alt;
static int minuto_mostrado = -1;
static int rede_mostrada = -1;
static int volume_mostrado = -1;

typedef struct {
    const char *nome;
    const char *programa;
    const uint32_t *px;        /* ícone 64x64 em ARGB (tools/mkarte.py, H81) */
} icone_t;

static const icone_t icones[] = {
    { "Arquivos",    "/bin/arquivos",    arte_px_arquivos },
    { "Terminal",    "/bin/terminal",    arte_px_terminal },
    { "Editor",      "/bin/editor",      arte_px_editor },
    { "Calculadora", "/bin/calculadora", arte_px_calculadora },
    { "Banana",      "/bin/banana",      arte_px_banana },
    { "Sagui 3D",    "/bin/sagui3d",     arte_px_sagui3d },   /* bônus B4 */
    { "Configurações", "/bin/configuracoes", arte_px_configuracoes },   /* H38 */
    { "Sobre",       "/bin/sobre-o-sistema",   arte_px_sobre },
    { "Pendrive",    "/bin/arquivos",    arte_px_pendrive },   /* só aparece com um pendrive conectado (H34) */
};
#define QTD_ICONES (int)(sizeof icones / sizeof icones[0])
#define ICONE_PENDRIVE (QTD_ICONES - 1)
static bool pendrive_visivel;
static char nome_pendrive[24] = "Pendrive";

/* Programas instalados no pendrive (H39): /usb/programas, um ícone cada,
 * depois dos ícones do sistema. A pasta é conferida a cada 2 segundos. */
#define MAX_INSTALADOS 8
static char instalados[MAX_INSTALADOS][NOME_MAX];
static int qtd_instalados;
static uint32_t conferido_em;

/* Programas do sistema escondidos pelo usuário (configuração
 * "programas.ocultos", uma lista separada por vírgulas). Eles continuam em
 * /bin e no Terminal; só o ícone some. */
static bool oculto[16];

static bool icone_visivel(int i)
{
    if (i >= QTD_ICONES) return i - QTD_ICONES < qtd_instalados;
    if (i == ICONE_PENDRIVE) return pendrive_visivel;
    return !oculto[i];
}

static void ler_ocultos(void)
{
    const char *lista = config_obter("programas.ocultos");
    for (int i = 0; i < QTD_ICONES && i < 16; i++) {
        oculto[i] = false;
        if (!lista || i == ICONE_PENDRIVE) continue;
        const char *nome = icones[i].programa + 5;   /* depois de "/bin/" */
        size_t n = txt_tamanho(nome);
        for (const char *p = lista; *p;) {
            while (*p == ',' || *p == ' ') p++;
            const char *fim = p;
            while (*fim && *fim != ',') fim++;
            size_t m = (size_t)(fim - p);
            while (m > 0 && p[m - 1] == ' ') m--;
            if (m == n && txt_compara_n(p, nome, n) == 0) oculto[i] = true;
            p = fim;
        }
    }
}

static const char *nome_icone(int i)
{
    if (i >= QTD_ICONES) return instalados[i - QTD_ICONES];
    if (i == ICONE_PENDRIVE) return nome_pendrive;
    /* Nome trocado pelo usuário (botão direito > Renomear): icone.<programa>. */
    char chave[48];
    snformatar(chave, sizeof chave, "icone.%s", icones[i].programa + 5);
    const char *apelido = config_obter(chave);
    return apelido && apelido[0] ? apelido : icones[i].nome;
}

const char *area_icone_nome(int i) { return nome_icone(i); }   /* H89: o menu Iniciar */

/* Que tipo de ícone é (para o menu do botão direito): 0 do sistema,
 * 1 programa instalado no pendrive, 2 o pendrive. */
int area_icone_tipo(int i)
{
    if (i >= QTD_ICONES) return 1;
    return i == ICONE_PENDRIVE ? 2 : 0;
}

/* Ações do menu do botão direito. Renomear e remover perguntam (texto ou
 * confirmação) num programa comum, /bin/icone, porque o kernel não tem
 * caixas de texto. */
void area_icone_acao(int i, int acao)
{
    char caminho[NOME_MAX + 24], nome[48];
    if (i < 0 || i >= QTD_ICONES + qtd_instalados) return;
    txt_copia_n(nome, nome_icone(i), sizeof nome);
    if (i >= QTD_ICONES) snformatar(caminho, sizeof caminho, "/usb/programas/%s", instalados[i - QTD_ICONES]);
    else txt_copia_n(caminho, icones[i].programa, sizeof caminho);
    if (acao == ACAO_ABRIR) { area_icone_abrir(i); return; }
    if (acao == ACAO_DESMONTAR) { area_icone_direito(i); return; }
    const char *verbo = acao == ACAO_RENOMEAR ? (i >= QTD_ICONES ? "renomear-programa" : "renomear-icone")
                      : acao == ACAO_REMOVER ? "remover" : "esconder";
    char *argv[] = { "icone", (char *)verbo, caminho, nome, NULL };
    int pid = processo_executar("/bin/icone", 4, argv, NULL);
    klog("[GUI] icone %s: %s %s (pid %d)\n", nome, verbo, caminho, pid);
}

static const uint32_t *px_icone(int i)
{
    return i >= QTD_ICONES ? arte_px_instalado : icones[i].px;
}
#define ICONE_X 20
#define ICONE_Y0 (BARRA_SUPERIOR + 16)
#define ICONE_PASSO 92
#define ICONE_COLUNA 110   /* distância entre as colunas de ícones */
#define ICONE_L 96
#define ICONE_A 88

int area_qtd_icones(void) { return QTD_ICONES + qtd_instalados; }

void area_icone_retangulo(int i, int *x, int *y, int *l, int *a)
{
    /* Os ícones descem pela coluna da esquerda e, quando não cabem mais
     * (8 ou mais numa tela de 768 linhas), continuam numa segunda coluna. */
    int por_coluna = (alt - ICONE_Y0) / ICONE_PASSO;
    if (por_coluna < 1) por_coluna = 1;
    /* A posição na tela conta só os ícones visíveis antes deste (um
     * programa oculto não deixa buraco). */
    int vaga = 0;
    for (int k = 0; k < i; k++) if (icone_visivel(k)) vaga++;
    *x = ICONE_X + (vaga / por_coluna) * ICONE_COLUNA;
    *y = ICONE_Y0 + (vaga % por_coluna) * ICONE_PASSO;
    *l = ICONE_L; *a = ICONE_A;
}

/* ------------------------------------------------------------ seleção (H90)
 *
 * O laço translúcido da área de trabalho veio do commit 0b3c462 do Robson
 * (ramo area-lixeira, do código da 1.0), refeito para o código atual: os
 * ícones tocados pelo retângulo ficam marcados; um clique num ícone marca só
 * ele; clicar no vazio desmarca. */
static bool selecionado[QTD_ICONES + MAX_INSTALADOS];

void area_selecionar(int x, int y, int l, int a, bool somar)
{
    for (int i = 0; i < QTD_ICONES + qtd_instalados; i++) {
        if (!somar) selecionado[i] = false;
        if (!icone_visivel(i)) continue;
        int ix, iy, il, ia;
        area_icone_retangulo(i, &ix, &iy, &il, &ia);
        if (ix < x + l && x < ix + il && iy < y + a && y < iy + ia) selecionado[i] = true;
    }
}

bool area_limpar_selecao(void)
{
    bool havia = false;
    for (int i = 0; i < QTD_ICONES + MAX_INSTALADOS; i++) { havia |= selecionado[i]; selecionado[i] = false; }
    return havia;
}

bool area_icone_selecionado(int i)
{
    return i >= 0 && i < QTD_ICONES + qtd_instalados && selecionado[i] && icone_visivel(i);
}

int area_qtd_selecionados(void)
{
    int n = 0;
    for (int i = 0; i < QTD_ICONES + qtd_instalados; i++) n += area_icone_selecionado(i);
    return n;
}

/* ------------------------------------------------------------ fundos (H81)
 *
 * "Brasil com sol" e "Mata Atlântica" vieram do pull request #1 do Robson
 * de Lima Barbosa. O primeiro é desenhado por código; o segundo é uma imagem
 * (gerada por IA, com o modelo Z-Image Turbo, a pedido do Robson) no formato
 * .sgi descrito em tools/mkfundo.py. */

/* Seno de um ângulo de 0 a 1023 (volta inteira), de -256 a 256, por
 * parábolas: o kernel não usa ponto flutuante. */
static int seno(int a)
{
    a &= 1023;
    int t = a & 511, v = t * (512 - t) / 256;
    return a < 512 ? v : -v;
}

/* Altura de uma onda (copa da mata ou rio) na coluna x: duas senoides somadas. */
static int onda(int x, int base, int amplitude, int passo, int fase)
{
    return base + (seno(x * passo / 16 + fase) * amplitude + seno(x * passo / 7 + fase * 3) * amplitude / 3) / 256;
}

/* Degradê verde-amarelo-azul com a luz de um sol no meio, a copa da mata no
 * alto e ondas de rio embaixo. Uma trama 4x4 disfarça as faixas do degradê. */
static void fundo_brasil_com_sol(void)
{
    static const uint8_t trama[4][4] = { {0,8,2,10}, {12,4,14,6}, {3,11,1,9}, {15,7,13,5} };
    const uint32_t verde = 0x0B7A3E, amarelo = 0xE8BE30, azul = 0x14337E, sol = 0xFFF0A0;
    int sx = larg * 3 / 5, sy = alt / 2, raio = alt / 2 + 40;
    for (int x = 0; x < larg; x++) {
        int mata1 = onda(x, alt / 7, 26, 5, 100), mata2 = onda(x, alt / 11, 20, 7, 420);
        int rio1 = onda(x, alt * 3 / 4, 24, 4, 0), rio2 = onda(x, alt * 5 / 6, 20, 6, 300), rio3 = onda(x, alt * 11 / 12, 12, 9, 640);
        for (int y = 0; y < alt; y++) {
            int m = alt / 2, num = (y < m ? y : y - m) * 16 + trama[y & 3][x & 3], den = (y < m ? m : alt - m) * 16;
            uint32_t c = y < m ? cor_mistura(verde, amarelo, num, den) : cor_mistura(amarelo, azul, num, den);
            int dx = x - sx, dy = y - sy, d = (dx * dx + dy * dy) / raio;
            if (d < raio) {
                int f = raio - d;
                c = cor_mistura(c, sol, f * f / raio * 3 / 5, raio);
            }
            if (y < mata1) c = cor_mistura(c, 0x075C2E, 1, 2);
            if (y < mata2) c = cor_mistura(c, 0x044422, 3, 5);
            if (y == mata1) c = cor_mistura(c, 0xFFFFFF, 1, 5);
            if (y > rio1) c = cor_mistura(c, 0x1C5AA8, 2, 5);
            if (y > rio2) c = cor_mistura(c, 0x123C86, 1, 2);
            if (y > rio3) c = cor_mistura(c, 0x0C2860, 1, 2);
            if (y == rio1 || y == rio2) c = cor_mistura(c, 0xFFFFFF, 1, 4);
            fundo[y * larg + x] = c;
        }
    }
}

/* Imagem .sgi (tools/mkfundo.py): "SGI1", largura, altura, paleta de 256
 * cores e os índices em RLE. Em outra resolução, pega o pixel mais próximo.
 * Devolve false se o arquivo faltar ou estiver errado. */
#define SGI_CAB 776
static bool fundo_imagem(const char *caminho)
{
    uint8_t *d;
    uint32_t tam;
    if (vfs_ler_inteiro(caminho, &d, &tam) < 0) return false;
    bool ok = false;
    int il = tam > SGI_CAB ? d[4] | d[5] << 8 : 0, ia = tam > SGI_CAB ? d[6] | d[7] << 8 : 0;
    uint8_t *idx = il > 0 && ia > 0 && d[0] == 'S' && d[1] == 'G' && d[2] == 'I' && d[3] == '1'
                 ? kmalloc((uint32_t)(il * ia)) : NULL;
    if (idx) {
        uint32_t i = SGI_CAB, o = 0, total = (uint32_t)(il * ia);
        while (i < tam && o < total) {
            uint32_t n = d[i++];
            if (n < 128) {
                for (n++; n && i < tam && o < total; n--) idx[o++] = d[i++];
            } else if (i < tam) {
                uint8_t v = d[i++];
                for (n -= 125; n && o < total; n--) idx[o++] = v;
            }
        }
        if (o == total) {
            for (int y = 0; y < alt; y++) {
                const uint8_t *linha = idx + (y * ia / alt) * il;
                for (int x = 0; x < larg; x++) {
                    const uint8_t *c = d + 8 + linha[x * il / larg] * 3;
                    fundo[y * larg + x] = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
                }
            }
            ok = true;
            klog("[GUI] fundo: %s %dx%d\n", caminho, il, ia);
        }
        kfree(idx);
    }
    kfree(d);
    return ok;
}

static void texto_com_sombra(superficie_t *s, int x, int y, const char *t, uint32_t cor)
{
    /* Contorno escuro embaixo e à direita: o nome continua legível em cima
     * da imagem da Mata Atlântica (H81, do Robson). */
    sup_texto(s, x + 1, y + 1, t, 0x101010);
    sup_texto(s, x + 1, y, t, 0x101010);
    sup_texto(s, x, y + 1, t, 0x101010);
    sup_texto(s, x, y, t, cor);
}

static void desenhar_fundo(void)
{
    superficie_t s;
    sup_iniciar(&s, fundo, larg, alt);
    /* Degradê vertical em três cores, escolhido em Configurações > Aparência
     * (H38). O padrão são as cores do Brasil (verde, amarelo, azul) em tons
     * mais suaves que os da bandeira, para o texto e os ícones continuarem
     * legíveis. */
    uint32_t c0 = 0x0B7A3E, c1 = 0xE0B92A, c2 = 0x14337E;
    const char *tema = config_obter("aparencia.fundo");
    bool pronto = false;
    if (tema && txt_compara(tema, "mata-atlantica") == 0) pronto = fundo_imagem("/arte/mata-atlantica.sgi");
    if (!pronto && tema && (txt_compara(tema, "sol") == 0 || txt_compara(tema, "mata-atlantica") == 0)) {
        fundo_brasil_com_sol();   /* (também a reserva, se a imagem faltar) */
        pronto = true;
    }
    if (tema && txt_compara(tema, "mata") == 0)       { c0 = 0x0E3B22; c1 = 0x2E7D32; c2 = 0x0A2416; }
    else if (tema && txt_compara(tema, "ceu") == 0)   { c0 = 0x1E5AA8; c1 = 0x6FB3E8; c2 = 0xD8EEF8; }
    else if (tema && txt_compara(tema, "noite") == 0) { c0 = 0x0B1026; c1 = 0x2B1E5A; c2 = 0x060814; }
    else if (tema && txt_compara(tema, "terra") == 0) { c0 = 0x7A3E14; c1 = 0xD98A3A; c2 = 0x3A1E0C; }
    for (int y = 0; y < alt && !pronto; y++) {
        uint32_t c = y < alt / 2 ? cor_mistura(c0, c1, y, alt / 2)
                                 : cor_mistura(c1, c2, y - alt / 2, alt - alt / 2);
        uint32_t *p = fundo + y * larg;
        for (int x = 0; x < larg; x++) p[x] = c;
    }
    /* O mascote, grande, no canto de baixo à direita (dá para esconder). */
    const char *mascote = config_obter("aparencia.sagui");
    if (!mascote || txt_compara(mascote, "nao") != 0) {
        int esc = larg >= 1000 ? 5 : 3;
        int mx = larg - 32 * esc - 30, my = alt - 32 * esc - 40;
        sup_desenho(&s, mx, my, &arte_sagui, esc);
        texto_com_sombra(&s, mx + 16 * esc - texto_largura("SaguiOS") / 2, my + 32 * esc + 6, "SaguiOS", 0xFFFFFF);
    }
    /* Ícones na coluna da esquerda, com o nome embaixo. */
    ler_ocultos();
    for (int i = 0; i < QTD_ICONES + qtd_instalados; i++) {
        if (!icone_visivel(i)) continue;
        int x, y, l, a;
        area_icone_retangulo(i, &x, &y, &l, &a);
        sup_icone(&s, x + (l - ICONE_LADO) / 2, y, px_icone(i), ICONE_LADO);
        const char *nome = nome_icone(i);
        texto_com_sombra(&s, x + (l - texto_largura(nome)) / 2, y + 68, nome, 0xFFFFFF);
    }
}

bool area_iniciar(void)
{
    larg = (int)video_largura();
    alt = (int)video_altura();
    fundo = kmalloc((uint32_t)(larg * alt) * 4);
    if (!fundo) return false;
    desenhar_fundo();
    return true;
}

/* A lista de programas em /usb/programas mudou (H39)? Confere a cada 2 s. */
bool area_instalados_mudaram(void)
{
    if (relogio_tiques() - conferido_em < 2 * TIQUES_POR_SEGUNDO) return false;
    conferido_em = relogio_tiques();
    char novos[MAX_INSTALADOS][NOME_MAX];
    int n = 0;
    if (disco_montado()) {
        entrada_pasta_t e;
        for (int k = 0; n < MAX_INSTALADOS && fat_listar("programas", k, &e) == 1; k++)
            if (e.tipo != TIPO_PASTA && e.nome[0] != '.') txt_copia_n(novos[n++], e.nome, NOME_MAX);
    }
    bool igual = n == qtd_instalados;
    for (int k = 0; igual && k < n; k++) igual = txt_compara(novos[k], instalados[k]) == 0;
    if (igual) return false;
    for (int k = 0; k < n; k++) txt_copia_n(instalados[k], novos[k], NOME_MAX);
    qtd_instalados = n;
    klog("[GUI] programas instalados no pendrive: %d\n", n);
    desenhar_fundo();
    return true;
}

/* As configurações mudaram (H38)? Redesenha o fundo; true = redesenhar a tela. */
bool area_config_mudou(void)
{
    static uint32_t vista = 0;
    if (config_geracao() == vista) return false;
    vista = config_geracao();
    desenhar_fundo();
    return true;
}

void area_fundo(superficie_t *s)
{
    sup_copiar(s, 0, 0, fundo, larg, alt, larg);
}

bool area_relogio_mudou(void)
{
    if (rede_estado() != rede_mostrada) return true;   /* o ícone de rede também vive na barra */
    if (sis_som_volume((uint32_t)-1, 0, 0, 0, 0) != volume_mostrado && volume_mostrado != -1) return true;
    /* Ler o CMOS é lento e acontece com as interrupções desligadas: uma vez
     * por segundo basta, e o mouse não engasga (H81, do Robson). */
    static uint32_t proxima;
    uint32_t agora = relogio_tiques();
    if ((int32_t)(agora - proxima) < 0) return false;
    proxima = agora + TIQUES_POR_SEGUNDO;
    data_hora_t d;
    cmos_ler_data(&d);
    return d.minuto != minuto_mostrado;
}

/* vidro: o fundo da barra já foi pintado (vidro, H74); claro: tema claro (H76). */
void area_barra(superficie_t *s, bool vidro, bool claro, bool frase_visivel)
{
    static const char *semana[] = { "dom", "seg", "ter", "qua", "qui", "sex", "sáb" };
    uint32_t nome = claro ? 0x0B6A36 : 0xFFDF4A, frase = claro ? 0x4A5560 : 0x9AA8B8, relogio = claro ? 0x1E2328 : 0xFFFFFF;
    if (!vidro) sup_retangulo(s, 0, 0, larg, BARRA_SUPERIOR, claro ? 0xE8ECEF : 0x18202A);
    sup_retangulo(s, 0, BARRA_SUPERIOR - 1, larg, 1, 0x0B7A3E);
    /* O botão do menu (H75): cabecinha do sagui (as linhas 2 a 17 do ícone
     * "sobre"), o nome e uma setinha. */
    desenho_t cabeca = { arte_icone_sobre.linhas + 2, 32, 16 };
    sup_desenho(s, 6, 4, &cabeca, 1);
    sup_texto(s, 44, 4, "SaguiOS", nome);
    for (int k = 0; k < 4; k++) sup_retangulo(s, 104 - 4 + k, 10 + k, 8 - 2 * k, 1, nome);
    if (frase_visivel) sup_texto(s, 120, 4, "um sistema operacional escrito do zero por IA", frase);
    (void)relogio;
    data_hora_t d;
    cmos_ler_data(&d);
    minuto_mostrado = d.minuto;
    char hora[48];
    snformatar(hora, sizeof hora, "%s %02u/%02u/%u  %02u:%02u", semana[dia_da_semana(d.ano, d.mes, d.dia)],
               d.dia, d.mes, d.ano, d.hora, d.minuto);
    sup_texto(s, larg - texto_largura(hora) - 10, 4, hora, relogio);
    /* Ícone de rede (H47), à esquerda do botão de energia: verde conectado,
     * amarelo pedindo endereço, cinza sem cabo; some se não há placa. */
    int estado = rede_estado();
    rede_mostrada = estado;
    if (estado > 0) {
        int ex0, ey0, el0, ea0;
        area_energia_retangulo(&ex0, &ey0, &el0, &ea0);
        uint32_t cor = estado == 3 ? 0x5FD35F : (estado == 2 ? 0xF4D03F : 0x8A939C);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                if (arte_icone_rede[y][x] == 'B') sup_retangulo(s, ex0 - 28 + x, ey0 + 2 + y, 1, 1, cor);
        if (estado == 1) {   /* sem cabo: um x vermelho por cima */
            for (int k = 0; k < 8; k++) {
                sup_retangulo(s, ex0 - 28 + 4 + k, ey0 + 2 + 2 + k, 1, 1, 0xE04848);
                sup_retangulo(s, ex0 - 28 + 11 - k, ey0 + 2 + 2 + k, 1, 1, 0xE04848);
            }
        }
    }
    /* Som (H75), à esquerda da rede: um alto-falante com ondas pelo volume. */
    int vol = sis_som_volume((uint32_t)-1, 0, 0, 0, 0);
    if (vol >= 0) {
        int sx, sy, sl, sa;
        area_som_retangulo(&sx, &sy, &sl, &sa);
        uint32_t c = claro ? 0x2A323A : 0xE8ECEF;
        sup_retangulo(s, sx + 2, sy + 7, 3, 6, c);
        for (int k = 0; k < 5; k++) sup_retangulo(s, sx + 5 + k, sy + 7 - k, 1, 6 + 2 * k, c);
        int ondas = vol == 0 ? 0 : vol < 40 ? 1 : vol < 75 ? 2 : 3;
        for (int o = 0; o < ondas; o++) {
            int ox = sx + 12 + o * 3, meio = sy + 10, h = 3 + o * 2;
            sup_retangulo(s, ox, meio - h, 1, 2 * h, c);
        }
        if (vol == 0)
            for (int k = 0; k < 6; k++) { sup_retangulo(s, sx + 12 + k, sy + 7 + k, 1, 1, 0xE04848); sup_retangulo(s, sx + 17 - k, sy + 7 + k, 1, 1, 0xE04848); }
        volume_mostrado = vol;
    }
    /* Botão de energia (H36), à esquerda do relógio. */
    int ex, ey, el, ea;
    area_energia_retangulo(&ex, &ey, &el, &ea);
    desenho_t energia = { arte_botao_energia, 16, 16 };
    sup_desenho(s, ex + 2, ey + 2, &energia, 1);
    klog("[GUI] relogio: %s\n", hora);
}

/* Onde fica o botão de energia na barra: logo à esquerda da data e hora. */
void area_energia_retangulo(int *x, int *y, int *l, int *a)
{
    *l = 20; *a = 20; *y = 2;
    *x = larg - texto_largura("ddd 00/00/0000  00:00") - 10 - 30;
}

/* O som fica à esquerda do ícone de rede. */
void area_som_retangulo(int *x, int *y, int *l, int *a)
{
    area_energia_retangulo(x, y, l, a);
    *x -= 56;
}

bool area_som_em(int x, int y)
{
    int sx, sy, sl, sa;
    area_som_retangulo(&sx, &sy, &sl, &sa);
    return x >= sx && x < sx + sl && y >= sy && y < sy + sa;
}

bool area_energia_em(int x, int y)
{
    int ex, ey, el, ea;
    area_energia_retangulo(&ex, &ey, &el, &ea);
    return x >= ex && x < ex + el && y >= ey && y < ey + ea;
}

void area_energia_abrir(void)
{
    char *argv[] = { "energia", NULL };
    int pid = processo_executar("/bin/energia", 1, argv, NULL);
    klog("[GUI] botao de energia -> /bin/energia (pid %d)\n", pid);
}

int area_icone_em(int x, int y)
{
    for (int i = 0; i < QTD_ICONES + qtd_instalados; i++) {
        if (!icone_visivel(i)) continue;
        int ix, iy, l, a;
        area_icone_retangulo(i, &ix, &iy, &l, &a);
        if (x >= ix && x < ix + l && y >= iy && y < iy + a) return i;
    }
    return -1;
}

/* Mostra ou esconde o ícone do pendrive (H34). Devolve true se mudou. */
bool area_pendrive(bool presente, const char *rotulo)
{
    char nome[24];
    txt_copia_n(nome, rotulo && rotulo[0] ? rotulo : "Pendrive", sizeof nome);
    if (presente == pendrive_visivel && txt_compara(nome, nome_pendrive) == 0) return false;
    pendrive_visivel = presente;
    txt_copia_n(nome_pendrive, nome, sizeof nome_pendrive);
    desenhar_fundo();
    return true;
}

static disco_t *primeiro_disco(void)
{
    for (int i = 0; i < MAX_DISCOS; i++) {
        disco_t *d = disco_indice(i);
        if (d && d->presente && !d->de_boot) return d;
    }
    return NULL;
}

/* Botão direito num ícone: no pendrive, "remover com segurança". */
void area_icone_direito(int i)
{
    if (i != ICONE_PENDRIVE) return;
    disco_t *d = disco_montado();
    if (d) disco_desmontar(d);
    else gui_aviso("O pendrive não está montado: pode remover.");
}

/* Abre o programa do ícone (H28). Ele não tem pai: ninguém espera por ele. */
void area_icone_abrir(int i)
{
    if (i < 0 || i >= QTD_ICONES + qtd_instalados) return;
    if (i >= QTD_ICONES) {
        char caminho[NOME_MAX + 24];
        snformatar(caminho, sizeof caminho, "/usb/programas/%s", instalados[i - QTD_ICONES]);
        /* Só um programa com a marca de janela (e_flags, posta pelo cc) abre
         * direto. Os de texto e os sem marca (compilados antes da marca
         * existir) abrem dentro de um Terminal: senão rodavam sem janela,
         * terminavam e nada aparecia na tela. */
        uint8_t *img = NULL;
        uint32_t tam = 0;
        bool janela = true;
        if (vfs_ler_inteiro(caminho, &img, &tam) == 0 && tam >= 52) {
            uint32_t flags = (uint32_t)img[36] | ((uint32_t)img[37] << 8) | ((uint32_t)img[38] << 16) | ((uint32_t)img[39] << 24);
            janela = flags == ELF_SAGUI_JANELA;
        }
        if (img) kfree(img);
        int pid;
        if (janela) {
            char *argv[] = { instalados[i - QTD_ICONES], NULL };
            pid = processo_executar(caminho, 1, argv, NULL);
        } else {
            char *argv[] = { "terminal", "-e", caminho, NULL };
            pid = processo_executar("/bin/terminal", 3, argv, NULL);
        }
        klog("[GUI] icone instalado %s -> %s%s (pid %d)\n", instalados[i - QTD_ICONES], janela ? "" : "terminal -e ", caminho, pid);
        return;
    }
    if (i == ICONE_PENDRIVE) {
        disco_t *d = primeiro_disco();
        if (!d) return;
        int r = disco_montar(d);
        if (r == ERRO_FORMATO) { gui_aviso("Este pendrive não está em FAT32: formato não suportado."); return; }
        if (r < 0 && r != ERRO_OCUPADO) { gui_aviso("Não consegui montar o pendrive."); return; }
        char *argv[] = { "arquivos", "/usb", NULL };
        int pid = processo_executar("/bin/arquivos", 2, argv, NULL);
        klog("[GUI] icone Pendrive -> /bin/arquivos /usb (pid %d)\n", pid);
        return;
    }
    char *argv[] = { (char *)icones[i].programa + 5, NULL };
    int pid = processo_executar(icones[i].programa, 1, argv, NULL);
    klog("[GUI] icone %s -> %s (pid %d)\n", icones[i].nome, icones[i].programa, pid);
}
