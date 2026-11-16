/* chip8 - o emulador de CHIP-8 do SaguiOS, numa janela (H92).
 *
 *     chip8 [-m chip8|schip|xochip] [rom]
 *
 * Trazido do emulador do fork SaguiOS-Extended de Alejandro Anuncibay (a
 * decisão D070 de lá) e reescrito sobre o main atual. Sem ROM, mostra a
 * lista dos programas de /jogos/chip8 (os exemplos que vêm com o sistema,
 * montados no build a partir dos .asm de lá) e do pendrive (/usb e
 * /usb/chip8, arquivos .ch8, .sc8 e .xo8). Enter ou duplo clique abre; Esc
 * volta para a lista; P pausa; F5 recomeça o programa.
 *
 * O tempo: os 60 quadros por segundo vêm do relógio (tempo_ms). Desde o
 * começo deveriam ter passado (agora - t0) * 60 / 1000 quadros; o laço roda
 * os que faltam (no máximo 6 de uma vez, para não correr depois de um
 * engasgo), cada um com as instruções do quadro e um tique dos
 * temporizadores, e dorme até o próximo.
 *
 * O teclado: as 16 teclas do CHIP-8 (0-F) são lidas como teclas seguradas
 * (teclas_seguradas, a chamada do Sagui 3D), pela posição física: o bloco
 * 1234 / QWER / ASDF / ZXCV faz o papel do teclado de 4x4 da máquina
 * original (1 2 3 C / 4 5 6 D / 7 8 9 E / A 0 B F). As setas valem como
 * W A S D (as teclas 5, 7, 8 e 9).
 *
 * A tela: só os pixels que mudaram desde o quadro anterior são redesenhados
 * (ampliados), e só o retângulo deles vai para o kernel: ampliar a tela
 * inteira a cada quadro pesaria no QEMU sem aceleração. */
#include "sagui.h"
#include "gui.h"
#include "nucleo.h"

#define LARG    640
#define AREA_A  320
#define ESTADO  24
#define ALT     (AREA_A + ESTADO)
#define MAX_ROMS 64

/* Cores: apagado, plano 1, plano 2 e os dois planos (XO-CHIP), no verde e
 * no amarelo-banana do SaguiOS. */
static const uint32_t cores[4] = { 0x10201A, 0x3CDC6E, 0xFFD52E, 0xF5F5F0 };

/* Código de varredura (conjunto 1) de cada tecla do CHIP-8, de 0 a F. */
static const uint8_t cv_tecla[16] = {
    0x2D, 0x02, 0x03, 0x04,   /* 0=X 1=1 2=2 3=3 */
    0x10, 0x11, 0x12, 0x1E,   /* 4=Q 5=W 6=E 7=A */
    0x1F, 0x20, 0x2C, 0x2E,   /* 8=S 9=D A=Z B=C */
    0x05, 0x13, 0x21, 0x2F,   /* C=4 D=R E=F F=V */
};

static jan_t *j;
static c8_t maq;
static uint8_t *rom;
static int tam_rom, modo_forcado = -1, modo;
static char nome_rom[64];
static bool rodando, pausado;
static unsigned t0, feitos;
static uint8_t tela_antes[C8_LARG_MAX * C8_ALT_MAX];
static bool tudo;                 /* redesenhar a tela inteira no próximo quadro */
static uint32_t freq_tocando;

static lista_t lista;
static char nomes[MAX_ROMS][64];
static char caminhos[MAX_ROMS][96];

/* ------------------------------------------------------------ desenho */

static int escala(void) { int e1 = LARG / maq.larg, e2 = AREA_A / maq.alt; return e1 < e2 ? e1 : e2; }

static void desenhar_tela(void)
{
    int e = escala(), ox = (LARG - maq.larg * e) / 2, oy = (AREA_A - maq.alt * e) / 2;
    int x0 = maq.larg, y0 = maq.alt, x1 = -1, y1 = -1;
    if (tudo) g_retangulo(j, 0, 0, LARG, AREA_A, 0x0A1510);
    for (int y = 0; y < maq.alt; y++)
        for (int x = 0; x < maq.larg; x++) {
            int k = y * maq.larg + x;
            if (!tudo && maq.tela[k] == tela_antes[k]) continue;
            tela_antes[k] = maq.tela[k];
            g_retangulo(j, ox + x * e, oy + y * e, e, e, cores[maq.tela[k] & 3]);
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    if (tudo) { tudo = false; jan_mostrar_area(j, 0, 0, LARG, AREA_A); }
    else if (x1 >= 0) jan_mostrar_area(j, ox + x0 * e, oy + y0 * e, (x1 - x0 + 1) * e, (y1 - y0 + 1) * e);
}

static void desenhar_estado(const char *extra)
{
    g_retangulo(j, 0, AREA_A, LARG, ESTADO, 0x1F6B2A);
    g_retangulo(j, 0, AREA_A, LARG, 2, 0xFFD52E);
    char t[160];
    if (extra) copia_texto(t, extra, sizeof t);
    else formatar_texto(t, sizeof t, "%s  (%s)%s   Esc: lista  P: pausa  F5: recomeça", nome_rom, c8_nome_modo(modo),
                        pausado ? "  PAUSADO" : "");
    g_texto(j, 8, AREA_A + 5, t, 0xFFF3B0);
    jan_mostrar_area(j, 0, AREA_A, LARG, ESTADO);
}

/* ------------------------------------------------------------ ROMs */

static bool termina_com(const char *s, const char *fim)
{
    int a = (int)tam_texto(s), b = (int)tam_texto(fim);
    if (a < b) return false;
    for (int k = 0; k < b; k++) {
        char c = s[a - b + k];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != fim[k]) return false;
    }
    return true;
}

static void listar_pasta(const char *pasta, const char *marca)
{
    entrada_pasta_t e;
    for (int k = 0; listar(pasta, k, &e) > 0 && lista.qtd < MAX_ROMS; k++) {
        if (!termina_com(e.nome, ".ch8") && !termina_com(e.nome, ".sc8") && !termina_com(e.nome, ".xo8")) continue;
        formatar_texto(nomes[lista.qtd], sizeof nomes[0], "%s%s", e.nome, marca);
        formatar_texto(caminhos[lista.qtd], sizeof caminhos[0], "%s/%s", pasta, e.nome);
        lista.qtd++;
    }
}

static void desenhar_lista(void)
{
    g_retangulo(j, 0, 0, LARG, ALT, COR_FUNDO);
    g_retangulo(j, 0, 0, LARG, 56, 0x10201A);
    g_texto(j, 16, 10, "CHIP-8 do SaguiOS", 0x3CDC6E);
    g_texto(j, 16, 30, "Escolha um programa (Enter ou duplo clique).", 0xFFD52E);
    if (lista.qtd) lista_desenhar(j, &lista);
    else g_texto(j, 16, 80, "Nenhum programa em /jogos/chip8 nem no pendrive.", COR_TEXTO);
    g_texto(j, 16, ALT - 46, "Teclas: 1234 / QWER / ASDF / ZXCV (as setas valem W A S D).", COR_TEXTO_FRACO);
    g_texto(j, 16, ALT - 26, "Os exemplos têm o código em /jogos/chip8/*.asm (chip8asm).", COR_TEXTO_FRACO);
    jan_mostrar(j);
}

static void montar_lista(void)
{
    lista.x = 16; lista.y = 68; lista.l = LARG - 32; lista.a = ALT - 68 - 56;
    lista.itens = nomes;
    lista.qtd = 0;
    lista.topo = 0;
    listar_pasta("/jogos/chip8", "");
    listar_pasta("/usb", "  (pendrive)");
    listar_pasta("/usb/chip8", "  (pendrive)");
    lista.sel = lista.qtd ? 0 : -1;
}

static void mostrar_lista(void)
{
    rodando = false;
    silenciar();
    freq_tocando = 0;
    titulo_janela(j->id, "CHIP-8");
    montar_lista();
    desenhar_lista();
    for (int k = 0; k < lista.qtd; k++) registrar("chip8: item %d %s", k, caminhos[k]);
    registrar("chip8: lista pronta (%d roms)", lista.qtd);
}

static void recomecar(void)
{
    uint32_t semente = 0;
    bytes_aleatorios(&semente, sizeof semente);
    c8_iniciar(&maq, modo, semente | 1);
    c8_carregar(&maq, rom, tam_rom);
    t0 = tempo_ms();
    feitos = 0;
    tudo = true;
    pausado = false;
    desenhar_tela();
    desenhar_estado(NULL);
}

static bool abrir_rom(const char *caminho)
{
    int fd = abrir(caminho, ABRIR_LER);
    if (fd < 0) { mensagem("CHIP-8", "Não consegui abrir o programa."); return false; }
    if (!rom) rom = aloca(65536);
    tam_rom = 0;
    int r;
    while (tam_rom < 65536 && (r = ler(fd, rom + tam_rom, (unsigned)(65536 - tam_rom))) > 0) tam_rom += r;
    fechar(fd);
    modo = modo_forcado >= 0 ? modo_forcado : c8_modo_por_extensao(caminho);
    if (tam_rom > c8_rom_max(modo)) { mensagem("CHIP-8", "O programa não cabe na memória da máquina."); return false; }
    const char *b = acha_ultimo_char(caminho, '/');
    copia_texto(nome_rom, b ? b + 1 : caminho, sizeof nome_rom);
    char titulo[96];
    formatar_texto(titulo, sizeof titulo, "CHIP-8 - %s", nome_rom);
    titulo_janela(j->id, titulo);
    registrar("chip8: rom %s carregada (%d bytes, %s)", caminho, tam_rom, c8_nome_modo(modo));
    rodando = true;
    recomecar();
    return true;
}

/* ------------------------------------------------------------ máquina */

static uint16_t ler_teclas(void)
{
    unsigned char mapa[32];
    if (teclas_seguradas(mapa) != 1) return 0;      /* sem foco: nada apertado */
    uint16_t t = 0;
    for (int k = 0; k < 16; k++) if (SEGURADA(mapa, cv_tecla[k])) t |= (uint16_t)(1 << k);
    if (SEGURADA(mapa, CV_CIMA)) t |= 1 << 5;
    if (SEGURADA(mapa, CV_ESQUERDA)) t |= 1 << 7;
    if (SEGURADA(mapa, CV_BAIXO)) t |= 1 << 8;
    if (SEGURADA(mapa, CV_DIREITA)) t |= 1 << 9;
    return t;
}

static void som(void)
{
    uint32_t f = c8_freq_som(&maq);
    if (f == freq_tocando) return;
    if (f) bip((int)f, maq.st * 1000 / 60 + 20);   /* o kernel toca sozinho pelo tempo pedido */
    else silenciar();
    freq_tocando = f;
}

static void emular(void)
{
    unsigned agora = tempo_ms();
    unsigned devidos = (agora - t0) * 60 / 1000;
    if (devidos - feitos > 6) feitos = devidos - 6;
    static uint16_t teclas_antes;
    while (feitos < devidos && maq.estado == C8_RODANDO) {
        uint16_t t = ler_teclas();
        if (t != teclas_antes) { registrar("chip8: teclas %04x", t); teclas_antes = t; }
        maq.teclas = t;
        c8_executar(&maq, c8_instrucoes_por_quadro(modo));
        c8_quadro(&maq);
        feitos++;
        if (feitos % 120 == 0)
            registrar("chip8: quadro %u soma %08x instrucoes %u", feitos, c8_soma_tela(&maq), maq.instrucoes);
    }
    if (maq.tela_mudou) { maq.tela_mudou = false; desenhar_tela(); }
    som();
    if (maq.estado != C8_RODANDO) {
        char t[120];
        if (maq.estado == C8_ERRO) formatar_texto(t, sizeof t, "Erro: %s   (Esc: lista, F5: recomeça)", maq.erro);
        else copia_texto(t, "O programa terminou.   (Esc: lista, F5: recomeça)", sizeof t);
        registrar("chip8: parou: %s", maq.estado == C8_ERRO ? maq.erro : "terminou");
        desenhar_estado(t);
        rodando = false;
    }
}

/* ------------------------------------------------------------ principal */

int principal(int argc, char **argv)
{
    const char *arquivo = NULL;
    for (int k = 1; k < argc; k++) {
        if (compara_texto(argv[k], "-m") == 0 && k + 1 < argc) {
            k++;
            modo_forcado = compara_texto(argv[k], "schip") == 0 ? C8_SCHIP : compara_texto(argv[k], "xochip") == 0 ? C8_XOCHIP : C8_CHIP8;
        } else arquivo = argv[k];
    }
    j = jan_abrir("CHIP-8", LARG, ALT);
    if (!j) return 1;
    c8_iniciar(&maq, C8_CHIP8, 1);
    if (!arquivo || !abrir_rom(arquivo)) mostrar_lista();
    evento_t e;
    for (;;) {
        bool ativo = rodando && !pausado;
        if (proximo_evento(j->id, &e, !ativo) != 1) {
            if (ativo) { emular(); dormir(5); }
            continue;
        }
        if (e.tipo == EV_FECHAR) break;
        /* nome_rom vazio = a lista está na janela. */
        if (nome_rom[0] && e.tipo == EV_TECLA) {
            if (e.tecla == TECLA_ESC) { nome_rom[0] = 0; mostrar_lista(); continue; }
            if (e.tecla == TECLA_F5) { rodando = true; recomecar(); continue; }
            if ((e.tecla == 'p' || e.tecla == 'P') && rodando) {
                pausado = !pausado;
                if (!pausado) { t0 = tempo_ms(); feitos = 0; }
                silenciar();
                freq_tocando = 0;
                desenhar_estado(NULL);
                registrar("chip8: %s", pausado ? "pausa" : "continua");
            }
            continue;
        }
        if (!nome_rom[0]) {
            int r = lista_tratar(j, &lista, &e);
            if (r == LISTA_ATIVOU && lista.sel >= 0) abrir_rom(caminhos[lista.sel]);
            else if (r == LISTA_SELECIONOU) jan_mostrar(j);
        }
    }
    silenciar();
    jan_fechar(j);
    return 0;
}
