/* regras.c - as regras da Paciência Banana (H92).
 *
 * Trazido do spider do fork SaguiOS-Extended de Alejandro Anuncibay e
 * reescrito sobre o main atual. O jogo é de domínio público; o nome e os
 * desenhos daqui são nossos. Pontos: começa com 500, cada jogada tira 1 e
 * cada sequência completa dá 100. */
#include "regras.h"

static uint32_t sorteio;

static uint32_t sortear(void)
{
    /* xorshift de 32 bits: basta para embaralhar e é repetível pela semente. */
    sorteio ^= sorteio << 13; sorteio ^= sorteio >> 17; sorteio ^= sorteio << 5;
    return sorteio;
}

void mesa_nova(mesa_t *m, int naipes, uint32_t semente)
{
    carta_t baralho[104];
    int k = 0;
    if (naipes != 2 && naipes != 4) naipes = 1;
    /* Oito vezes os 13 valores; com menos naipes, eles se repetem. */
    for (int s = 0; s < 8; s++)
        for (int v = 1; v <= 13; v++) baralho[k++] = CARTA(s % naipes, v, 0);
    /* Embaralha trocando cada posição, de trás para a frente, com uma
     * sorteada entre ela e o começo: toda ordem tem a mesma chance. */
    sorteio = semente ? semente : 1;
    for (int i = 103; i > 0; i--) {
        int r = (int)(sortear() % (uint32_t)(i + 1));
        carta_t t = baralho[i]; baralho[i] = baralho[r]; baralho[r] = t;
    }
    memset(m, 0, sizeof *m);
    k = 0;
    for (int c = 0; c < COLUNAS; c++) {
        int q = c < 4 ? 6 : 5;
        for (int i = 0; i < q; i++) m->col[c][i] = baralho[k++];
        m->n[c] = (uint8_t)q;
        m->col[c][q - 1] |= 0x80;
    }
    for (int i = 0; i < 50; i++) m->monte[i] = baralho[k++];
    m->n_monte = 50;
    m->pontos = 500;
}

int mesa_inicio_escada(const mesa_t *m, int c)
{
    int i = m->n[c] - 1;
    if (i < 0) return 0;
    while (i > 0) {
        carta_t a = m->col[c][i - 1], b = m->col[c][i];
        if (!ABERTA(a) || NAIPE(a) != NAIPE(b) || VALOR(a) != VALOR(b) + 1) break;
        i--;
    }
    return i;
}

bool mesa_pode_pegar(const mesa_t *m, int c, int i)
{
    return c >= 0 && c < COLUNAS && i >= 0 && i < m->n[c] && ABERTA(m->col[c][i]) && i >= mesa_inicio_escada(m, c);
}

bool mesa_pode_soltar(const mesa_t *m, int c, int valor)
{
    return m->n[c] == 0 || VALOR(m->col[c][m->n[c] - 1]) == valor + 1;
}

/* Do rei ao ás do mesmo naipe, abertos, no fim da coluna? */
static bool sequencia_completa(const mesa_t *m, int c)
{
    int n = m->n[c];
    if (n < 13) return false;
    for (int k = 0; k < 13; k++) {
        carta_t x = m->col[c][n - 13 + k];
        if (!ABERTA(x) || VALOR(x) != 13 - k || NAIPE(x) != NAIPE(m->col[c][n - 1])) return false;
    }
    return true;
}

static void tirar_completas(mesa_t *m)
{
    for (int c = 0; c < COLUNAS; c++)
        while (sequencia_completa(m, c)) {
            if (m->feitas < SEQUENCIAS) m->naipe_feita[m->feitas] = (uint8_t)NAIPE(m->col[c][m->n[c] - 1]);
            m->feitas++;
            m->pontos += 100;
            m->n[c] = (uint8_t)(m->n[c] - 13);
            if (m->n[c]) m->col[c][m->n[c] - 1] |= 0x80;      /* a carta de baixo se abre */
        }
}

bool mesa_mover(mesa_t *m, int de, int i, int para)
{
    if (para < 0 || para >= COLUNAS || de == para || !mesa_pode_pegar(m, de, i)) return false;
    if (!mesa_pode_soltar(m, para, VALOR(m->col[de][i]))) return false;
    int q = m->n[de] - i;
    memcpy(&m->col[para][m->n[para]], &m->col[de][i], (size_t)q);
    m->n[para] = (uint8_t)(m->n[para] + q);
    m->n[de] = (uint8_t)i;
    if (i > 0) m->col[de][i - 1] |= 0x80;
    if (m->pontos > 0) m->pontos--;
    m->jogadas++;
    tirar_completas(m);
    return true;
}

int mesa_dar(mesa_t *m)
{
    if (m->n_monte == 0) return DAR_SEM_CARTAS;
    for (int c = 0; c < COLUNAS; c++) if (m->n[c] == 0) return DAR_COLUNA_VAZIA;   /* a regra do jogo */
    for (int c = 0; c < COLUNAS; c++) m->col[c][m->n[c]++] = m->monte[--m->n_monte] | 0x80;
    if (m->pontos > 0) m->pontos--;
    m->jogadas++;
    tirar_completas(m);
    return DAR_OK;
}

/* Quanto vale levar a pilha que começa em col[c][i] para a coluna t (-1 =
 * não pode ou não adianta). Mesma naipe vale mais (junta uma escada); abrir
 * uma carta escondida vale mais ainda; separar uma escada do mesmo naipe só
 * vale se for para juntá-la a outra do mesmo naipe; levar a coluna inteira
 * para uma coluna vazia não muda nada. */
static int valor_jogada(const mesa_t *m, int c, int i, int t)
{
    if (t == c || !mesa_pode_pegar(m, c, i)) return -1;
    carta_t x = m->col[c][i];
    int v;
    if (m->n[t] == 0) {
        if (i == 0) return -1;
        v = 2;
    } else {
        carta_t topo = m->col[t][m->n[t] - 1];
        if (VALOR(topo) != VALOR(x) + 1) return -1;
        v = NAIPE(topo) == NAIPE(x) ? 30 : 10;
    }
    bool abre = i > 0 && !ABERTA(m->col[c][i - 1]);
    bool separa = i > 0 && ABERTA(m->col[c][i - 1]) && NAIPE(m->col[c][i - 1]) == NAIPE(x) &&
                  VALOR(m->col[c][i - 1]) == VALOR(x) + 1;
    if (separa && v < 30) return -1;
    if (abre) v += 8;
    return v + (m->n[c] - i);
}

int mesa_melhor_destino(const mesa_t *m, int c, int i)
{
    int melhor = -1, mv = -1;
    for (int t = 0; t < COLUNAS; t++) {
        if (t == c || !mesa_pode_pegar(m, c, i) || !mesa_pode_soltar(m, t, VALOR(m->col[c][i]))) continue;
        if (m->n[t] == 0 && i == 0) continue;
        /* Mesmo naipe > outro naipe > coluna vazia; empate: a coluna mais alta. */
        int v = m->n[t] == 0 ? 1 : NAIPE(m->col[t][m->n[t] - 1]) == NAIPE(m->col[c][i]) ? 3 : 2;
        v = v * 200 + m->n[t];
        if (v > mv) { mv = v; melhor = t; }
    }
    return melhor;
}

bool mesa_dica(const mesa_t *m, int ordem, jogada_t *j)
{
    /* Junta as jogadas que valem algo e escolhe a "ordem"-ésima melhor, para
     * a tecla de dica ir mostrando outras a cada vez. */
    static jogada_t lista[400];
    static int valor[400];
    int n = 0;
    for (int c = 0; c < COLUNAS; c++)
        for (int i = mesa_inicio_escada(m, c); i < m->n[c]; i++)
            for (int t = 0; t < COLUNAS && n < 400; t++) {
                int v = valor_jogada(m, c, i, t);
                if (v < 0) continue;
                lista[n].de = c; lista[n].i = i; lista[n].para = t; valor[n] = v;
                n++;
            }
    if (!n) return false;
    /* Ordena do maior valor para o menor (inserção: são poucas). */
    for (int a = 1; a < n; a++)
        for (int b = a; b > 0 && valor[b] > valor[b - 1]; b--) {
            int tv = valor[b]; valor[b] = valor[b - 1]; valor[b - 1] = tv;
            jogada_t tj = lista[b]; lista[b] = lista[b - 1]; lista[b - 1] = tj;
        }
    *j = lista[(ordem % n + n) % n];
    return true;
}

void mesa_quase_ganha(mesa_t *m)
{
    /* Sete sequências já feitas; na coluna 0, rei a 2 de espadas; na 1, o ás:
     * levar o ás para a coluna 0 completa a última. */
    memset(m, 0, sizeof *m);
    m->feitas = 7;
    for (int k = 0; k < 7; k++) m->naipe_feita[k] = (uint8_t)(k % 4);
    for (int v = 13; v >= 2; v--) m->col[0][m->n[0]++] = CARTA(ESPADAS, v, 1);
    m->col[1][m->n[1]++] = CARTA(ESPADAS, 1, 1);
    m->pontos = 1200;
}
