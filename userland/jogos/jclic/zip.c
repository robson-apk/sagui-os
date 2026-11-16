/* zip.c - leitura de arquivos ZIP (H92). Ver zip.h.
 *
 * Os números do ZIP são little-endian. O registro "fim do diretório
 * central" (assinatura 0x06054B50) fica nos últimos 22 bytes, ou um pouco
 * antes se o arquivo tiver um comentário (até 65535 bytes): procura-se de
 * trás para a frente. Ele diz quantas entradas há e onde começa o
 * diretório; cada entrada (0x02014B50) traz o método, os tamanhos, o nome e
 * onde está o cabeçalho local (0x04034B50), depois do qual vêm os dados. */
#include "zip.h"

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

int zip_abrir(zip_t *z, const uint8_t *d, uint32_t n)
{
    memset(z, 0, sizeof *z);
    if (n < 22) return ERRO_INVALIDO;
    uint32_t fim = 0;
    bool achou = false;
    for (uint32_t p = n - 22 + 1; p-- > 0 && n - p <= 22 + 65535;)
        if (le32(d + p) == 0x06054B50) { fim = p; achou = true; break; }
    if (!achou) return ERRO_INVALIDO;
    int qtd = (int)le16(d + fim + 10);
    uint32_t pos = le32(d + fim + 16);
    z->e = aloca_zerado((size_t)(qtd > 0 ? qtd : 1) * sizeof(zip_entrada_t));
    if (!z->e) return ERRO_SEM_MEMORIA;
    z->d = d;
    z->n = n;
    for (int k = 0; k < qtd; k++) {
        if (pos + 46 > n || le32(d + pos) != 0x02014B50) break;
        zip_entrada_t *e = &z->e[z->qtd];
        e->metodo = (int)le16(d + pos + 10);
        e->tam_comprimido = le32(d + pos + 20);
        e->tam = le32(d + pos + 24);
        uint32_t ln = le16(d + pos + 28), le = le16(d + pos + 30), lc = le16(d + pos + 32);
        e->local = le32(d + pos + 42);
        if (pos + 46 + ln > n) break;
        uint32_t c = ln < sizeof e->nome - 1 ? ln : sizeof e->nome - 1;
        memcpy(e->nome, d + pos + 46, c);
        e->nome[c] = 0;
        z->qtd++;
        pos += 46 + ln + le + lc;
    }
    return z->qtd ? 0 : ERRO_INVALIDO;
}

void zip_fechar(zip_t *z)
{
    libera(z->e);
    memset(z, 0, sizeof *z);
}

static int minuscula(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static bool igual(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (minuscula((uint8_t)*a) != minuscula((uint8_t)*b)) return false;
    return !*a && !*b;
}

static const char *so_nome(const char *s)
{
    const char *r = s;
    for (; *s; s++) if (*s == '/' || *s == '\\') r = s + 1;
    return r;
}

int zip_achar(const zip_t *z, const char *nome)
{
    for (int k = 0; k < z->qtd; k++) if (igual(z->e[k].nome, nome)) return k;
    for (int k = 0; k < z->qtd; k++) if (igual(so_nome(z->e[k].nome), so_nome(nome))) return k;
    return -1;
}

uint8_t *zip_extrair(const zip_t *z, int i, uint32_t *n)
{
    if (i < 0 || i >= z->qtd) return NULL;
    const zip_entrada_t *e = &z->e[i];
    uint32_t p = e->local;
    if (p + 30 > z->n || le32(z->d + p) != 0x04034B50) return NULL;
    p += 30 + le16(z->d + p + 26) + le16(z->d + p + 28);
    if (p + e->tam_comprimido > z->n || e->tam > 64u * 1024 * 1024) return NULL;
    uint8_t *s = aloca(e->tam + 1);
    if (!s) return NULL;
    if (e->metodo == 0 && e->tam_comprimido == e->tam) memcpy(s, z->d + p, e->tam);
    else if (e->metodo != 8 || inflar(z->d + p, e->tam_comprimido, s, e->tam) != (int)e->tam) { libera(s); return NULL; }
    s[e->tam] = 0;
    *n = e->tam;
    return s;
}
