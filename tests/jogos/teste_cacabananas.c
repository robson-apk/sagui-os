/* teste_cacabananas.c - testa as regras do Caça-Bananas (H92) no computador
 * de desenvolvimento: compilado com o gcc de lá e ligado ao mesmo
 * userland/jogos/cacabananas/regras.c que vai para o SaguiOS.
 * Escreve "ok: ..." para cada conferência e termina com 1 se alguma falhar. */
#include <stdio.h>
#include "regras.h"

static int falhas;

static void conferir(int cond, const char *msg)
{
    printf("%s: %s\n", cond ? "ok" : "FALHOU", msg);
    if (!cond) falhas++;
}

static int contar(const campo_t *c, uint8_t marca)
{
    int n = 0;
    for (int i = 0; i < c->cols * c->linhas; i++) if (c->casa[i] & marca) n++;
    return n;
}

int main(void)
{
    static campo_t c;
    /* 1. O primeiro clique nunca acha bomba e abre uma área (vizinhas livres). */
    int seguros = 1, vizinhas_livres = 1, total_certo = 1;
    for (uint32_t s = 1; s <= 300; s++) {
        int x = (int)(s * 7 % 30), y = (int)(s * 3 % 16);
        campo_novo(&c, 30, 16, 99, s);
        campo_abrir(&c, x, y);
        if (c.estado == CAMPO_PERDEU) seguros = 0;
        if (campo_vizinhas(&c, x, y, C_BOMBA) != 0 || (CAMPO_CASA(&c, x, y) & C_BOMBA)) vizinhas_livres = 0;
        if (contar(&c, C_BOMBA) != 99) total_certo = 0;
    }
    conferir(seguros, "o primeiro clique nunca perde (300 partidas no Difícil)");
    conferir(vizinhas_livres, "as 8 vizinhas do primeiro clique ficam sem bomba");
    conferir(total_certo, "o campo tem exatamente o número de bombas do nível");

    /* 2. A mesma semente dá o mesmo campo (os testes no QEMU dependem disso). */
    static campo_t d;
    campo_novo(&c, 9, 9, 10, 1234); campo_abrir(&c, 4, 4);
    campo_novo(&d, 9, 9, 10, 1234); campo_abrir(&d, 4, 4);
    int iguais = 1;
    for (int i = 0; i < 81; i++) if ((c.casa[i] & C_BOMBA) != (d.casa[i] & C_BOMBA)) iguais = 0;
    conferir(iguais, "a mesma semente sorteia o mesmo campo");

    /* 3. A área aberta pelo primeiro clique: toda casa aberta sem número tem
     *    as vizinhas abertas (a fila não deixou ninguém para trás). */
    int completo = 1;
    for (int y = 0; y < 9; y++)
        for (int x = 0; x < 9; x++) {
            if (!(CAMPO_CASA(&c, x, y) & C_ABERTA) || campo_vizinhas(&c, x, y, C_BOMBA)) continue;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int a = x + dx, b = y + dy;
                    if (a >= 0 && b >= 0 && a < 9 && b < 9 && !(CAMPO_CASA(&c, a, b) & C_ABERTA)) completo = 0;
                }
        }
    conferir(completo && c.abertas == contar(&c, C_ABERTA), "abrir uma casa vazia abre as vizinhas em cadeia");

    /* 4. Bandeira: nada -> bandeira -> "?" -> nada; casa com bandeira não abre. */
    int fx = -1, fy = -1;
    for (int i = 0; i < 81 && fx < 0; i++) if (!(c.casa[i] & C_ABERTA)) { fx = i % 9; fy = i / 9; }
    campo_marcar(&c, fx, fy, 1);
    int b1 = (CAMPO_CASA(&c, fx, fy) & C_BANDEIRA) && c.bandeiras == 1;
    campo_abrir(&c, fx, fy);
    int nao_abriu = !(CAMPO_CASA(&c, fx, fy) & C_ABERTA);
    campo_marcar(&c, fx, fy, 1);
    int b2 = (CAMPO_CASA(&c, fx, fy) & C_DUVIDA) && c.bandeiras == 0;
    campo_marcar(&c, fx, fy, 1);
    int b3 = !(CAMPO_CASA(&c, fx, fy) & (C_DUVIDA | C_BANDEIRA));
    conferir(b1 && nao_abriu && b2 && b3, "botão direito: bandeira, \"?\" e nada; a bandeira protege a casa");

    /* 5. Acorde: num número com as bandeiras certas, abre as vizinhas. */
    int acorde_ok = 0;
    for (int y = 0; y < 9 && !acorde_ok; y++)
        for (int x = 0; x < 9 && !acorde_ok; x++) {
            int n = campo_vizinhas(&c, x, y, C_BOMBA);
            if (!(CAMPO_CASA(&c, x, y) & C_ABERTA) || !n) continue;
            int fechadas_sem_bomba = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int a = x + dx, b = y + dy;
                    if ((!dx && !dy) || a < 0 || b < 0 || a >= 9 || b >= 9) continue;
                    uint8_t v = CAMPO_CASA(&c, a, b);
                    if (v & C_BOMBA) { if (!(v & C_BANDEIRA)) campo_marcar(&c, a, b, 1); }
                    else if (!(v & C_ABERTA)) fechadas_sem_bomba++;
                }
            if (!fechadas_sem_bomba) continue;
            campo_acorde(&c, x, y);
            acorde_ok = c.estado != CAMPO_PERDEU;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int a = x + dx, b = y + dy;
                    if (a < 0 || b < 0 || a >= 9 || b >= 9) continue;
                    uint8_t v = CAMPO_CASA(&c, a, b);
                    if (!(v & C_BOMBA) && !(v & C_ABERTA)) acorde_ok = 0;
                }
        }
    conferir(acorde_ok, "acorde: as vizinhas de um número com as bandeiras certas se abrem");

    /* 6. Abrir todas as casas sem bomba ganha; as bombas ganham bandeira. */
    for (int i = 0; i < 81; i++) {
        if (c.casa[i] & C_BOMBA) continue;
        if (c.casa[i] & C_BANDEIRA) campo_marcar(&c, i % 9, i / 9, 0);
        campo_abrir(&c, i % 9, i / 9);
    }
    conferir(c.estado == CAMPO_GANHOU && c.bandeiras == 10 && contar(&c, C_BANDEIRA) == 10,
             "abrir todas as casas sem bomba ganha a partida");

    /* 7. Abrir uma bomba perde, mostra as bombas e marca as bandeiras erradas. */
    campo_novo(&c, 9, 9, 10, 99);
    campo_abrir(&c, 0, 0);
    int bx = -1, by = -1, lx = -1, ly = -1;
    for (int i = 0; i < 81; i++) {
        if ((c.casa[i] & C_BOMBA) && bx < 0) { bx = i % 9; by = i / 9; }
        if (!(c.casa[i] & (C_BOMBA | C_ABERTA)) && lx < 0) { lx = i % 9; ly = i / 9; }
    }
    if (lx >= 0) campo_marcar(&c, lx, ly, 0);
    campo_abrir(&c, bx, by);
    conferir(c.estado == CAMPO_PERDEU && (CAMPO_CASA(&c, bx, by) & C_ESTOUROU) &&
             contar(&c, C_BOMBA | C_ABERTA) >= 10 && (lx < 0 || (CAMPO_CASA(&c, lx, ly) & C_ERRADA)),
             "abrir uma banana-bomba perde, mostra as bombas e as bandeiras erradas");
    campo_abrir(&c, lx, ly);
    conferir(c.estado == CAMPO_PERDEU, "depois de perder, o campo não muda mais");

    /* 8. Os limites do nível personalizado. */
    campo_novo(&c, 100, 2, 5000, 1);
    conferir(c.cols == CAMPO_MAX_COLS && c.linhas == 5 && c.bombas == CAMPO_MAX_COLS * 5 - 9,
             "o personalizado fica dentro dos limites (30 colunas, 5 linhas, sobra a área do primeiro clique)");
    return falhas ? 1 : 0;
}
