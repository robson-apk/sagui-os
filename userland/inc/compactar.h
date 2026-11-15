/* compactar.h - DEFLATE (compressão), zlib, CRC-32 e Adler-32 da libsagui (H87).
 * A descompressão fica no Cipó (userland/navegador/descompactar.c). */
#ifndef SAGUI_COMPACTAR_H
#define SAGUI_COMPACTAR_H
#include "sagui.h"

uint32_t crc32(uint32_t crc, const void *dados, uint32_t n);      /* comece com 0 */
uint32_t adler32(uint32_t soma, const void *dados, uint32_t n);   /* comece com 1 */

/* O pior caso do DEFLATE com códigos fixos: 9 bits por byte, mais o fim. */
#define DEFLAR_MAX(n) ((n) + (n) / 8 + 64)

/* Comprime "n" bytes em DEFLATE puro (RFC 1951) ou com o envelope zlib
 * (RFC 1950, o que o PNG usa). Devolve o tamanho, ou -1 se não coube em
 * "max" ou faltou memória. */
int deflar(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max);
int deflar_zlib(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max);

#endif
