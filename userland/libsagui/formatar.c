/* formatar.c - imprimir e formatar texto nos programas.
 *
 * Mesma ideia do formatador do kernel: cada número é escrito de trás para
 * frente num buffer pequeno, e uma rotina única cuida da largura e do
 * alinhamento. */
#include "sagui.h"

typedef struct { char *d; size_t max, pos; } destino_t;

static void poe(destino_t *s, char c)
{
    if (s->pos + 1 < s->max) s->d[s->pos] = c;
    s->pos++;
}

static void campo(destino_t *s, const char *t, size_t n, int larg, bool esq, char pre)
{
    int falta = larg > (int)n ? larg - (int)n : 0;
    if (!esq && pre == '0' && n && *t == '-') { poe(s, '-'); t++; n--; }
    if (!esq) while (falta--) poe(s, pre);
    while (n--) poe(s, *t++);
    if (esq) while (falta-- > 0) poe(s, ' ');
}

static char *digitos(char *fim, uint32_t v, unsigned base)
{
    *fim = 0;
    do { *--fim = "0123456789abcdef"[v % base]; v /= base; } while (v);
    return fim;
}

int formatar_lista(char *dst, size_t max, const char *fmt, va_list ap)
{
    destino_t s = { dst, max, 0 };
    char buf[16];
    for (; *fmt; fmt++) {
        if (*fmt != '%') { poe(&s, *fmt); continue; }
        fmt++;
        bool esq = false;
        char pre = ' ';
        int larg = 0;
        for (;; fmt++) {
            if (*fmt == '-') esq = true;
            else if (*fmt == '0') pre = '0';
            else break;
        }
        if (*fmt == '*') { larg = va_arg(ap, int); if (larg < 0) { esq = true; larg = -larg; } fmt++; }
        while (*fmt >= '0' && *fmt <= '9') larg = larg * 10 + (*fmt++ - '0');
        while (*fmt == 'l') fmt++;
        char *t;
        switch (*fmt) {
        case 's': t = va_arg(ap, char *); if (!t) t = "(nulo)"; campo(&s, t, tam_texto(t), larg, esq, ' '); break;
        case 'c': buf[0] = (char)va_arg(ap, int); campo(&s, buf, 1, larg, esq, ' '); break;
        case 'd': case 'i': {
            int v = va_arg(ap, int);
            t = digitos(buf + 15, v < 0 ? 0u - (uint32_t)v : (uint32_t)v, 10);
            if (v < 0) *--t = '-';
            campo(&s, t, tam_texto(t), larg, esq, pre);
            break;
        }
        case 'u': t = digitos(buf + 15, va_arg(ap, uint32_t), 10); campo(&s, t, tam_texto(t), larg, esq, pre); break;
        case 'x': t = digitos(buf + 15, va_arg(ap, uint32_t), 16); campo(&s, t, tam_texto(t), larg, esq, pre); break;
        case 'p': t = digitos(buf + 15, (uint32_t)va_arg(ap, void *), 16); poe(&s, '0'); poe(&s, 'x'); campo(&s, t, tam_texto(t), 8, false, '0'); break;
        case '%': poe(&s, '%'); break;
        case 0: fmt--; break;
        default: poe(&s, '%'); poe(&s, *fmt);
        }
    }
    if (max) dst[s.pos < max ? s.pos : max - 1] = 0;
    return (int)s.pos;
}

int formatar_texto(char *dst, size_t max, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = formatar_lista(dst, max, fmt, ap);
    va_end(ap);
    return n;
}

/* Um pipe ou o terminal podem aceitar menos bytes do que pedimos: insiste
 * até escrever tudo (ou dar erro). */
int escrever_tudo(int fd, const void *buf, unsigned n)
{
    const char *p = buf;
    unsigned feito = 0;
    while (feito < n) {
        int r = escrever(fd, p + feito, n - feito);
        if (r < 0) return r;
        if (r == 0) break;
        feito += (unsigned)r;
    }
    return (int)feito;
}

int escrever_texto(int fd, const char *s)
{
    return escrever_tudo(fd, s, tam_texto(s));
}

static int imprimir_lista(int fd, const char *fmt, va_list ap)
{
    char buf[512];
    int n = formatar_lista(buf, sizeof buf, fmt, ap);
    if (n >= (int)sizeof buf) n = sizeof buf - 1;
    return escrever_tudo(fd, buf, (unsigned)n);
}

int imprimir(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = imprimir_lista(SAIDA, fmt, ap);
    va_end(ap);
    return n;
}

int imprimir_em(int fd, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = imprimir_lista(fd, fmt, ap);
    va_end(ap);
    return n;
}

/* Uma linha no log do sistema (serial), sem aparecer na tela. */
void registrar(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    formatar_lista(buf, sizeof buf, fmt, ap);
    va_end(ap);
    size_t n = tam_texto(buf);
    if (n && buf[n - 1] == '\n') buf[n - 1] = 0;
    registrar_texto(buf);
}

const char *nome_erro(int e)
{
    switch (e) {
    case ERRO_INVALIDO: return "argumento inválido";
    case ERRO_NAO_EXISTE: return "não existe";
    case ERRO_JA_EXISTE: return "já existe";
    case ERRO_SEM_MEMORIA: return "sem memória";
    case ERRO_ENDERECO: return "endereço inválido";
    case ERRO_NAO_E_PASTA: return "não é uma pasta";
    case ERRO_E_PASTA: return "é uma pasta";
    case ERRO_NAO_VAZIA: return "a pasta não está vazia";
    case ERRO_LIMITE: return "limite atingido";
    case ERRO_DESCRITOR: return "descritor inválido";
    case ERRO_PIPE_QUEBRADO: return "ninguém lendo o pipe";
    case ERRO_SEM_PROCESSO: return "processo não existe";
    case ERRO_FORMATO: return "não é um programa válido";
    case ERRO_ACESSO: return "operação não permitida";
    case ERRO_OCUPADO: return "em uso";
    case ERRO_REDE: return "sem rede ou sem resposta";
    case ERRO_FIM: return "conexão terminada";
    case ERRO_ES: return "erro de leitura ou gravação no disco";
    default: return "erro desconhecido";
    }
}
