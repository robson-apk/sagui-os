/* sagsh - o shell do SaguiOS.
 *
 * Um programa comum, no anel 3: lê uma linha, separa em palavras e
 * operadores, e executa. Os comandos clássicos são programas em /bin; só
 * os que precisam mudar o próprio shell (cd, limpar, sair...) são internos.
 *
 * Redirecionamento e pipes são montados sem "fork" (D010): o shell guarda
 * a própria entrada e saída com duplicar, coloca o arquivo ou o pipe no
 * lugar de 0 ou 1, executa o programa (que herda 0, 1 e 2) e restaura.
 *
 * A linha é editada pelo próprio shell, com o terminal em modo bruto, para
 * ter histórico (setas) e completar nomes com Tab. */
#include "sagui.h"

#define MAX_LINHA    256
#define MAX_PALAVRAS 32
#define MAX_ETAPAS   6
#define MAX_HIST     32
#define FD_GUARDA_0  14
#define FD_GUARDA_1  15

/* ------------------------------------------------------------------ comandos */

typedef struct {
    const char *nome, *apelido;
    bool        interno;
    bool        grafico;   /* abre uma janela: roda em segundo plano, sem prender o shell */
    const char *curta;     /* até 17 caracteres, para a lista do "ajuda" */
    const char *uso;
    const char *explicacao;
} comando_t;

static const comando_t comandos[] = {
    { "ls", "listar", false, false, "lista arquivos", "ls [pasta]", "Mostra os arquivos e pastas (pastas terminam com /)." },
    { "cd", "entrar", true, false, "entra numa pasta", "cd <pasta>", "Muda a pasta atual. \"cd ..\" volta uma; \"cd\" sozinho vai para a raiz." },
    { "pwd", "onde", true, false, "mostra a pasta", "pwd", "Mostra em que pasta você está." },
    { "cat", "ver", false, false, "mostra um arquivo", "cat <arquivo...>", "Mostra o conteúdo dos arquivos. Sem arquivo, mostra o que chegar pela entrada." },
    { "echo", "eco", false, false, "escreve um texto", "echo <texto>", "Escreve o texto. Com \"> arquivo\" grava num arquivo." },
    { "cp", "copiar", false, false, "copia arquivo", "cp <origem> <destino>", "Copia um arquivo. Se o destino for uma pasta, copia para dentro dela." },
    { "mv", "mover", false, false, "move ou renomeia", "mv <origem> <destino>", "Move ou renomeia um arquivo ou pasta." },
    { "rm", "apagar", false, false, "apaga arquivo", "rm <arquivo...>", "Apaga arquivos (pastas: use rmdir)." },
    { "mkdir", "criar-pasta", false, false, "cria pasta", "mkdir <pasta>", "Cria uma pasta nova." },
    { "rmdir", "remover-pasta", false, false, "apaga pasta vazia", "rmdir <pasta>", "Remove uma pasta, se estiver vazia." },
    { "wc", "conta", false, false, "conta linhas", "wc [arquivo]", "Conta linhas, palavras e letras de um arquivo ou da entrada (ex.: ls bin | conta)." },
    { "ps", "processos", false, false, "lista processos", "ps", "Mostra os processos em execução: PID, pai, estado, memória e tempo de CPU." },
    { "top", "monitor", false, false, "processos ao vivo", "top [-n vezes]", "Mostra os processos, a memória e a %CPU de cada um, atualizando a cada segundo. q sai." },
    { "grep", "procurar", false, false, "procura texto", "grep [-i] [-n] [-v] [-c] <texto> [arquivo...]", "Mostra as linhas que contêm o texto (ex.: ls bin | grep a). -i ignora maiúsculas, -n numera, -v inverte, -c conta." },
    { "kill", "matar", false, false, "encerra processo", "kill <pid>", "Encerra um processo pelo número (veja os números com ps)." },
    { "clear", "limpar", true, false, "limpa a tela", "clear", "Limpa a tela." },
    { "ajuda", NULL, true, false, "esta ajuda", "ajuda [comando]", "Lista os comandos, ou explica um comando." },
    { "mem", NULL, false, false, "memória", "mem", "Mostra a memória total, usada e livre." },
    { "data", NULL, false, false, "data e hora", "data", "Mostra a data e a hora do relógio da placa-mãe." },
    { "sobre", NULL, false, false, "sobre o SaguiOS", "sobre", "Versão e créditos do SaguiOS." },
    { "uname", "sistema", false, false, "nome do sistema", "uname [-a | -r | -m]", "Diz o nome do sistema; -a mostra também a versão, a arquitetura e há quanto tempo está ligado." },
    { "fetch", "ficha", false, false, "ficha do sistema", "fetch", "O sagui desenhado com letras e um resumo do sistema: versão, processador, memória, tela." },
    { "ola", NULL, false, false, "mostra argumentos", "ola [argumentos]", "Imprime os argumentos recebidos (teste do carregador de programas)." },
    { "contador", NULL, false, false, "conta segundos", "contador [segundos]", "Conta segundos (padrão: 10). Rode com & para ver a multitarefa." },
    { "falha", NULL, false, false, "provoca um erro", "falha [memoria|kernel|divisao|instrucao]", "Faz um erro de propósito: só este programa é encerrado, o sistema continua." },
    { "reiniciar", NULL, true, false, "reinicia o PC", "reiniciar", "Grava o que falta nos pendrives e reinicia o computador." },
    { "poweroff", "desligar", true, false, "desliga o PC", "desligar", "Grava o que falta nos pendrives e desliga o computador." },
    { "sair", NULL, true, false, "fecha o shell", "sair", "Fecha o shell." },
    { "discos", NULL, false, false, "lista os pendrives", "discos", "Mostra os pendrives conectados, o formato e se estão montados." },
    { "df", "espaco", false, false, "espaço dos discos", "df [-k]", "Mostra o espaço total, usado e livre de / (a memória) e de /usb (o pendrive). -k: tudo em KB." },
    { "montar", NULL, false, false, "monta o pendrive", "montar [usb0]", "Monta o pendrive FAT32 em /usb." },
    { "desmontar", NULL, false, false, "libera o pendrive", "desmontar [usb0]", "Grava tudo o que falta e libera o pendrive para ser removido." },
    { "usb", NULL, false, false, "diagnóstico USB", "usb", "Mostra os controladores USB 2.0, as portas e os dispositivos." },
    { "pci", NULL, false, false, "lista o hardware", "pci", "Lista os dispositivos PCI (rede, som, USB, vídeo) e o que o SaguiOS já usa." },
    { "config", NULL, false, false, "configurações", "config [chave [valor]]", "Lê ou muda uma configuração do sistema (guardada no pendrive)." },
    { "som", NULL, false, false, "placa de som", "som [teste [Hz [ms]] | volume [0-100]]", "Mostra a placa de som, toca um tom de teste ou muda o volume." },
    { "tocar", NULL, false, false, "toca um .wav", "tocar arquivo.wav", "Toca um arquivo WAV (PCM de 8 ou 16 bits) pela placa de som." },
    { "rede", NULL, false, false, "mostra a rede", "rede", "Mostra a placa de rede, o endereço IP, o roteador e o DNS." },
    { "ping", NULL, false, false, "testa a rede", "ping <nome ou IP> [vezes]", "Pergunta a outro computador se ele responde e mede o tempo." },
    { "dns", NULL, false, false, "IP de um nome", "dns <nome>", "Descobre o endereço IP de um nome (ex.: dns google.com)." },
    { "baixar", NULL, false, false, "baixa da web", "baixar <url> [arquivo]", "Baixa um arquivo da web (http:// ou https://) para a pasta atual." },
    { "relogio", NULL, false, false, "hora, pela internet", "relogio [internet [servidor]]", "Mostra a hora, ou acerta o relógio pela internet (NTP)." },
    { "bip", NULL, false, false, "toca um bip", "bip [Hz [ms]] | bip escala", "Toca uma nota no alto-falante do PC (padrão: 880 Hz por 200 ms)." },
    { "arquivos", NULL, false, true, "abre o Arquivos", "arquivos [pasta]", "Abre o Arquivos numa janela." },
    { "editor", NULL, false, true, "editor de texto", "editor [arquivo]", "Abre o Editor de texto numa janela." },
    { "calculadora", NULL, false, true, "calculadora", "calculadora", "Abre a Calculadora numa janela." },
    { "banana", NULL, false, true, "jogo do sagui", "banana", "Abre o jogo Sagui Pega Banana." },
    { "configuracoes", NULL, false, true, "abre as Configurações", "configuracoes [aba]", "Abre as Configurações (relógio, aparência, sobre o computador)." },
    { "sagui3d", NULL, false, true, "jogo em 3D", "sagui3d [fase]", "Abre o jogo Sagui 3D (ou só uma fase, dando o arquivo)." },
    { "triangulos", NULL, false, true, "teste de desenho", "triangulos", "Triângulos girando em 3D, com os quadros por segundo: mede a velocidade do desenho. +/- muda a quantidade." },
    { "chuva", NULL, false, true, "chuva de letras", "chuva", "Abre uma janela com letras verdes caindo. Espaço pausa; Esc fecha." },
    { "sagui-monitor", NULL, false, true, "monitor em janela", "sagui-monitor", "Abre o Sagui Monitor numa janela: CPU, memória e processos. P, M e N ordenam; setas escolhem; Q ou Esc fecha." },
};
#define QTD_COMANDOS (int)(sizeof comandos / sizeof comandos[0])

static const comando_t *achar_comando(const char *nome)
{
    for (int i = 0; i < QTD_COMANDOS; i++)
        if (compara_texto(comandos[i].nome, nome) == 0 ||
            (comandos[i].apelido && compara_texto(comandos[i].apelido, nome) == 0))
            return &comandos[i];
    return NULL;
}

/* ------------------------------------------------------------------ edição da linha */

static char historico[MAX_HIST][MAX_LINHA];
static int qtd_hist;

static void mostrar(const char *s) { escrever_texto(SAIDA, s); }

static void prompt(void)
{
    char pasta[128];
    if (pasta_atual(pasta, sizeof pasta) < 0) copia_texto(pasta, "?", sizeof pasta);
    imprimir("sagui:%s$ ", pasta);
}

/* Lê uma tecla do terminal em modo bruto: caractere, ou 0x100 + letra para
 * as teclas especiais (abi.h). */
static int ler_tecla(void)
{
    uint8_t c;
    if (ler(ENTRADA, &c, 1) <= 0) return -1;
    if (c != 27) return c;
    uint8_t d;
    if (ler(ENTRADA, &d, 1) <= 0) return -1;
    return 0x100 | d;
}

/* Edição da linha com o cursor em qualquer posição. A tela entende
 * ESC [ D (cursor à esquerda), ESC [ C (à direita) e ESC [ K (apaga até o fim
 * da linha); ver kernel/dispositivos/terminal_grafico.c. */
static void cursor_esquerda(int n) { while (n-- > 0) mostrar("\x1b[D"); }
static void cursor_direita(int n)  { while (n-- > 0) mostrar("\x1b[C"); }

/* Reescreve da posição "de" até o fim, apaga o resto da linha antiga e
 * devolve o cursor para "cur". */
static void reescrever_de(const char *linha, int n, int de, int cur)
{
    escrever(SAIDA, linha + de, (unsigned)(n - de));
    mostrar("\x1b[K");
    cursor_esquerda(n - cur);
}

static void trocar_linha(char *linha, int *n, int *cur, const char *nova)
{
    cursor_esquerda(*cur);
    copia_texto(linha, nova, MAX_LINHA);
    *n = *cur = (int)tam_texto(linha);
    reescrever_de(linha, *n, 0, *cur);
}

/* Junta numa lista os candidatos para completar "prefixo". */
static int candidatos(const char *linha, int n, char lista[][NOME_MAX], int max, int *ini_palavra, bool *de_pasta)
{
    int ini = n;
    while (ini > 0 && linha[ini - 1] != ' ' && linha[ini - 1] != '|' && linha[ini - 1] != '>' && linha[ini - 1] != '<')
        ini--;
    *ini_palavra = ini;
    char palavra[MAX_LINHA];
    copia_texto(palavra, linha + ini, (size_t)(n - ini) + 1);
    bool primeira = true;
    for (int i = 0; i < ini; i++)
        if (linha[i] != ' ') { primeira = false; break; }
    int qtd = 0;
    *de_pasta = false;
    if (primeira && !acha_char(palavra, '/')) {
        for (int i = 0; i < QTD_COMANDOS && qtd < max; i++) {
            if (comeca_com(comandos[i].nome, palavra)) copia_texto(lista[qtd++], comandos[i].nome, NOME_MAX);
            if (comandos[i].apelido && comeca_com(comandos[i].apelido, palavra) && qtd < max)
                copia_texto(lista[qtd++], comandos[i].apelido, NOME_MAX);
        }
        /* Também os programas de /bin e os instalados no pendrive que não
         * estão na tabela (o registro, por exemplo, não completava). */
        static const char *const pastas_prog[] = { "/bin", "/usb/programas" };
        for (int p = 0; p < 2; p++) {
            entrada_pasta_t e;
            for (int i = 0; listar(pastas_prog[p], i, &e) == 1 && qtd < max; i++) {
                if (e.tipo == TIPO_PASTA || e.nome[0] == '.' || !comeca_com(e.nome, palavra)) continue;
                bool repetido = false;
                for (int k = 0; k < qtd && !repetido; k++) repetido = compara_texto(lista[k], e.nome) == 0;
                if (!repetido) copia_texto(lista[qtd++], e.nome, NOME_MAX);
            }
        }
        return qtd;
    }
    /* Nome de arquivo: separa a pasta do começo do nome. */
    char pasta[MAX_LINHA];
    char *barra = acha_ultimo_char(palavra, '/');
    const char *prefixo = palavra;
    if (barra) {
        size_t tp = (size_t)(barra - palavra);
        copia_texto(pasta, palavra, tp + 1);
        if (tp == 0) copia_texto(pasta, "/", sizeof pasta);
        prefixo = barra + 1;
    } else {
        copia_texto(pasta, ".", sizeof pasta);
    }
    *de_pasta = true;
    entrada_pasta_t e;
    for (int i = 0; listar(pasta, i, &e) == 1 && qtd < max; i++)
        if (comeca_com(e.nome, prefixo)) {
            copia_texto(lista[qtd], e.nome, NOME_MAX);
            if (e.tipo == TIPO_PASTA) junta_texto(lista[qtd], "/", NOME_MAX);
            qtd++;
        }
    /* O que já foi digitado do nome entra na conta do "ini". */
    *ini_palavra = ini + (int)(prefixo - palavra);
    return qtd;
}

static void completar(char *linha, int *n)
{
    static char lista[48][NOME_MAX];
    int ini;
    bool de_pasta;
    int qtd = candidatos(linha, *n, lista, 48, &ini, &de_pasta);
    if (qtd == 0) return;
    int ja = *n - ini;   /* quanto do nome já está digitado */
    /* Maior começo comum entre os candidatos. */
    int comum = (int)tam_texto(lista[0]);
    for (int i = 1; i < qtd; i++) {
        int k = 0;
        while (k < comum && lista[i][k] == lista[0][k]) k++;
        comum = k;
    }
    if (comum > ja) {
        for (int k = ja; k < comum && *n < MAX_LINHA - 2; k++) {
            linha[(*n)++] = lista[0][k];
            escrever(SAIDA, &lista[0][k], 1);
        }
        if (qtd == 1 && lista[0][comum - 1] != '/' && *n < MAX_LINHA - 2) {
            linha[(*n)++] = ' ';
            mostrar(" ");
        }
        linha[*n] = 0;
        return;
    }
    if (qtd == 1) return;
    /* Várias opções e nada a acrescentar: mostra as opções. */
    mostrar("\n");
    for (int i = 0; i < qtd; i++)
        imprimir("%-20s%s", lista[i], (i % 4 == 3 || i == qtd - 1) ? "\n" : "");
    prompt();
    linha[*n] = 0;
    mostrar(linha);
}

/* Lê uma linha com edição. Devolve o tamanho, ou -1 no fim da entrada. */
static int ler_comando(char *linha)
{
    int n = 0, cur = 0, pos_hist = qtd_hist;
    linha[0] = 0;
    modo_terminal(TERM_BRUTO);
    for (;;) {
        int t = ler_tecla();
        if (t < 0) { modo_terminal(TERM_LINHA); return -1; }
        if (t == '\n' || t == '\r') break;
        if (t == '\b' || t == 0x7F) {                    /* apaga antes do cursor */
            if (cur > 0) {
                bool no_fim = cur == n;
                memmove(linha + cur - 1, linha + cur, (size_t)(n - cur + 1));
                n--; cur--;
                /* Caso comum: só apaga o último. "\b" só volta o cursor (é assim
                 * nos terminais); o espaço é que apaga a letra, e o segundo "\b"
                 * volta de novo. Sem o espaço, o Terminal da 2.0 deixava a letra. */
                if (no_fim) { mostrar("\b \b"); continue; }
                cursor_esquerda(1);
                reescrever_de(linha, n, cur, cur);
            }
        } else if (t == (0x100 | TECLA_B_DELETE)) {      /* apaga sob o cursor */
            if (cur < n) {
                memmove(linha + cur, linha + cur + 1, (size_t)(n - cur));
                n--;
                reescrever_de(linha, n, cur, cur);
            }
        } else if (t == (0x100 | TECLA_B_ESQUERDA)) {
            if (cur > 0) { cur--; cursor_esquerda(1); }
        } else if (t == (0x100 | TECLA_B_DIREITA)) {
            if (cur < n) { cur++; cursor_direita(1); }
        } else if (t == (0x100 | TECLA_B_HOME)) {
            cursor_esquerda(cur); cur = 0;
        } else if (t == (0x100 | TECLA_B_END)) {
            cursor_direita(n - cur); cur = n;
        } else if (t == (0x100 | TECLA_B_REDESENHAR)) {  /* outro programa escreveu na tela */
            prompt();
            reescrever_de(linha, n, 0, cur);
        } else if (t == '\t') {
            cursor_direita(n - cur); cur = n;              /* completa sempre no fim */
            completar(linha, &n);
            cur = n;
        } else if (t == 3) {                              /* Ctrl+C: descarta a linha */
            cursor_direita(n - cur);
            mostrar("^C\n");
            n = cur = 0;
            linha[0] = 0;
            prompt();
        } else if (t == 12) {                             /* Ctrl+L: limpa a tela */
            mostrar("\x1b[J");
            prompt();
            reescrever_de(linha, n, 0, cur);
        } else if (t == (0x100 | TECLA_B_CIMA)) {
            if (pos_hist > 0) trocar_linha(linha, &n, &cur, historico[--pos_hist]);
        } else if (t == (0x100 | TECLA_B_BAIXO)) {
            if (pos_hist < qtd_hist - 1) trocar_linha(linha, &n, &cur, historico[++pos_hist]);
            else if (pos_hist < qtd_hist) { pos_hist = qtd_hist; trocar_linha(linha, &n, &cur, ""); }
        } else if (t >= 0x20 && t < 0x100 && n < MAX_LINHA - 1) {   /* insere no cursor */
            memmove(linha + cur + 1, linha + cur, (size_t)(n - cur + 1));
            linha[cur] = (char)t;
            n++;
            if (cur == n - 1) escrever(SAIDA, &linha[cur], 1);   /* caso comum: digitando no fim */
            else reescrever_de(linha, n, cur, cur + 1);
            cur++;
        }
    }
    mostrar("\n");
    modo_terminal(TERM_LINHA);
    linha[n] = 0;
    if (n > 0 && (qtd_hist == 0 || compara_texto(historico[qtd_hist - 1], linha) != 0)) {
        if (qtd_hist == MAX_HIST) {
            for (int i = 1; i < MAX_HIST; i++) copia_texto(historico[i - 1], historico[i], MAX_LINHA);
            qtd_hist--;
        }
        copia_texto(historico[qtd_hist++], linha, MAX_LINHA);
    }
    return n;
}

/* ------------------------------------------------------------------ análise */

typedef struct {
    char *texto;
    char  op;   /* 0 = palavra; '|', '>', 'A' (>>), '<', '&' */
} token_t;

static bool eh_operador(char c) { return c == '|' || c == '<' || c == '>' || c == '&'; }

/* Separa a linha em palavras e operadores (que podem vir colados nas
 * palavras: "ls bin|conta"). Aspas agrupam palavras com espaços; dentro
 * delas, | > < & são texto comum. As palavras são copiadas, sem as aspas,
 * para um armazém à parte. */
static char armazem[MAX_LINHA * 2];

static int separar(const char *linha, token_t *t, int max)
{
    int n = 0;
    size_t usado = 0;
    const char *p = linha;
    while (*p && n < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (eh_operador(*p)) {
            char op = *p++;
            if (op == '>' && *p == '>') { op = 'A'; p++; }
            t[n].texto = NULL;
            t[n++].op = op;
            continue;
        }
        char *ini = armazem + usado;
        while (*p && *p != ' ' && !eh_operador(*p) && usado < sizeof armazem - 2) {
            if (*p == '"') {
                p++;
                while (*p && *p != '"' && usado < sizeof armazem - 2) armazem[usado++] = *p++;
                if (*p == '"') p++;
            } else {
                armazem[usado++] = *p++;
            }
        }
        armazem[usado++] = 0;
        t[n].texto = ini;
        t[n++].op = 0;
    }
    return n;
}

typedef struct {
    char *argv[MAX_PALAVRAS];
    int   argc;
    char *entrada, *saida;
    bool  acrescentar;
} etapa_t;

/* ------------------------------------------------------------------ comandos internos */

static void interno_ajuda(int argc, char **argv)
{
    if (argc > 1) {
        const comando_t *c = achar_comando(argv[1]);
        if (!c) { imprimir("ajuda: não conheço o comando %s\n", argv[1]); return; }
        imprimir("Uso: %s\n%s\n", c->uso, c->explicacao);
        if (c->apelido) imprimir("Apelido em português: %s\n", c->apelido);
        imprimir("Tipo: %s\n", c->interno ? "comando interno do shell" : "programa em /bin");
        return;
    }
    mostrar("Comandos do SaguiOS (nome/apelido):\n");
    for (int i = 0; i < QTD_COMANDOS; i++) {
        char nome[32];
        formatar_texto(nome, sizeof nome, "%s%s%s", comandos[i].nome, comandos[i].apelido ? "/" : "",
                       comandos[i].apelido ? comandos[i].apelido : "");
        /* Duas colunas de 39 caracteres: a linha nunca chega a 80 (a tela
         * pularia uma linha a mais). */
        imprimir("  %-20s%-17s%s", nome, comandos[i].curta, (i % 2 == 1 || i == QTD_COMANDOS - 1) ? "\n" : "");
    }
    mostrar("Arquivo: cmd > arq, >> arq, < arq   Pipe: cmd1 | cmd2   Segundo plano: cmd &\n");
    mostrar("ajuda <comando> explica um comando. Setas: histórico. Tab: completa nomes.\n");
}

static bool executar_interno(const comando_t *c, int argc, char **argv)
{
    const char *n = c->nome;
    if (compara_texto(n, "cd") == 0) {
        int r = mudar_pasta(argc > 1 ? argv[1] : "/");
        if (r < 0) imprimir("cd: %s: %s\n", argc > 1 ? argv[1] : "/", nome_erro(r));
    } else if (compara_texto(n, "pwd") == 0) {
        char p[128];
        pasta_atual(p, sizeof p);
        imprimir("%s\n", p);
    } else if (compara_texto(n, "clear") == 0) {
        mostrar("\x1b[J");   /* funciona no terminal de tela cheia e no Terminal em janela */
    } else if (compara_texto(n, "ajuda") == 0) {
        interno_ajuda(argc, argv);
    } else if (compara_texto(n, "reiniciar") == 0) {
        mostrar("Reiniciando...\n");
        dormir(200);
        reiniciar();
    } else if (compara_texto(n, "desligar") == 0 || compara_texto(n, "poweroff") == 0) {   /* poweroff: do #4 */
        mostrar("Desligando...\n");
        dormir(200);
        desligar();
    } else if (compara_texto(n, "sair") == 0) {
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ execução */

static void recolher_fundo(void)
{
    int codigo, pid;
    while ((pid = recolher(&codigo)) > 0)
        imprimir("[%d] terminou (código %d)\n", pid, codigo);
}

static void caminho_do_programa(const char *nome, char *saida, size_t max)
{
    if (acha_char(nome, '/')) { copia_texto(saida, nome, max); return; }
    const comando_t *c = achar_comando(nome);
    formatar_texto(saida, max, "/bin/%s", c ? c->nome : nome);
    /* Programas instalados no pendrive (H39): /usb/programas, depois de /bin. */
    info_arquivo_t inf;
    if (!c && info_arquivo(saida, &inf) < 0) {
        char instalado[128];
        formatar_texto(instalado, sizeof instalado, "/usb/programas/%s", nome);
        if (info_arquivo(instalado, &inf) >= 0) copia_texto(saida, instalado, max);
    }
}

static void rodar(etapa_t *et, int qtd, bool fundo)
{
    int pids[MAX_ETAPAS];
    int guarda0 = duplicar(ENTRADA, FD_GUARDA_0);
    int guarda1 = duplicar(SAIDA, FD_GUARDA_1);
    int leitura_anterior = -1;
    bool ok = true;
    for (int i = 0; i < qtd; i++) {
        pids[i] = -1;
        /* Entrada desta etapa. */
        if (i == 0 && et[i].entrada) {
            int fd = abrir(et[i].entrada, ABRIR_LER);
            if (fd < 0) { imprimir_em(ERROS, "sagsh: %s: %s\n", et[i].entrada, nome_erro(fd)); ok = false; break; }
            duplicar(fd, ENTRADA);
            fechar(fd);
        } else if (leitura_anterior >= 0) {
            duplicar(leitura_anterior, ENTRADA);
            fechar(leitura_anterior);
            leitura_anterior = -1;
        } else {
            duplicar(guarda0, ENTRADA);
        }
        /* Saída desta etapa. */
        if (i < qtd - 1) {
            int p[2];
            if (criar_pipe(p) < 0) { mostrar("sagsh: não consegui criar o pipe\n"); ok = false; break; }
            duplicar(p[1], SAIDA);
            fechar(p[1]);
            leitura_anterior = p[0];
        } else if (et[i].saida) {
            int modo = ABRIR_ESCREVER | ABRIR_CRIAR | (et[i].acrescentar ? ABRIR_ACRESCENTAR : ABRIR_TRUNCAR);
            int fd = abrir(et[i].saida, modo);
            if (fd < 0) {
                duplicar(guarda1, SAIDA);
                imprimir("sagsh: %s: %s\n", et[i].saida, nome_erro(fd));
                ok = false;
                break;
            }
            duplicar(fd, SAIDA);
            fechar(fd);
        } else {
            duplicar(guarda1, SAIDA);
        }
        char caminho[128];
        caminho_do_programa(et[i].argv[0], caminho, sizeof caminho);
        pids[i] = executar(caminho, et[i].argv);
        if (pids[i] < 0) {
            int erro = pids[i];
            duplicar(guarda1, SAIDA);
            if (erro == ERRO_NAO_EXISTE)
                imprimir("sagsh: comando não encontrado: %s (digite ajuda)\n", et[i].argv[0]);
            else
                imprimir("sagsh: %s: %s\n", et[i].argv[0], nome_erro(erro));
        }
    }
    if (leitura_anterior >= 0) fechar(leitura_anterior);
    duplicar(guarda0, ENTRADA);
    duplicar(guarda1, SAIDA);
    fechar(guarda0);
    fechar(guarda1);
    (void)ok;
    for (int i = 0; i < qtd; i++) {
        if (pids[i] <= 0) continue;
        if (fundo) {
            const comando_t *cg = achar_comando(et[i].argv[0]);
            if (cg && cg->grafico) imprimir("Abrindo %s (processo %d)...\n", et[i].argv[0], pids[i]);
            else imprimir("[%d] %s em segundo plano\n", pids[i], et[i].argv[0]);
            continue;
        }
        int codigo;
        esperar(pids[i], &codigo);
    }
}

/* Interpreta e executa uma linha. Devolve false se o shell deve sair. */
static bool executar_linha(char *linha)
{
    token_t tok[MAX_PALAVRAS * 2];
    int nt = separar(linha, tok, MAX_PALAVRAS * 2);
    if (nt == 0) return true;
    etapa_t et[MAX_ETAPAS];
    memset(et, 0, sizeof et);
    int qtd = 1;
    bool fundo = false;
    for (int i = 0; i < nt; i++) {
        etapa_t *e = &et[qtd - 1];
        char op = tok[i].op;
        if (op == 0) {
            if (e->argc < MAX_PALAVRAS - 1) e->argv[e->argc++] = tok[i].texto;
        } else if (op == '|') {
            if (e->argc == 0 || qtd == MAX_ETAPAS) { mostrar("sagsh: pipe sem comando\n"); return true; }
            qtd++;
        } else if (op == '&') {
            fundo = true;
        } else {
            if (i + 1 >= nt || tok[i + 1].op) { mostrar("sagsh: falta o nome do arquivo\n"); return true; }
            char *arq = tok[++i].texto;
            if (op == '<') e->entrada = arq;
            else { e->saida = arq; e->acrescentar = (op == 'A'); }
        }
    }
    if (et[qtd - 1].argc == 0) { mostrar("sagsh: pipe sem comando\n"); return true; }
    for (int i = 0; i < qtd; i++) et[i].argv[et[i].argc] = NULL;

    const comando_t *c = achar_comando(et[0].argv[0]);
    if (c && c->interno) {
        if (qtd > 1) { mostrar("sagsh: comandos internos não funcionam num pipe\n"); return true; }
        return executar_interno(c, et[0].argc, et[0].argv);
    }
    for (int i = 1; i < qtd; i++) {
        const comando_t *ci = achar_comando(et[i].argv[0]);
        if (ci && ci->interno) { mostrar("sagsh: comandos internos não funcionam num pipe\n"); return true; }
    }
    if (qtd == 1 && c && c->grafico) fundo = true;   /* programa gráfico não prende o shell */
    rodar(et, qtd, fundo);
    return true;
}

int principal(int argc, char **argv)
{
    /* sagsh -c "linha": roda uma linha só e sai (o Sagui Code usa, H70). */
    if (argc >= 3 && compara_texto(argv[1], "-c") == 0) {
        char linha[MAX_LINHA];
        copia_texto(linha, argv[2], sizeof linha);
        executar_linha(linha);
        return 0;
    }
    imprimir("sagsh: o shell do SaguiOS (processo %d). Digite ajuda para ver os comandos.\n", meu_pid());
    char linha[MAX_LINHA];
    for (;;) {
        recolher_fundo();
        prompt();
        int n = ler_comando(linha);
        if (n < 0) break;
        if (n == 0) continue;
        if (!executar_linha(linha)) break;
    }
    mostrar("sagsh: até logo!\n");
    return 0;
}
