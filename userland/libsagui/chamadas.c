/* chamadas.c - uma função por chamada de sistema (convenção em abi.h). */
#include "sagui.h"

static inline int chamar(unsigned n, unsigned a, unsigned b, unsigned c)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

int  executar(const char *c, char *const argv[]) { return chamar(SIS_EXECUTAR, (unsigned)c, (unsigned)argv, 0); }
int  esperar(int pid, int *codigo)    { return chamar(SIS_ESPERAR, (unsigned)pid, (unsigned)codigo, 0); }
int  recolher(int *codigo)            { return chamar(SIS_ESPERAR, (unsigned)-1, (unsigned)codigo, 1); }
void sair(int codigo)                 { chamar(SIS_SAIR, (unsigned)codigo, 0, 0); for (;;) ; }
int  meu_pid(void)                    { return chamar(SIS_PID, 0, 0, 0); }
int  matar(int pid)                   { return chamar(SIS_MATAR, (unsigned)pid, 0, 0); }
int  abrir(const char *c, int modo)   { return chamar(SIS_ABRIR, (unsigned)c, (unsigned)modo, 0); }
int  ler(int fd, void *b, unsigned n) { return chamar(SIS_LER, (unsigned)fd, (unsigned)b, n); }
int  escrever(int fd, const void *b, unsigned n) { return chamar(SIS_ESCREVER, (unsigned)fd, (unsigned)b, n); }
int  fechar(int fd)                   { return chamar(SIS_FECHAR, (unsigned)fd, 0, 0); }
int  posicionar(int fd, int d, int o) { return chamar(SIS_POSICIONAR, (unsigned)fd, (unsigned)d, (unsigned)o); }
int  info_arquivo(const char *c, info_arquivo_t *i) { return chamar(SIS_INFO_ARQUIVO, (unsigned)c, (unsigned)i, 0); }
int  criar_pasta(const char *c)       { return chamar(SIS_CRIAR_PASTA, (unsigned)c, 0, 0); }
int  remover_pasta(const char *c)     { return chamar(SIS_REMOVER_PASTA, (unsigned)c, 0, 0); }
int  apagar(const char *c)            { return chamar(SIS_APAGAR, (unsigned)c, 0, 0); }
int  listar(const char *c, int i, entrada_pasta_t *e) { return chamar(SIS_LISTAR, (unsigned)c, (unsigned)i, (unsigned)e); }
int  mudar_pasta(const char *c)       { return chamar(SIS_MUDAR_PASTA, (unsigned)c, 0, 0); }
int  pasta_atual(char *b, unsigned n) { return chamar(SIS_PASTA_ATUAL, (unsigned)b, n, 0); }
int  renomear(const char *o, const char *d) { return chamar(SIS_RENOMEAR, (unsigned)o, (unsigned)d, 0); }
int  criar_pipe(int fds[2])           { return chamar(SIS_PIPE, (unsigned)fds, 0, 0); }
int  duplicar(int a, int n)           { return chamar(SIS_DUPLICAR, (unsigned)a, (unsigned)n, 0); }
int  disponivel(int fd)               { return chamar(SIS_DISPONIVEL, (unsigned)fd, 0, 0); }
int  dormir(unsigned ms)              { return chamar(SIS_DORMIR, ms, 0, 0); }
unsigned tempo_ms(void)               { return (unsigned)chamar(SIS_TEMPO, 0, 0, 0); }
int  ler_data(data_t *d)              { return chamar(SIS_DATA, (unsigned)d, 0, 0); }
int  info_sistema(int t, void *b, unsigned n) { return chamar(SIS_INFO_SISTEMA, (unsigned)t, (unsigned)b, n); }
void *aumentar_memoria(unsigned n)    { int r = chamar(SIS_AUMENTAR_MEM, n, 0, 0); return (r < 0 && r > -100) ? NULL : (void *)r; }
void reiniciar(void)                  { chamar(SIS_REINICIAR, 0, 0, 0); }
int  config_ler(const char *chave, char *valor, int max) { return chamar(SIS_CONFIG_LER, (unsigned)chave, (unsigned)valor, (unsigned)max); }
int  config_gravar(const char *chave, const char *valor) { return chamar(SIS_CONFIG_GRAVAR, (unsigned)chave, (unsigned)valor, 0); }
int  ajustar_data(const data_t *d)    { return chamar(SIS_AJUSTAR_DATA, (unsigned)d, 0, 0); }
int  som_abrir(void)                  { return chamar(SIS_SOM_ABRIR, 0, 0, 0); }
int  som_enviar(int f, const void *q, unsigned bytes) { return chamar(SIS_SOM_ENVIAR, (unsigned)f, (unsigned)q, bytes); }
int  som_fechar(int f)                { return chamar(SIS_SOM_FECHAR, (unsigned)f, 0, 0); }
int  som_volume(int novo)             { return chamar(SIS_SOM_VOLUME, (unsigned)novo, 0, 0); }
int  bytes_aleatorios(void *destino, unsigned n) { return chamar(SIS_ALEATORIO, (unsigned)destino, n, 0); }
void desligar(void)                   { chamar(SIS_DESLIGAR, 0, 0, 0); }
int  modo_terminal(int m)             { return chamar(SIS_MODO_TERMINAL, (unsigned)m, 0, 0); }
int  limpar_tela(void)                { return chamar(SIS_LIMPAR_TELA, 0, 0, 0); }
int  registrar_texto(const char *t)   { return chamar(SIS_REGISTRAR, (unsigned)t, 0, 0); }
int  listar_discos(int i, info_disco_t *d) { return chamar(SIS_LISTAR_DISCOS, (unsigned)i, (unsigned)d, 0); }
int  montar(const char *nome)         { return chamar(SIS_MONTAR, (unsigned)nome, 0, 0); }
int  desmontar(const char *nome)      { return chamar(SIS_DESMONTAR, (unsigned)nome, 0, 0); }
int  tocar(const nota_t *notas, int n, int modo) { return chamar(SIS_BIP, (unsigned)notas, (unsigned)n, (unsigned)modo); }
int  bip(int freq, int ms)
{
    nota_t n = { (uint16_t)freq, (uint16_t)ms };
    return tocar(&n, 1, BIP_SUBSTITUIR);
}
int  teclas_seguradas(unsigned char mapa[32]) { return chamar(SIS_TECLAS_SEGURADAS, (unsigned)mapa, 0, 0); }
void silenciar(void)                  { chamar(SIS_BIP, 0, 0, 0); }
int  criar_janela(const char *t, int l, int a, uint32_t **px)
                                      { return chamar(SIS_CRIAR_JANELA, (unsigned)t, (unsigned)(l | (a << 16)), (unsigned)px); }
int  fechar_janela(int id)            { return chamar(SIS_FECHAR_JANELA, (unsigned)id, 0, 0); }
int  atualizar_janela(int id, const retangulo_t *r) { return chamar(SIS_ATUALIZAR_JANELA, (unsigned)id, (unsigned)r, 0); }
int  proximo_evento(int id, evento_t *e, bool esperar) { return chamar(SIS_PROXIMO_EVENTO, (unsigned)id, (unsigned)e, esperar); }
int  titulo_janela(int id, const char *t) { return chamar(SIS_TITULO_JANELA, (unsigned)id, (unsigned)t, 0); }
int  ler_tela(uint32_t *px, unsigned bytes) { return chamar(SIS_LER_TELA, (unsigned)px, bytes, 0); }
int  area_copia_gravar(const char *t, unsigned n) { return chamar(SIS_AREA_COPIA, 0, (unsigned)t, n); }
int  area_copia_ler(char *buf, unsigned max) { return chamar(SIS_AREA_COPIA, 1, (unsigned)buf, max); }
int  menu_janela(int id, int x, int y, const char *itens) { return chamar(SIS_MENU_JANELA, (unsigned)id, (unsigned)(x | (y << 16)), (unsigned)itens); }
int  janela_opcoes(int id, int opcoes, int larg_min, int alt_min) { return chamar(SIS_JANELA_OPCOES, (unsigned)id, (unsigned)opcoes, (unsigned)(larg_min | (alt_min << 16))); }
