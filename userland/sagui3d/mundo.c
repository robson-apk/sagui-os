/* mundo.c - as regras do Sagui 3D: andar, macacos-prego, sementes, bananas.
 *
 * Tudo anda em "tiques" de 30 ms (TIQUE_MS), independentemente de quantos
 * quadros por segundo o computador desenha: no G470 e no QEMU o jogo tem a
 * mesma velocidade.
 *
 * Como os macacos-prego decidem para onde andar (ver também docs/decisoes.md):
 *   - de tempos em tempos (não a cada tique, para economizar conta) cada um
 *     olha para o sagui: se ele está a menos de VISTA_MACACO palmos e a linha reta
 *     entre os dois passa só por passagens abertas, o macaco "vê" o sagui e
 *     guarda onde o viu;
 *   - andando, vai em linha reta para o último lugar onde o viu. Se bate
 *     numa parede, desvia 45 graus para um lado sorteado por um tempinho e
 *     tenta de novo (contorna quinas e pilares sem precisar de mapa de
 *     caminhos);
 *   - se chega lá e não vê mais o sagui, fica quieto, de guarda;
 *   - três sementes: fica tonto e depois foge para longe do sagui até sumir
 *     na mata. Pisar numa casca: escorrega, gira e fica tonto um tempo, e
 *     depois volta a caçar.
 * Ninguém se machuca: encostar no sagui só tira uma vida dele (o macaco
 * "pega" uma banana do bolso e sai correndo com ela por uns segundos antes
 * de voltar a caçar; o sagui fica uns segundos sem poder ser pego de novo). */
#include "jogo.h"

sagui_t sagui;
int  fase_atual;
char aviso[64];
int  aviso_tempo;

#define VISTA_MACACO 640   /* até onde um macaco enxerga, em palmos */

static int  tique;
static int  ultimo_registro;
static fixo registro_x, registro_y;
static int  espera_porteira;

void mundo_mostrar_aviso(const char *t)
{
    copia_texto(aviso, t, sizeof aviso);
    aviso_tempo = 90;   /* ~2,7 s */
}

bool porteira_aberta(void) { return sagui.bananas >= mapa.minimo; }

/* ------------------------------------------------------------ movimento */

/* Dá para ir da sala "de" para a sala "para"? O degrau não pode passar de
 * DEGRAU e o vão entre o chão mais alto e o teto mais baixo precisa caber
 * a altura de quem passa. Descer qualquer altura pode. */
static bool pode_passar(const sala_t *de, const sala_t *para, int altura)
{
    if (para->chao - de->chao > FX(DEGRAU)) return false;
    fixo teto = para->teto < de->teto ? para->teto : de->teto;
    fixo chao = para->chao > de->chao ? para->chao : de->chao;
    return teto - chao >= FX(altura);
}

/* Move um corpo redondo (raio) de (x, y) por (dx, dy), escorregando ao longo
 * das paredes. Anda em passinhos de no máximo 6 palmos para não atravessar
 * uma parede fina num tique só. Em cada passinho, para cada lado da sala que
 * ficou a menos de "raio" do centro: se é parede (ou passagem que não dá
 * para usar), empurra o corpo de volta para dentro, na direção da normal
 * (o que sobra do movimento é o deslize ao longo da parede); se é passagem
 * boa e o centro já cruzou, o corpo muda de sala. Devolve true se bateu. */
static bool mover(fixo *x, fixo *y, int *sala, fixo raio_c, int altura, fixo dx, fixo dy, bool atravessa_segredo)
{
    fixo maior = dx < 0 ? -dx : dx;
    if ((dy < 0 ? -dy : dy) > maior) maior = dy < 0 ? -dy : dy;
    int passos = INTEIRO(maior / 6) + 1;
    fixo px = dx / passos, py = dy / passos;
    bool bateu = false;
    for (int p = 0; p < passos; p++) {
        fixo nx = *x + px, ny = *y + py;
        int s_atual = *sala;
        for (int volta = 0; volta < 3; volta++) {
            const sala_t *s = &mapa.salas[s_atual];
            bool trocou = false;
            for (int l = 0; l < s->n && !trocou; l++) {
                const lado_t *d = &s->lados[l];
                fixo dist = distancia_ao_lado(s, l, nx, ny);
                if (dist >= raio_c) continue;
                const ponto_t *a = &mapa.pontos[d->a];
                fixo ao_longo = fmul(nx - a->x, d->ny) - fmul(ny - a->y, d->nx);
                if (ao_longo < -raio_c || ao_longo > d->comprimento + raio_c) continue;
                bool livre = d->vizinha >= 0 && (!d->secreta || atravessa_segredo) &&
                             pode_passar(s, &mapa.salas[d->vizinha], altura);
                if (livre) {
                    if (dist < 0 && dentro_da_sala(d->vizinha, nx, ny)) { s_atual = d->vizinha; trocou = true; }
                    continue;
                }
                nx += fmul(raio_c - dist, d->nx);
                ny += fmul(raio_c - dist, d->ny);
                bateu = true;
            }
            if (!trocou) break;
        }
        int confere = sala_do_ponto(nx, ny, s_atual);
        if (confere < 0) { bateu = true; break; }
        if (confere != *sala && confere != s_atual) {
            /* Caiu numa sala sem passar pelo teste acima (quina): só aceita se der para passar. */
            if (!pode_passar(&mapa.salas[*sala], &mapa.salas[confere], altura)) { bateu = true; break; }
        }
        *x = nx;
        *y = ny;
        *sala = confere;
    }
    return bateu;
}

static fixo distancia(fixo ax, fixo ay, fixo bx, fixo by)
{
    int dx = INTEIRO(ax - bx), dy = INTEIRO(ay - by);
    if (dx > 4000 || dx < -4000 || dy > 4000 || dy < -4000) return FX(9999);
    return FX(raiz((uint32_t)(dx * dx + dy * dy)));
}

static coisa_t *coisa_nova(int tipo)
{
    coisa_t *c = NULL;
    for (int i = 0; i < mapa.qtd_coisas && !c; i++)
        if (!mapa.coisas[i].ativa && mapa.coisas[i].tipo != C_MACACO) c = &mapa.coisas[i];
    if (!c && mapa.qtd_coisas < MAX_COISAS) c = &mapa.coisas[mapa.qtd_coisas++];
    if (!c) return NULL;
    memset(c, 0, sizeof *c);
    c->tipo = tipo;
    c->ativa = true;
    return c;
}

/* ------------------------------------------------------------ fase */

void mundo_iniciar_fase(void)
{
    sagui.x = mapa.inicio_x;
    sagui.y = mapa.inicio_y;
    sagui.ang = mapa.inicio_ang;
    sagui.sala = mapa.inicio_sala;
    sagui.z = mapa.salas[sagui.sala].chao + FX(OLHO);
    sagui.bananas = 0;
    sagui.comidas = 0;
    sagui.cascas = 0;
    sagui.cacho = false;
    sagui.espantados = 0;
    sagui.invulneravel = 0;
    sagui.recarga = 0;
    sagui.puxado = 0;
    if (sagui.sementes < 15) sagui.sementes = 15;
    for (int i = 0; i < mapa.qtd_coisas; i++) {
        coisa_t *c = &mapa.coisas[i];
        if (c->tipo == C_MACACO) {
            c->estado = M_QUIETO;
            c->ang = (int)(sorteio() % VOLTA);
        }
    }
    tique = 0;
    ultimo_registro = -100;
    espera_porteira = 0;
    aviso_tempo = 0;
    registrar("sagui3d: fase %d carregada: %d salas, %d paredes, %d passagens, %d bananas (minimo %d)",
              fase_atual, mapa.qtd_salas, mapa.paredes, mapa.passagens, mapa.total_bananas, mapa.minimo);
}

/* ------------------------------------------------------------ sagui */

static void atirar(void)
{
    if (sagui.sementes <= 0) {
        mundo_mostrar_aviso("Sem sementes! Procure um pote de jabuticabas.");
        sagui.recarga = 20;
        return;
    }
    coisa_t *s = coisa_nova(C_SEMENTE_VOANDO);
    if (!s) return;
    fixo cs = cosseno(sagui.ang), sn = seno(sagui.ang);
    s->x = sagui.x;
    s->y = sagui.y;
    s->sala = sagui.sala;
    s->z = sagui.z - FX(8);
    s->vx = cs * 14;
    s->vy = sn * 14;
    s->tempo = 60;
    sagui.sementes--;
    sagui.recarga = 9;
    sagui.puxado = 6;
    som_tiro();
    registrar("sagui3d: tiro (sementes %d)", sagui.sementes);
}

void mundo_largar_casca(void)
{
    if (sagui.cascas <= 0) { mundo_mostrar_aviso("Sem cascas: coma uma banana antes."); return; }
    coisa_t *c = coisa_nova(C_CASCA);
    if (!c) return;
    /* Um passo à frente do sagui, se não houver parede no caminho. */
    fixo x = sagui.x, y = sagui.y;
    int sala = sagui.sala;
    mover(&x, &y, &sala, FX(4), 8, cosseno(sagui.ang) * 28, seno(sagui.ang) * 28, false);
    c->x = x;
    c->y = y;
    c->sala = sala;
    sagui.cascas--;
    som_casca();
    registrar("sagui3d: casca no chao em x=%d y=%d", INTEIRO(x), INTEIRO(y));
}

static void pegar_coisas(void)
{
    for (int i = 0; i < mapa.qtd_coisas; i++) {
        coisa_t *c = &mapa.coisas[i];
        if (!c->ativa) continue;
        fixo d = distancia(c->x, c->y, sagui.x, sagui.y);
        if (c->tipo == C_BANANA && d < FX(22)) {
            c->ativa = false;
            sagui.bananas++;
            sagui.comidas++;
            sagui.cascas++;
            sagui.pontos += 10;
            som_banana();
            registrar("sagui3d: comeu banana (bananas %d de %d, cascas %d)", sagui.bananas, mapa.minimo, sagui.cascas);
            if (sagui.comidas % 5 == 0) {
                if (sagui.vidas < 9) sagui.vidas++;
                mundo_mostrar_aviso("Cinco bananas: uma vida a mais!");
            } else if (sagui.bananas == mapa.minimo) {
                mundo_mostrar_aviso("A porteira da banana abriu!");
                registrar("sagui3d: porteira aberta");
            }
        } else if (c->tipo == C_CACHO && d < FX(24)) {
            c->ativa = false;
            sagui.cacho = true;
            sagui.pontos += 100;
            som_cacho();
            mundo_mostrar_aviso("Cacho dourado! +100 pontos");
            registrar("sagui3d: achou o cacho dourado");
        } else if (c->tipo == C_POTE && d < FX(22)) {
            c->ativa = false;
            sagui.sementes += 10;
            som_banana();
            mundo_mostrar_aviso("Pote de jabuticabas: +10 sementes");
            registrar("sagui3d: pegou sementes (%d)", sagui.sementes);
        }
    }
}

/* ------------------------------------------------------------ macacos-prego */

static void macaco_mudar(coisa_t *m, int estado, int tempo)
{
    m->estado = estado;
    m->tempo = tempo;
}

static int indice(const coisa_t *m) { return (int)(m - mapa.coisas); }

static int macaco_acertado(coisa_t *m)
{
    /* Já tonto: a semente bate e cai, sem contar de novo (antes chegava a
     * "4/3" e o macaco ficava tonto outra vez, com os pontos em dobro). */
    if (m->acertos >= 3) return 0;
    m->acertos++;
    som_acerto();
    registrar("sagui3d: semente acertou macaco %d (%d/3)", indice(m), m->acertos);
    if (m->acertos >= 3) {
        macaco_mudar(m, M_TONTO, 100);
        sagui.pontos += 20;
        registrar("sagui3d: macaco %d ficou tonto e vai fugir", indice(m));
        mundo_mostrar_aviso("O macaco-prego ficou tonto!");
    } else if (m->estado != M_TONTO && m->estado != M_ESCORREGANDO) {
        macaco_mudar(m, M_PARADO, 10);
    }
    return 0;
}

static int macaco_tique(coisa_t *m, int i)
{
    const sala_t *sm = &mapa.salas[m->sala];
    fixo d = distancia(m->x, m->y, sagui.x, sagui.y);
    if (m->tempo > 0) m->tempo--;

    switch (m->estado) {
    case M_TONTO:
        if (m->tempo == 0) {
            if (m->acertos >= 3) { macaco_mudar(m, M_FUGINDO, 160); registrar("sagui3d: macaco %d fugiu", i); }
            else macaco_mudar(m, M_CACANDO, 0);
        }
        return R_NADA;
    case M_ESCORREGANDO:
        /* Desliza um pouco para a frente (sem passar do sagui) e fica girando. */
        if (m->tempo > 118 && distancia(m->x, m->y, sagui.x, sagui.y) > FX(40))
            mover(&m->x, &m->y, &m->sala, FX(RAIO_MACACO), 44, cosseno(m->ang) * 5 / 2, seno(m->ang) * 5 / 2, false);
        if (m->tempo == 0) macaco_mudar(m, m->acertos >= 3 ? M_FUGINDO : M_CACANDO, m->acertos >= 3 ? 160 : 0);
        return R_NADA;
    case M_FUGINDO: {
        int a = angulo_de(m->x - sagui.x, m->y - sagui.y) + m->desvio;
        if (mover(&m->x, &m->y, &m->sala, FX(RAIO_MACACO), 44, cosseno(a) * 6, seno(a) * 6, false))
            m->desvio = (sorteio() & 1) ? 700 : -700;
        if (m->tempo == 0) {
            m->estado = M_FORA;
            m->ativa = false;
            sagui.espantados++;
            sagui.pontos += 50;
            registrar("sagui3d: macaco %d voltou para a mata", i);
        }
        return R_NADA;
    }
    case M_PARADO:
        if (m->tempo == 0) macaco_mudar(m, M_CACANDO, 0);
        return R_NADA;
    case M_RECUANDO: {   /* correndo com a banana roubada */
        int a = angulo_de(m->x - sagui.x, m->y - sagui.y) + m->desvio;
        if (mover(&m->x, &m->y, &m->sala, FX(RAIO_MACACO), 44, cosseno(a) * 4, seno(a) * 4, false))
            m->desvio = (sorteio() & 1) ? 700 : -700;
        if (m->tempo == 0) { m->desvio = 0; macaco_mudar(m, M_QUIETO, 0); }
        return R_NADA;
    }
    default:
        break;
    }

    /* Quieto ou caçando: olha para o sagui a cada 4 tiques (cada macaco num tique diferente). */
    if ((tique + i) % 4 == 0 && d < FX(VISTA_MACACO)) {
        fixo altura = ((sm->chao + FX(30)) + sagui.z) / 2;
        if (linha_livre(m->sala, m->x, m->y, sagui.x, sagui.y, altura)) {
            if (m->estado == M_QUIETO) registrar("sagui3d: macaco %d viu o sagui", i);
            m->alvo_x = sagui.x;
            m->alvo_y = sagui.y;
            m->tem_alvo = true;
            m->estado = M_CACANDO;
        }
    }

    /* Casca no caminho? */
    for (int k = 0; k < mapa.qtd_coisas; k++) {
        coisa_t *c = &mapa.coisas[k];
        if (c->ativa && c->tipo == C_CASCA && distancia(c->x, c->y, m->x, m->y) < FX(18)) {
            c->ativa = false;
            macaco_mudar(m, M_ESCORREGANDO, 130);
            som_escorregou();
            registrar("sagui3d: macaco %d escorregou na casca", i);
            mundo_mostrar_aviso("O macaco-prego escorregou na casca!");
            return R_NADA;
        }
    }

    if (m->estado == M_CACANDO && m->tem_alvo) {
        fixo da = distancia(m->x, m->y, m->alvo_x, m->alvo_y);
        if (da < FX(12)) {
            m->tem_alvo = false;
            m->estado = M_QUIETO;
        } else {
            if (m->tempo == 0) m->desvio = 0;
            m->ang = ANG(angulo_de(m->alvo_x - m->x, m->alvo_y - m->y) + m->desvio);
            fixo vel = FX(34) / 10;
            if (mover(&m->x, &m->y, &m->sala, FX(RAIO_MACACO), 44, fmul(cosseno(m->ang), vel), fmul(seno(m->ang), vel), false)
                && m->tempo == 0) {
                m->desvio = (sorteio() & 1) ? 512 : -512;   /* bateu: contorna por um lado */
                m->tempo = 20;
            }
        }
    }

    /* Encostou no sagui. */
    if (d < FX(26) && sagui.invulneravel == 0) {
        sagui.vidas--;
        sagui.invulneravel = 100;
        som_vida_perdida();
        registrar("sagui3d: perdeu uma vida (vidas %d)", sagui.vidas);
        mundo_mostrar_aviso("O macaco-prego pegou uma banana do seu bolso!");
        m->tem_alvo = false;
        macaco_mudar(m, M_RECUANDO, 90);
        if (sagui.vidas <= 0) return R_FIM_DE_JOGO;
    }
    return R_NADA;
}

/* ------------------------------------------------------------ sementes voando */

static void semente_tique(coisa_t *s)
{
    if (--s->tempo <= 0) { s->ativa = false; return; }
    fixo nx = s->x + s->vx, ny = s->y + s->vy;
    const sala_t *sa = &mapa.salas[s->sala];
    for (int volta = 0; volta < 4; volta++) {
        int saida = -1;
        for (int l = 0; l < sa->n; l++)
            if (distancia_ao_lado(sa, l, nx, ny) < 0) { saida = l; break; }
        if (saida < 0) break;
        const lado_t *d = &sa->lados[saida];
        if (d->vizinha < 0 || d->secreta) { s->ativa = false; return; }   /* bateu na parede */
        const sala_t *n = &mapa.salas[d->vizinha];
        if (s->z <= n->chao || s->z >= n->teto) { s->ativa = false; return; }   /* bateu no degrau */
        s->sala = d->vizinha;
        sa = n;
    }
    if (!dentro_da_sala(s->sala, nx, ny)) { s->ativa = false; return; }
    s->x = nx;
    s->y = ny;
    for (int i = 0; i < mapa.qtd_coisas; i++) {
        coisa_t *m = &mapa.coisas[i];
        if (!m->ativa || m->tipo != C_MACACO || m->estado == M_FUGINDO) continue;
        if (distancia(m->x, m->y, s->x, s->y) < FX(18)) {
            s->ativa = false;
            macaco_acertado(m);
            return;
        }
    }
}

/* ------------------------------------------------------------ tique */

int mundo_tique(const comandos_t *cmd)
{
    tique++;
    if (aviso_tempo > 0) aviso_tempo--;
    if (sagui.invulneravel > 0) sagui.invulneravel--;
    if (sagui.recarga > 0) sagui.recarga--;
    if (sagui.puxado > 0) sagui.puxado--;
    if (espera_porteira > 0) espera_porteira--;

    /* Girar e andar. */
    int giro = 40;
    if (cmd->esquerda) sagui.ang += giro;
    if (cmd->direita) sagui.ang -= giro;
    sagui.ang = ANG(sagui.ang + cmd->giro_mouse);
    fixo vel = cmd->correr ? FX(8) : FX(5), frente = 0;
    if (cmd->frente) frente += vel;
    if (cmd->tras) frente -= vel * 2 / 3;
    if (frente)
        mover(&sagui.x, &sagui.y, &sagui.sala, FX(RAIO_SAGUI), ALTURA_SAGUI,
              fmul(cosseno(sagui.ang), frente), fmul(seno(sagui.ang), frente), true);

    /* O olho acompanha o chão: sobe degraus suave e cai um pouco mais rápido. */
    fixo alvo = mapa.salas[sagui.sala].chao + FX(OLHO);
    if (sagui.z < alvo) sagui.z = alvo - sagui.z > FX(4) ? sagui.z + FX(4) : alvo;
    else if (sagui.z > alvo) sagui.z = sagui.z - alvo > FX(6) ? sagui.z - FX(6) : alvo;

    if (cmd->atirar && sagui.recarga == 0) atirar();
    pegar_coisas();

    int resultado = R_NADA;
    for (int i = 0; i < mapa.qtd_coisas; i++) {
        coisa_t *c = &mapa.coisas[i];
        if (!c->ativa) continue;
        if (c->tipo == C_SEMENTE_VOANDO) semente_tique(c);
        else if (c->tipo == C_MACACO) {
            int r = macaco_tique(c, i);
            if (r != R_NADA) resultado = r;
        }
    }
    if (resultado != R_NADA) return resultado;

    /* A porteira. */
    for (int i = 0; i < mapa.qtd_coisas; i++) {
        coisa_t *c = &mapa.coisas[i];
        if (!c->ativa || c->tipo != C_PORTEIRA) continue;
        if (distancia(c->x, c->y, sagui.x, sagui.y) >= FX(36)) continue;
        if (porteira_aberta()) {
            sagui.pontos += 100;
            som_fase_concluida();
            registrar("sagui3d: fase %d concluida (bananas %d, cacho %s, pontos %d)", fase_atual,
                      sagui.bananas, sagui.cacho ? "sim" : "nao", sagui.pontos);
            return R_FASE_CONCLUIDA;
        }
        if (espera_porteira == 0) {
            char t[64];
            formatar_texto(t, sizeof t, "A porteira pede %d bananas (faltam %d).", mapa.minimo, mapa.minimo - sagui.bananas);
            mundo_mostrar_aviso(t);
            som_porteira_fechada();
            registrar("sagui3d: porteira fechada: %d de %d bananas", sagui.bananas, mapa.minimo);
            espera_porteira = 70;
        }
    }

    if (tique - ultimo_registro >= 15 && (sagui.x != registro_x || sagui.y != registro_y)) {
        registrar("sagui3d: posicao x=%d y=%d sala=%d angulo=%d", INTEIRO(sagui.x), INTEIRO(sagui.y), sagui.sala, sagui.ang);
        registro_x = sagui.x;
        registro_y = sagui.y;
        ultimo_registro = tique;
    }
    return R_NADA;
}
