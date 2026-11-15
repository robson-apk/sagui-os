/* area_copia.c - a área de cópia do sistema: copiar num programa e colar
 * em outro (pedido do Eli: copiar no Terminal e colar no Editor).
 *
 * É só um texto guardado pelo kernel, de até 64 KB. Copiar troca o texto
 * inteiro; colar devolve uma cópia. Os programas usam pela libsagui
 * (area_copia_gravar e area_copia_ler). Não há formatos (imagem, arquivo):
 * só texto em Latin-1, como o resto do SaguiOS. */
#include "sistema.h"
#include "memoria.h"
#include "texto.h"
#include "abi.h"
#include "console.h"

#define AREA_MAX (64 * 1024)
static char *texto;
static uint32_t tamanho;

/* SIS_AREA_COPIA (modo, &buffer, n):
 *   modo 0: guarda os n bytes do buffer (copiar) -> n;
 *   modo 1: copia até n bytes para o buffer (colar) -> o tamanho do texto guardado. */
int32_t sis_area_copia(uint32_t modo, uint32_t ptr, uint32_t n, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    if (modo == 0) {
        if (n > AREA_MAX) n = AREA_MAX;
        if (!usuario_ok((const void *)ptr, n, false)) return ERRO_ENDERECO;
        if (!texto) texto = kmalloc(AREA_MAX);
        if (!texto) return ERRO_SEM_MEMORIA;
        memcpy(texto, (const void *)ptr, n);
        tamanho = n;
        klog("[SAGUI] area de copia: %u bytes\n", n);
        return (int32_t)n;
    }
    if (modo == 1) {
        if (n && !usuario_ok((const void *)ptr, n, true)) return ERRO_ENDERECO;   /* n = 0: só pergunta o tamanho */
        uint32_t c = n < tamanho ? n : tamanho;
        if (c) memcpy((void *)ptr, texto, c);
        return (int32_t)tamanho;
    }
    return ERRO_INVALIDO;
}
