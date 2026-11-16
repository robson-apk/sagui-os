/* zip.h - leitura de arquivos ZIP, para os projetos .jclic.zip (H92).
 *
 * Feito a partir da especificação pública do formato (PKWARE, APPNOTE.TXT):
 * o "diretório central" no fim do arquivo lista as entradas; cada uma diz
 * onde está o seu cabeçalho local, o método (0 = guardado, 8 = DEFLATE) e
 * os tamanhos. O DEFLATE é o da libsagui (userland/libsagui/compactar.c; antes o do Cipó,
 * RFC 1951), que já é do SaguiOS. */
#ifndef JCLIC_ZIP_H
#define JCLIC_ZIP_H
#include "sagui.h"

typedef struct {
    char     nome[128];
    uint32_t tam_comprimido, tam, local;
    int      metodo;
} zip_entrada_t;

typedef struct {
    const uint8_t *d;
    uint32_t       n;
    int            qtd;
    zip_entrada_t *e;
} zip_t;

int      zip_abrir(zip_t *z, const uint8_t *dados, uint32_t n);   /* 0 ou erro */
void     zip_fechar(zip_t *z);
int      zip_achar(const zip_t *z, const char *nome);   /* sem diferenciar maiúsculas; também só pelo nome do arquivo */
uint8_t *zip_extrair(const zip_t *z, int i, uint32_t *n); /* aloca (com um zero a mais no fim); NULL = erro */

/* Do Cipó (userland/navegador): DEFLATE e as imagens PNG e JPEG. */
typedef struct {
    uint32_t  larg, alt;
    uint32_t *px;                 /* 0xAARRGGBB */
} imagem_t;
/* O DEFLATE vem da libsagui (compactar.h, H93). */
#include "compactar.h"
imagem_t *png_ler(const uint8_t *d, uint32_t n, char *erro, int max_erro);
imagem_t *jpeg_ler(const uint8_t *d, uint32_t n, char *erro, int max_erro);

#endif
