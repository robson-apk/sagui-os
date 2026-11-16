/* entrada.c - leitura de linhas e de arquivos inteiros. */
#include "sagui.h"

/* Lê até o fim da linha (o '\n' não entra no buffer). Devolve o tamanho
 * da linha, ou -1 se o arquivo acabou antes de ler qualquer coisa. Lê um
 * byte por vez: assim nunca consome o que vem depois da linha, o que
 * importa quando outro programa vai ler o resto do mesmo pipe. */
int ler_linha(int fd, char *buf, unsigned max)
{
    unsigned n = 0;
    for (;;) {
        char c;
        int r = ler(fd, &c, 1);
        if (r <= 0) {
            if (n == 0) return -1;
            break;
        }
        if (c == '\n') break;
        if (n + 1 < max) buf[n++] = c;
    }
    buf[n] = 0;
    return (int)n;
}

/* ---- ajudas dos utilitários de texto (H91) ----
 * head, sort, cut e companhia fazem sempre as mesmas coisas: abrir um
 * arquivo (ou a entrada, se o nome for "-"), ler tudo e conferir números
 * dados nas opções. Aqui elas ficam uma vez só. */

/* Abre para ler. "-" quer dizer a entrada (como nos sistemas Unix: assim
 * "cat a.txt | diff - b.txt" funciona). Em caso de erro, já avisa no
 * formato "programa: nome: motivo" e devolve o erro (negativo). Pastas são
 * recusadas aqui, porque ler uma pasta como texto não faz sentido. */
int abrir_para_ler(const char *programa, const char *nome)
{
    if (nome[0] == '-' && nome[1] == 0) return ENTRADA;
    info_arquivo_t inf;
    if (info_arquivo(nome, &inf) == 0 && inf.tipo == TIPO_PASTA) {
        imprimir_em(ERROS, "%s: %s é uma pasta\n", programa, nome);
        return ERRO_ACESSO;
    }
    int fd = abrir(nome, ABRIR_LER);
    if (fd < 0) imprimir_em(ERROS, "%s: %s: %s\n", programa, nome, nome_erro(fd));
    return fd;
}

/* Lê até o fim, em blocos, num buffer que cresce conforme precisa. Um ler()
 * num pipe pode devolver menos do que foi pedido (só o que já chegou), por
 * isso o laço continua até ler() dizer que acabou. O buffer termina com um
 * zero a mais (não contado em *tam), para poder ser usado como texto.
 * Devolve NULL se faltar memória; quem chama libera com libera(). */
char *ler_ate_o_fim(int fd, unsigned *tam)
{
    unsigned cap = 4096, n = 0;
    char *b = aloca(cap);
    if (!b) return NULL;
    for (;;) {
        if (cap - n < 2048) {
            char *novo = realoca(b, cap * 2);
            if (!novo) { libera(b); return NULL; }
            b = novo;
            cap *= 2;
        }
        int r = ler(fd, b + n, cap - n - 1);
        if (r <= 0) break;
        n += (unsigned)r;
    }
    b[n] = 0;
    if (tam) *tam = n;
    return b;
}

/* Converte um número inteiro escrito em decimal, com sinal opcional, e
 * diz se o texto era mesmo um número ("12a" e "" não são). Serve para
 * conferir opções como "head -n 5". */
bool converter_numero(const char *s, int *valor)
{
    int sinal = 1;
    unsigned v = 0;
    if (*s == '-') { sinal = -1; s++; } else if (*s == '+') s++;
    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (unsigned)(*s++ - '0');
        if (v > 0x7FFFFFFF) return false;
    }
    if (*s) return false;
    *valor = (int)v * sinal;
    return true;
}

/* Minúscula de uma letra Latin-1: as maiúsculas acentuadas (À a Þ) ficam 32
 * posições antes das minúsculas, como no ASCII; a exceção é o × (0xD7),
 * que não é letra. */
int minuscula_latin1(int c)
{
    c &= 0xFF;
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    return c;
}
