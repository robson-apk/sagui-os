/* regras.c - as regras do Caça-Bananas (H92).
 *
 * Trazido do buscaminas do fork SaguiOS-Extended de Alejandro Anuncibay e
 * reescrito sobre o main atual. O jogo de achar minas num campo é de domínio
 * público; nomes, regras escritas e desenhos daqui são nossos.
 *
 * - As bombas só são sorteadas no primeiro clique, longe dele: a casa
 *   clicada e, se couber, as 8 vizinhas ficam livres, então o primeiro clique
 *   sempre abre uma área (e nunca perde).
 * - Abrir uma casa sem bomba vizinha abre as vizinhas também. Em vez de uma
 *   função que chama a si mesma (que num campo grande gastaria muita pilha),
 *   usa uma fila: cada casa vazia aberta põe as vizinhas na fila. */
#include "regras.h"

static uint32_t sortear(campo_t *c)
{
    /* xorshift de 32 bits: três deslocamentos e "ou exclusivos" que passeiam
     * por todos os valores menos o zero. Basta para embaralhar um campo. */
    uint32_t s = c->sorteio;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    c->sorteio = s;
    return s;
}

void campo_novo(campo_t *c, int cols, int linhas, int bombas, uint32_t semente)
{
    if (cols < 5) cols = 5;
    if (cols > CAMPO_MAX_COLS) cols = CAMPO_MAX_COLS;
    if (linhas < 5) linhas = 5;
    if (linhas > CAMPO_MAX_LINHAS) linhas = CAMPO_MAX_LINHAS;
    if (bombas < 1) bombas = 1;
    if (bombas > cols * linhas - 9) bombas = cols * linhas - 9;
    c->cols = cols; c->linhas = linhas; c->bombas = bombas;
    memset(c->casa, 0, sizeof c->casa);
    c->estado = CAMPO_ESPERA;
    c->abertas = c->bandeiras = 0;
    c->sorteio = semente ? semente : 0x5A6E1u;
}

static bool dentro(const campo_t *c, int x, int y) { return x >= 0 && y >= 0 && x < c->cols && y < c->linhas; }

int campo_vizinhas(const campo_t *c, int x, int y, uint8_t marca)
{
    int n = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if ((dx || dy) && dentro(c, x + dx, y + dy) && (CAMPO_CASA(c, x + dx, y + dy) & marca)) n++;
    return n;
}

/* Sorteia as bombas entre as casas permitidas: tira uma candidata ao acaso,
 * põe a última no lugar dela e segue com uma a menos (sem repetir). */
static void sortear_bombas(campo_t *c, int px, int py)
{
    static int candidata[CAMPO_MAX];
    int n = 0;
    bool folga = c->cols * c->linhas - 9 >= c->bombas;
    for (int y = 0; y < c->linhas; y++)
        for (int x = 0; x < c->cols; x++) {
            int dx = x - px, dy = y - py;
            bool perto = folga ? (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) : (dx == 0 && dy == 0);
            if (!perto) candidata[n++] = y * c->cols + x;
        }
    for (int k = 0; k < c->bombas && n > 0; k++) {
        int r = (int)(sortear(c) % (uint32_t)n);
        c->casa[candidata[r]] |= C_BOMBA;
        candidata[r] = candidata[--n];
    }
}

static void perder(campo_t *c, int x, int y)
{
    c->estado = CAMPO_PERDEU;
    for (int i = 0; i < c->cols * c->linhas; i++) {
        if ((c->casa[i] & C_BOMBA) && !(c->casa[i] & C_BANDEIRA)) c->casa[i] |= C_ABERTA;
        if (!(c->casa[i] & C_BOMBA) && (c->casa[i] & C_BANDEIRA)) c->casa[i] |= C_ERRADA;
    }
    CAMPO_CASA(c, x, y) |= C_ESTOUROU;
}

static void ganhar(campo_t *c)
{
    c->estado = CAMPO_GANHOU;
    /* As bombas que faltavam ganham bandeira, para o campo mostrar tudo. */
    for (int i = 0; i < c->cols * c->linhas; i++)
        if (c->casa[i] & C_BOMBA) { c->casa[i] |= C_BANDEIRA; c->casa[i] &= (uint8_t)~C_DUVIDA; }
    c->bandeiras = c->bombas;
}

void campo_abrir(campo_t *c, int x, int y)
{
    static int fila[CAMPO_MAX];
    if (!dentro(c, x, y)) return;
    uint8_t *casa = &CAMPO_CASA(c, x, y);
    if (*casa & (C_ABERTA | C_BANDEIRA)) return;
    if (c->estado == CAMPO_ESPERA) { sortear_bombas(c, x, y); c->estado = CAMPO_JOGANDO; }
    if (c->estado != CAMPO_JOGANDO) return;
    *casa &= (uint8_t)~C_DUVIDA;
    if (*casa & C_BOMBA) { perder(c, x, y); return; }
    int ini = 0, fim = 0;
    *casa |= C_ABERTA;
    c->abertas++;
    fila[fim++] = y * c->cols + x;
    while (ini < fim) {
        int i = fila[ini++], cx = i % c->cols, cy = i / c->cols;
        if (campo_vizinhas(c, cx, cy, C_BOMBA)) continue;    /* tem número: para aqui */
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int a = cx + dx, b = cy + dy;
                if (!dentro(c, a, b)) continue;
                uint8_t *v = &CAMPO_CASA(c, a, b);
                if (*v & (C_ABERTA | C_BANDEIRA)) continue;
                *v = (uint8_t)((*v & ~C_DUVIDA) | C_ABERTA);
                c->abertas++;
                fila[fim++] = b * c->cols + a;
            }
    }
    if (c->abertas == c->cols * c->linhas - c->bombas) ganhar(c);
}

void campo_acorde(campo_t *c, int x, int y)
{
    if (c->estado != CAMPO_JOGANDO || !dentro(c, x, y)) return;
    int n = campo_vizinhas(c, x, y, C_BOMBA);
    if (!(CAMPO_CASA(c, x, y) & C_ABERTA) || n == 0 || campo_vizinhas(c, x, y, C_BANDEIRA) != n) return;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx || dy) campo_abrir(c, x + dx, y + dy);
            if (c->estado != CAMPO_JOGANDO) return;
        }
}

void campo_marcar(campo_t *c, int x, int y, bool com_duvida)
{
    if ((c->estado != CAMPO_JOGANDO && c->estado != CAMPO_ESPERA) || !dentro(c, x, y)) return;
    uint8_t *casa = &CAMPO_CASA(c, x, y);
    if (*casa & C_ABERTA) return;
    if (*casa & C_BANDEIRA) {
        *casa &= (uint8_t)~C_BANDEIRA;
        c->bandeiras--;
        if (com_duvida) *casa |= C_DUVIDA;
    } else if (*casa & C_DUVIDA) *casa &= (uint8_t)~C_DUVIDA;
    else { *casa |= C_BANDEIRA; c->bandeiras++; }
}
