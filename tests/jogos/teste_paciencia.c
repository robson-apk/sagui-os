/* teste_paciencia.c - testa as regras da Paciência Banana (H92) no
 * computador de desenvolvimento, com o mesmo userland/jogos/paciencia/regras.c
 * que vai para o SaguiOS. Termina com 1 se alguma conferência falhar. */
#include <stdio.h>
#include "regras.h"

static int falhas;

static void conferir(int cond, const char *msg)
{
    printf("%s: %s\n", cond ? "ok" : "FALHOU", msg);
    if (!cond) falhas++;
}

static int total_cartas(const mesa_t *m)
{
    int t = m->n_monte + m->feitas * 13;
    for (int c = 0; c < COLUNAS; c++) t += m->n[c];
    return t;
}

int main(void)
{
    static mesa_t m, outra;
    /* 1. A distribuição: 6,6,6,6,5,...; só a última aberta; 50 no monte; cada
     *    carta aparece 8/naipes vezes por naipe. */
    for (int naipes = 1; naipes <= 4; naipes *= 2) {
        mesa_nova(&m, naipes, 77);
        int ok = m.n_monte == 50 && total_cartas(&m) == 104;
        for (int c = 0; c < COLUNAS; c++) {
            ok = ok && m.n[c] == (c < 4 ? 6 : 5);
            for (int i = 0; i < m.n[c]; i++) ok = ok && (ABERTA(m.col[c][i]) == (i == m.n[c] - 1));
        }
        int conta[4][14] = { { 0 } };
        for (int c = 0; c < COLUNAS; c++) for (int i = 0; i < m.n[c]; i++) conta[NAIPE(m.col[c][i])][VALOR(m.col[c][i])]++;
        for (int i = 0; i < 50; i++) conta[NAIPE(m.monte[i])][VALOR(m.monte[i])]++;
        for (int s = 0; s < 4; s++)
            for (int v = 1; v <= 13; v++) ok = ok && conta[s][v] == (s < naipes ? 8 / naipes : 0);
        char msg[100];
        snprintf(msg, sizeof msg, "distribuição certa com %d naipe%s (104 cartas, 54 na mesa, 50 no monte)", naipes, naipes > 1 ? "s" : "");
        conferir(ok, msg);
    }
    mesa_nova(&m, 4, 5); mesa_nova(&outra, 4, 5);
    conferir(memcmp(&m, &outra, sizeof m) == 0, "a mesma semente dá a mesma partida");

    /* 2. Jogadas: só sobre um valor um ponto maior; carta fechada não sai. */
    memset(&m, 0, sizeof m);
    m.col[0][m.n[0]++] = CARTA(COPAS, 9, 0);
    m.col[0][m.n[0]++] = CARTA(COPAS, 7, 1);
    m.col[1][m.n[1]++] = CARTA(ESPADAS, 8, 1);
    m.col[2][m.n[2]++] = CARTA(OUROS, 9, 1);
    conferir(!mesa_mover(&m, 0, 1, 2), "não pode pôr um 7 sobre um 9");
    conferir(!mesa_mover(&m, 0, 0, 3), "carta fechada não pode ser pega");
    conferir(mesa_mover(&m, 0, 1, 1) && m.n[1] == 2 && ABERTA(m.col[0][0]),
             "7 de copas sobre 8 de espadas (outro naipe vale) e a carta de baixo se abre");
    conferir(!mesa_pode_pegar(&m, 1, 0), "escada de naipes diferentes não sai inteira");
    conferir(mesa_mover(&m, 1, 1, 3) && m.n[3] == 1, "uma carta vai para a coluna vazia");

    /* 3. Escada do mesmo naipe sai inteira; rei a ás do mesmo naipe sai da mesa. */
    memset(&m, 0, sizeof m);
    for (int v = 13; v >= 2; v--) m.col[0][m.n[0]++] = CARTA(PAUS, v, 1);
    m.col[1][m.n[1]++] = CARTA(OUROS, 5, 0);
    m.col[1][m.n[1]++] = CARTA(PAUS, 1, 1);
    m.pontos = 500;
    conferir(mesa_pode_pegar(&m, 0, 0), "do rei ao 2 de paus: a escada inteira pode ser pega");
    int antes = m.pontos;
    conferir(mesa_mover(&m, 1, 1, 0) && m.feitas == 1 && m.n[0] == 0 && m.pontos == antes - 1 + 100 &&
             ABERTA(m.col[1][0]), "o ás completa a sequência: ela sai da mesa e vale 100 pontos");

    /* 4. Dar cartas: uma em cada coluna; com coluna vazia não pode. */
    mesa_nova(&m, 1, 3);
    int n0 = m.n[0];
    conferir(mesa_dar(&m) == DAR_OK && m.n_monte == 40 && m.n[0] == n0 + 1 && ABERTA(m.col[0][m.n[0] - 1]),
             "dar cartas põe uma carta aberta em cada coluna");
    m.n[5] = 0;
    conferir(mesa_dar(&m) == DAR_COLUNA_VAZIA, "com uma coluna vazia não se dá cartas");
    m.n[5] = 1;
    for (int k = 0; k < 4; k++) mesa_dar(&m);
    conferir(m.n_monte == 0 && mesa_dar(&m) == DAR_SEM_CARTAS, "o monte acaba depois de 5 rodadas");

    /* 5. Dica e melhor destino: preferem o mesmo naipe. */
    memset(&m, 0, sizeof m);
    m.col[0][m.n[0]++] = CARTA(COPAS, 6, 1);
    m.col[1][m.n[1]++] = CARTA(ESPADAS, 7, 1);
    m.col[2][m.n[2]++] = CARTA(COPAS, 7, 1);
    jogada_t j;
    conferir(mesa_dica(&m, 0, &j) && j.de == 0 && j.para == 2, "a primeira dica junta o mesmo naipe");
    conferir(mesa_melhor_destino(&m, 0, 0) == 2, "o clique leva a carta para o mesmo naipe");

    /* 6. Vitória: na partida quase ganha, o ás termina o jogo. */
    mesa_quase_ganha(&m);
    conferir(!MESA_GANHOU(&m) && mesa_mover(&m, 1, 0, 0) && MESA_GANHOU(&m), "a oitava sequência ganha o jogo");

    /* 7. Uma partida jogada só com as dicas e o monte nunca perde nem cria cartas. */
    int certo = 1;
    for (uint32_t s = 1; s <= 40; s++) {
        mesa_nova(&m, 1, s);
        for (int passo = 0; passo < 400; passo++) {
            if (mesa_dica(&m, 0, &j)) { if (!mesa_mover(&m, j.de, j.i, j.para)) { certo = 0; break; } }
            else if (mesa_dar(&m) != DAR_OK) break;
            if (total_cartas(&m) != 104) { certo = 0; break; }
        }
    }
    conferir(certo, "jogando pelas dicas (40 partidas) toda dica é válida e as 104 cartas continuam lá");
    return falhas ? 1 : 0;
}
