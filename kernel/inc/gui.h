/* gui.h - área de trabalho e gerenciador de janelas (fase 5). */
#ifndef SAGUI_GUI_H
#define SAGUI_GUI_H
#include "tipos.h"
#include "desenho.h"

#define BARRA_SUPERIOR 24

/* Área de trabalho (area_trabalho.c) */
bool area_iniciar(void);
void area_fundo(superficie_t *s);          /* copia o fundo pronto para o recorte de s */
void area_barra(superficie_t *s, bool vidro, bool claro, bool frase);   /* barra superior com o relógio */
void area_som_retangulo(int *x, int *y, int *l, int *a);
bool area_som_em(int x, int y);
bool area_relogio_mudou(void);
int  area_icone_em(int x, int y);          /* índice do ícone sob o ponto, ou -1 */
void area_energia_retangulo(int *x, int *y, int *l, int *a);   /* botão de energia na barra (H36) */
bool area_energia_em(int x, int y);
void area_energia_abrir(void);
bool area_config_mudou(void);              /* H38: a aparência mudou, redesenhar */
bool area_instalados_mudaram(void);        /* H39: programas em /usb/programas mudaram */
void area_icone_abrir(int i);
int  area_qtd_icones(void);
void area_icone_retangulo(int i, int *x, int *y, int *l, int *a);
void area_icone_direito(int i);
#define ACAO_ABRIR      1
#define ACAO_RENOMEAR   2
#define ACAO_REMOVER    3
#define ACAO_ESCONDER   4
#define ACAO_DESMONTAR  5
const char *area_icone_nome(int i);      /* H89 */
/* H90: seleção de ícones pelo laço (somar: mantém os já marcados). */
void area_selecionar(int x, int y, int l, int a, bool somar);
bool area_limpar_selecao(void);            /* true se havia algo marcado */
bool area_icone_selecionado(int i);
int  area_qtd_selecionados(void);
int  area_icone_tipo(int i);              /* 0 sistema, 1 instalado, 2 pendrive */
void area_icone_acao(int i, int acao);
bool area_pendrive(bool presente, const char *rotulo);

/* Gerenciador de janelas (janelas.c) */
bool gui_iniciar(void);                    /* liga a área de trabalho e o gerenciador */
bool gui_ativa(void);
void gui_redesenhar(int x, int y, int l, int a);
void gui_mostrar(void);
void gui_aviso(const char *texto);   /* notificação no canto da tela por alguns segundos */
struct processo;
void janelas_do_processo_fechar(struct processo *p);

#endif
