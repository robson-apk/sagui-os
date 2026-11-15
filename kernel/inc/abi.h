/* abi.h - o contrato entre o kernel e os programas do SaguiOS.
 *
 * Este arquivo é incluído pelo kernel e pela libsagui. Convenção (PRD 4.5):
 * "int 0x80", número da chamada em EAX, argumentos em EBX, ECX, EDX (e ESI,
 * EDI quando precisa de mais), resultado em EAX; negativo = erro.
 * Os números são agrupados como no livro do Tanenbaum. */
#ifndef SAGUI_ABI_H
#define SAGUI_ABI_H

/* Processos */
#define SIS_EXECUTAR        1   /* (caminho, argv, 0) -> pid */
#define SIS_ESPERAR         2   /* (pid ou -1, &codigo, sem_bloquear) -> pid (0 = ninguém terminou) */
#define SIS_SAIR            3   /* (codigo) */
#define SIS_PID             4   /* () -> pid */
#define SIS_MATAR           5   /* (pid) */
/* Arquivos */
#define SIS_ABRIR          10   /* (caminho, modo) -> fd */
#define SIS_LER            11   /* (fd, buf, n) -> lidos (0 = fim) */
#define SIS_ESCREVER       12   /* (fd, buf, n) -> escritos */
#define SIS_FECHAR         13   /* (fd) */
#define SIS_POSICIONAR     14   /* (fd, desloc, origem) -> nova posição */
#define SIS_INFO_ARQUIVO   15   /* (caminho, &info_arquivo) */
/* Diretórios */
#define SIS_CRIAR_PASTA    20   /* (caminho) */
#define SIS_REMOVER_PASTA  21   /* (caminho) */
#define SIS_APAGAR         22   /* (caminho) */
#define SIS_LISTAR         23   /* (caminho, indice, &entrada_pasta) -> 1 achou, 0 fim */
#define SIS_MUDAR_PASTA    24   /* (caminho) */
#define SIS_PASTA_ATUAL    25   /* (buf, n) */
#define SIS_RENOMEAR       26   /* (origem, destino) */
/* Comunicação */
#define SIS_PIPE           30   /* (int fds[2]) */
#define SIS_DUPLICAR       31   /* (fd_antigo, fd_novo) -> fd_novo */
#define SIS_DISPONIVEL     32   /* (fd) -> bytes num pipe sem bloquear; ERRO_PIPE_QUEBRADO = acabou */
/* Diversas */
#define SIS_DORMIR         40   /* (ms) */
#define SIS_TEMPO          41   /* () -> milissegundos desde o boot */
#define SIS_DATA           42   /* (&data_hora) */
#define SIS_INFO_SISTEMA   43   /* (tipo, buf, n) */
#define SIS_AUMENTAR_MEM   44   /* (bytes) -> fim antigo da área de dados */
#define SIS_REINICIAR      45   /* () */
#define SIS_MODO_TERMINAL  46   /* (TERM_LINHA ou TERM_BRUTO) */
#define SIS_LIMPAR_TELA    47   /* () */
#define SIS_REGISTRAR      48   /* (texto) -> escreve no log do sistema (serial), não na tela */
/* Janelas (fase 5) */
#define SIS_CRIAR_JANELA   50   /* (titulo, largura | altura << 16, &pixels) -> id */
#define SIS_FECHAR_JANELA  51   /* (id) */
#define SIS_ATUALIZAR_JANELA 52 /* (id, &retangulo ou 0 = tudo) */
#define SIS_PROXIMO_EVENTO 53   /* (id ou 0 = qualquer janela minha, &evento, esperar) -> 1 se houve */
#define SIS_TITULO_JANELA  54   /* (id, titulo) */
/* Dispositivos (fase 6) */
#define SIS_LISTAR_DISCOS  55   /* (indice, &info_disco) -> 1 achou, 0 fim */
#define SIS_MONTAR         56   /* (nome, ex.: "usb0") -> monta em /usb */
#define SIS_DESMONTAR      57   /* (nome) -> grava o que falta e libera para remover */
/* Som (bônus B3) */
#define SIS_BIP            58   /* (&notas, quantidade, modo) -> volta na hora; o tique do relógio toca */
/* Jogos (bônus B4) */
#define SIS_TECLAS_SEGURADAS 59 /* (&mapa[32]) -> 1 se a janela do processo está em foco */
/* Versão 2.0 */
#define SIS_DESLIGAR       60   /* () -> grava os pendrives e desliga pelo ACPI (H36) */
#define SIS_CONFIG_LER     61   /* (chave, &valor, max) -> tamanho ou ERRO_NAO_EXISTE (H37) */
#define SIS_CONFIG_GRAVAR  62   /* (chave, valor) -> 1 guardada no pendrive, 0 só até desligar */
#define SIS_AJUSTAR_DATA   63   /* (&data_t) -> acerta o relógio da placa-mãe (H38) */
#define SIS_SOM_ABRIR      64   /* () -> fonte de som, ou ERRO_NAO_EXISTE sem placa (H41) */
#define SIS_SOM_ENVIAR     65   /* (fonte, &quadros, bytes) -> bytes aceitos; 16 bits estéreo 48 kHz */
#define SIS_SOM_FECHAR     66   /* (fonte) */
#define SIS_SOM_VOLUME     67   /* (novo 0-100 ou -1) -> volume atual */
/* Rede (fase 3 da 2.0) */
#define SIS_REDE_INFO      68   /* (&info_rede_t) -> 0, ou ERRO_NAO_EXISTE sem placa */
#define SIS_REDE_SOCKET    69   /* (REDE_UDP, REDE_ICMP ou REDE_TCP) -> socket */
#define SIS_REDE_CONECTAR  70   /* (socket, ip, porta) */
#define SIS_REDE_ENVIAR    71   /* (socket, &dados, tamanho) -> bytes enviados */
#define SIS_REDE_RECEBER   72   /* (socket, &dados, max, &ip_origem) -> bytes; 0 = nada ainda */
#define SIS_REDE_FECHAR    73   /* (socket) */
#define SIS_ALEATORIO      75   /* (&destino, n) -> n bytes aleatórios (H50) */
#define SIS_REDE_ESTADO    74   /* (socket TCP) -> 0 conectando, 1 aberta, 2 o outro lado terminou; <0 falhou (H48) */
#define SIS_JANELA_OPCOES  76   /* (id, opções, larg_min | alt_min << 16) -> 0 (H78) */
#define SIS_AREA_COPIA     78   /* (0, &texto, n) copia; (1, &buffer, max) cola -> tamanho guardado */
#define SIS_MENU_JANELA    77   /* (id, x | y << 16, "Item|Item|Item") -> abre um menu na janela; a escolha chega em EV_MENU (H81) */
#define SIS_LER_TELA       79   /* (&pixels, bytes) -> largura | altura << 16; copia a tela (0x00RRGGBB) se couber (H87) */
#define SIS_MAX            128   /* espaço para as chamadas da versão 2.0 */

/* Erros (sempre negativos) */
#define ERRO_INVALIDO      -1
#define ERRO_NAO_EXISTE    -2
#define ERRO_JA_EXISTE     -3
#define ERRO_SEM_MEMORIA   -4
#define ERRO_ENDERECO      -5   /* ponteiro inválido vindo do programa */
#define ERRO_NAO_E_PASTA   -6
#define ERRO_E_PASTA       -7
#define ERRO_NAO_VAZIA     -8
#define ERRO_LIMITE        -9   /* tabela cheia (arquivos, processos) */
#define ERRO_DESCRITOR    -10
#define ERRO_PIPE_QUEBRADO -11
#define ERRO_SEM_PROCESSO -12
#define ERRO_FORMATO      -13   /* não é um executável ELF válido */
#define ERRO_ACESSO       -14   /* operação não permitida */
#define ERRO_OCUPADO      -15
#define ERRO_REDE         -16   /* sem rede configurada, ou o destino não responde */
#define ERRO_FIM          -17   /* a conexão terminou: o outro lado não manda mais nada */

/* Modos de abrir */
#define ABRIR_LER       1
#define ABRIR_ESCREVER  2
#define ABRIR_CRIAR     4
#define ABRIR_TRUNCAR   8
#define ABRIR_ACRESCENTAR 16

/* Origem do posicionar */
#define POS_INICIO 0
#define POS_ATUAL  1
#define POS_FIM    2

/* Tipos de entrada */
#define TIPO_ARQUIVO  1
#define TIPO_PASTA    2
#define TIPO_PROGRAMA 3
#define TIPO_TERMINAL 4
#define TIPO_PIPE     5

#define NOME_MAX 64

typedef struct {
    unsigned int tipo;
    unsigned int tamanho;
} info_arquivo_t;

typedef struct {
    char         nome[NOME_MAX];
    unsigned int tipo;
    unsigned int tamanho;
} entrada_pasta_t;

typedef struct {
    unsigned short ano;
    unsigned char  mes, dia, hora, minuto, segundo, dia_semana;
} data_t;

/* SIS_BIP: uma nota. freq 0 = pausa. Duração arredondada para 10 ms. */
typedef struct {
    unsigned short freq;   /* Hz */
    unsigned short ms;
} nota_t;
#define BIP_SUBSTITUIR 0   /* para o que estiver tocando e começa esta lista */
#define BIP_ENFILEIRAR 1   /* toca depois do que já está na fila */

/* Rede (fase 3 da 2.0). Endereços IPv4 na ordem da rede: 10.0.2.15 é
 * 10 | 0 << 8 | 2 << 16 | 15 << 24. */
#define REDE_UDP  1
#define REDE_ICMP 2
#define REDE_TCP  3
typedef struct {
    char          placa[32];
    unsigned char mac[6];
    unsigned char cabo, configurado, dhcp, reservado;
    unsigned int  ip, mascara, roteador, dns;
    unsigned int  pacotes_rx, pacotes_tx;
} info_rede_t;

/* SIS_INFO_SISTEMA */
#define INFO_MEMORIA   1
#define INFO_PROCESSOS 2
#define INFO_VERSAO    3
#define INFO_USB       4   /* relatório em texto: controladores, portas e dispositivos */
#define INFO_PCI       5   /* relatório em texto: dispositivos PCI (H35) */
/* e_flags dos executáveis feitos pelo cc do SaguiOS: abre janela. Os sem a
 * marca são programas de texto, abertos dentro de um Terminal pela área de
 * trabalho e pelo Editor. */
#define ELF_SAGUI_JANELA 0x4A475301u
#define ELF_SAGUI_TEXTO  0x54475301u   /* feito pelo cc e sem janela: programa de texto */
#define INFO_REGISTRO  6   /* o diário do kernel (as últimas linhas do klog), para diagnóstico no hardware */
/* Espaço dos discos (H83, comando df): um info_espaco_t para "/" e outro para
 * "/usb", se houver pendrive montado. n = quantos cabem; devolve quantos vieram. */
#define INFO_DISCOS_ESPACO 7

typedef struct {
    unsigned int ram_total_kb;
    unsigned int gerida_kb;
    unsigned int livre_kb;
    unsigned int usada_kb;
} info_memoria_t;

typedef struct {
    int          pid;
    int          pai;
    unsigned int estado;     /* 1 pronto, 2 executando, 3 bloqueado, 4 zumbi */
    unsigned int memoria_kb;
    unsigned int cpu_ms;
    unsigned int usuario;    /* 1 = programa no anel 3 */
    char         nome[32];
} info_processo_t;

typedef struct {
    char         versao[32];
    unsigned int imagem_bytes;   /* tamanho útil da imagem de boot */
    unsigned int segundos_ligado;
    unsigned int largura, altura;
} info_versao_t;

typedef struct {
    char         ponto[8];         /* "/" ou "/usb" */
    char         tipo[24];         /* "ramdisk + RAM" ou "FAT32" */
    char         rotulo[12];       /* rótulo do pendrive (vazio para "/") */
    unsigned int total_kb;
    unsigned int usado_kb;
    unsigned int livre_kb;
} info_espaco_t;

typedef struct {
    char         nome[8];          /* "usb0" */
    char         fabricante[32];
    char         produto[48];
    char         formato[40];      /* "FAT32" ou "...: formato não suportado" */
    char         rotulo[12];
    unsigned int setores;          /* de 512 bytes */
    unsigned int montado;
    unsigned int suportado;
} info_disco_t;

/* SIS_TECLAS_SEGURADAS: posição de algumas teclas no mapa (código de
 * varredura do conjunto 1; 128 + código para as teclas estendidas E0). */
#define CV_ESC        0x01
#define CV_CTRL       0x1D
#define CV_SHIFT      0x2A
#define CV_ALT        0x38
#define CV_ESPACO     0x39
#define CV_B          0x30
#define CV_CTRL_DIR   (0x80 | 0x1D)
#define CV_CIMA       (0x80 | 0x48)
#define CV_BAIXO      (0x80 | 0x50)
#define CV_ESQUERDA   (0x80 | 0x4B)
#define CV_DIREITA    (0x80 | 0x4D)

/* Modos do terminal */
#define TERM_LINHA 0
#define TERM_BRUTO 1

/* No modo bruto, teclas sem caractere chegam como ESC (27) + uma letra; a
 * própria tecla Esc chega como 27, 27. */
#define TECLA_B_CIMA     'A'
#define TECLA_B_BAIXO    'B'
#define TECLA_B_DIREITA  'C'
#define TECLA_B_ESQUERDA 'D'
#define TECLA_B_HOME     'H'
#define TECLA_B_END      'F'
#define TECLA_B_DELETE   'X'
#define TECLA_B_PGUP     'P'
#define TECLA_B_PGDN     'N'
#define TECLA_B_REDESENHAR 'R'   /* não é tecla: outro processo escreveu na tela */

/* Sequências que o terminal entende na saída: ESC [ D (cursor à esquerda),
 * ESC [ C (à direita), ESC [ K (apaga até o fim da linha), ESC [ J (limpa a tela). */

/* Eventos de janela */
#define EV_CLIQUE    1   /* botão apertado; "cliques" = 2 no duplo clique */
#define EV_SOLTOU    2
#define EV_MOVER     3   /* movimento sobre a janela */
#define EV_TECLA     4   /* "tecla": caractere Latin-1, ou 0x100+ para setas etc. */
#define EV_FECHAR    5   /* clicaram no X da moldura */
#define EV_REDIMENSIONAR 6  /* a janela mudou de tamanho: x = largura, y = altura novas (H78) */
#define EV_MENU      7   /* item escolhido no menu aberto por SIS_MENU_JANELA: tecla = índice (H81) */

/* Opções de janela (SIS_JANELA_OPCOES). */
#define JAN_REDIMENSIONAVEL 1

/* Teclas especiais nos eventos de janela. São os mesmos valores do enum do
 * kernel (teclado.h), que por isso não precisa destas definições. */
#ifndef SAGUI_KERNEL
#define TECLA_CIMA      0x100
#define TECLA_BAIXO     0x101
#define TECLA_ESQUERDA  0x102
#define TECLA_DIREITA   0x103
#define TECLA_HOME      0x104
#define TECLA_END       0x105
#define TECLA_PGUP      0x106
#define TECLA_PGDN      0x107
#define TECLA_INSERT    0x108
#define TECLA_DELETE    0x109
#define TECLA_F2        0x10B   /* renomear no Arquivos (H91); F1 é 0x10A, F12 0x115 */
#define TECLA_ESC       0x116
#define TECLA_IMPRIMIR  0x117   /* Print Screen (H87): o gerenciador de janelas abre o /bin/captura */
#endif

#define JANELA_LARG_MAX 1000
#define JANELA_ALT_MAX  700

typedef struct {
    unsigned int tipo;
    int          janela;
    int          x, y;        /* relativo à área de conteúdo da janela */
    unsigned int botoes;     /* 1 esquerdo, 2 direito */
    unsigned int cliques;
    unsigned int tecla;
    unsigned int modificadores;   /* 1 Shift, 2 Ctrl */
} evento_t;

typedef struct { int x, y, largura, altura; } retangulo_t;

/* Código de saída de um processo encerrado pelo kernel (erro do programa). */
#define SAIDA_ENCERRADO_PELO_KERNEL -1000

#endif
