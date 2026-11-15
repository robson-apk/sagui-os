/* teclado.c - driver do teclado PS/2 (controlador i8042, IRQ 1), layout ABNT2.
 *
 * O controlador entrega um byte por interrupção na porta 0x60. Com a tradução
 * ligada pela BIOS (padrão), os bytes seguem o "scan code set 1": cada tecla
 * tem um código ao ser apertada e o mesmo código + 0x80 ao ser solta; algumas
 * teclas (setas, Ctrl e Alt da direita...) vêm precedidas de 0xE0.
 *
 * O trabalho do driver é transformar essa sequência em teclas com
 * significado: acompanhar Shift, Ctrl, Alt, AltGr e Caps Lock, aplicar o
 * mapa ABNT2 (PRD 4.4) e combinar as teclas mortas (´ ` ~ ^ ¨) com a letra
 * seguinte. O resultado vai para um buffer circular que o terminal lê. */
#include "teclado.h"
#include "console.h"
#include "cpu.h"
#include "portas.h"
#include "processo.h"
#include "texto.h"

#define PORTA_DADOS  0x60
#define PORTA_ESTADO 0x64
#define TAM_BUFFER   128

/* Mapa ABNT2: [scan code] = { normal, com Shift }. Teclas mortas são marcadas
 * com o próprio acento somado a MORTA; 0 = tecla sem caractere. */
#define MORTA 0x8000
static const uint16_t mapa[0x80][2] = {
    [0x02] = {'1', '!'}, [0x03] = {'2', '@'}, [0x04] = {'3', '#'}, [0x05] = {'4', '$'},
    [0x06] = {'5', '%'}, [0x07] = {'6', MORTA | 0xA8 /* ¨ */}, [0x08] = {'7', '&'},
    [0x09] = {'8', '*'}, [0x0A] = {'9', '('}, [0x0B] = {'0', ')'}, [0x0C] = {'-', '_'},
    [0x0D] = {'=', '+'}, [0x0E] = {'\b', '\b'}, [0x0F] = {'\t', '\t'},
    [0x10] = {'q', 'Q'}, [0x11] = {'w', 'W'}, [0x12] = {'e', 'E'}, [0x13] = {'r', 'R'},
    [0x14] = {'t', 'T'}, [0x15] = {'y', 'Y'}, [0x16] = {'u', 'U'}, [0x17] = {'i', 'I'},
    [0x18] = {'o', 'O'}, [0x19] = {'p', 'P'},
    [0x1A] = {MORTA | 0xB4 /* ´ */, MORTA | '`'}, [0x1B] = {'[', '{'}, [0x1C] = {'\n', '\n'},
    [0x1E] = {'a', 'A'}, [0x1F] = {'s', 'S'}, [0x20] = {'d', 'D'}, [0x21] = {'f', 'F'},
    [0x22] = {'g', 'G'}, [0x23] = {'h', 'H'}, [0x24] = {'j', 'J'}, [0x25] = {'k', 'K'},
    [0x26] = {'l', 'L'}, [0x27] = {0xE7 /* ç */, 0xC7 /* Ç */},
    [0x28] = {MORTA | '~', MORTA | '^'}, [0x29] = {'\'', '"'}, [0x2B] = {']', '}'},
    [0x2C] = {'z', 'Z'}, [0x2D] = {'x', 'X'}, [0x2E] = {'c', 'C'}, [0x2F] = {'v', 'V'},
    [0x30] = {'b', 'B'}, [0x31] = {'n', 'N'}, [0x32] = {'m', 'M'}, [0x33] = {',', '<'},
    [0x34] = {'.', '>'}, [0x35] = {';', ':'}, [0x37] = {'*', '*'}, [0x39] = {' ', ' '},
    [0x4A] = {'-', '-'}, [0x4E] = {'+', '+'},
    [0x56] = {'\\', '|'}, [0x73] = {'/', '?'}, [0x7E] = {'.', '.'},
};

/* AltGr (Alt da direita) no ABNT2: os símbolos gravados no canto das teclas. */
static uint16_t com_altgr(uint8_t sc)
{
    switch (sc) {
    case 0x10: return '/';   case 0x11: return '?';   case 0x12: return 0xB0; /* ° */
    case 0x02: return 0xB9;  case 0x03: return 0xB2;  case 0x04: return 0xB3; /* ¹ ² ³ */
    case 0x05: return 0xA3;  case 0x06: return 0xA2;  case 0x07: return 0xAC; /* £ ¢ ¬ */
    case 0x0D: return 0xA7;  case 0x1B: return 0xAA;  case 0x2B: return 0xBA; /* § ª º */
    case 0x73: return 0xB0;
    default: return 0;
    }
}

/* Teclado numérico com Num Lock desligado e teclas estendidas (0xE0). */
static int navegacao(uint8_t sc)
{
    switch (sc) {
    case 0x48: return TECLA_CIMA;     case 0x50: return TECLA_BAIXO;
    case 0x4B: return TECLA_ESQUERDA; case 0x4D: return TECLA_DIREITA;
    case 0x47: return TECLA_HOME;     case 0x4F: return TECLA_END;
    case 0x49: return TECLA_PGUP;     case 0x51: return TECLA_PGDN;
    case 0x52: return TECLA_INSERT;   case 0x53: return TECLA_DELETE;
    default: return 0;
    }
}

/* Combinação de tecla morta + letra. Cada linha: acento, letras base e
 * resultados na mesma ordem (Latin-1). */
static const struct { uint8_t acento; const char *base; const char *resultado; } combinacoes[] = {
    { 0xB4, "aeiouyAEIOUY", "\xE1\xE9\xED\xF3\xFA\xFD\xC1\xC9\xCD\xD3\xDA\xDD" },
    { '`',  "aeiouAEIOU",   "\xE0\xE8\xEC\xF2\xF9\xC0\xC8\xCC\xD2\xD9" },
    { '^',  "aeiouAEIOU",   "\xE2\xEA\xEE\xF4\xFB\xC2\xCA\xCE\xD4\xDB" },
    { '~',  "aonAON",       "\xE3\xF5\xF1\xC3\xD5\xD1" },
    { 0xA8, "aeiouyAEIOU",  "\xE4\xEB\xEF\xF6\xFC\xFF\xC4\xCB\xCF\xD6\xDC" },
};

static uint16_t buffer[TAM_BUFFER];
static volatile uint32_t ini, fim;
static uint32_t mods;
static bool estendida, num_lock;
static uint8_t morta;   /* acento esperando a próxima letra (0 = nenhum) */

static void guardar(int tecla)
{
    uint32_t prox = (fim + 1) % TAM_BUFFER;
    if (prox == ini)
        return;   /* buffer cheio: a tecla é perdida em vez de sobrescrever as antigas */
    buffer[fim] = (uint16_t)tecla;
    fim = prox;
    acordar(teclado_canal());   /* quem estava esperando uma tecla volta a ficar pronto */
}

/* Canal em que os processos esperam por teclas (ver bloquear/acordar). */
static const char canal = 0;
const void *teclado_canal(void) { return &canal; }

static void atualizar_leds(void)
{
    /* Comando 0xED + máscara (bit 2 = Caps Lock, bit 1 = Num Lock). A
     * resposta (0xFA) chega pela IRQ e é ignorada lá. */
    for (int i = 0; i < 10000 && (porta_le8(PORTA_ESTADO) & 2); i++) ;
    porta_escreve8(PORTA_DADOS, 0xED);
    for (int i = 0; i < 10000 && (porta_le8(PORTA_ESTADO) & 2); i++) ;
    porta_escreve8(PORTA_DADOS, (uint8_t)(((mods & MOD_CAPS) ? 4 : 0) | (num_lock ? 2 : 0)));
}

static void caractere(int c)
{
    if (morta) {
        uint8_t acento = morta;
        morta = 0;
        if (c == ' ') { guardar(acento); return; }   /* acento + espaço = o próprio acento */
        for (unsigned i = 0; i < sizeof combinacoes / sizeof combinacoes[0]; i++) {
            if (combinacoes[i].acento != acento) continue;
            for (int k = 0; combinacoes[i].base[k]; k++)
                if ((uint8_t)combinacoes[i].base[k] == c) {
                    guardar((uint8_t)combinacoes[i].resultado[k]);
                    return;
                }
        }
        guardar(acento);   /* combinação inválida: o acento e depois a letra */
    }
    guardar(c);
}

/* Teclas seguradas agora (B4): um bit por código de varredura, 0-127 para
 * as comuns e 128-255 para as estendidas (E0 xx). Os jogos precisam saber
 * que a seta continua apertada, e não só que foi apertada uma vez; o
 * caminho normal de caracteres não diz quando a tecla é solta. */
static volatile uint8_t seguradas[32];

void teclado_seguradas(uint8_t mapa[32])
{
    bool i = int_salva_desliga();
    memcpy(mapa, (const void *)seguradas, 32);
    int_restaura(i);
}

void teclado_byte(uint8_t b)
{
    if (b == 0xFA || b == 0xFE || b == 0x00 || b == 0xFF)
        return;   /* respostas do teclado (ACK, reenviar, erro) */
    if (b == 0xE0) { estendida = true; return; }
    if (b == 0xE1) return;   /* Pause: sequência longa sem uso aqui */

    bool solta = b & 0x80;
    uint8_t sc = b & 0x7F;
    bool ext = estendida;
    estendida = false;
    if (!(ext && sc == 0x2A)) {   /* o "shift falso" das setas não conta */
        uint8_t k = (uint8_t)(sc | (ext ? 0x80 : 0));
        if (solta) seguradas[k >> 3] &= (uint8_t)~(1u << (k & 7));
        else seguradas[k >> 3] |= (uint8_t)(1u << (k & 7));
    }

    /* Tecla Windows (H95): modificador; sozinha, vira TECLA_SUPER ao soltar. */
    static bool super_com_outra;
    if (ext && (sc == 0x5B || sc == 0x5C)) {
        if (solta) {
            mods &= ~MOD_SUPER;
            if (!super_com_outra) guardar(TECLA_SUPER);
        } else if (!(mods & MOD_SUPER)) {
            mods |= MOD_SUPER;
            super_com_outra = false;
        }
        return;
    }
    if (!solta && (mods & MOD_SUPER)) super_com_outra = true;

    /* Modificadores. */
    uint32_t m = 0;
    if (sc == 0x2A || sc == 0x36) m = MOD_SHIFT;
    else if (sc == 0x1D) m = MOD_CTRL;
    else if (sc == 0x38) m = ext ? MOD_ALTGR : MOD_ALT;
    if (m) {
        if (ext && sc == 0x2A) return;   /* "shift falso" que alguns teclados mandam junto das setas */
        if (solta) mods &= ~m; else mods |= m;
        return;
    }
    if (solta)
        return;

    if (sc == 0x3A) { mods ^= MOD_CAPS; atualizar_leds(); return; }
    if (sc == 0x45) { num_lock = !num_lock; atualizar_leds(); return; }
    if (sc == 0x01) { morta = 0; guardar(TECLA_ESC); return; }
    if (sc >= 0x3B && sc <= 0x44) { guardar(TECLA_F1 + (sc - 0x3B)); return; }
    if (sc == 0x57 || sc == 0x58) { guardar(TECLA_F11 + (sc - 0x57)); return; }

    if (ext) {
        if (sc == 0x1C) { caractere('\n'); return; }   /* Enter do teclado numérico */
        if (sc == 0x35) { caractere('/'); return; }    /* / do teclado numérico */
        if (sc == 0x37) { guardar(TECLA_IMPRIMIR); return; }   /* Print Screen (E0 2A E0 37) */
        int n = navegacao(sc);
        if (n) guardar(n);
        return;
    }
    if (sc >= 0x47 && sc <= 0x53 && sc != 0x4A && sc != 0x4E) {
        if (!num_lock) { int n = navegacao(sc); if (n) guardar(n); return; }
        static const char numerico[] = "789-456+1230,";   /* vírgula: o "Del" do ABNT2 */
        caractere(numerico[sc - 0x47]);
        return;
    }

    uint16_t v;
    if (mods & MOD_ALTGR) {
        v = com_altgr(sc);
        if (v) caractere(v);
        return;
    }
    bool shift = mods & MOD_SHIFT;
    v = mapa[sc][shift ? 1 : 0];
    if (!v)
        return;
    /* Caps Lock inverte só as letras (e o ç). */
    uint8_t base = (uint8_t)mapa[sc][0];
    bool letra = (base >= 'a' && base <= 'z') || base == 0xE7;
    if (letra && (mods & MOD_CAPS))
        v = mapa[sc][shift ? 0 : 1];
    if (v & MORTA) {
        if (morta) { uint8_t a = morta; morta = 0; guardar(a); }   /* duas mortas seguidas */
        morta = (uint8_t)(v & 0xFF);
        return;
    }
    if ((mods & MOD_CTRL) && base >= 'a' && base <= 'z') {
        morta = 0;
        guardar(base & 0x1F);   /* Ctrl+letra: código de controle */
        return;
    }
    caractere(v);
}

void teclado_iniciar(void)
{
    /* A leitura do controlador (portas 0x60/0x64) é feita pelo i8042.c, que
     * entrega cada byte do teclado a teclado_byte(). Ver D020. */
    ini = fim = 0;
    mods = 0;
    num_lock = false;
    atualizar_leds();
}

int teclado_tentar(void)
{
    bool i = int_salva_desliga();
    int t = -1;
    if (ini != fim) {
        t = buffer[ini];
        ini = (ini + 1) % TAM_BUFFER;
    }
    int_restaura(i);
    return t;
}

int teclado_ler(void)
{
    for (;;) {
        int t = teclado_tentar();
        if (t >= 0) return t;
        cpu_espera();   /* dorme até a próxima interrupção (tecla ou relógio) */
    }
}

uint32_t teclado_modificadores(void) { return mods; }
