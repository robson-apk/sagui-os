/* cal (calendario) - mostra o calendário de um mês ou de um ano (H91; a
 * ideia veio do fork SaguiOS-Extended, do Alejandro Anuncibay, e o programa
 * foi reescrito sobre a libsagui atual).
 *
 *   cal              o mês de hoje (pelo relógio da placa-mãe)
 *   cal 2027         o ano inteiro, três meses por linha
 *   cal 2 2028       fevereiro de 2028 (o mês pode ser 1 a 12)
 *
 * A semana começa no domingo, como nos calendários do Brasil. As contas
 * usam o calendário gregoriano para qualquer ano (o "gregoriano
 * proléptico" da norma ISO 8601), sem o pulo de 1752 que o cal dos sistemas
 * Unix faz por causa da troca de calendário na Inglaterra.
 *
 * O dia da semana sai da contagem de dias desde 1º de janeiro do ano 1, que
 * nesse calendário foi uma segunda-feira: 365 por ano, mais um a cada ano
 * bissexto (divisível por 4, menos os divisíveis por 100 que não são por
 * 400). */
#include "sagui.h"

static const char *const MESES[12] = {
    "janeiro", "fevereiro", "março", "abril", "maio", "junho",
    "julho", "agosto", "setembro", "outubro", "novembro", "dezembro"
};

static bool bissexto(int a) { return (a % 4 == 0 && a % 100 != 0) || a % 400 == 0; }

static int dias_no_mes(int m, int a)
{
    static const int dias[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && bissexto(a) ? 29 : dias[m - 1];
}

/* Dia da semana do dia 1 do mês: 0 = domingo ... 6 = sábado. */
static int primeiro_dia(int m, int a)
{
    unsigned anos = (unsigned)a - 1;
    unsigned dias = anos * 365 + anos / 4 - anos / 100 + anos / 400;   /* dias antes de 1/1/a */
    for (int k = 1; k < m; k++) dias += (unsigned)dias_no_mes(k, a);
    /* O dia número 0 da contagem (1/1/1) foi segunda-feira (1). */
    return (int)((dias + 1) % 7);
}

/* Monta as 8 linhas (título, cabeçalho e até 6 semanas) de um mês, cada
 * uma com 20 colunas, para poder pôr três meses lado a lado. */
static void montar_mes(int m, int a, bool com_ano, char linhas[8][24])
{
    char titulo[32];
    if (com_ano) formatar_texto(titulo, sizeof titulo, "%s %d", MESES[m - 1], a);
    else copia_texto(titulo, MESES[m - 1], sizeof titulo);
    int t = (int)tam_texto(titulo), esq = (20 - t) / 2;
    formatar_texto(linhas[0], 24, "%*s%s%*s", esq, "", titulo, 20 - t - esq, "");
    copia_texto(linhas[1], "do se te qu qu se sá", 24);
    int dia = 1 - primeiro_dia(m, a), total = dias_no_mes(m, a);
    for (int s = 0; s < 6; s++) {
        char *l = linhas[2 + s];
        int n = 0;
        for (int d = 0; d < 7; d++, dia++) {
            if (dia >= 1 && dia <= total) n += formatar_texto(l + n, (size_t)(24 - n), "%2d", dia);
            else n += formatar_texto(l + n, (size_t)(24 - n), "  ");
            if (d < 6) l[n++] = ' ';
        }
        l[n] = 0;
    }
}

/* Escreve a linha sem os espaços do fim (como o cal clássico). */
static void escrever_linha(const char *l)
{
    int n = (int)tam_texto(l);
    while (n > 0 && l[n - 1] == ' ') n--;
    escrever_tudo(SAIDA, l, (unsigned)n);
    escrever_tudo(SAIDA, "\n", 1);
}

static void mostrar_ano(int a)
{
    char cab[32];
    formatar_texto(cab, sizeof cab, "%d", a);
    imprimir("%*s%s\n\n", (64 - (int)tam_texto(cab)) / 2, "", cab);
    for (int trio = 0; trio < 4; trio++) {
        char m[3][8][24];
        for (int k = 0; k < 3; k++) montar_mes(trio * 3 + k + 1, a, false, m[k]);
        for (int l = 0; l < 8; l++) {
            char linha[80];
            formatar_texto(linha, sizeof linha, "%-20s  %-20s  %s", m[0][l], m[1][l], m[2][l]);
            escrever_linha(linha);
        }
        if (trio < 3) escrever_tudo(SAIDA, "\n", 1);
    }
}

int principal(int argc, char **argv)
{
    data_t hoje;
    ler_data(&hoje);
    int mes = hoje.mes, ano = hoje.ano;
    if (argc > 3) {
        imprimir_em(ERROS, "uso: cal [[mês] ano]\n");
        return 1;
    }
    if (argc >= 2 && (!converter_numero(argv[argc - 1], &ano) || ano < 1 || ano > 9999)) {
        imprimir_em(ERROS, "cal: ano inválido: %s (use 1 a 9999)\n", argv[argc - 1]);
        return 1;
    }
    if (argc == 3 && (!converter_numero(argv[1], &mes) || mes < 1 || mes > 12)) {
        imprimir_em(ERROS, "cal: mês inválido: %s (use 1 a 12)\n", argv[1]);
        return 1;
    }
    if (argc == 2) { mostrar_ano(ano); return 0; }
    char m[8][24];
    montar_mes(mes, ano, true, m);
    for (int l = 0; l < 8; l++) escrever_linha(m[l]);
    return 0;
}
