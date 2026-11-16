/* montador.c - o montador de CHIP-8 / SUPER-CHIP (H92).
 *
 * Inspirado no montador do fork SaguiOS-Extended de Alejandro Anuncibay (a
 * decisão D071 de lá), escrito de novo, menor, sobre o main atual. Os
 * mnemônicos são os da referência técnica do CHIP-8 de Thomas P. Greene
 * ("Cowgod's Chip-8 Technical Reference"), com as instruções do SUPER-CHIP.
 *
 * Duas passadas sobre o texto: a primeira só conta bytes e anota o endereço
 * de cada etiqueta; a segunda calcula as expressões e grava. O tamanho de
 * uma linha nunca depende de uma expressão (toda instrução tem 2 bytes; db e
 * dw contam os itens), então a primeira passada sempre sabe os endereços.
 *
 * A sintaxe, uma instrução por linha:
 *     ; comentário até o fim da linha
 *     inicio:              etiqueta (pode vir antes de uma instrução)
 *     VELOCIDADE = 3       constante (também "VELOCIDADE equ 3")
 *     ld v0, 10            instruções: cls ret jp call se sne ld add or and
 *                          xor sub shr subn shl rnd drw skp sknp, e do
 *                          SUPER-CHIP: scd scr scl exit low high
 *     db 1, 0x2A, 0b101, %..##..#   bytes (no %, "." é 0 e "#" é 1: bom para desenhos)
 *     dw 0x1234            palavras de 16 bits
 *     org 0x300            continua a partir deste endereço
 * Números: 255, 0xFF, 0b1010, %#.#. e 'A'. Expressões: números e nomes
 * ligados por + e -. Registros: v0 a vf, i, [i], dt, st, k, f, hf, b, r. */
#include "montador.h"

#define MAX_SIMBOLOS 256
#define MAX_NOME     24
#define MAX_LINHA    200
#define INICIO       0x200

typedef struct { char nome[MAX_NOME]; int valor; } simbolo_t;

static struct {
    int        passada;
    int        pc;
    int        linha;
    uint8_t   *saida;
    int        max, fim;
    simbolo_t  sim[MAX_SIMBOLOS];
    int        qtd;
    char      *erro;
    int        max_erro;
    bool       falhou;
} M;

static void falhar(const char *msg, const char *detalhe)
{
    if (M.falhou) return;
    M.falhou = true;
    formatar_texto(M.erro, (size_t)M.max_erro, "linha %d: %s%s", M.linha, msg, detalhe ? detalhe : "");
}

/* ------------------------------------------------------------ texto */

static int minuscula(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static bool espaco(int c) { return c == ' ' || c == '\t' || c == '\r'; }
static bool letra(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.'; }
static bool digito(int c) { return c >= '0' && c <= '9'; }

static bool igual(const char *a, const char *b)
{
    while (*a && *b) if (minuscula(*a++) != minuscula(*b++)) return false;
    return *a == *b;
}

static char *aparar(char *s)
{
    while (espaco(*s)) s++;
    int n = 0;
    while (s[n]) n++;
    while (n > 0 && espaco(s[n - 1])) s[--n] = 0;
    return s;
}

/* ------------------------------------------------------------ símbolos */

static simbolo_t *achar(const char *nome)
{
    for (int k = 0; k < M.qtd; k++) if (igual(M.sim[k].nome, nome)) return &M.sim[k];
    return NULL;
}

static void definir(const char *nome, int valor)
{
    simbolo_t *s = achar(nome);
    if (s) {
        if (M.passada == 1) falhar("nome repetido: ", nome);
        s->valor = valor;
        return;
    }
    if (M.qtd >= MAX_SIMBOLOS) { falhar("nomes demais", NULL); return; }
    int k = 0;
    while (nome[k] && k < MAX_NOME - 1) { M.sim[M.qtd].nome[k] = nome[k]; k++; }
    M.sim[M.qtd].nome[k] = 0;
    M.sim[M.qtd++].valor = valor;
}

/* ------------------------------------------------------------ expressões */

/* Um termo: número, caractere, desenho com % ou nome. Na primeira passada
 * um nome ainda desconhecido vale 0 (o tamanho não depende dele). */
static bool termo(const char **pp, int *v)
{
    const char *p = *pp;
    while (espaco(*p)) p++;
    int r = 0;
    if (*p == '\'' && p[1] && p[2] == '\'') { r = (uint8_t)p[1]; p += 3; }
    else if (*p == '%') {
        p++;
        if (*p != '.' && *p != '#' && *p != '0' && *p != '1') return false;
        while (*p == '.' || *p == '#' || *p == '0' || *p == '1') r = r * 2 + (*p == '#' || *p == '1'), p++;
    } else if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        if (!digito(*p) && !(minuscula(*p) >= 'a' && minuscula(*p) <= 'f')) return false;
        for (;; p++) {
            int c = minuscula(*p);
            if (digito(c)) r = r * 16 + c - '0';
            else if (c >= 'a' && c <= 'f') r = r * 16 + c - 'a' + 10;
            else break;
        }
    } else if (*p == '0' && (p[1] == 'b' || p[1] == 'B')) {
        p += 2;
        if (*p != '0' && *p != '1') return false;
        while (*p == '0' || *p == '1') r = r * 2 + *p++ - '0';
    } else if (digito(*p)) {
        while (digito(*p)) r = r * 10 + *p++ - '0';
    } else if (letra(*p)) {
        char nome[MAX_NOME];
        int k = 0;
        while ((letra(*p) || digito(*p)) && k < MAX_NOME - 1) nome[k++] = *p++;
        nome[k] = 0;
        simbolo_t *s = achar(nome);
        if (s) r = s->valor;
        else if (M.passada == 2) { falhar("nome não definido: ", nome); return false; }
    } else return false;
    *pp = p;
    *v = r;
    return true;
}

static bool expressao(const char *texto, int *v)
{
    const char *p = texto;
    int total = 0, sinal = 1;
    for (;;) {
        while (espaco(*p)) p++;
        if (*p == '-') { sinal = -sinal; p++; continue; }
        int t;
        if (!termo(&p, &t)) { if (!M.falhou) falhar("expressão inválida: ", texto); return false; }
        total += sinal * t;
        while (espaco(*p)) p++;
        if (*p == '+') { sinal = 1; p++; }
        else if (*p == '-') { sinal = -1; p++; }
        else if (*p == 0) break;
        else { falhar("expressão inválida: ", texto); return false; }
    }
    *v = total;
    return true;
}

/* ------------------------------------------------------------ operandos */

enum { OP_REG, OP_NUM, OP_I, OP_IIND, OP_DT, OP_ST, OP_K, OP_F, OP_HF, OP_B, OP_R };

typedef struct { int tipo, valor; } operando_t;

static bool operando(const char *t, operando_t *o)
{
    static const struct { const char *nome; int tipo; } nomes[] = {
        { "i", OP_I }, { "[i]", OP_IIND }, { "dt", OP_DT }, { "st", OP_ST }, { "k", OP_K },
        { "f", OP_F }, { "hf", OP_HF }, { "b", OP_B }, { "r", OP_R },
    };
    for (unsigned k = 0; k < sizeof nomes / sizeof nomes[0]; k++)
        if (igual(t, nomes[k].nome)) { o->tipo = nomes[k].tipo; o->valor = 0; return true; }
    if (minuscula(t[0]) == 'v' && t[1] && !t[2]) {
        int c = minuscula(t[1]);
        if (digito(c) || (c >= 'a' && c <= 'f')) { o->tipo = OP_REG; o->valor = digito(c) ? c - '0' : c - 'a' + 10; return true; }
    }
    o->tipo = OP_NUM;
    return expressao(t, &o->valor);
}

/* ------------------------------------------------------------ gravação */

static void byte(int b)
{
    if (M.passada == 2) {
        int pos = M.pc - INICIO;
        if (pos < 0 || pos >= M.max) { falhar("o programa não cabe na memória", NULL); return; }
        M.saida[pos] = (uint8_t)b;
        if (pos + 1 > M.fim) M.fim = pos + 1;
    }
    M.pc++;
}

static void palavra(int w) { byte((w >> 8) & 0xFF); byte(w & 0xFF); }

static bool confere(int v, int max, const char *oque)
{
    if (M.passada == 2 && (v < 0 || v > max)) { falhar("valor fora do limite para ", oque); return false; }
    return true;
}

/* Uma instrução: "nome" e até 3 operandos já separados. */
static void instrucao(const char *nome, operando_t *o, int n)
{
#define REG(k) (n > (k) && o[k].tipo == OP_REG)
#define NUM(k) (n > (k) && o[k].tipo == OP_NUM)
    int x = n > 0 ? o[0].valor : 0, y = n > 1 ? o[1].valor : 0;
    if (igual(nome, "cls") && n == 0) { palavra(0x00E0); return; }
    if (igual(nome, "ret") && n == 0) { palavra(0x00EE); return; }
    if (igual(nome, "scr") && n == 0) { palavra(0x00FB); return; }
    if (igual(nome, "scl") && n == 0) { palavra(0x00FC); return; }
    if (igual(nome, "exit") && n == 0) { palavra(0x00FD); return; }
    if (igual(nome, "low") && n == 0) { palavra(0x00FE); return; }
    if (igual(nome, "high") && n == 0) { palavra(0x00FF); return; }
    if (igual(nome, "scd") && n == 1 && NUM(0)) { confere(x, 15, "scd"); palavra(0x00C0 | (x & 15)); return; }
    if (igual(nome, "sys") && n == 1 && NUM(0)) { confere(x, 0xFFF, "sys"); palavra(x & 0xFFF); return; }
    if (igual(nome, "jp")) {
        if (n == 1 && NUM(0)) { confere(x, 0xFFF, "jp"); palavra(0x1000 | (x & 0xFFF)); return; }
        if (n == 2 && REG(0) && x == 0 && NUM(1)) { confere(y, 0xFFF, "jp"); palavra(0xB000 | (y & 0xFFF)); return; }
    }
    if (igual(nome, "call") && n == 1 && NUM(0)) { confere(x, 0xFFF, "call"); palavra(0x2000 | (x & 0xFFF)); return; }
    if ((igual(nome, "se") || igual(nome, "sne")) && n == 2 && REG(0)) {
        bool se = igual(nome, "se");
        if (NUM(1)) { confere(y, 255, nome); palavra((se ? 0x3000 : 0x4000) | x << 8 | (y & 0xFF)); return; }
        if (REG(1)) { palavra((se ? 0x5000 : 0x9000) | x << 8 | y << 4); return; }
    }
    if (igual(nome, "ld") && n == 2) {
        int a = o[0].tipo, b = o[1].tipo;
        if (a == OP_REG && b == OP_NUM) { confere(y, 255, "ld"); palavra(0x6000 | x << 8 | (y & 0xFF)); return; }
        if (a == OP_REG && b == OP_REG) { palavra(0x8000 | x << 8 | y << 4); return; }
        if (a == OP_I && b == OP_NUM) { confere(y, 0xFFF, "ld i"); palavra(0xA000 | (y & 0xFFF)); return; }
        if (a == OP_REG && b == OP_DT) { palavra(0xF007 | x << 8); return; }
        if (a == OP_REG && b == OP_K) { palavra(0xF00A | x << 8); return; }
        if (a == OP_DT && b == OP_REG) { palavra(0xF015 | y << 8); return; }
        if (a == OP_ST && b == OP_REG) { palavra(0xF018 | y << 8); return; }
        if (a == OP_F && b == OP_REG) { palavra(0xF029 | y << 8); return; }
        if (a == OP_HF && b == OP_REG) { palavra(0xF030 | y << 8); return; }
        if (a == OP_B && b == OP_REG) { palavra(0xF033 | y << 8); return; }
        if (a == OP_IIND && b == OP_REG) { palavra(0xF055 | y << 8); return; }
        if (a == OP_REG && b == OP_IIND) { palavra(0xF065 | x << 8); return; }
        if (a == OP_R && b == OP_REG) { palavra(0xF075 | y << 8); return; }
        if (a == OP_REG && b == OP_R) { palavra(0xF085 | x << 8); return; }
    }
    if (igual(nome, "add") && n == 2) {
        if (REG(0) && NUM(1)) { confere(y, 255, "add"); palavra(0x7000 | x << 8 | (y & 0xFF)); return; }
        if (REG(0) && REG(1)) { palavra(0x8004 | x << 8 | y << 4); return; }
        if (o[0].tipo == OP_I && REG(1)) { palavra(0xF01E | y << 8); return; }
    }
    static const struct { const char *nome; int cod; } logicas[] = {
        { "or", 1 }, { "and", 2 }, { "xor", 3 }, { "sub", 5 }, { "shr", 6 }, { "subn", 7 }, { "shl", 0xE },
    };
    for (unsigned k = 0; k < sizeof logicas / sizeof logicas[0]; k++) {
        if (!igual(nome, logicas[k].nome)) continue;
        if (n == 2 && REG(0) && REG(1)) { palavra(0x8000 | x << 8 | y << 4 | logicas[k].cod); return; }
        if (n == 1 && REG(0) && (logicas[k].cod == 6 || logicas[k].cod == 0xE)) { palavra(0x8000 | x << 8 | x << 4 | logicas[k].cod); return; }
    }
    if (igual(nome, "rnd") && n == 2 && REG(0) && NUM(1)) { confere(y, 255, "rnd"); palavra(0xC000 | x << 8 | (y & 0xFF)); return; }
    if (igual(nome, "drw") && n == 3 && REG(0) && REG(1) && NUM(2)) {
        confere(o[2].valor, 15, "drw");
        palavra(0xD000 | x << 8 | y << 4 | (o[2].valor & 15));
        return;
    }
    if (igual(nome, "skp") && n == 1 && REG(0)) { palavra(0xE09E | x << 8); return; }
    if (igual(nome, "sknp") && n == 1 && REG(0)) { palavra(0xE0A1 | x << 8); return; }
    palavra(0);     /* conta os 2 bytes mesmo com erro, para os endereços não andarem */
    falhar("instrução desconhecida ou operandos errados: ", nome);
#undef REG
#undef NUM
}

/* Separa os operandos pelas vírgulas (no máximo "max"). */
static int separar(char *s, char **partes, int max)
{
    int n = 0;
    s = aparar(s);
    if (!*s) return 0;
    while (n < max) {
        char *v = s;
        while (*v && *v != ',') v++;
        bool mais = *v == ',';
        *v = 0;
        partes[n++] = aparar(s);
        if (!mais) break;
        s = v + 1;
    }
    return n;
}

static void linha(char *s)
{
    /* Tira o comentário (fora de aspas simples). */
    for (char *p = s; *p; p++) {
        if (*p == '\'' && p[1] && p[2] == '\'') { p += 2; continue; }
        if (*p == ';') { *p = 0; break; }
    }
    s = aparar(s);
    /* Etiqueta: "nome:" no começo. */
    char *p = s;
    while (letra(*p) || digito(*p)) p++;
    if (p > s && *p == ':' && letra(*s)) {
        *p = 0;
        if (M.passada == 1) definir(s, M.pc);
        s = aparar(p + 1);
    }
    if (!*s) return;
    /* O nome da instrução ou diretiva. */
    char nome[MAX_NOME];
    int k = 0;
    p = s;
    while ((letra(*p) || digito(*p)) && k < MAX_NOME - 1) nome[k++] = *p++;
    nome[k] = 0;
    char *resto = aparar(p);
    /* Constante: "nome = expr" ou "nome equ expr". */
    if (*resto == '=' || (minuscula(resto[0]) == 'e' && minuscula(resto[1]) == 'q' && minuscula(resto[2]) == 'u' && espaco(resto[3]))) {
        int v;
        if (expressao(*resto == '=' ? resto + 1 : resto + 3, &v)) definir(nome, v);
        return;
    }
    char *partes[40];
    if (igual(nome, "db") || igual(nome, "dw")) {
        int n = separar(resto, partes, 40);
        if (!n) { falhar("faltam os valores de ", nome); return; }
        for (int i = 0; i < n; i++) {
            int v = 0;
            if (M.passada == 2 && !expressao(partes[i], &v)) return;
            if (igual(nome, "db")) { confere(v, 255, "db"); byte(v); }
            else { confere(v, 0xFFFF, "dw"); palavra(v); }
        }
        return;
    }
    if (igual(nome, "org")) {
        int v;
        if (!expressao(resto, &v)) return;
        if (v < M.pc || v >= INICIO + M.max) { falhar("org para trás ou fora da memória", NULL); return; }
        while (M.pc < v) byte(0);
        return;
    }
    operando_t o[3];
    int n = separar(resto, partes, 4);
    if (n > 3) { falhar("operandos demais", NULL); return; }
    for (int i = 0; i < n; i++)
        if (!operando(partes[i], &o[i])) return;
    instrucao(nome, o, n);
}

int c8_montar(const char *fonte, int n, uint8_t *saida, int max, char *erro, int max_erro)
{
    M.qtd = 0;
    M.saida = saida;
    M.max = max;
    M.erro = erro;
    M.max_erro = max_erro;
    M.falhou = false;
    erro[0] = 0;
    for (M.passada = 1; M.passada <= 2; M.passada++) {
        M.pc = INICIO;
        M.fim = 0;
        M.linha = 0;
        int ini = 0;
        while (ini < n && !M.falhou) {
            int fim = ini;
            while (fim < n && fonte[fim] != '\n') fim++;
            char l[MAX_LINHA];
            int t = fim - ini < MAX_LINHA - 1 ? fim - ini : MAX_LINHA - 1;
            memcpy(l, fonte + ini, (size_t)t);
            l[t] = 0;
            M.linha++;
            linha(l);
            ini = fim + 1;
        }
        if (M.falhou) return -1;
    }
    return M.fim;
}
