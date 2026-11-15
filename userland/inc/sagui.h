/* sagui.h - libsagui, a biblioteca dos programas do SaguiOS.
 *
 * Tudo o que um programa usa vem daqui: não há libc. Os nomes são em
 * português. Um programa define "int principal(int argc, char **argv)"; a
 * libsagui chama essa função e, quando ela volta, encerra o processo com o
 * valor devolvido. */
#ifndef SAGUI_H
#define SAGUI_H
#include "abi.h"

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;
typedef unsigned int       size_t;
typedef _Bool              bool;
#define true  1
#define false 0
#define NULL  ((void *)0)

typedef __builtin_va_list va_list;
#define va_start(l, u) __builtin_va_start(l, u)
#define va_arg(l, t)   __builtin_va_arg(l, t)
#define va_end(l)      __builtin_va_end(l)

#define ENTRADA 0
#define SAIDA   1
#define ERROS   2

/* ---- processos ---- */
int  executar(const char *caminho, char *const argv[]);
int  esperar(int pid, int *codigo);
int  recolher(int *codigo);   /* um filho que já terminou, sem esperar; 0 se nenhum */
void sair(int codigo) __attribute__((noreturn));
int  meu_pid(void);
int  matar(int pid);
/* ---- arquivos ---- */
int  abrir(const char *caminho, int modo);
int  ler(int fd, void *buf, unsigned n);
int  escrever(int fd, const void *buf, unsigned n);
int  fechar(int fd);
int  posicionar(int fd, int desloc, int origem);
int  info_arquivo(const char *caminho, info_arquivo_t *info);
/* ---- pastas ---- */
int  criar_pasta(const char *caminho);
int  remover_pasta(const char *caminho);
int  apagar(const char *caminho);
int  listar(const char *caminho, int indice, entrada_pasta_t *e);
int  mudar_pasta(const char *caminho);
int  pasta_atual(char *buf, unsigned n);
int  renomear(const char *origem, const char *destino);
/* ---- comunicação ---- */
int  criar_pipe(int fds[2]);
int  duplicar(int antigo, int novo);
int  disponivel(int fd);   /* bytes para ler num pipe sem bloquear; ERRO_PIPE_QUEBRADO = acabou */
/* ---- diversas ---- */
int      dormir(unsigned ms);
unsigned tempo_ms(void);
int      ler_data(data_t *d);
int      info_sistema(int tipo, void *buf, unsigned n);
void    *aumentar_memoria(unsigned bytes);
void     reiniciar(void);
void     desligar(void);   /* grava os pendrives e desliga (H36) */
int      bytes_aleatorios(void *destino, unsigned n);   /* bytes imprevisíveis, para chaves (H50) */
/* Configurações do sistema (H37): guardadas em /usb/.saguios/configuracoes.txt. */
int      config_ler(const char *chave, char *valor, int max);    /* tamanho, ou ERRO_NAO_EXISTE */
int      config_gravar(const char *chave, const char *valor);    /* 1 guardada no pendrive, 0 só até desligar */
int      ajustar_data(const data_t *d);                         /* acerta o relógio da placa-mãe (H38) */
/* Som pela placa (H41): quadros de 16 bits, estéreo, 48 kHz (4 bytes cada). */
#define SOM_TAXA 48000
int      som_abrir(void);                                       /* fonte, ou ERRO_NAO_EXISTE sem placa */
int      som_enviar(int fonte, const void *quadros, unsigned bytes);   /* bytes aceitos (não espera) */
int      som_fechar(int fonte);
int      som_volume(int novo);                                  /* -1 só consulta; devolve 0-100 */
/* Rede (fase 3 da 2.0). IPs na ordem da rede (o primeiro número no byte baixo). */
int      rede_info(info_rede_t *r);
int      rede_socket(int tipo);                                 /* REDE_UDP, REDE_ICMP, REDE_TCP */
int      rede_conectar(int s, unsigned ip, int porta);
int      rede_enviar(int s, const void *dados, unsigned n);
int      rede_receber(int s, void *dados, unsigned max, unsigned *origem);   /* 0 = nada ainda */
int      rede_fechar(int s);
void     ip_para_texto(unsigned ip, char *texto);               /* texto com 16 bytes */
unsigned texto_para_ip(const char *texto);                      /* 0 se não for IP */
int      dns_resolver(const char *nome, unsigned *ip);          /* 0, ERRO_NAO_EXISTE ou ERRO_REDE */
int      rede_estado(int s);                                    /* TCP: 0 conectando, 1 aberta, 2 o outro lado terminou, <0 falhou */
int      tcp_conectar(unsigned ip, int porta, unsigned ms);     /* socket pronto, ou erro */
int      tcp_enviar_tudo(int s, const void *dados, unsigned n);
int      modo_terminal(int modo);
int      limpar_tela(void);

/* ---- janelas (as funções de desenho e os componentes estão em gui.h) ---- */
int  criar_janela(const char *titulo, int largura, int altura, uint32_t **pixels);
int  fechar_janela(int id);
int  atualizar_janela(int id, const retangulo_t *r);   /* r = NULL: a janela toda */
int  proximo_evento(int id, evento_t *e, bool esperar); /* 1 = veio um evento */
int  titulo_janela(int id, const char *titulo);
int  janela_opcoes(int id, int opcoes, int larg_min, int alt_min);   /* JAN_REDIMENSIONAVEL (H78) */
int  menu_janela(int id, int x, int y, const char *itens);           /* "A|B|C"; escolha em EV_MENU (H81) */
int  ler_tela(uint32_t *pixels, unsigned bytes);   /* largura | altura << 16; bytes = 0 só mede (H87) */

/* ---- dispositivos (fase 6) ---- */
int  listar_discos(int indice, info_disco_t *d);   /* 1 = achou, 0 = acabou */
int  montar(const char *nome);                     /* "usb0" -> /usb */
int  desmontar(const char *nome);

/* ---- som (bônus B3): volta na hora, o kernel toca em segundo plano ---- */
int  tocar(const nota_t *notas, int n, int modo);  /* modo BIP_SUBSTITUIR ou BIP_ENFILEIRAR */
int  bip(int freq, int ms);
void silenciar(void);

/* ---- jogos (bônus B4) ---- */
int  teclas_seguradas(unsigned char mapa[32]);   /* 1 = em foco; CV_* em abi.h */
#define SEGURADA(mapa, cv) (((mapa)[(cv) >> 3] >> ((cv) & 7)) & 1)

/* ---- memória e texto ---- */
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int v, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t tam_texto(const char *s);
int    compara_texto(const char *a, const char *b);
int    compara_texto_n(const char *a, const char *b, size_t n);
char  *copia_texto(char *dst, const char *src, size_t max);    /* sempre termina em zero */
char  *junta_texto(char *dst, const char *src, size_t max);
char  *acha_char(const char *s, int c);
char  *acha_ultimo_char(const char *s, int c);
bool   comeca_com(const char *s, const char *prefixo);
int    texto_para_int(const char *s);
size_t utf8_para_latin1(char *s, size_t n, size_t *sobra);   /* texto UTF-8 de fora -> Latin-1 */
bool   eh_espaco(int c);

void *aloca(size_t n);
void *aloca_zerado(size_t n);
void *realoca(void *p, size_t n);
void  libera(void *p);

/* ---- saída formatada (%s %c %d %u %x %p, largura, '-', '0', '*') ---- */
int  formatar_lista(char *dst, size_t max, const char *fmt, va_list ap);
int  formatar_texto(char *dst, size_t max, const char *fmt, ...);
int  imprimir(const char *fmt, ...);
int  imprimir_em(int fd, const char *fmt, ...);
int  escrever_tudo(int fd, const void *buf, unsigned n);
int  escrever_texto(int fd, const char *s);
const char *nome_erro(int erro);
void registrar(const char *fmt, ...);   /* log do sistema (serial), não aparece na tela */
int  registrar_texto(const char *t);

/* ---- entrada ---- */
int  ler_linha(int fd, char *buf, unsigned max);   /* tamanho, ou -1 no fim do arquivo */

int principal(int argc, char **argv);

#endif
