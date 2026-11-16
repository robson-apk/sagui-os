/* teste_jclic.c - testa o leitor de XML, o projeto e as regras das
 * atividades JClic (H92) no computador de desenvolvimento, com os mesmos
 * xml.c, projeto.c e motor.c do SaguiOS.
 *
 *     teste_jclic projeto.jclic
 * Lê o projeto e resolve cada atividade pelas funções que a janela usa,
 * como um jogador faria (cliques e letras). */
#include <stdio.h>
#include "motor.h"

static int falhas;

static void conferir(int cond, const char *msg)
{
    printf("%s: %s\n", cond ? "ok" : "FALHOU", msg);
    if (!cond) falhas++;
}

static int achar_orig(int p, int orig, int menos)
{
    for (int i = 0; i < M.p[p].n; i++) if (M.p[p].c[i].orig == orig && i != menos) return i;
    return -1;
}

static void resolver(const atividade_t *a)
{
    switch (a->tipo) {
    case AT_MEMORIA:
        for (int par = 0; par < M.alvo; par++) {
            int i = achar_orig(0, par, -1), j = achar_orig(0, par, i);
            motor_clique(0, i);
            motor_clique(0, j);
        }
        break;
    case AT_ASSOCIACAO:
        for (int i = 0; i < M.p[0].n; i++) {
            motor_clique(0, i);
            motor_clique(1, achar_orig(1, M.p[0].c[i].orig, -1));
        }
        break;
    case AT_PUZZLE_TROCA:
        for (int i = 0; i < M.p[0].n; i++)
            if (M.p[0].c[i].orig != i) { motor_clique(0, i); motor_clique(0, achar_orig(0, i, -1)); }
        break;
    case AT_CACA_PALAVRAS:
        for (int w = 0; w < a->n_palavras; w++) {
            int n = (int)strlen(a->palavra[w]);
            for (int y = 0; y < a->gl; y++)
                for (int x = 0; x < a->gc; x++)
                    for (int d = 0; d < 8; d++) {
                        static const int dx[8] = { 1, 0, 1, -1, -1, 0, -1, 1 }, dy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
                        int k = 0;
                        while (k < n) {
                            int xx = x + k * dx[d], yy = y + k * dy[d];
                            if (xx < 0 || yy < 0 || xx >= a->gc || yy >= a->gl || M.letra[yy][xx] != a->palavra[w][k]) break;
                            k++;
                        }
                        if (k == n && !M.achou[w]) {
                            motor_clique_casa(x, y);
                            motor_clique_casa(x + (n - 1) * dx[d], y + (n - 1) * dy[d]);
                        }
                    }
        }
        break;
    case AT_CRUZADINHA:
        for (int y = 0; y < a->gl; y++)
            for (int x = 0; x < a->gc; x++)
                if (a->grade[y][x] != '*') { M.foco_x = -1; motor_clique_casa(x, y); motor_letra(a->grade[y][x] | 32); }
        break;
    default: break;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "uso: teste_jclic projeto.jclic\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    char *xml = malloc(1 << 20);
    int n = (int)fread(xml, 1, (1 << 20) - 1, f);
    fclose(f);

    /* 1. O leitor de XML: entidades, UTF-8 para Latin-1, comentários. */
    char t1[] = "<?xml version=\"1.0\"?><!-- c --><a x='1 &amp; 2'><b>p&#233; &lt;&gt;</b><c/><b>s\xc3\xa3o</b></a>";
    char erro[120];
    xno_t *r = xml_ler(t1, (int)strlen(t1), erro, sizeof erro);
    xno_t *b2 = r ? xml_proximo(xml_filho(r, "b"), "b") : NULL;
    conferir(r && !strcmp(xml_atr(r, "x"), "1 & 2") && !strcmp(xml_filho(r, "b")->texto, "p\xe9 <>") && xml_filho(r, "c") &&
             b2 && !strcmp(b2->texto, "s\xe3o"), "XML: atributos, entidades, &#233; e UTF-8 viram Latin-1");
    xml_liberar(r);
    char t2[] = "<a><b></a>";
    r = xml_ler(t2, (int)strlen(t2), erro, sizeof erro);
    xml_liberar(r);
    char t3[] = "<a x=1/>";
    conferir(!xml_ler(t3, (int)strlen(t3), erro, sizeof erro) && strstr(erro, "aspas"), "XML: valor sem aspas é erro");

    /* 2. O projeto de exemplo. */
    static projeto_t p;
    conferir(projeto_ler(&p, xml, n, erro, sizeof erro) == 0 && !strcmp(p.titulo, "O sagui e a banana") && p.n == 6,
             "o projeto de exemplo tem título e 6 atividades na sequência");
    static const int tipos[6] = { AT_INFO, AT_MEMORIA, AT_ASSOCIACAO, AT_PUZZLE_TROCA, AT_CACA_PALAVRAS, AT_CRUZADINHA };
    for (int k = 0; k < p.n; k++) {
        static atividade_t a;
        projeto_atividade(&p, k, &a);
        char msg[200];
        snprintf(msg, sizeof msg, "atividade %d \"%s\" é %s", k, a.nome, nome_do_tipo(a.tipo));
        conferir(a.tipo == tipos[k], msg);
        motor_iniciar(&a, 1234 + (uint32_t)k);
        motor_dispor(0, 0, 800, 460);
        if (a.tipo == AT_INFO) {
            conferir(M.p[0].n == 1 && a.a.c[0].texto && strstr(a.a.c[0].texto, "Eu sou o sagui"), "tela de informação com o texto");
        } else {
            if (a.tipo == AT_MEMORIA) {
                /* Um erro: duas cartas que não são par voltam a virar depois do tempo. */
                int i = 0, j = 1;
                while (M.p[0].c[j].orig == M.p[0].c[i].orig) j++;
                motor_clique(0, i);
                motor_clique(0, j);
                int virou = M.som == SOM_ERRO && M.p[0].c[i].est == CX_NORMAL;
                motor_tique(1000);
                motor_tique(3000);
                conferir(virou && M.p[0].c[i].est == CX_VIRADA && M.p[0].c[j].est == CX_VIRADA,
                         "memória: um par errado volta a ficar virado depois de um tempo");
            }
            if (a.tipo == AT_ASSOCIACAO) {
                int j = achar_orig(1, M.p[0].c[0].orig, -1);
                motor_clique(0, 0);
                motor_clique(1, (j + 1) % M.p[1].n);
                conferir(M.som == SOM_ERRO && M.acertos == 0, "associação errada não conta");
            }
            if (a.tipo == AT_CRUZADINHA) {
                motor_clique_casa(2, 2);
                const char *pista = motor_pista();
                motor_clique_casa(3, 4);
                const char *pista2 = motor_pista();
                conferir(pista && strstr(pista, "sagui pula") && pista2 && strstr(pista2, "Doce"),
                         "cruzadinha: a pista segue a palavra e o sentido do cursor");
            }
            resolver(&a);
            snprintf(msg, sizeof msg, "atividade \"%s\" resolvida pelas regras (%d de %d acertos)", a.nome, M.acertos, M.alvo);
            conferir(M.resolvida && M.som == SOM_FIM, msg);
        }
        atividade_liberar(&a);
    }
    projeto_liberar(&p);

    /* 3. Os tipos que o exemplo não usa, com atividades montadas aqui. */
    static celula_t cels[6], cels_b[3];
    static atividade_t s;
    for (int k = 0; k < 6; k++) { cels[k].id = -1; cels[k].texto = "x"; }
    for (int k = 0; k < 3; k++) { cels_b[k].id = -1; cels_b[k].texto = "y"; }
    grupo_t ga = { true, 3, 2, 50, 50, NULL, true, 0, 0xFFFFFF, 6, cels };
    grupo_t gb = { true, 3, 1, 50, 50, NULL, true, 0, 0xFFFFFF, 3, cels_b };

    memset(&s, 0, sizeof s);
    s.tipo = AT_PUZZLE_DUPLO; s.a = ga; strcpy(s.disposicao, "AB");
    motor_iniciar(&s, 7);
    motor_dispor(0, 0, 800, 400);
    motor_clique(0, achar_orig(0, 0, -1));
    motor_clique(1, 1);
    int errou = M.som == SOM_ERRO && M.acertos == 0;
    for (int k = 0; k < 6; k++) { motor_clique(0, achar_orig(0, k, -1)); motor_clique(1, k); }
    conferir(errou && M.resolvida, "quebra-cabeça duplo: peça no lugar errado não fica; todas no lugar resolve");

    s.tipo = AT_PUZZLE_BURACO;
    motor_iniciar(&s, 9);
    motor_dispor(0, 0, 800, 400);
    int legal = 1;
    for (int k = 0; k < 6; k++) if (M.p[0].c[k].est == CX_VAZIA && k != M.buraco) legal = 0;
    /* Monta à mão o quebra-cabeça a uma jogada do fim: tudo no lugar, o
     * buraco na casa 4 e a última peça na 5. */
    for (int k = 0; k < 6; k++) { M.p[0].c[k].cel = M.p[0].c[k].orig = k; M.p[0].c[k].est = CX_NORMAL; }
    M.p[0].c[4].est = CX_VAZIA; M.p[0].c[4].orig = 5; M.p[0].c[5].orig = 4; M.p[0].c[5].cel = 4; M.buraco = 4;
    motor_clique(0, 0);                       /* longe do buraco: não anda */
    int longe = M.som == SOM_ERRO && M.buraco == 4;
    motor_clique(0, 5);
    conferir(legal && longe && M.resolvida && M.buraco == 5, "quebra-cabeça de buraco: só a vizinha do buraco anda; no lugar resolve");

    s.tipo = AT_ASSOCIACAO_COMPLEXA; s.b = gb;
    for (int k = 0; k < 6; k++) cels[k].id = k % 3;   /* duas de A para cada uma de B */
    motor_iniciar(&s, 3);
    motor_dispor(0, 0, 800, 400);
    motor_clique(0, 0);
    motor_clique(1, achar_orig(1, 1, -1));
    errou = M.som == SOM_ERRO;
    for (int k = 0; k < 6; k++) { motor_clique(0, k); motor_clique(1, achar_orig(1, cels[M.p[0].c[k].cel].id, -1)); }
    conferir(errou && M.resolvida && M.acertos == 6, "associação complexa: várias de A vão para a mesma de B pelo id");

    s.tipo = AT_IDENTIFICAR;
    for (int k = 0; k < 6; k++) cels[k].id = k == 2 || k == 4 ? 1 : 0;
    motor_iniciar(&s, 3);
    motor_clique(0, 0);
    errou = M.som == SOM_ERRO;
    motor_clique(0, 2);
    motor_clique(0, 4);
    conferir(errou && M.resolvida && M.alvo == 2, "identificar: só as células certas contam");

    s.tipo = AT_EXPLORAR;
    motor_iniciar(&s, 3);
    motor_dispor(0, 0, 800, 400);
    motor_clique(0, 1);
    conferir(M.p[1].n == 1 && M.p[1].c[0].cel == 1, "explorar: clicar em A mostra a célula de B");
    return falhas ? 1 : 0;
}
