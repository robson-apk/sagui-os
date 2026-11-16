/* find (procurar-arquivo) - procura arquivos e pastas pelo nome, tipo ou
 * tamanho, descendo pelas subpastas (H91; a ideia veio do fork
 * SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi reescrito
 * sobre a libsagui atual).
 *
 *   find [pasta...] [condições]          (sem pasta, procura em .)
 *
 *   -name PADRÃO   o nome combina com o padrão: * (qualquer coisa),
 *                  ? (uma letra), [abc] (uma destas); -iname ignora
 *                  maiúsculas. Use aspas: find /usb -name "*.txt";
 *   -type f|d      só arquivos (f) ou só pastas (d);
 *   -size [+|-]N[c|k]  tamanho maior (+), menor (-) ou igual a N; c = bytes,
 *                  k = KB, sem letra = blocos de 512 bytes, como na norma;
 *   -empty         arquivos com 0 bytes e pastas vazias;
 *   -maxdepth N / -mindepth N   até / a partir de que profundidade
 *                  (0 é a própria pasta dada);
 *   ! ou -not      inverte a condição seguinte.
 * Todas as condições precisam valer ao mesmo tempo. Cada achado sai numa
 * linha, com o caminho a partir da pasta dada. Segue a descrição do find na
 * norma POSIX (IEEE Std 1003.1), sem -o, parênteses e -exec. */
#include "sagui.h"

#define MAX_CONDICOES 16
#define CAMINHO_MAX   512

enum { C_NOME, C_NOME_I, C_TIPO, C_TAMANHO, C_VAZIO };

typedef struct {
    int         tipo;
    bool        negar;
    const char *texto;
    int         sinal;      /* -size: -1 menor, 0 igual, +1 maior */
    unsigned    valor;      /* -size: em bytes; arredondamento pela unidade */
    unsigned    unidade;
} condicao_t;

static condicao_t cond[MAX_CONDICOES];
static int qtd_cond, prof_max = 1000, prof_min;
static bool erro;

/* O nome combina com o padrão? '*' tenta engolir 0, 1, 2... letras até o
 * resto combinar; '?' aceita uma letra qualquer; [...] aceita uma letra da
 * lista (com intervalos a-z; [!...] ou [^...] inverte). */
static bool combina(const char *p, const char *s, bool sem_caixa)
{
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (p[1] == '*') p++;
            for (const char *t = s;; t++) {
                if (combina(p + 1, t, sem_caixa)) return true;
                if (!*t) return false;
            }
        }
        if (!*s) return false;
        int c = sem_caixa ? minuscula_latin1((unsigned char)*s) : (unsigned char)*s;
        if (*p == '?') continue;
        if (*p == '[') {
            const char *q = p + 1;
            bool inverter = *q == '!' || *q == '^';
            if (inverter) q++;
            bool achou = false;
            for (bool primeiro = true; *q && (*q != ']' || primeiro); q++, primeiro = false) {
                int a = sem_caixa ? minuscula_latin1((unsigned char)*q) : (unsigned char)*q, b = a;
                if (q[1] == '-' && q[2] && q[2] != ']') {
                    b = sem_caixa ? minuscula_latin1((unsigned char)q[2]) : (unsigned char)q[2];
                    q += 2;
                }
                if (c >= a && c <= b) achou = true;
            }
            if (*q != ']') return false;   /* colchete sem fechar: padrão inválido */
            if (achou == inverter) return false;
            p = q;
            continue;
        }
        int d = sem_caixa ? minuscula_latin1((unsigned char)*p) : (unsigned char)*p;
        if (c != d) return false;
    }
    return !*s;
}

static bool pasta_vazia(const char *caminho)
{
    entrada_pasta_t e;
    return listar(caminho, 0, &e) != 1;
}

static bool aceita(const char *caminho, const char *nome, int tipo, unsigned tamanho)
{
    for (int i = 0; i < qtd_cond; i++) {
        condicao_t *c = &cond[i];
        bool v = false;
        if (c->tipo == C_NOME || c->tipo == C_NOME_I) v = combina(c->texto, nome, c->tipo == C_NOME_I);
        else if (c->tipo == C_TIPO) v = (c->texto[0] == 'd') == (tipo == TIPO_PASTA);
        else if (c->tipo == C_VAZIO) v = tipo == TIPO_PASTA ? pasta_vazia(caminho) : tamanho == 0;
        else if (c->tipo == C_TAMANHO) {
            /* Como na norma, o tamanho é arredondado para cima na unidade:
             * com -size 1k, um arquivo de 10 bytes "tem" 1 KB. */
            unsigned em_unidades = (tamanho + c->unidade - 1) / c->unidade;
            v = c->sinal > 0 ? em_unidades > c->valor : c->sinal < 0 ? em_unidades < c->valor : em_unidades == c->valor;
        }
        if (v == c->negar) return false;
    }
    return true;
}

static void percorrer(const char *caminho, const char *nome, int tipo, unsigned tamanho, int prof)
{
    if (prof >= prof_min && aceita(caminho, nome, tipo, tamanho)) imprimir("%s\n", caminho);
    if (tipo != TIPO_PASTA || prof >= prof_max) return;
    entrada_pasta_t e;
    int r;
    for (int i = 0; (r = listar(caminho, i, &e)) == 1; i++) {
        char filho[CAMINHO_MAX];
        size_t t = tam_texto(caminho);
        formatar_texto(filho, sizeof filho, "%s%s%s", caminho, t && caminho[t - 1] == '/' ? "" : "/", e.nome);
        percorrer(filho, e.nome, (int)e.tipo, e.tamanho, prof + 1);
    }
    if (r < 0) { imprimir_em(ERROS, "find: %s: %s\n", caminho, nome_erro(r)); erro = true; }
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: find [pasta...] [-name P] [-iname P] [-type f|d] [-size [+-]N[ck]] [-empty]\n"
                       "            [-maxdepth N] [-mindepth N] [! condição]\n");
    return 1;
}

int principal(int argc, char **argv)
{
    int i = 1;
    while (i < argc && argv[i][0] != '-' && compara_texto(argv[i], "!") != 0) i++;
    int primeira_pasta = 1, qtd_pastas = i - 1;
    bool negar = false;
    for (; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (compara_texto(a, "!") == 0 || compara_texto(a, "-not") == 0) { negar = !negar; continue; }
        if (compara_texto(a, "-print") == 0) continue;   /* já é o que o find faz */
        if (qtd_cond == MAX_CONDICOES) { imprimir_em(ERROS, "find: condições demais\n"); return 1; }
        condicao_t *c = &cond[qtd_cond];
        c->negar = negar;
        negar = false;
        if (compara_texto(a, "-empty") == 0) { c->tipo = C_VAZIO; qtd_cond++; continue; }
        if (!v) { imprimir_em(ERROS, "find: falta o valor de %s\n", a); return 1; }
        i++;
        if (compara_texto(a, "-name") == 0) { c->tipo = C_NOME; c->texto = v; }
        else if (compara_texto(a, "-iname") == 0) { c->tipo = C_NOME_I; c->texto = v; }
        else if (compara_texto(a, "-type") == 0) {
            if ((v[0] != 'f' && v[0] != 'd') || v[1]) { imprimir_em(ERROS, "find: -type aceita f ou d\n"); return 1; }
            c->tipo = C_TIPO;
            c->texto = v;
        } else if (compara_texto(a, "-size") == 0) {
            c->tipo = C_TAMANHO;
            c->sinal = *v == '+' ? 1 : *v == '-' ? -1 : 0;
            if (*v == '+' || *v == '-') v++;
            c->valor = 0;
            if (*v < '0' || *v > '9') { imprimir_em(ERROS, "find: tamanho inválido\n"); return 1; }
            while (*v >= '0' && *v <= '9') c->valor = c->valor * 10 + (unsigned)(*v++ - '0');
            c->unidade = *v == 'c' ? 1 : *v == 'k' ? 1024 : *v == 'M' ? 1024 * 1024 : 512;
            if (*v && v[1]) { imprimir_em(ERROS, "find: tamanho inválido\n"); return 1; }
        } else if (compara_texto(a, "-maxdepth") == 0 || compara_texto(a, "-mindepth") == 0) {
            int n;
            if (!converter_numero(v, &n) || n < 0) { imprimir_em(ERROS, "find: profundidade inválida: %s\n", v); return 1; }
            if (a[2] == 'a') prof_max = n; else prof_min = n;
            continue;
        } else {
            imprimir_em(ERROS, "find: condição desconhecida: %s\n", a);
            return uso();
        }
        qtd_cond++;
    }
    static char ponto[] = ".";
    char *padrao[1] = { ponto };
    char **pastas = qtd_pastas ? argv + primeira_pasta : padrao;
    for (int k = 0; k < (qtd_pastas ? qtd_pastas : 1); k++) {
        info_arquivo_t inf;
        int r = info_arquivo(pastas[k], &inf);
        if (r < 0) { imprimir_em(ERROS, "find: %s: %s\n", pastas[k], nome_erro(r)); erro = true; continue; }
        const char *nome = acha_ultimo_char(pastas[k], '/');
        nome = nome && nome[1] ? nome + 1 : pastas[k];
        percorrer(pastas[k], nome, (int)inf.tipo, inf.tamanho, 0);
    }
    return erro ? 1 : 0;
}
