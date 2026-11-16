/* base64 - codifica bytes quaisquer em letras e números, e volta (H91; a
 * ideia veio do fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa
 * foi reescrito sobre a libsagui atual).
 *
 *   base64 [-w N] [arquivo]       codifica (linhas de 76 letras; -w 0 = uma
 *                                 linha só)
 *   base64 -d [-i] [arquivo]      decodifica (-i ignora o que não for do
 *                                 alfabeto; sem -i, só quebras de linha)
 *
 * Segue a RFC 4648 (seção 4): cada 3 bytes (24 bits) viram 4 letras de 6
 * bits, do alfabeto A-Z a-z 0-9 + /. Se no fim sobram 1 ou 2 bytes, o
 * pedaço é completado com zeros e as letras que faltam viram "=". Assim
 * um arquivo qualquer pode passar por lugares que só aceitam texto. */
#include "sagui.h"

static const char ALFABETO[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int largura = 76, coluna;

/* A saída vai juntando num buffer e é escrita de 4 KB em 4 KB: uma chamada
 * de sistema por letra deixaria um arquivo grande muito lento. */
static char buf_saida[4096];
static unsigned usados;

static void despejar(void)
{
    escrever_tudo(SAIDA, buf_saida, usados);
    usados = 0;
}

static void poe_byte(char c)
{
    if (usados == sizeof buf_saida) despejar();
    buf_saida[usados++] = c;
}

static void poe(char c)
{
    poe_byte(c);
    if (largura && ++coluna == largura) { poe_byte('\n'); coluna = 0; }
}

static void codificar(const uint8_t *b, unsigned n)
{
    for (unsigned i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)b[i] << 16;
        if (i + 1 < n) v |= (uint32_t)b[i + 1] << 8;
        if (i + 2 < n) v |= b[i + 2];
        poe(ALFABETO[v >> 18 & 63]);
        poe(ALFABETO[v >> 12 & 63]);
        poe(i + 1 < n ? ALFABETO[v >> 6 & 63] : '=');
        poe(i + 2 < n ? ALFABETO[v & 63] : '=');
    }
    if (coluna) poe_byte('\n');
    despejar();
}

static int valor(int c)
{
    const char *p = c ? acha_char(ALFABETO, c) : NULL;
    return p ? (int)(p - ALFABETO) : -1;
}

/* Junta 4 letras de cada vez (6 bits cada) em 3 bytes. Um "=" encerra. */
static int decodificar(const uint8_t *b, unsigned n, bool ignorar)
{
    static uint8_t saida[3 * 1024];
    unsigned s = 0;
    uint32_t v = 0;
    int qtd = 0;
    for (unsigned i = 0; i < n; i++) {
        int c = b[i];
        if (c == '\n' || c == '\r') continue;
        if (c == '=') break;
        int x = valor(c);
        if (x < 0) {
            if (ignorar) continue;
            imprimir_em(ERROS, "base64: entrada inválida (letra '%c')\n", c);
            return 1;
        }
        v = v << 6 | (uint32_t)x;
        if (++qtd == 4) {
            saida[s++] = (uint8_t)(v >> 16);
            saida[s++] = (uint8_t)(v >> 8);
            saida[s++] = (uint8_t)v;
            qtd = 0;
            v = 0;
            if (s == sizeof saida) { escrever_tudo(SAIDA, saida, s); s = 0; }
        }
    }
    /* Pedaço final: 2 letras dão 1 byte; 3 letras dão 2. */
    if (qtd == 1) { imprimir_em(ERROS, "base64: entrada inválida (tamanho)\n"); return 1; }
    if (qtd >= 2) saida[s++] = (uint8_t)(v >> (qtd == 2 ? 4 : 10));
    if (qtd == 3) saida[s++] = (uint8_t)(v >> 2);
    escrever_tudo(SAIDA, saida, s);
    return 0;
}

int principal(int argc, char **argv)
{
    bool decodif = false, ignorar = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (compara_texto(argv[i], "--") == 0) { i++; break; }
        for (const char *o = argv[i] + 1; *o; o++) {
            if (*o == 'w') {
                const char *v = o[1] ? o + 1 : (i + 1 < argc ? argv[++i] : NULL);
                if (!v || !converter_numero(v, &largura) || largura < 0) {
                    imprimir_em(ERROS, "base64: largura inválida\n");
                    return 1;
                }
                break;
            }
            if (*o == 'd') decodif = true;
            else if (*o == 'i') ignorar = true;
            else {
                imprimir_em(ERROS, "base64: opção desconhecida -%c\nuso: base64 [-d [-i]] [-w N] [arquivo]\n", *o);
                return 1;
            }
        }
    }
    if (argc - i > 1) { imprimir_em(ERROS, "uso: base64 [-d [-i]] [-w N] [arquivo]\n"); return 1; }
    int fd = abrir_para_ler("base64", i < argc ? argv[i] : "-");
    if (fd < 0) return 1;
    unsigned n;
    uint8_t *b = (uint8_t *)ler_ate_o_fim(fd, &n);
    if (fd != ENTRADA) fechar(fd);
    if (!b) { imprimir_em(ERROS, "base64: memória insuficiente\n"); return 1; }
    if (decodif) return decodificar(b, n, ignorar);
    codificar(b, n);
    return 0;
}
