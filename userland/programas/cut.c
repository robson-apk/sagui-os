/* cut (cortar) - tira pedaços de cada linha: campos ou colunas (H91; a ideia
 * veio do fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa foi
 * reescrito sobre a libsagui atual).
 *
 *   cut -f LISTA [-d C] [-s] [arquivo...]
 *   cut -c LISTA [arquivo...]        (-b é o mesmo que -c)
 *
 *   -f LISTA  os campos da lista, separados pelo delimitador;
 *   -d C      o delimitador dos campos (o padrão é o Tab);
 *   -s        com -f, pula as linhas que não têm o delimitador (sem -s,
 *             elas saem inteiras);
 *   -c LISTA  as colunas (letras) da lista.
 * A LISTA tem números e intervalos separados por vírgula, contando do 1:
 * "2", "1,3", "2-4", "3-" (do 3 ao fim), "-2" (do começo ao 2).
 *
 * Segue a descrição do cut na norma POSIX (IEEE Std 1003.1). O SaguiOS
 * guarda o texto em Latin-1, uma letra por byte, então colunas e bytes são
 * a mesma coisa. */
#include "sagui.h"

#define MAX_INTERVALOS 32
#define INFINITO 0x7FFFFFFF

static int de[MAX_INTERVALOS], ate[MAX_INTERVALOS], qtd_intervalos;
static bool por_campos, so_com_delimitador;
static char delimitador = '\t';

/* Lê um número da lista; devolve o ponteiro depois dele (ou NULL). */
static const char *numero(const char *s, int *v)
{
    if (*s < '0' || *s > '9') return NULL;
    *v = 0;
    while (*s >= '0' && *s <= '9') *v = *v * 10 + (*s++ - '0');
    return *v > 0 ? s : NULL;
}

static bool ler_lista(const char *s)
{
    qtd_intervalos = 0;
    while (*s) {
        if (qtd_intervalos == MAX_INTERVALOS) return false;
        int a = 1, b;
        if (*s != '-' && !(s = numero(s, &a))) return false;
        b = a;
        if (*s == '-') {
            s++;
            b = INFINITO;
            if (*s && *s != ',' && !(s = numero(s, &b))) return false;
        }
        if (b < a) return false;
        de[qtd_intervalos] = a;
        ate[qtd_intervalos++] = b;
        if (*s == ',') s++;
        else if (*s) return false;
    }
    return qtd_intervalos > 0;
}

static bool escolhido(int posicao)
{
    for (int i = 0; i < qtd_intervalos; i++)
        if (posicao >= de[i] && posicao <= ate[i]) return true;
    return false;
}

/* Corta uma linha (sem o '\n') e escreve o resultado. Os pedaços saem na
 * ordem da linha, não na da lista, como manda a norma. */
static void cortar(const char *l, unsigned n)
{
    static char saida[2048];
    unsigned s = 0;
    if (!por_campos) {
        for (unsigned i = 0; i < n && s < sizeof saida; i++)
            if (escolhido((int)i + 1)) saida[s++] = l[i];
    } else {
        bool tem = false;
        for (unsigned i = 0; i < n; i++) if (l[i] == delimitador) { tem = true; break; }
        if (!tem) {
            if (!so_com_delimitador) { escrever_tudo(SAIDA, l, n); escrever_tudo(SAIDA, "\n", 1); }
            return;
        }
        int campo = 1;
        bool primeiro = true;
        unsigned ini = 0;
        for (unsigned i = 0; i <= n; i++) {
            if (i < n && l[i] != delimitador) continue;
            if (escolhido(campo)) {
                if (!primeiro && s < sizeof saida) saida[s++] = delimitador;
                for (unsigned k = ini; k < i && s < sizeof saida; k++) saida[s++] = l[k];
                primeiro = false;
            }
            campo++;
            ini = i + 1;
        }
    }
    escrever_tudo(SAIDA, saida, s);
    escrever_tudo(SAIDA, "\n", 1);
}

static int uso(void)
{
    imprimir_em(ERROS, "uso: cut -f LISTA [-d C] [-s] [arquivo...]  ou  cut -c LISTA [arquivo...]\n");
    return 1;
}

int principal(int argc, char **argv)
{
    bool tem_lista = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        for (const char *o = argv[i] + 1; *o; o++) {
            if (*o == 'f' || *o == 'c' || *o == 'b' || *o == 'd') {
                const char *v = o[1] ? o + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!v) return uso();
                if (*o == 'd') {
                    if (!v[0] || v[1]) { imprimir_em(ERROS, "cut: o delimitador deve ser uma letra só\n"); return 1; }
                    delimitador = v[0];
                } else {
                    if (!ler_lista(v)) { imprimir_em(ERROS, "cut: lista inválida: %s\n", v); return 1; }
                    por_campos = *o == 'f';
                    tem_lista = true;
                }
                break;
            }
            if (*o == 's') so_com_delimitador = true;
            else { imprimir_em(ERROS, "cut: opção desconhecida -%c\n", *o); return uso(); }
        }
    }
    if (!tem_lista) return uso();
    int arquivos = argc - i, erros = 0;
    for (int k = 0; k < (arquivos ? arquivos : 1); k++) {
        int fd = abrir_para_ler("cut", arquivos ? argv[i + k] : "-");
        if (fd < 0) { erros++; continue; }
        unsigned n;
        char *b = ler_ate_o_fim(fd, &n);
        if (fd != ENTRADA) fechar(fd);
        if (!b) { imprimir_em(ERROS, "cut: memória insuficiente\n"); return 1; }
        n = (unsigned)utf8_para_latin1(b, n, NULL);
        for (unsigned p = 0; p < n;) {
            unsigned f = p;
            while (f < n && b[f] != '\n') f++;
            unsigned t = f;
            if (t > p && b[t - 1] == '\r') t--;
            cortar(b + p, t - p);
            p = f + 1;
        }
        libera(b);
    }
    return erros ? 1 : 0;
}
