/* sistema.c - porta de entrada das chamadas de sistema (int 0x80).
 *
 * A porta 0x80 da IDT aceita "int" vindo do anel 3 (D007). O despachante lê
 * o número em EAX, chama a função da tabela com EBX, ECX, EDX, ESI, EDI e
 * devolve o resultado em EAX. Depois olha o estado do processo:
 *   - a função devolveu SIS_REPETIR: o processo bloqueou; o EIP volta 2
 *     bytes (o tamanho de "int 0x80") e a chamada roda de novo ao acordar;
 *   - o processo bloqueou com resultado pronto (ex.: dormir): grava o
 *     resultado e passa a vez;
 *   - o processo terminou (sair, ou foi encerrado): passa a vez.
 * Ver D010. */
#include "sistema.h"
#include "console.h"
#include "cpu.h"
#include "paginacao.h"
#include "processo.h"
#include "texto.h"
#include "som.h"
#include "hda.h"
#include "rede.h"
#include "configuracoes.h"

static const chamada_t tabela[SIS_MAX] = {
    [SIS_EXECUTAR] = sis_executar,       [SIS_ESPERAR] = sis_esperar,
    [SIS_SAIR] = sis_sair,               [SIS_PID] = sis_pid,
    [SIS_MATAR] = sis_matar,
    [SIS_ABRIR] = sis_abrir,             [SIS_LER] = sis_ler,
    [SIS_ESCREVER] = sis_escrever,       [SIS_FECHAR] = sis_fechar,
    [SIS_POSICIONAR] = sis_posicionar,   [SIS_INFO_ARQUIVO] = sis_info_arquivo,
    [SIS_CRIAR_PASTA] = sis_criar_pasta, [SIS_REMOVER_PASTA] = sis_remover_pasta,
    [SIS_APAGAR] = sis_apagar,           [SIS_LISTAR] = sis_listar,
    [SIS_MUDAR_PASTA] = sis_mudar_pasta, [SIS_PASTA_ATUAL] = sis_pasta_atual,
    [SIS_RENOMEAR] = sis_renomear,
    [SIS_PIPE] = sis_pipe,               [SIS_DUPLICAR] = sis_duplicar,
    [SIS_DISPONIVEL] = sis_disponivel,
    [SIS_DORMIR] = sis_dormir,           [SIS_TEMPO] = sis_tempo,
    [SIS_DATA] = sis_data,               [SIS_INFO_SISTEMA] = sis_info_sistema,
    [SIS_AUMENTAR_MEM] = sis_aumentar_memoria, [SIS_REINICIAR] = sis_reiniciar,
    [SIS_REGISTRAR] = sis_registrar,
    [SIS_MODO_TERMINAL] = sis_modo_terminal,   [SIS_LIMPAR_TELA] = sis_limpar_tela,
    [SIS_CRIAR_JANELA] = sis_criar_janela,     [SIS_FECHAR_JANELA] = sis_fechar_janela,
    [SIS_ATUALIZAR_JANELA] = sis_atualizar_janela, [SIS_PROXIMO_EVENTO] = sis_proximo_evento,
    [SIS_TITULO_JANELA] = sis_titulo_janela,
    [SIS_LISTAR_DISCOS] = sis_listar_discos,   [SIS_MONTAR] = sis_montar,
    [SIS_DESMONTAR] = sis_desmontar,
    [SIS_BIP] = sis_bip,
    [SIS_TECLAS_SEGURADAS] = sis_teclas_seguradas,
    [SIS_DESLIGAR] = sis_desligar,
    [SIS_CONFIG_LER] = sis_config_ler,   [SIS_CONFIG_GRAVAR] = sis_config_gravar,
    [SIS_AJUSTAR_DATA] = sis_ajustar_data,
    [SIS_SOM_ABRIR] = sis_som_abrir,     [SIS_SOM_ENVIAR] = sis_som_enviar,
    [SIS_SOM_FECHAR] = sis_som_fechar,   [SIS_SOM_VOLUME] = sis_som_volume,
    [SIS_REDE_INFO] = sis_rede_info,     [SIS_REDE_SOCKET] = sis_rede_socket,
    [SIS_REDE_CONECTAR] = sis_rede_conectar, [SIS_REDE_ENVIAR] = sis_rede_enviar,
    [SIS_REDE_RECEBER] = sis_rede_receber,   [SIS_REDE_FECHAR] = sis_rede_fechar,
    [SIS_REDE_ESTADO] = sis_rede_estado,
    [SIS_ALEATORIO] = sis_aleatorio,
    [SIS_JANELA_OPCOES] = sis_janela_opcoes,
    [SIS_MENU_JANELA]   = sis_menu_janela,
    [SIS_LER_TELA]      = sis_ler_tela,
};

bool usuario_ok(const void *p, uint32_t n, bool escrita)
{
    uint32_t ini = (uint32_t)p;
    if (n == 0) return true;
    if (ini < USUARIO_INICIO || ini >= USUARIO_FIM || n > USUARIO_FIM - ini)
        return false;
    for (uint32_t pag = ini & ~0xFFFu; pag < ini + n; pag += 4096) {
        uint32_t flags;
        if (!espaco_traduzir(atual->espaco, pag, &flags)) return false;
        if (!(flags & PG_USUARIO)) return false;
        if (escrita && !(flags & PG_ESCRITA)) return false;
    }
    return true;
}

int usuario_texto(const char *origem, char *destino, uint32_t max)
{
    for (uint32_t i = 0; i < max; i++) {
        /* Confere cada página quando entra nela (o texto pode cruzar páginas). */
        if ((i == 0 || (((uint32_t)origem + i) & 0xFFF) == 0) && !usuario_ok(origem + i, 1, false))
            return ERRO_ENDERECO;
        destino[i] = origem[i];
        if (!origem[i]) return (int)i;
    }
    destino[max - 1] = 0;
    return ERRO_INVALIDO;   /* longo demais */
}

static quadro_t *tratar_sistema(quadro_t *q)
{
    uint32_t n = q->eax;
    if (n >= SIS_MAX || !tabela[n]) {
        q->eax = (uint32_t)ERRO_INVALIDO;
        return q;
    }
    int32_t r = tabela[n](q->ebx, q->ecx, q->edx, q->esi, q->edi);
    if (r == SIS_REPETIR) {
        q->eip -= 2;               /* executa o "int 0x80" de novo ao acordar */
        return escalonar(q);
    }
    q->eax = (uint32_t)r;
    if (atual->estado != P_EXECUTANDO)   /* bloqueou com resultado pronto, ou terminou */
        return escalonar(q);
    return q;
}

void sistema_iniciar(void)
{
    idt_registrar(VETOR_SISTEMA, tratar_sistema);
}
