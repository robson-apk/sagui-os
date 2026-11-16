/* agente.h - o núcleo do Sagui Code (H89): a conversa, o laço de pedidos ao
 * modelo e de ferramentas, o resumo da conversa quando ela cresce, a conta do
 * gasto e as conversas guardadas.
 *
 * O núcleo não escreve na tela: tudo o que acontece vira uma chamada às
 * funções de agente_ui_t. Assim, a mesma lógica serve ao Terminal
 * (saguicode.c) e à janela (janela.c), e cada interface decide como mostrar.
 *
 * O laço anda um passo de cada vez (agente_passo): cada passo é uma chamada
 * ao modelo e as ferramentas que ele pedir. Quem tem uma janela processa os
 * eventos entre um passo e outro, e ela não congela; o Terminal só chama
 * agente_passo até devolver false.
 *
 * Textos: a conversa (e o que vai para o modelo) é UTF-8; os textos do
 * SaguiOS (caminhos, arquivos, avisos) são Latin-1. Cada função diz qual. */
#ifndef SAGUI_AGENTE_H
#define SAGUI_AGENTE_H
#include "ia.h"

/* Tipos de aviso (a interface escolhe a cor). */
#define AV_ERRO_API       1   /* a chamada ao modelo falhou (o texto traz o erro) */
#define AV_LIMITE_GASTO   2   /* a sessão chegou no limite de dólares */
#define AV_LIMITE_PASSOS  3   /* o pedido chegou no limite de passos */
#define AV_CONTEXTO       4   /* a conversa foi resumida para caber no limite */
#define AV_PARADO         5   /* a pessoa pediu para parar */

/* Notas de uma ferramenta enquanto ela roda ("lendo x", "compilou"). */
#define NOTA_INFO 0
#define NOTA_OK   1
#define NOTA_ERRO 2

typedef struct {
    void *ctx;
    /* Um pedaço da resposta do modelo, enquanto ela chega (UTF-8; um
     * caractere pode vir partido entre dois pedaços). */
    void (*texto)(void *ctx, const char *utf8, unsigned n);
    /* Vai chamar o modelo: a chamada bloqueia até a resposta acabar. */
    void (*pensando)(void *ctx);
    /* O modelo terminou o pedido sem pedir ferramentas (texto inteiro, UTF-8). */
    void (*resposta)(void *ctx, const char *utf8);
    /* Uma ferramenta vai rodar: o nome, os argumentos resumidos (UTF-8, como
     * vieram) e uma descrição curta para pessoas (Latin-1: "Lendo x.c"). */
    void (*ferramenta)(void *ctx, const char *nome, const char *args, const char *descricao);
    void (*nota)(void *ctx, int tipo, const char *latin1);
    /* A ferramenta acabou: deu certo ou não, e uma linha de resumo (Latin-1). */
    void (*ferramenta_fim)(void *ctx, const char *nome, bool ok, const char *resumo);
    void (*aviso)(void *ctx, int tipo, const char *latin1);
    /* Pede licença ("Sobrescrever x (10 bytes)?", Latin-1). Sem esta função,
     * nada que precise de licença acontece. */
    bool (*confirmar)(void *ctx, const char *pergunta);
    /* O pedido acabou: os números do pedido e da sessão estão no agente. */
    void (*fim)(void *ctx);
} agente_ui_t;

typedef struct {
    ia_t        ia;
    agente_ui_t ui;
    int   max_passos;              /* por pedido */
    long  limite_micro;            /* gasto máximo da sessão, em milionésimos de dólar */
    int   max_contexto;            /* tokens (estimados por letras / 4) */
    long  gasto_sessao;            /* desde que o programa abriu */
    long  gasto_conversa;          /* da conversa atual (vai junto quando ela é guardada) */
    int   tokens_sessao_entrada, tokens_sessao_saida;
    bool  permitir_tudo;           /* a pessoa já deu licença para tudo nesta conversa */
    bool  pendrive;                /* há /usb/.saguios (configurações e conversas guardadas) */
    char  pasta_trabalho[96];
    char  pasta_config[64];
    char  titulo[64];              /* da conversa (Latin-1); vazio = sem título */
    /* O pedido em andamento. */
    bool  ocupado;
    bool  parar;
    int   passo;
    long  gasto_pedido;
    int   entrada, saida, cache;   /* tokens do pedido */
} agente_t;

/* Prepara tudo: pastas, configurações e a chave. Não começa a conversa. */
void agente_iniciar(agente_t *ag);
void agente_ler_chave(agente_t *ag);          /* de novo (a pessoa pode ter acabado de gravar) */
void agente_liberar(agente_t *ag);            /* apaga a chave da memória */
/* Uma conversa nova: só as instruções. */
void agente_nova_conversa(agente_t *ag);

/* Um pedido (Latin-1). Depois, agente_passo até devolver false. */
void agente_pedir(agente_t *ag, const char *latin1);
bool agente_passo(agente_t *ag);
void agente_parar(agente_t *ag);              /* termina depois do passo atual */

int  agente_estimar_tokens(agente_t *ag);
void agente_dinheiro(long micro, char *saida, int max);    /* "US$ 0,0012" */
/* Descrição de uma chamada de ferramenta para pessoas (Latin-1). */
void agente_descrever(const char *nome, const char *args_utf8, char *saida, int max);

/* Conversa em JSON (sem a chave nem as instruções), para guardar. */
char *agente_para_json(agente_t *ag, unsigned *tam);
/* Troca a conversa pela do JSON. Devolve o número de mensagens, ou -1 (o
 * motivo vai em erro). */
int   agente_de_json(agente_t *ag, const char *json, unsigned tam, char *erro, int max_erro);
int   agente_salvar(agente_t *ag, const char *caminho);    /* 0 ou erro */
/* -1: não achou; -2: estragada (motivo em erro); senão, o número de mensagens. */
int   agente_abrir(agente_t *ag, const char *caminho, char *erro, int max_erro);

/* Textos: Latin-1 -> UTF-8 em memória nova; UTF-8 -> Latin-1 no lugar. */
char    *agente_utf8(const char *latin1, unsigned n);
unsigned agente_latin1(char *s);

#endif
