/* tree (arvore) - desenha as pastas e os arquivos como uma árvore (H91; a
 * ideia veio do fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa
 * foi reescrito sobre a libsagui atual).
 *
 *   tree [-a] [-d] [-L N] [pasta...]
 *
 *   -a    mostra também os nomes que começam com ponto (escondidos);
 *   -d    só as pastas;
 *   -L N  desce no máximo N níveis.
 *
 * Cada nível é desenhado com "|-- " (tem mais itens depois) ou "`-- " (o
 * último); embaixo de um item que não é o último, a linha "|" continua.
 * Os nomes de cada pasta saem em ordem alfabética, e no fim vem a conta de
 * pastas e arquivos. Só letras ASCII: funciona em qualquer terminal. */
#include "sagui.h"

#define CAMINHO_MAX 512

static bool escondidos, so_pastas;
static int nivel_max = 1000, qtd_pastas, qtd_arquivos;

static bool visivel(const entrada_pasta_t *e)
{
    if (!escondidos && e->nome[0] == '.') return false;
    return !so_pastas || e->tipo == TIPO_PASTA;
}

/* Lê os itens de uma pasta numa lista e ordena por nome (por inserção:
 * pastas costumam ter poucos itens). Devolve a quantidade, ou -1. */
static int ler_pasta(const char *caminho, entrada_pasta_t **lista)
{
    int cap = 16, n = 0, r;
    entrada_pasta_t *v = aloca((size_t)cap * sizeof *v), e;
    if (!v) return -1;
    for (int i = 0; (r = listar(caminho, i, &e)) == 1; i++) {
        if (!visivel(&e)) continue;
        if (n == cap) {
            entrada_pasta_t *novo = realoca(v, (size_t)cap * 2 * sizeof *v);
            if (!novo) break;
            v = novo;
            cap *= 2;
        }
        int k = n++;
        while (k > 0 && compara_texto(v[k - 1].nome, e.nome) > 0) { v[k] = v[k - 1]; k--; }
        v[k] = e;
    }
    if (r < 0) { libera(v); return r; }
    *lista = v;
    return n;
}

/* "prefixo" é o desenho das linhas dos níveis de cima ("|   " ou "    "). */
static void desenhar(const char *caminho, char *prefixo, int nivel)
{
    entrada_pasta_t *v;
    int n = ler_pasta(caminho, &v);
    if (n < 0) { imprimir("%s`-- [não deu para ler: %s]\n", prefixo, nome_erro(n)); return; }
    size_t tp = tam_texto(prefixo);
    for (int i = 0; i < n; i++) {
        bool ultimo = i == n - 1, pasta = v[i].tipo == TIPO_PASTA;
        imprimir("%s%s%s%s\n", prefixo, ultimo ? "`-- " : "|-- ", v[i].nome, pasta ? "/" : "");
        if (pasta) qtd_pastas++; else qtd_arquivos++;
        if (pasta && nivel < nivel_max && tp + 5 < CAMINHO_MAX) {
            char filho[CAMINHO_MAX];
            size_t t = tam_texto(caminho);
            formatar_texto(filho, sizeof filho, "%s%s%s", caminho, t && caminho[t - 1] == '/' ? "" : "/", v[i].nome);
            copia_texto(prefixo + tp, ultimo ? "    " : "|   ", CAMINHO_MAX - tp);
            desenhar(filho, prefixo, nivel + 1);
            prefixo[tp] = 0;
        }
    }
    libera(v);
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: tree [-a] [-d] [-L N] [pasta...]\n");
    return 1;
}

int principal(int argc, char **argv)
{
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        for (const char *o = argv[i] + 1; *o; o++) {
            if (*o == 'L') {
                const char *v = o[1] ? o + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!v || !converter_numero(v, &nivel_max) || nivel_max < 1) {
                    imprimir_em(ERROS, "tree: -L precisa de um número maior que zero\n");
                    return 1;
                }
                break;
            }
            if (*o == 'a') escondidos = true;
            else if (*o == 'd') so_pastas = true;
            else { imprimir_em(ERROS, "tree: opção desconhecida -%c\n", *o); return uso(); }
        }
    }
    int erros = 0;
    for (int k = i; k < (i < argc ? argc : i + 1); k++) {
        const char *pasta = i < argc ? argv[k] : ".";
        info_arquivo_t inf;
        int r = info_arquivo(pasta, &inf);
        if (r < 0 || inf.tipo != TIPO_PASTA) {
            imprimir_em(ERROS, "tree: %s: %s\n", pasta, r < 0 ? nome_erro(r) : "não é uma pasta");
            erros++;
            continue;
        }
        static char prefixo[CAMINHO_MAX];
        prefixo[0] = 0;
        imprimir("%s\n", pasta);
        desenhar(pasta, prefixo, 1);
    }
    imprimir("\n%d %s, %d %s\n", qtd_pastas, qtd_pastas == 1 ? "pasta" : "pastas",
             qtd_arquivos, qtd_arquivos == 1 ? "arquivo" : "arquivos");
    return erros ? 1 : 0;
}
