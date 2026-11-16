/* sistema.h - chamadas de sistema (int 0x80). */
#ifndef SAGUI_SISTEMA_H
#define SAGUI_SISTEMA_H
#include "tipos.h"
#include "abi.h"

/* Valor especial devolvido por uma chamada que bloqueou e deve ser
 * executada de novo quando o processo acordar (D010). */
#define SIS_REPETIR ((int32_t)0x80000000)

typedef int32_t (*chamada_t)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);

void sistema_iniciar(void);

/* Validação de ponteiros vindos do anel 3: o intervalo inteiro precisa estar
 * na área dos programas e mapeado para o usuário (e gravável, se for escrita). */
bool usuario_ok(const void *p, uint32_t n, bool escrita);
/* Copia um texto do programa para o kernel; devolve o tamanho ou um erro. */
int  usuario_texto(const char *origem, char *destino, uint32_t max);

/* Implementadas em vários arquivos: */
int32_t sis_sair(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_pid(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_matar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_executar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_esperar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_dormir(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_tempo(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_data(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_info_sistema(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_aumentar_memoria(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_reiniciar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_registrar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_abrir(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_ler(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_escrever(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_fechar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_posicionar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_info_arquivo(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_criar_pasta(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_remover_pasta(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_apagar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_listar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_mudar_pasta(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_pasta_atual(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_renomear(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_pipe(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_duplicar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_disponivel(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_modo_terminal(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_limpar_tela(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_criar_janela(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_fechar_janela(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_atualizar_janela(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_proximo_evento(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_titulo_janela(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_listar_discos(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_montar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_desmontar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_teclas_seguradas(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_desligar(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_ajustar_data(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_aleatorio(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_janela_opcoes(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_menu_janela(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_ler_tela(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_disco_setores(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_disco_reler(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
int32_t sis_area_copia(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

#endif
