/* motor.c - as regras das atividades JClic (H92). Ver motor.h.
 *
 * Trazido do visor de JClic do fork SaguiOS-Extended de Alejandro
 * Anuncibay e reescrito sobre o main atual. As regras são as que as
 * atividades pedem a quem joga (cada uma explicada no seu "iniciar"); o
 * código é nosso, sem nada do JClic nem do JClic.js. */
#include "motor.h"

motor_t M;

static uint32_t sortear(void)
{
    M.sorteio ^= M.sorteio << 13; M.sorteio ^= M.sorteio >> 17; M.sorteio ^= M.sorteio << 5;
    return M.sorteio;
}

/* Uma ordem embaralhada de 0..n-1 que não seja a original (se der). */
static void embaralhar(int *v, int n)
{
    for (int i = 0; i < n; i++) v[i] = i;
    for (int tentativa = 0; tentativa < 8 && n > 1; tentativa++) {
        for (int i = n - 1; i > 0; i--) {
            int j = (int)(sortear() % (uint32_t)(i + 1));
            int t = v[i]; v[i] = v[j]; v[j] = t;
        }
        for (int i = 0; i < n; i++) if (v[i] != i) return;
    }
}

static painel_t *painel(int k, const grupo_t *g, int n)
{
    painel_t *p = &M.p[k];
    memset(p, 0, sizeof *p);
    if (n > CAIXAS_MAX) n = CAIXAS_MAX;
    p->existe = true;
    p->n = n;
    p->cols = g->cols;
    p->linhas = g->linhas;
    p->cw = g->cw;
    p->ch = g->ch;
    for (int i = 0; i < n; i++) { p->c[i].g = g; p->c[i].cel = i; p->c[i].orig = i; }
    return p;
}

static void acerto(void)
{
    M.acertos++;
    M.som = SOM_ACERTO;
    if (M.alvo > 0 && M.acertos >= M.alvo) { M.resolvida = true; M.som = SOM_FIM; }
}

static void erro(void) { M.som = SOM_ERRO; }

static void contar_lugares(void)
{
    M.acertos = 0;
    for (int i = 0; i < M.p[0].n; i++) if (M.p[0].c[i].est != CX_VAZIA && M.p[0].c[i].orig == i) M.acertos++;
}

static bool vizinhas(const painel_t *p, int a, int b)
{
    int la = a / p->cols, ca = a % p->cols, lb = b / p->cols, cb = b % p->cols;
    return (la == lb && (ca - cb == 1 || cb - ca == 1)) || (ca == cb && (la - lb == 1 || lb - la == 1));
}

/* ------------------------------------------------------------ início de cada tipo */

static void iniciar_caca(const atividade_t *a)
{
    /* As casas com "*" (ou vazias) ganham letras sorteadas. */
    for (int y = 0; y < a->gl; y++) {
        for (int x = 0; x < a->gc; x++) {
            char c = a->grade[y][x];
            M.letra[y][x] = c == '*' || c == ' ' || c == '_' ? (char)('A' + sortear() % 26) : c;
        }
        M.letra[y][a->gc] = 0;
    }
    M.ini_x = -1;
    M.alvo = a->n_palavras;
}

static void iniciar_cruzadinha(const atividade_t *a)
{
    /* Numera as casas onde começa uma palavra (na horizontal ou na vertical,
     * com pelo menos duas letras), linha por linha. */
    int n = 0;
    for (int y = 0; y < a->gl; y++) {
        for (int x = 0; x < a->gc; x++) {
            M.escrito[y][x] = ' ';
            if (a->grade[y][x] == '*') continue;
            M.alvo++;
            bool h = (x == 0 || a->grade[y][x - 1] == '*') && x + 1 < a->gc && a->grade[y][x + 1] != '*';
            bool v = (y == 0 || a->grade[y - 1][x] == '*') && y + 1 < a->gl && a->grade[y + 1][x] != '*';
            if (h || v) M.numero[y][x] = ++n;
        }
        M.escrito[y][a->gc] = 0;
    }
    M.foco_x = -1;
}

void motor_iniciar(const atividade_t *a, uint32_t semente)
{
    memset(&M, 0, sizeof M);
    M.a = a;
    M.sel_p = M.sel_i = -1;
    M.mem_a = M.mem_b = -1;
    M.sorteio = semente ? semente : 1;
    const grupo_t *ga = &a->a, *gb = &a->b;
    static int v[CAIXAS_MAX];
    switch (a->tipo) {
    case AT_INFO:
        if (ga->existe) painel(0, ga, ga->n);
        break;
    case AT_PUZZLE_TROCA: {
        /* Peças embaralhadas; trocando duas de lugar, cada uma vai para a sua casa. */
        if (!ga->existe) break;
        painel_t *p = painel(0, ga, ga->n);
        embaralhar(v, p->n);
        for (int i = 0; i < p->n; i++) p->c[i].cel = p->c[i].orig = v[i];
        M.alvo = p->n;
        contar_lugares();
        break;
    }
    case AT_PUZZLE_DUPLO: {
        /* As peças embaralhadas em A; B começa vazio; cada peça vai para a sua casa em B. */
        if (!ga->existe) break;
        painel_t *p = painel(0, ga, ga->n), *q = painel(1, ga, ga->n);
        embaralhar(v, p->n);
        for (int i = 0; i < p->n; i++) {
            p->c[i].cel = p->c[i].orig = v[i];
            q->c[i].cel = -1; q->c[i].est = CX_VAZIA;
        }
        M.alvo = p->n;
        break;
    }
    case AT_PUZZLE_BURACO: {
        /* A última peça sai e deixa um buraco; o embaralhamento é feito com
         * jogadas permitidas (sempre tem solução). Uma peça vizinha do
         * buraco desliza para ele. */
        if (!ga->existe) break;
        painel_t *p = painel(0, ga, ga->n);
        int b = p->n - 1, anterior = -1;
        p->c[b].est = CX_VAZIA;
        for (int k = 0; k < p->n * 20; k++) {
            int l = b / p->cols, c = b % p->cols, viz[4], nv = 0;
            if (l > 0) viz[nv++] = b - p->cols;
            if (l < p->linhas - 1 && b + p->cols < p->n) viz[nv++] = b + p->cols;
            if (c > 0) viz[nv++] = b - 1;
            if (c < p->cols - 1 && b + 1 < p->n) viz[nv++] = b + 1;
            if (!nv) break;
            int q = viz[sortear() % (uint32_t)nv];
            if (q == anterior && nv > 1) continue;
            bloco_t t = p->c[q]; p->c[q] = p->c[b]; p->c[b] = t;
            anterior = b;
            b = q;
        }
        M.buraco = b;
        M.alvo = p->n;
        contar_lugares();
        break;
    }
    case AT_ASSOCIACAO:
    case AT_ASSOCIACAO_COMPLEXA:
    case AT_EXPLORAR: {
        /* Associação: cada caixa de A com a sua de B (a de mesmo número; na
         * complexa, a que o "id" da célula de A indica, e várias de A podem
         * ir para a mesma de B). Explorar: clicar em A mostra a de B. */
        if (!ga->existe) break;
        painel_t *p = painel(0, ga, ga->n);
        if (a->embaralhar_a) { embaralhar(v, p->n); for (int i = 0; i < p->n; i++) p->c[i].cel = p->c[i].orig = v[i]; }
        if (gb->existe) {
            painel_t *q = painel(1, gb, a->tipo == AT_EXPLORAR ? 1 : gb->n);
            if (a->tipo == AT_EXPLORAR) { q->cols = q->linhas = 1; q->c[0].cel = -1; }
            else if (a->embaralhar_b) { embaralhar(v, q->n); for (int i = 0; i < q->n; i++) q->c[i].cel = q->c[i].orig = v[i]; }
        }
        if (a->tipo == AT_ASSOCIACAO) M.alvo = ga->n < gb->n ? ga->n : gb->n;
        else if (a->tipo == AT_ASSOCIACAO_COMPLEXA) M.alvo = ga->n;
        break;
    }
    case AT_MEMORIA: {
        /* Cartas viradas para baixo, cada uma com um par: com B, a célula i de
         * A e a i de B; sem B, a mesma célula de A duas vezes. Viram-se duas
         * por vez; se forem par, ficam abertas. */
        if (!ga->existe) break;
        int pares = gb->existe ? (ga->n < gb->n ? ga->n : gb->n) : ga->n;
        if (pares * 2 > CAIXAS_MAX) pares = CAIXAS_MAX / 2;
        int m = pares * 2, cols = 2;
        while (cols * cols < m * 3 / 2) cols++;
        painel_t *p = painel(0, ga, m);
        p->cols = cols;
        p->linhas = (m + cols - 1) / cols;
        embaralhar(v, m);
        for (int i = 0; i < m; i++) {
            int par = v[i] % pares;
            bool segunda = v[i] >= pares;
            p->c[i].orig = par;
            p->c[i].cel = par;
            p->c[i].g = segunda && gb->existe ? gb : ga;
            p->c[i].est = CX_VIRADA;
        }
        M.alvo = pares;
        break;
    }
    case AT_IDENTIFICAR: {
        /* As células com id 1 são as certas: é preciso achar todas. */
        if (!ga->existe) break;
        painel_t *p = painel(0, ga, ga->n);
        if (a->embaralhar_a) { embaralhar(v, p->n); for (int i = 0; i < p->n; i++) p->c[i].cel = p->c[i].orig = v[i]; }
        for (int i = 0; i < ga->n; i++) if (ga->c[i].id == 1) M.alvo++;
        break;
    }
    case AT_CACA_PALAVRAS: iniciar_caca(a); break;
    case AT_CRUZADINHA: iniciar_cruzadinha(a); break;
    default: break;
    }
}

bool motor_eh_grade(void) { return M.a && (M.a->tipo == AT_CACA_PALAVRAS || M.a->tipo == AT_CRUZADINHA); }

/* ------------------------------------------------------------ disposição */

void motor_dispor(int x, int y, int w, int h)
{
    const atividade_t *a = M.a;
    if (motor_eh_grade()) {
        int l = a->gc ? w / a->gc : 30, m = a->gl ? h / a->gl : 30;
        M.gcw = l < m ? l : m;
        if (M.gcw > 44) M.gcw = 44;
        if (M.gcw < 12) M.gcw = 12;
        M.gx = x + (w - M.gcw * a->gc) / 2;
        M.gy = y + (h - M.gcw * a->gl) / 2;
        return;
    }
    painel_t *pa = &M.p[0], *pb = &M.p[1];
    if (!pa->existe) return;
    /* Os dois painéis lado a lado ("AB", "BA") ou um sobre o outro ("AUB",
     * "BUA"), no tamanho natural das células, ampliados ou reduzidos (em
     * 1/1024) para caber na área. */
    bool dois = pb->existe, emcima = dois && acha_char(a->disposicao, 'U'), b_antes = dois && a->disposicao[0] == 'B';
    int vao = 14;
    int wa = pa->cols * pa->cw, ha = pa->linhas * pa->ch, wb = dois ? pb->cols * pb->cw : 0, hb = dois ? pb->linhas * pb->ch : 0;
    int tw = !dois ? wa : emcima ? (wa > wb ? wa : wb) : wa + wb;
    int th = !dois ? ha : emcima ? ha + hb : (ha > hb ? ha : hb);
    int dw = w - 16 - (dois && !emcima ? vao : 0), dh = h - 16 - (dois && emcima ? vao : 0);
    int e1 = dw * 1024 / (tw > 0 ? tw : 1), e2 = dh * 1024 / (th > 0 ? th : 1), e = e1 < e2 ? e1 : e2;
    if (e > 2048) e = 2048;
    if (e < 64) e = 64;
    int pwa = wa * e / 1024, pha = ha * e / 1024, pwb = wb * e / 1024, phb = hb * e / 1024;
    int lt = !dois ? pwa : emcima ? (pwa > pwb ? pwa : pwb) : pwa + vao + pwb;
    int at = !dois ? pha : emcima ? pha + vao + phb : (pha > phb ? pha : phb);
    int ox = x + (w - lt) / 2, oy = y + (h - at) / 2;
    painel_t *p1 = b_antes ? pb : pa, *p2 = b_antes ? pa : pb;
    int w1 = b_antes ? pwb : pwa, h1 = b_antes ? phb : pha, w2 = b_antes ? pwa : pwb, h2 = b_antes ? pha : phb;
    p1->x = ox; p1->y = oy; p1->w = w1; p1->h = h1;
    if (dois) {
        if (emcima) { p1->x = ox + (lt - w1) / 2; p2->x = ox + (lt - w2) / 2; p2->y = oy + h1 + vao; }
        else { p1->y = oy + (at - h1) / 2; p2->x = ox + w1 + vao; p2->y = oy + (at - h2) / 2; }
        p2->w = w2; p2->h = h2;
    }
    for (int k = 0; k < 2; k++) {
        painel_t *p = &M.p[k];
        if (!p->existe) continue;
        for (int i = 0; i < p->n; i++) {
            int l = i / p->cols, c = i % p->cols;
            p->c[i].x = p->x + c * p->w / p->cols;
            p->c[i].y = p->y + l * p->h / p->linhas;
            p->c[i].w = p->x + (c + 1) * p->w / p->cols - p->c[i].x;
            p->c[i].h = p->y + (l + 1) * p->h / p->linhas - p->c[i].y;
        }
    }
}

bool motor_caixa_em(int x, int y, int *p, int *i)
{
    for (int k = 0; k < 2; k++) {
        if (!M.p[k].existe) continue;
        for (int j = 0; j < M.p[k].n; j++) {
            bloco_t *c = &M.p[k].c[j];
            if (x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h) { *p = k; *i = j; return true; }
        }
    }
    return false;
}

/* ------------------------------------------------------------ cliques */

static bool associacao_certa(int ia, int ib)
{
    bloco_t *a = &M.p[0].c[ia], *b = &M.p[1].c[ib];
    if (M.a->tipo == AT_ASSOCIACAO_COMPLEXA) {
        int id = a->g->c[a->cel].id;
        return (id >= 0 ? id : a->cel) == b->orig;
    }
    return a->orig == b->orig;
}

static void esconder_memoria(void)
{
    if (M.mem_a >= 0 && M.mem_b >= 0) {
        M.p[0].c[M.mem_a].est = CX_VIRADA;
        M.p[0].c[M.mem_b].est = CX_VIRADA;
    }
    M.mem_a = M.mem_b = -1;
}

void motor_clique(int p, int i)
{
    if (p < 0 || p > 1 || i < 0 || !M.p[p].existe || i >= M.p[p].n) return;
    M.som = SOM_NADA;
    bloco_t *c = &M.p[p].c[i];
    switch (M.a->tipo) {
    case AT_PUZZLE_TROCA:
        if (M.resolvida) return;
        if (M.sel_p < 0 || M.sel_i == i) { M.sel_p = M.sel_i == i ? -1 : 0; M.sel_i = M.sel_p < 0 ? -1 : i; return; }
        {
            bloco_t *o = &M.p[0].c[M.sel_i];
            int cel = o->cel, orig = o->orig;
            o->cel = c->cel; o->orig = c->orig;
            c->cel = cel; c->orig = orig;
            int antes = M.acertos;
            M.tentativas++;
            M.sel_p = M.sel_i = -1;
            contar_lugares();
            M.som = M.acertos > antes ? SOM_ACERTO : SOM_ERRO;
            if (M.acertos >= M.alvo) { M.resolvida = true; M.som = SOM_FIM; }
        }
        break;
    case AT_PUZZLE_DUPLO:
        if (M.resolvida) return;
        if (p == 0 && c->est != CX_VAZIA) { M.sel_p = M.sel_i == i ? -1 : 0; M.sel_i = M.sel_p < 0 ? -1 : i; return; }
        if (p == 1 && M.sel_p == 0 && c->est == CX_VAZIA) {
            bloco_t *o = &M.p[0].c[M.sel_i];
            M.tentativas++;
            if (o->orig == i) { c->cel = c->orig = o->orig; c->est = CX_ACERTO; o->est = CX_VAZIA; acerto(); }
            else erro();
            M.sel_p = M.sel_i = -1;
        }
        break;
    case AT_PUZZLE_BURACO:
        if (M.resolvida || c->est == CX_VAZIA) return;
        if (!vizinhas(&M.p[0], i, M.buraco)) { erro(); return; }
        {
            bloco_t *b = &M.p[0].c[M.buraco];
            bloco_t t = *b;
            b->cel = c->cel; b->orig = c->orig; b->est = CX_NORMAL;
            c->cel = t.cel; c->orig = t.orig; c->est = CX_VAZIA;
            M.buraco = i;
            M.tentativas++;
            contar_lugares();
            /* Resolvido quando todas as peças estão no lugar e o buraco ficou no fim. */
            if (M.buraco == M.p[0].n - 1 && M.acertos == M.p[0].n - 1) { M.acertos = M.alvo; M.resolvida = true; M.som = SOM_FIM; }
        }
        break;
    case AT_ASSOCIACAO:
    case AT_ASSOCIACAO_COMPLEXA:
        if (M.resolvida || c->est == CX_ACERTO || !M.p[1].existe) return;
        if (M.sel_p < 0 || M.sel_p == p) { M.sel_p = M.sel_i == i && M.sel_p == p ? -1 : p; M.sel_i = M.sel_p < 0 ? -1 : i; return; }
        {
            int ia = p == 0 ? i : M.sel_i, ib = p == 0 ? M.sel_i : i;
            M.tentativas++;
            M.sel_p = M.sel_i = -1;
            if (associacao_certa(ia, ib)) {
                M.p[0].c[ia].est = CX_ACERTO;
                if (M.a->tipo == AT_ASSOCIACAO) M.p[1].c[ib].est = CX_ACERTO;
                acerto();
            } else erro();
        }
        break;
    case AT_MEMORIA:
        if (M.resolvida || c->est != CX_VIRADA) return;
        if (M.mem_a >= 0 && M.mem_b >= 0) esconder_memoria();   /* clicou antes do tempo: esconde já */
        c->est = CX_NORMAL;
        if (M.mem_a < 0) { M.mem_a = i; return; }
        M.mem_b = i;
        M.tentativas++;
        if (M.p[0].c[M.mem_a].orig == c->orig) {
            M.p[0].c[M.mem_a].est = CX_ACERTO;
            c->est = CX_ACERTO;
            M.mem_a = M.mem_b = -1;
            acerto();
        } else { M.esconder_em = 0; erro(); }
        break;
    case AT_EXPLORAR:
        if (p == 0 && M.p[1].existe) { M.p[1].c[0].cel = c->cel < M.a->b.n ? c->cel : -1; M.sel_p = 0; M.sel_i = i; }
        break;
    case AT_IDENTIFICAR:
        if (M.resolvida || c->est == CX_ACERTO) return;
        M.tentativas++;
        if (c->g->c[c->cel].id == 1) { c->est = CX_ACERTO; acerto(); }
        else erro();
        break;
    default: break;
    }
}

bool motor_tique(unsigned agora)
{
    if (M.a && M.a->tipo == AT_MEMORIA && M.mem_a >= 0 && M.mem_b >= 0) {
        if (!M.esconder_em) M.esconder_em = agora + 900;      /* deixa ver as duas por quase 1 s */
        else if ((int)(agora - M.esconder_em) >= 0) { esconder_memoria(); return true; }
    }
    return false;
}

/* ------------------------------------------------------------ grade de letras */

bool motor_casa_em(int x, int y, int *gx, int *gy)
{
    if (!motor_eh_grade() || x < M.gx || y < M.gy) return false;
    *gx = (x - M.gx) / M.gcw;
    *gy = (y - M.gy) / M.gcw;
    return *gx < M.a->gc && *gy < M.a->gl;
}

/* Caça-palavras: a linha reta de (x0,y0) a (x1,y1) forma uma palavra (lida
 * de um lado ou do outro) ainda não achada? */
static void conferir_linha(int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? 1 : x1 < x0 ? -1 : 0, dy = y1 > y0 ? 1 : y1 < y0 ? -1 : 0;
    int n = (x1 - x0) * dx > (y1 - y0) * dy ? (x1 - x0) * dx : (y1 - y0) * dy;
    if ((x1 - x0) * dx != n && dx) { erro(); return; }       /* nem reta nem diagonal */
    if ((y1 - y0) * dy != n && dy) { erro(); return; }
    n++;
    char s[GRADE_MAX + 1], r[GRADE_MAX + 1];
    for (int k = 0; k < n; k++) { s[k] = M.letra[y0 + k * dy][x0 + k * dx]; r[n - 1 - k] = s[k]; }
    s[n] = r[n] = 0;
    M.tentativas++;
    for (int w = 0; w < M.a->n_palavras; w++) {
        if (M.achou[w] || (compara_texto(s, M.a->palavra[w]) && compara_texto(r, M.a->palavra[w]))) continue;
        M.achou[w] = true;
        for (int k = 0; k < n; k++) M.marcada[y0 + k * dy][x0 + k * dx] = 1;
        acerto();
        return;
    }
    erro();
}

static bool letra_cruz(int x, int y) { return x >= 0 && y >= 0 && x < M.a->gc && y < M.a->gl && M.a->grade[y][x] != '*'; }

void motor_clique_casa(int gx, int gy)
{
    M.som = SOM_NADA;
    if (M.resolvida) return;
    if (M.a->tipo == AT_CACA_PALAVRAS) {
        if (M.ini_x < 0) { M.ini_x = gx; M.ini_y = gy; return; }
        int x0 = M.ini_x, y0 = M.ini_y;
        M.ini_x = -1;
        if (x0 == gx && y0 == gy) return;     /* clicou duas vezes na mesma: desiste */
        conferir_linha(x0, y0, gx, gy);
    } else if (M.a->tipo == AT_CRUZADINHA) {
        if (!letra_cruz(gx, gy)) return;
        if (gx == M.foco_x && gy == M.foco_y) M.direcao ^= 1;       /* o mesmo lugar: troca o sentido */
        else {
            M.foco_x = gx; M.foco_y = gy;
            /* Começa no sentido em que a casa faz parte de uma palavra. */
            bool h = letra_cruz(gx - 1, gy) || letra_cruz(gx + 1, gy), v = letra_cruz(gx, gy - 1) || letra_cruz(gx, gy + 1);
            if (M.direcao == 0 && !h && v) M.direcao = 1;
            else if (M.direcao == 1 && !v && h) M.direcao = 0;
        }
    }
}

void motor_letra(int c)
{
    if (M.a->tipo != AT_CRUZADINHA || M.foco_x < 0 || M.resolvida) return;
    M.som = SOM_NADA;
    int dx = M.direcao ? 0 : 1, dy = M.direcao ? 1 : 0;
    if (c == 8) {                          /* apagar: volta uma casa e limpa */
        if (M.escrito[M.foco_y][M.foco_x] == ' ' && letra_cruz(M.foco_x - dx, M.foco_y - dy)) { M.foco_x -= dx; M.foco_y -= dy; }
        M.escrito[M.foco_y][M.foco_x] = ' ';
    } else {
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c -= 32;
        if (c < 'A' && !(c >= '0' && c <= '9')) return;
        M.escrito[M.foco_y][M.foco_x] = (char)c;
        if (letra_cruz(M.foco_x + dx, M.foco_y + dy)) { M.foco_x += dx; M.foco_y += dy; }
    }
    M.acertos = 0;
    for (int y = 0; y < M.a->gl; y++)
        for (int x = 0; x < M.a->gc; x++)
            if (M.a->grade[y][x] != '*' && M.escrito[y][x] == M.a->grade[y][x]) M.acertos++;
    if (M.acertos >= M.alvo) { M.resolvida = true; M.som = SOM_FIM; }
}

const char *motor_pista(void)
{
    /* A pista é a k-ésima célula do grupo do sentido, em que k é a ordem da
     * palavra (as palavras de cada sentido contadas linha por linha). */
    if (!M.a || M.a->tipo != AT_CRUZADINHA || M.foco_x < 0) return NULL;
    int x = M.foco_x, y = M.foco_y, dx = M.direcao ? 0 : 1, dy = M.direcao ? 1 : 0;
    while (letra_cruz(x - dx, y - dy)) { x -= dx; y -= dy; }
    int k = 0;
    for (int yy = 0; yy < M.a->gl; yy++)
        for (int xx = 0; xx < M.a->gc; xx++) {
            if (!letra_cruz(xx, yy) || letra_cruz(xx - dx, yy - dy) || !letra_cruz(xx + dx, yy + dy)) continue;
            if (xx == x && yy == y) {
                const grupo_t *g = M.direcao ? &M.a->pistas_v : &M.a->pistas_h;
                return k < g->n && g->c[k].texto ? g->c[k].texto : NULL;
            }
            k++;
        }
    return NULL;
}
