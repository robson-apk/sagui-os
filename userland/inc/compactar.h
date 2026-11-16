/* compactar.h - compactação na libsagui (H93): CRC-32, Adler-32, DEFLATE
 * (compactar e descompactar), zlib e gzip.
 *
 * Especificações: RFC 1951 (DEFLATE), RFC 1950 (zlib), RFC 1952 (gzip e o
 * CRC-32 da ISO 3309). O código está em userland/libsagui/compactar.c.
 *
 * Os tamanhos devolvidos são int: >= 0 é a quantidade de bytes, < 0 é um dos
 * erros COMP_ERRO_* abaixo. */
#ifndef COMPACTAR_H
#define COMPACTAR_H
/* (A guarda deixa os testes no computador usarem um sagui.h simulado.) */
#ifndef SAGUI_H
#include "sagui.h"
/* ---- apoio ao tar e ao zip (apoio_arquivos.c) ---- */

/* Junta o nome guardado num pacote à pasta de destino, com segurança: as
 * barras invertidas viram '/', "." some, e nomes absolutos ("/x") ou com
 * ".." são RECUSADOS (false). Sem isso, um pacote malicioso poderia
 * gravar fora da pasta escolhida ("zip slip"; travessia de caminho). */
bool caminho_seguro(const char *nome, const char *destino, char *saida, int max);
/* Cria a pasta e as que faltarem no caminho até ela ("mkdir -p"). */
int criar_pastas(const char *caminho);
/* Cria as pastas que faltam antes do último nome do caminho. */
int criar_pastas_ate(const char *caminho);
/* Nomes de arquivo: os do SaguiOS são Latin-1; dentro dos pacotes vão em
 * UTF-8. Na volta, o que não existe em Latin-1 vira '_'. Devolvem o
 * tamanho, ou -1 se não couber. */
int nome_latin1_para_utf8(const char *src, char *dst, int max);
int nome_utf8_para_latin1(const char *src, int n, char *dst, int max);
bool nome_ascii(const char *s);
/* Percorre um arquivo ou uma pasta (com tudo o que tem dentro, pastas
 * antes do conteúdo). real é o caminho no disco; nome, o caminho como vai
 * para o pacote. A função recebe também o tipo (TIPO_*) e o tamanho. */
typedef int (*percorrer_fn)(const char *real, const char *nome, int tipo, uint32_t tamanho, void *ctx);
int percorrer(const char *real, const char *nome, percorrer_fn fn, void *ctx);
bool existe_arquivo(const char *caminho);
/* Grava um arquivo inteiro (cria ou substitui). */
int gravar_arquivo_inteiro(const char *caminho, const void *dados, uint32_t n);

#endif

#define COMP_ERRO_DADOS    -101   /* os dados compactados estão errados */
#define COMP_ERRO_CHEIO    -102   /* o resultado não cabe no espaço dado */
#define COMP_ERRO_CRC      -103   /* a soma de conferência não bate */
#define COMP_ERRO_MEMORIA  -104   /* faltou memória */
#define COMP_ERRO_TRUNCADO -105   /* os dados acabaram antes da hora */
#define COMP_ERRO_METODO   -106   /* método ou recurso que não sabemos ler */

/* O maior resultado que as funções que alocam aceitam produzir (proteção
 * contra "bombas": poucos bytes que viram gigabytes). */
#define COMP_LIMITE (256u * 1024 * 1024)

const char *comp_nome_erro(int erro);   /* também entende os ERRO_* do sistema */

/* Somas. Comece com crc = 0 e adler = 1; dá para somar em pedaços. */
uint32_t crc32_somar(uint32_t crc, const void *dados, size_t n);
uint32_t adler32_somar(uint32_t adler, const void *dados, size_t n);

/* DEFLATE puro (RFC 1951). Devolve quantos bytes saíram, ou um erro.
 * inflar_usados também diz quantos bytes da entrada o fluxo ocupou (para
 * achar o que vem depois dele, como o rodapé do gzip). */
int inflar(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max);
int inflar_usados(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max, uint32_t *usados);
/* Igual, mas a saída é alocada (e cresce) aqui; libere com libera().
 * dica: um palpite do tamanho final (0 se não souber). Devolve o tamanho. */
int inflar_alocando(const uint8_t *dados, uint32_t n, uint8_t **saida, uint32_t dica, uint32_t *usados);
/* zlib (RFC 1950) e gzip (RFC 1952) com a saída num espaço já reservado. */
int inflar_zlib(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max);
int inflar_gzip(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max);

/* Compactação DEFLATE. nivel: 0 só guarda, 1 rápido ... 9 melhor (6 é o
 * padrão). saida precisa de deflar_limite(n) bytes para nunca faltar. */
uint32_t deflar_limite(uint32_t n);
int deflar(const uint8_t *dados, uint32_t n, uint8_t *saida, uint32_t max, int nivel);

/* gzip (RFC 1952), alocando o resultado. nome pode ser NULL; mtime em
 * segundos desde 1970 (0 = sem data). */
int gzip_compactar(const uint8_t *dados, uint32_t n, const char *nome, uint32_t mtime, int nivel,
                   uint8_t **saida);
/* Lê todos os membros do arquivo .gz, um depois do outro, e confere o CRC e
 * o tamanho de cada um. nome (pode ser NULL) recebe o nome original. */
int gzip_descompactar(const uint8_t *dados, uint32_t n, uint8_t **saida, char *nome, int max_nome);
bool eh_gzip(const uint8_t *dados, uint32_t n);

/* ---- apoio ao tar e ao zip (apoio_arquivos.c) ---- */

/* Junta o nome guardado num pacote à pasta de destino, com segurança: as
 * barras invertidas viram '/', "." some, e nomes absolutos ("/x") ou com
 * ".." são RECUSADOS (false). Sem isso, um pacote malicioso poderia
 * gravar fora da pasta escolhida ("zip slip"; travessia de caminho). */
bool caminho_seguro(const char *nome, const char *destino, char *saida, int max);
/* Cria a pasta e as que faltarem no caminho até ela ("mkdir -p"). */
int criar_pastas(const char *caminho);
/* Cria as pastas que faltam antes do último nome do caminho. */
int criar_pastas_ate(const char *caminho);
/* Nomes de arquivo: os do SaguiOS são Latin-1; dentro dos pacotes vão em
 * UTF-8. Na volta, o que não existe em Latin-1 vira '_'. Devolvem o
 * tamanho, ou -1 se não couber. */
int nome_latin1_para_utf8(const char *src, char *dst, int max);
int nome_utf8_para_latin1(const char *src, int n, char *dst, int max);
bool nome_ascii(const char *s);
/* Percorre um arquivo ou uma pasta (com tudo o que tem dentro, pastas
 * antes do conteúdo). real é o caminho no disco; nome, o caminho como vai
 * para o pacote. A função recebe também o tipo (TIPO_*) e o tamanho. */
typedef int (*percorrer_fn)(const char *real, const char *nome, int tipo, uint32_t tamanho, void *ctx);
int percorrer(const char *real, const char *nome, percorrer_fn fn, void *ctx);
bool existe_arquivo(const char *caminho);
/* Grava um arquivo inteiro (cria ou substitui). */
int gravar_arquivo_inteiro(const char *caminho, const void *dados, uint32_t n);

#endif
