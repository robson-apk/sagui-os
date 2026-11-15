/* configuracoes - o app de Configurações do SaguiOS (H38).
 *
 * Uma janela com abas na lateral. Cada aba é um pequeno "painel" com uma
 * função que desenha e outra que trata os eventos. As escolhas vão para as
 * configurações do sistema (config_gravar, H37), guardadas no pendrive; o
 * kernel percebe a mudança e redesenha o que depender delas (o fundo da
 * área de trabalho, por exemplo). As fases seguintes acrescentam abas
 * (Som, Rede, IA, Programas). */
#include "sagui.h"
#include "gui.h"

#define L 600
#define A 440
#define LATERAL 170
#define X0 (LATERAL + 20)

static jan_t *j;
static int aba;
static char recado[96];

enum { ABA_RELOGIO, ABA_APARENCIA, ABA_SOM, ABA_REDE, ABA_PROGRAMAS, ABA_SOBRE, ABA_IA, QTD_ABAS };
static const char *const nomes_abas[QTD_ABAS] = { "Relógio", "Aparência", "Som", "Rede", "Programas", "Sobre o computador", "IA" };
static botao_t botoes_abas[QTD_ABAS];

static void mostrar_mensagem(const char *fmt, int r)
{
    /* r: 1 guardado no pendrive, 0 só até desligar, <0 erro */
    if (r == 1) formatar_texto(recado, sizeof recado, "%s Guardado no pendrive.", fmt);
    else if (r == 0) formatar_texto(recado, sizeof recado, "%s Sem pendrive: vale até desligar.", fmt);
    else formatar_texto(recado, sizeof recado, "%s", fmt);
    g_retangulo(j, X0, A - 36, L - X0 - 10, LETRA_A, COR_FUNDO);
    g_texto(j, X0, A - 36, recado, r < 0 && r != -100 ? 0xB03030 : COR_DESTAQUE);
    jan_mostrar_area(j, X0, A - 36, L - X0 - 10, LETRA_A);
}

static void titulo(const char *t)
{
    g_texto(j, X0, 16, t, COR_TEXTO);
    g_retangulo(j, X0, 36, L - X0 - 20, 1, COR_BORDA);
}

/* ------------------------------------------------------------ Relógio */

static caixa_t c_dia, c_mes, c_ano, c_hora, c_min, c_fuso;
static botao_t b_aplicar, b_fuso, b_automatico, b_internet_agora;

static void numero_em(caixa_t *c, int v, int digitos)
{
    char t[8];
    formatar_texto(t, sizeof t, digitos == 4 ? "%04d" : "%02d", v);
    caixa_definir(c, t);
}

static void relogio_agora(void)
{
    data_t d;
    ler_data(&d);
    char t[48];
    formatar_texto(t, sizeof t, "Agora: %02u/%02u/%u  %02u:%02u:%02u", d.dia, d.mes, d.ano, d.hora, d.minuto, d.segundo);
    g_retangulo(j, X0, 52, 320, LETRA_A, COR_FUNDO);
    g_texto(j, X0, 52, t, COR_TEXTO);
    jan_mostrar_area(j, X0, 52, 320, LETRA_A);
}

static void relogio_desenhar(void)
{
    titulo("Relógio");
    relogio_agora();
    data_t d;
    ler_data(&d);
    g_texto(j, X0, 92, "Dia    Mês    Ano        Hora   Minuto", COR_TEXTO_FRACO);
    c_dia = (caixa_t){ .x = X0, .y = 112, .l = 40 };            numero_em(&c_dia, d.dia, 2);
    c_mes = (caixa_t){ .x = X0 + 56, .y = 112, .l = 40 };       numero_em(&c_mes, d.mes, 2);
    c_ano = (caixa_t){ .x = X0 + 112, .y = 112, .l = 56 };      numero_em(&c_ano, d.ano, 4);
    c_hora = (caixa_t){ .x = X0 + 200, .y = 112, .l = 40 };     numero_em(&c_hora, d.hora, 2);
    c_min = (caixa_t){ .x = X0 + 256, .y = 112, .l = 40 };      numero_em(&c_min, d.minuto, 2);
    caixa_desenhar(j, &c_dia); caixa_desenhar(j, &c_mes); caixa_desenhar(j, &c_ano);
    caixa_desenhar(j, &c_hora); caixa_desenhar(j, &c_min);
    b_aplicar = (botao_t){ X0, 150, 150, 28, "Acertar o relógio", false };
    botao_desenhar(j, &b_aplicar);

    g_texto(j, X0, 210, "Fuso horário (horas a partir de UTC):", COR_TEXTO);
    g_texto(j, X0, 230, "Brasília = -3. Usado pelo relógio automático", COR_TEXTO_FRACO);
    g_texto(j, X0, 248, "pela internet.", COR_TEXTO_FRACO);
    char fuso[16];
    if (config_ler("relogio.fuso", fuso, sizeof fuso) < 0) copia_texto(fuso, "-3", sizeof fuso);
    c_fuso = (caixa_t){ .x = X0, .y = 274, .l = 60 };
    caixa_definir(&c_fuso, fuso);
    caixa_desenhar(j, &c_fuso);
    b_fuso = (botao_t){ X0 + 76, 272, 110, 28, "Salvar fuso", false };
    botao_desenhar(j, &b_fuso);
    char aut[8];
    bool automatico = config_ler("relogio.automatico", aut, sizeof aut) >= 0 && compara_texto(aut, "sim") == 0;
    b_automatico = (botao_t){ X0, 320, 220, 28, automatico ? "Automático: desligar" : "Acertar sozinho pela rede", false };
    b_internet_agora = (botao_t){ X0 + 230, 320, 150, 28, "Acertar agora", false };
    botao_desenhar(j, &b_automatico);
    botao_desenhar(j, &b_internet_agora);
}

static void relogio_tratar(const evento_t *e)
{
    caixa_t *cs[] = { &c_dia, &c_mes, &c_ano, &c_hora, &c_min, &c_fuso };
    for (int i = 0; i < 6; i++) caixa_tratar(j, cs[i], e);
    if (botao_tratar(j, &b_aplicar, e)) {
        data_t d = { 0 };
        d.dia = (unsigned char)texto_para_int(c_dia.texto);
        d.mes = (unsigned char)texto_para_int(c_mes.texto);
        d.ano = (unsigned short)texto_para_int(c_ano.texto);
        d.hora = (unsigned char)texto_para_int(c_hora.texto);
        d.minuto = (unsigned char)texto_para_int(c_min.texto);
        int r = ajustar_data(&d);
        registrar("configuracoes: acertar relogio -> %d", r);
        mostrar_mensagem(r < 0 ? "Data ou hora inválida." : "Relógio acertado.", r < 0 ? -1 : -100);
        relogio_agora();
    }
    if (botao_tratar(j, &b_automatico, e)) {
        char aut[8];
        bool automatico = config_ler("relogio.automatico", aut, sizeof aut) >= 0 && compara_texto(aut, "sim") == 0;
        int r = config_gravar("relogio.automatico", automatico ? "nao" : "sim");
        registrar("configuracoes: relogio automatico %s", automatico ? "desligado" : "ligado");
        g_retangulo(j, LATERAL + 1, 0, L - LATERAL - 1, A - 40, COR_FUNDO);
        relogio_desenhar();
        jan_mostrar(j);
        mostrar_mensagem(automatico ? "Relógio automático desligado." : "Vai acertar sozinho quando a rede ligar.", r);
        return;
    }
    if (botao_tratar(j, &b_internet_agora, e)) {
        char *argv[] = { "relogio", "internet", NULL };
        int pid = executar("/bin/relogio", argv);
        int codigo = 1;
        if (pid > 0) esperar(pid, &codigo);
        registrar("configuracoes: acertar pela internet -> %d", codigo);
        mostrar_mensagem(codigo == 0 ? "Relógio acertado pela internet." : "Não consegui falar com o servidor de hora.", codigo == 0 ? -100 : -1);
        relogio_agora();
        return;
    }
    if (botao_tratar(j, &b_fuso, e)) {
        int f = texto_para_int(c_fuso.texto);
        if (f < -12 || f > 14) { mostrar_mensagem("Fuso inválido (de -12 a 14).", -1); return; }
        char t[8];
        formatar_texto(t, sizeof t, "%d", f);
        int r = config_gravar("relogio.fuso", t);
        registrar("configuracoes: fuso %s -> %d", t, r);
        mostrar_mensagem("Fuso salvo.", r);
    }
}

/* ------------------------------------------------------------ Aparência */

#define QTD_FUNDOS 7
/* "sol" e "mata-atlantica" vieram do pull request #1 (Robson): o Brasil com
 * sol é desenhado pelo kernel; a Mata Atlântica é uma imagem feita por IA. */
static const char *const fundos[QTD_FUNDOS] = { "brasil", "sol", "mata-atlantica", "mata", "ceu", "noite", "terra" };
static const char *const nomes_fundos[QTD_FUNDOS] = { "Brasil", "Com sol", "M. Atlântica", "Mata", "Céu", "Noite", "Terra" };
static const uint32_t amostras[QTD_FUNDOS][3] = {
    { 0x0B7A3E, 0xE0B92A, 0x14337E }, { 0x2E8A4A, 0xFFE89A, 0x1C5AA8 }, { 0xE8D49A, 0x5E8A4A, 0x3A7AB8 },
    { 0x0E3B22, 0x2E7D32, 0x0A2416 }, { 0x1E5AA8, 0x6FB3E8, 0xD8EEF8 },
    { 0x0B1026, 0x2B1E5A, 0x060814 }, { 0x7A3E14, 0xD98A3A, 0x3A1E0C },
};
#define QTD_TEMAS 4
static const char *const temas[QTD_TEMAS] = { "escuro", "claro", "mata", "azul" };
static const char *const nomes_temas[QTD_TEMAS] = { "Escuro", "Claro", "Mata", "Azul" };
static botao_t b_fundos[QTD_FUNDOS], b_sagui, b_tema, b_barra, b_efeitos;

static int tema_atual(void)
{
    char t[12];
    if (config_ler("aparencia.tema", t, sizeof t) < 0) return 0;
    for (int i = 0; i < QTD_TEMAS; i++) if (compara_texto(t, temas[i]) == 0) return i;
    return 0;
}

static void aparencia_desenhar(void)
{
    titulo("Aparência");
    char atual[16];
    if (config_ler("aparencia.fundo", atual, sizeof atual) < 0) copia_texto(atual, "brasil", sizeof atual);
    g_texto(j, X0, 52, "Fundo da área de trabalho:", COR_TEXTO);
    for (int i = 0; i < QTD_FUNDOS; i++) {
        int x = X0 + (i % 4) * 100, y = 76 + (i / 4) * 84;
        for (int k = 0; k < 3; k++) g_retangulo(j, x, y + k * 14, 90, 14, amostras[i][k]);
        bool escolhido = compara_texto(atual, fundos[i]) == 0;
        g_moldura(j, x - 2, y - 2, 94, 46, escolhido ? COR_SELECAO : COR_BORDA);
        if (escolhido) g_moldura(j, x - 3, y - 3, 96, 48, COR_SELECAO);
        b_fundos[i] = (botao_t){ x, y + 48, 90, 24, nomes_fundos[i], false };
        botao_desenhar(j, &b_fundos[i]);
    }
    /* O sagui, o tema e a doca ou a barra de tarefas, lado a lado. */
    char v[12];
    bool mostra = config_ler("aparencia.sagui", v, sizeof v) < 0 || compara_texto(v, "nao") != 0;
    bool tarefas = config_ler("aparencia.barra", v, sizeof v) >= 0 && compara_texto(v, "tarefas") == 0;
    static char rot_tema[24];
    formatar_texto(rot_tema, sizeof rot_tema, "%s (trocar)", nomes_temas[tema_atual()]);
    g_texto(j, X0, 252, "O sagui:", COR_TEXTO);
    g_texto(j, X0 + 135, 252, "Tema:", COR_TEXTO);
    g_texto(j, X0 + 270, 252, "Embaixo:", COR_TEXTO);
    b_sagui = (botao_t){ X0, 274, 125, 28, mostra ? "Mostrando" : "Escondido", false };
    b_tema = (botao_t){ X0 + 135, 274, 125, 28, rot_tema, false };
    b_barra = (botao_t){ X0 + 270, 274, 125, 28, tarefas ? "Barra em cima" : "Doca", false };
    botao_desenhar(j, &b_sagui);
    botao_desenhar(j, &b_tema);
    botao_desenhar(j, &b_barra);
    char efeitos[12];
    if (config_ler("aparencia.efeitos", efeitos, sizeof efeitos) < 0) copia_texto(efeitos, "auto", sizeof efeitos);
    g_texto(j, X0, 318, "Efeitos (vidro, sombras, animações):", COR_TEXTO);
    b_efeitos = (botao_t){ X0, 340, 395, 28,
                           compara_texto(efeitos, "leves") == 0 ? "Leves (rápido): trocar para automático" :
                           compara_texto(efeitos, "completos") == 0 ? "Completos: trocar para leves" :
                           "Automático (pela velocidade): trocar para completos", false };
    botao_desenhar(j, &b_efeitos);
}

static void aparencia_redesenhar(const char *msg, int r)
{
    g_retangulo(j, LATERAL + 1, 0, L - LATERAL - 1, A - 40, COR_FUNDO);
    aparencia_desenhar();
    jan_mostrar(j);
    mostrar_mensagem(msg, r);
}

static void aparencia_tratar(const evento_t *e)
{
    char v[12];
    if (botao_tratar(j, &b_tema, e)) {
        const char *novo = temas[(tema_atual() + 1) % QTD_TEMAS];
        int r = config_gravar("aparencia.tema", novo);
        registrar("configuracoes: tema %s", novo);
        aparencia_redesenhar("Tema trocado.", r);
        return;
    }
    if (botao_tratar(j, &b_barra, e)) {
        bool tarefas = config_ler("aparencia.barra", v, sizeof v) >= 0 && compara_texto(v, "tarefas") == 0;
        int r = config_gravar("aparencia.barra", tarefas ? "doca" : "tarefas");
        registrar("configuracoes: barra %s", tarefas ? "doca" : "tarefas");
        aparencia_redesenhar(tarefas ? "A doca voltou." : "Janelas na barra de cima.", r);
        return;
    }
    if (botao_tratar(j, &b_efeitos, e)) {
        if (config_ler("aparencia.efeitos", v, sizeof v) < 0) copia_texto(v, "auto", sizeof v);
        const char *novo = compara_texto(v, "auto") == 0 ? "completos" : compara_texto(v, "completos") == 0 ? "leves" : "auto";
        int r = config_gravar("aparencia.efeitos", novo);
        registrar("configuracoes: efeitos %s", novo);
        aparencia_redesenhar("Aparência trocada.", r);
        return;
    }
    for (int i = 0; i < QTD_FUNDOS; i++)
        if (botao_tratar(j, &b_fundos[i], e)) {
            int r = config_gravar("aparencia.fundo", fundos[i]);
            registrar("configuracoes: fundo %s -> %d", fundos[i], r);
            aparencia_redesenhar("Fundo trocado.", r);
            return;
        }
    if (botao_tratar(j, &b_sagui, e)) {
        char sagui[8];
        bool mostra = config_ler("aparencia.sagui", sagui, sizeof sagui) < 0 || compara_texto(sagui, "nao") != 0;
        int r = config_gravar("aparencia.sagui", mostra ? "nao" : "sim");
        registrar("configuracoes: sagui %s -> %d", mostra ? "nao" : "sim", r);
        g_retangulo(j, LATERAL + 1, 0, L - LATERAL - 1, A - 40, COR_FUNDO);
        aparencia_desenhar();
        jan_mostrar(j);
        mostrar_mensagem(mostra ? "O sagui foi descansar." : "O sagui voltou.", r);
    }
}

/* ------------------------------------------------------------ Som (H41) */

static botao_t b_menos, b_mais, b_sistema, b_testar;

static void som_desenhar(void)
{
    titulo("Som");
    int vol = som_volume(-1);
    if (vol < 0) {
        g_texto(j, X0, 56, "Nenhuma placa de som HD Audio encontrada.", COR_TEXTO);
        g_texto(j, X0, 76, "Os sons saem pelo alto-falante do PC (bip).", COR_TEXTO_FRACO);
        return;
    }
    char t[32];
    g_texto(j, X0, 56, "Volume:", COR_TEXTO);
    b_menos = (botao_t){ X0, 80, 36, 28, "-", false };
    b_mais = (botao_t){ X0 + 260, 80, 36, 28, "+", false };
    botao_desenhar(j, &b_menos);
    botao_desenhar(j, &b_mais);
    g_moldura(j, X0 + 44, 86, 208, 16, COR_BORDA);
    g_retangulo(j, X0 + 46, 88, 204 * vol / 100, 12, COR_DESTAQUE);
    formatar_texto(t, sizeof t, "%d%%", vol);
    g_texto(j, X0 + 306, 86, t, COR_TEXTO);
    char sis[8];
    bool ligados = config_ler("som.sistema", sis, sizeof sis) < 0 || compara_texto(sis, "nao") != 0;
    g_texto(j, X0, 136, "Sons do sistema (início, avisos):", COR_TEXTO);
    b_sistema = (botao_t){ X0, 158, 200, 28, ligados ? "Ligados: desligar" : "Desligados: ligar", false };
    botao_desenhar(j, &b_sistema);
    b_testar = (botao_t){ X0, 210, 140, 28, "Testar o som", false };
    botao_desenhar(j, &b_testar);
}

static void som_redesenhar(void)
{
    g_retangulo(j, LATERAL + 1, 0, L - LATERAL - 1, A - 40, COR_FUNDO);
    som_desenhar();
    jan_mostrar(j);
}

static void som_tratar(const evento_t *e)
{
    int vol = som_volume(-1);
    if (vol < 0) return;
    if (botao_tratar(j, &b_menos, e) || botao_tratar(j, &b_mais, e)) {
        bool mais = e->x >= b_mais.x;
        vol = som_volume(mais ? (vol + 10 > 100 ? 100 : vol + 10) : (vol - 10 < 0 ? 0 : vol - 10));
        registrar("configuracoes: volume %d", vol);
        static const nota_t clique[] = { { 880, 40 } };
        tocar(clique, 1, BIP_SUBSTITUIR);
        som_redesenhar();
        char p[8];
        config_ler("sistema.persistente", p, sizeof p);
        mostrar_mensagem("Volume ajustado.", compara_texto(p, "sim") == 0 ? 1 : 0);
        return;
    }
    if (botao_tratar(j, &b_sistema, e)) {
        char sis[8];
        bool ligados = config_ler("som.sistema", sis, sizeof sis) < 0 || compara_texto(sis, "nao") != 0;
        int r = config_gravar("som.sistema", ligados ? "nao" : "sim");
        registrar("configuracoes: sons do sistema %s", ligados ? "desligados" : "ligados");
        som_redesenhar();
        mostrar_mensagem(ligados ? "Sons do sistema desligados." : "Sons do sistema ligados.", r);
        return;
    }
    if (botao_tratar(j, &b_testar, e)) {
        static const nota_t melodia[] = { { 523, 120 }, { 659, 120 }, { 784, 120 }, { 1047, 300 } };
        tocar(melodia, 4, BIP_SUBSTITUIR);
        registrar("configuracoes: testar o som");
    }
}

/* ------------------------------------------------------------ Rede (H47) */

static caixa_t c_ip, c_masc, c_rot, c_dns;
static botao_t b_modo, b_aplicar_rede, b_testar_rede;   /* testar: H87 */
static bool rede_manual;

static void rede_estado_desenhar(void)
{
    info_rede_t r;
    char t[64], ip[16];
    g_retangulo(j, X0, 52, L - X0 - 10, 40, COR_FUNDO);
    if (rede_info(&r) < 0) { g_texto(j, X0, 52, "Nenhuma placa de rede com driver.", COR_TEXTO); return; }
    g_texto_n(j, X0, 52, r.placa, 44, COR_TEXTO);
    ip_para_texto(r.ip, ip);
    if (!r.cabo) copia_texto(t, "Cabo desligado.", sizeof t);
    else if (!r.configurado) copia_texto(t, "Pedindo endereço ao roteador...", sizeof t);
    else formatar_texto(t, sizeof t, "Conectado: %s", ip);
    g_texto(j, X0, 72, t, r.configurado ? COR_DESTAQUE : COR_TEXTO_FRACO);
    jan_mostrar_area(j, X0, 52, L - X0 - 10, 40);
}

static void caixa_com_rotulo(caixa_t *c, int y, const char *rotulo, const char *chave, unsigned padrao)
{
    g_texto(j, X0, y + 4, rotulo, rede_manual ? COR_TEXTO : COR_TEXTO_FRACO);
    *c = (caixa_t){ .x = X0 + 110, .y = y, .l = 150 };
    char v[24];
    if (config_ler(chave, v, sizeof v) < 0) ip_para_texto(padrao, v);
    caixa_definir(c, v);
    caixa_desenhar(j, c);
}

static void rede_desenhar(void)
{
    titulo("Rede");
    rede_estado_desenhar();
    char modo[12];
    rede_manual = config_ler("rede.modo", modo, sizeof modo) >= 0 && compara_texto(modo, "manual") == 0;
    b_modo = (botao_t){ X0, 104, 270, 28, rede_manual ? "Manual: usar automático (DHCP)" : "Automático (DHCP): usar manual", false };
    botao_desenhar(j, &b_modo);
    info_rede_t r;
    if (rede_info(&r) < 0) memset(&r, 0, sizeof r);
    caixa_com_rotulo(&c_ip, 148, "Endereço:", "rede.ip", r.ip);
    caixa_com_rotulo(&c_masc, 180, "Máscara:", "rede.mascara", r.mascara);
    caixa_com_rotulo(&c_rot, 212, "Roteador:", "rede.roteador", r.roteador);
    caixa_com_rotulo(&c_dns, 244, "DNS:", "rede.dns", r.dns);
    b_aplicar_rede = (botao_t){ X0, 284, 140, 28, "Aplicar", false };
    if (rede_manual) botao_desenhar(j, &b_aplicar_rede);
    b_testar_rede = (botao_t){ X0 + (rede_manual ? 150 : 0), 284, 160, 28, "Testar conexão", false };
    botao_desenhar(j, &b_testar_rede);
}

/* H87: testa a internet em dois passos, cada um com o tempo: o DNS acha o
 * endereço de um nome, e uma conexão TCP na porta 80 desse endereço abre. */
static void rede_testar(void)
{
    static const char *const nome = "example.com";
    char t[120], ipt[16];
    int y = 324;
    g_retangulo(j, X0, y, L - X0 - 10, 40, COR_FUNDO);
    g_texto(j, X0, y, "Testando...", COR_TEXTO_FRACO);
    jan_mostrar_area(j, X0, y, L - X0 - 10, 40);
    info_rede_t r;
    if (rede_info(&r) < 0 || !r.cabo || !r.configurado) {
        g_retangulo(j, X0, y, L - X0 - 10, 40, COR_FUNDO);
        g_texto(j, X0, y, "Sem rede: placa, cabo ou endereço faltando.", 0xC0392B);
        jan_mostrar_area(j, X0, y, L - X0 - 10, 40);
        registrar("configuracoes: teste de rede: sem rede");
        return;
    }
    unsigned t0 = tempo_ms();
    unsigned ip = 0;
    int d = dns_resolver(nome, &ip);
    unsigned t_dns = tempo_ms() - t0;
    g_retangulo(j, X0, y, L - X0 - 10, 40, COR_FUNDO);
    if (d < 0) {
        formatar_texto(t, sizeof t, "DNS falhou para %s (%s).", nome, nome_erro(d));
        g_texto(j, X0, y, t, 0xC0392B);
        jan_mostrar_area(j, X0, y, L - X0 - 10, 40);
        registrar("configuracoes: teste de rede: dns falhou (%d)", d);
        return;
    }
    ip_para_texto(ip, ipt);
    formatar_texto(t, sizeof t, "DNS: %s = %s (%u ms)", nome, ipt, t_dns);
    g_texto(j, X0, y, t, COR_DESTAQUE);
    jan_mostrar_area(j, X0, y, L - X0 - 10, 40);
    t0 = tempo_ms();
    int s = tcp_conectar(ip, 80, 5000);
    unsigned t_tcp = tempo_ms() - t0;
    if (s >= 0) { rede_fechar(s); formatar_texto(t, sizeof t, "Internet: conexão aberta em %u ms.", t_tcp); }
    else formatar_texto(t, sizeof t, "Internet: a conexão não abriu (%s).", nome_erro(s));
    g_texto(j, X0, y + 20, t, s >= 0 ? COR_DESTAQUE : 0xC0392B);
    jan_mostrar_area(j, X0, y, L - X0 - 10, 40);
    registrar("configuracoes: teste de rede: dns %u ms, tcp %s %u ms", t_dns, s >= 0 ? "ok" : "falhou", t_tcp);
}

static void rede_redesenhar(void)
{
    g_retangulo(j, LATERAL + 1, 0, L - LATERAL - 1, A - 40, COR_FUNDO);
    rede_desenhar();
    jan_mostrar(j);
}

static void rede_tratar(const evento_t *e)
{
    if (rede_manual) {
        caixa_t *cs[] = { &c_ip, &c_masc, &c_rot, &c_dns };
        for (int i = 0; i < 4; i++) caixa_tratar(j, cs[i], e);
    }
    if (botao_tratar(j, &b_modo, e)) {
        int r = config_gravar("rede.modo", rede_manual ? "dhcp" : "manual");
        registrar("configuracoes: rede %s", rede_manual ? "automatica" : "manual");
        rede_redesenhar();
        mostrar_mensagem(rede_manual ? "Modo manual: preencha e aplique." : "Endereço automático (DHCP).", r);
        return;
    }
    if (botao_tratar(j, &b_testar_rede, e)) { rede_testar(); return; }
    if (rede_manual && botao_tratar(j, &b_aplicar_rede, e)) {
        caixa_t *cs[] = { &c_ip, &c_masc, &c_rot, &c_dns };
        static const char *const chaves[] = { "rede.ip", "rede.mascara", "rede.roteador", "rede.dns" };
        for (int i = 0; i < 4; i++)
            if (!texto_para_ip(cs[i]->texto)) { mostrar_mensagem("Endereço inválido (use 192.168.0.10).", -1); return; }
        int r = 0;
        for (int i = 0; i < 4; i++) r = config_gravar(chaves[i], cs[i]->texto);
        config_gravar("rede.modo", "manual");   /* reaplica com os valores novos */
        registrar("configuracoes: rede manual %s", c_ip.texto);
        mostrar_mensagem("Rede configurada.", r);
    }
}

/* ------------------------------------------------------------ Programas (H39) */

#define MAX_INST 8
static char inst[MAX_INST][NOME_MAX];
static int qtd_inst;
static botao_t b_remover[MAX_INST];

/* Programas do sistema que podem ter o ícone escondido (os de /bin com ícone). */
#define QTD_SIST 7
static const char *const sist[QTD_SIST] = { "arquivos", "terminal", "editor", "calculadora", "banana", "sagui3d", "sobre-o-sistema" };
static const char *const nomes_sist[QTD_SIST] = { "Arquivos", "Terminal", "Editor", "Calculadora", "Banana", "Sagui 3D", "Sobre" };
static botao_t b_ocultar[QTD_SIST];

static bool esta_oculto(const char *nome)
{
    char lista[96];
    if (config_ler("programas.ocultos", lista, sizeof lista) < 0) return false;
    size_t n = tam_texto(nome);
    for (const char *p = lista; *p;) {
        while (*p == ',' || *p == ' ') p++;
        const char *fim = p;
        while (*fim && *fim != ',') fim++;
        if ((size_t)(fim - p) == n && compara_texto_n(p, nome, n) == 0) return true;
        p = fim;
    }
    return false;
}

static void programas_desenhar(void)
{
    titulo("Programas");
    g_texto(j, X0, 52, "Instalados no pendrive (/usb/programas):", COR_TEXTO);
    qtd_inst = 0;
    entrada_pasta_t e;
    for (int i = 0; qtd_inst < MAX_INST && listar("/usb/programas", i, &e) == 1; i++)
        if (e.tipo != TIPO_PASTA) copia_texto(inst[qtd_inst++], e.nome, NOME_MAX);
    int y = 76;
    if (qtd_inst == 0) { g_texto(j, X0 + 10, y, "Nenhum. Copie um programa para lá.", COR_TEXTO_FRACO); y += 24; }
    for (int i = 0; i < qtd_inst; i++, y += 30) {
        g_texto(j, X0 + 10, y + 5, inst[i], COR_TEXTO);
        b_remover[i] = (botao_t){ X0 + 220, y, 90, 26, "Remover", false };
        botao_desenhar(j, &b_remover[i]);
    }
    y += 10;
    g_texto(j, X0, y, "Do sistema (o ícone pode ser escondido):", COR_TEXTO);
    y += 22;
    for (int i = 0; i < QTD_SIST; i++) {
        int x = X0 + (i % 2) * 200, yy = y + (i / 2) * 30;
        g_texto(j, x + 4, yy + 5, nomes_sist[i], COR_TEXTO);
        b_ocultar[i] = (botao_t){ x + 104, yy, 84, 26, esta_oculto(sist[i]) ? "Mostrar" : "Esconder", false };
        botao_desenhar(j, &b_ocultar[i]);
    }
}

static void programas_redesenhar(void)
{
    g_retangulo(j, LATERAL + 1, 0, L - LATERAL - 1, A - 40, COR_FUNDO);
    programas_desenhar();
    jan_mostrar(j);
}

static void programas_tratar(const evento_t *e)
{
    for (int i = 0; i < qtd_inst; i++)
        if (botao_tratar(j, &b_remover[i], e)) {
            char pergunta[96], caminho[NOME_MAX + 20];
            formatar_texto(pergunta, sizeof pergunta, "Remover o programa %s do pendrive?", inst[i]);
            if (!confirmar("Remover programa", pergunta)) { registrar("configuracoes: remover %s cancelado", inst[i]); return; }
            formatar_texto(caminho, sizeof caminho, "/usb/programas/%s", inst[i]);
            int r = apagar(caminho);
            registrar("configuracoes: removido %s -> %d", inst[i], r);
            programas_redesenhar();
            mostrar_mensagem(r < 0 ? "Não consegui remover." : "Programa removido.", r < 0 ? -1 : -100);
            return;
        }
    for (int i = 0; i < QTD_SIST; i++)
        if (botao_tratar(j, &b_ocultar[i], e)) {
            /* Monta a lista nova: todos os ocultos, trocando o estado deste. */
            char lista[96] = "";
            for (int k = 0; k < QTD_SIST; k++) {
                bool o = esta_oculto(sist[k]);
                if (k == i) o = !o;
                if (!o) continue;
                size_t n = tam_texto(lista);
                formatar_texto(lista + n, sizeof lista - n, n ? ",%s" : "%s", sist[k]);
            }
            int r = config_gravar("programas.ocultos", lista);
            registrar("configuracoes: ocultos = %s -> %d", lista, r);
            programas_redesenhar();
            mostrar_mensagem(esta_oculto(sist[i]) ? "Ícone escondido." : "Ícone de volta.", r);
            return;
        }
}

/* ------------------------------------------------------------ Sobre o computador */

/* Nome do processador pela instrução CPUID (que os programas podem usar). */
static void nome_processador(char *t, int max)
{
    unsigned a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u));
    if (a < 0x80000004u) { copia_texto(t, "processador x86", (size_t)max); return; }
    char nome[49];
    unsigned *p = (unsigned *)nome;
    for (unsigned f = 0; f < 3; f++) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000002u + f));
        p[f * 4] = a; p[f * 4 + 1] = b; p[f * 4 + 2] = c; p[f * 4 + 3] = d;
    }
    nome[48] = 0;
    const char *s = nome;
    while (*s == ' ') s++;
    copia_texto(t, s, (size_t)max);
}

static void sobre_desenhar(void)
{
    titulo("Sobre o computador");
    char t[96], cpu[49];
    info_versao_t v;
    info_memoria_t m;
    info_sistema(INFO_VERSAO, &v, sizeof v);
    info_sistema(INFO_MEMORIA, &m, sizeof m);
    nome_processador(cpu, sizeof cpu);
    registrar("configuracoes: processador %s, memoria %u MB", cpu, m.ram_total_kb / 1024);
    int y = 52;
    formatar_texto(t, sizeof t, "SaguiOS %s", v.versao);                       g_texto(j, X0, y, t, COR_TEXTO); y += 22;
    g_texto(j, X0, y, "Processador:", COR_TEXTO_FRACO);                         g_texto(j, X0 + 112, y, cpu, COR_TEXTO); y += 22;
    formatar_texto(t, sizeof t, "%u MB no total, %u MB livres", m.ram_total_kb / 1024, m.livre_kb / 1024);
    g_texto(j, X0, y, "Memória:", COR_TEXTO_FRACO);                             g_texto(j, X0 + 112, y, t, COR_TEXTO); y += 22;
    formatar_texto(t, sizeof t, "%ux%u", v.largura, v.altura);
    g_texto(j, X0, y, "Tela:", COR_TEXTO_FRACO);                                g_texto(j, X0 + 112, y, t, COR_TEXTO); y += 22;
    formatar_texto(t, sizeof t, "%u min", v.segundos_ligado / 60);
    g_texto(j, X0, y, "Ligado há:", COR_TEXTO_FRACO);                           g_texto(j, X0 + 112, y, t, COR_TEXTO); y += 30;
    g_texto(j, X0, y, "Dispositivos (comando pci para detalhes):", COR_TEXTO_FRACO); y += 22;
    static char pci[8192];
    int n = info_sistema(INFO_PCI, pci, sizeof pci - 1);
    if (n < 0) n = 0;
    pci[n] = 0;
    /* As linhas "BB:DD.F  vvvv:mmmm  Classe" viram uma lista de classes. */
    for (char *l = pci; *l && y < A - 50;) {
        char *fim = l;
        while (*fim && *fim != '\n') fim++;
        char guardado = *fim;
        *fim = 0;
        if (l[2] == ':' && l[5] == '.' && tam_texto(l) > 20) {
            g_texto_n(j, X0, y, l + 20, 48, COR_TEXTO);
            y += 18;
        }
        if (!guardado) break;
        l = fim + 1;
    }
}

/* ------------------------------------------------------------ IA (H69) */

/* O Sagui Code conversa com modelos pelo OpenRouter. Aqui ficam o modelo, o
 * limite de gasto por sessão e a chave. A chave vai para
 * /usb/.saguios/chaves.txt (nunca para configuracoes.txt) e nunca aparece na
 * tela: a caixa mostra só asteriscos. */
static caixa_t c_modelo, c_limite, c_chave;
static char chave_digitada[200];
static botao_t b_salvar_ia;

static bool chave_guardada(void)
{
    int fd = abrir("/usb/.saguios/chaves.txt", ABRIR_LER);
    if (fd < 0) return false;
    char buf[512];
    int n = ler(fd, buf, sizeof buf - 1);
    fechar(fd);
    if (n <= 0) return false;
    buf[n] = 0;
    bool tem = false;
    for (char *p = buf; *p; p++) if (comeca_com(p, "openrouter") && (p == buf || p[-1] == '\n')) tem = true;
    memset(buf, 0, sizeof buf);
    return tem;
}

static void ia_desenhar(void)
{
    titulo("IA (Sagui Code)");
    char v[128];
    g_texto(j, X0, 56, "Modelo (do OpenRouter):", COR_TEXTO);
    c_modelo = (caixa_t){ .x = X0, .y = 76, .l = 360 };
    caixa_definir(&c_modelo, config_ler("ia.modelo", v, sizeof v) > 0 ? v : "anthropic/claude-sonnet-4.5");
    caixa_desenhar(j, &c_modelo);
    g_texto(j, X0, 116, "Limite de gasto por sessão (centavos de dólar):", COR_TEXTO);
    c_limite = (caixa_t){ .x = X0, .y = 136, .l = 100 };
    caixa_definir(&c_limite, config_ler("ia.limite_centavos", v, sizeof v) > 0 ? v : "100");
    caixa_desenhar(j, &c_limite);
    g_texto(j, X0, 176, chave_guardada() ? "Chave: guardada no pendrive. Para trocar, cole outra:" : "Chave: ainda não há. Cole a chave do OpenRouter:", COR_TEXTO);
    c_chave = (caixa_t){ .x = X0, .y = 196, .l = 360 };
    chave_digitada[0] = 0;
    caixa_definir(&c_chave, "");
    caixa_desenhar(j, &c_chave);
    g_texto(j, X0, 226, "(a chave nunca aparece na tela nem no log)", COR_TEXTO_FRACO);
    b_salvar_ia = (botao_t){ X0, 256, 120, 28, "Guardar", false };
    botao_desenhar(j, &b_salvar_ia);
}

/* Troca (ou acrescenta) a linha "openrouter = ..." de chaves.txt. */
static int gravar_chave(const char *chave)
{
    info_arquivo_t i;
    if (info_arquivo("/usb/.saguios", &i) < 0) return -1;
    static char antigo[2048], novo[2600];
    int n = 0, fd = abrir("/usb/.saguios/chaves.txt", ABRIR_LER);
    if (fd >= 0) { n = ler(fd, antigo, sizeof antigo - 1); fechar(fd); if (n < 0) n = 0; }
    antigo[n] = 0;
    int k = 0;
    for (char *p = antigo; *p;) {
        char *fim = acha_char(p, '\n');
        if (fim) *fim = 0;
        if (*p && !comeca_com(p, "openrouter")) k += formatar_texto(novo + k, sizeof novo - (size_t)k, "%s\n", p);
        if (!fim) break;
        p = fim + 1;
    }
    k += formatar_texto(novo + k, sizeof novo - (size_t)k, "openrouter = %s\n", chave);
    fd = abrir("/usb/.saguios/chaves.txt", ABRIR_ESCREVER | ABRIR_CRIAR | ABRIR_TRUNCAR);
    int r = fd >= 0 ? escrever_tudo(fd, novo, (unsigned)k) : fd;
    if (fd >= 0) fechar(fd);
    memset(antigo, 0, sizeof antigo);
    memset(novo, 0, sizeof novo);
    return r < 0 ? r : 0;
}

static void ia_tratar(const evento_t *e)
{
    caixa_tratar(j, &c_modelo, e);
    caixa_tratar(j, &c_limite, e);
    /* A caixa da chave: guardamos o texto de verdade aqui e a caixa só mostra '*'. */
    if (e->tipo == EV_CLIQUE) { c_chave.foco = false; caixa_tratar(j, &c_chave, e); }
    else if (e->tipo == EV_TECLA && c_chave.foco) {
        int n = (int)tam_texto(chave_digitada);
        if (e->tecla == 8 && n > 0) chave_digitada[--n] = 0;
        else if (e->tecla >= 33 && e->tecla < 127 && n < (int)sizeof chave_digitada - 1) { chave_digitada[n++] = (char)e->tecla; chave_digitada[n] = 0; }
        char estrelas[48];
        int m = n < 40 ? n : 40;
        memset(estrelas, '*', (size_t)m);
        estrelas[m] = 0;
        caixa_definir(&c_chave, estrelas);
        c_chave.foco = true;
        caixa_desenhar(j, &c_chave);
        jan_mostrar_area(j, c_chave.x, c_chave.y, c_chave.l, 24);
    }
    if (botao_tratar(j, &b_salvar_ia, e)) {
        int r = config_gravar("ia.modelo", c_modelo.texto);
        if (texto_para_int(c_limite.texto) > 0) config_gravar("ia.limite_centavos", c_limite.texto);
        if (chave_digitada[0]) {
            if (gravar_chave(chave_digitada) < 0) { mostrar_mensagem("Sem pendrive com .saguios: a chave não foi guardada.", -1); memset(chave_digitada, 0, sizeof chave_digitada); return; }
            memset(chave_digitada, 0, sizeof chave_digitada);
            registrar("configuracoes: chave do OpenRouter guardada");
        }
        registrar("configuracoes: ia modelo %s, limite %s", c_modelo.texto, c_limite.texto);
        g_retangulo(j, LATERAL + 1, 0, L - LATERAL - 1, A - 40, COR_FUNDO);
        ia_desenhar();
        jan_mostrar(j);
        mostrar_mensagem("IA configurada.", r);
    }
}

/* ------------------------------------------------------------ janela */

static void desenhar_tudo(void)
{
    g_retangulo(j, 0, 0, L, A, COR_FUNDO);
    g_retangulo(j, 0, 0, LATERAL, A, 0xE6EAE6);
    g_retangulo(j, LATERAL, 0, 1, A, COR_BORDA);
    for (int i = 0; i < QTD_ABAS; i++) {
        botoes_abas[i] = (botao_t){ 10, 16 + i * 40, LATERAL - 20, 30, nomes_abas[i], i == aba };
        botao_desenhar(j, &botoes_abas[i]);
    }
    if (aba == ABA_RELOGIO) relogio_desenhar();
    else if (aba == ABA_APARENCIA) aparencia_desenhar();
    else if (aba == ABA_PROGRAMAS) programas_desenhar();
    else if (aba == ABA_SOM) som_desenhar();
    else if (aba == ABA_REDE) rede_desenhar();
    else if (aba == ABA_IA) ia_desenhar();
    else sobre_desenhar();
    char p[8];
    config_ler("sistema.persistente", p, sizeof p);
    g_texto(j, 10, A - 54, compara_texto(p, "sim") == 0 ? "Guardando no" : "Sem pendrive:", COR_TEXTO_FRACO);
    g_texto(j, 10, A - 36, compara_texto(p, "sim") == 0 ? "pendrive." : "até desligar.", COR_TEXTO_FRACO);
    jan_mostrar(j);
}

int principal(int argc, char **argv)
{
    j = jan_abrir("Configurações", L, A);
    if (!j) return 1;
    if (argc > 1) {
        for (int i = 0; i < QTD_ABAS; i++)
            if (compara_texto_n(argv[1], nomes_abas[i], 3) == 0) aba = i;
        if (compara_texto(argv[1], "aparencia") == 0) aba = ABA_APARENCIA;
        if (compara_texto(argv[1], "sobre") == 0) aba = ABA_SOBRE;
        if (compara_texto(argv[1], "programas") == 0) aba = ABA_PROGRAMAS;
        if (compara_texto(argv[1], "som") == 0) aba = ABA_SOM;
        if (compara_texto(argv[1], "rede") == 0) aba = ABA_REDE;
        if (compara_texto(argv[1], "ia") == 0) aba = ABA_IA;
    }
    desenhar_tudo();
    registrar("configuracoes: aberto na aba %s", nomes_abas[aba]);
    evento_t e;
    unsigned ultimo = tempo_ms();
    for (;;) {
        if (proximo_evento(j->id, &e, false) == 1) {
            if (e.tipo == EV_FECHAR) break;
            bool trocou = false;
            for (int i = 0; i < QTD_ABAS; i++)
                if (botao_tratar(j, &botoes_abas[i], &e) && i != aba) {
                    aba = i;
                    trocou = true;
                    recado[0] = 0;
                    registrar("configuracoes: aba %s", nomes_abas[aba]);
                }
            if (trocou) { desenhar_tudo(); continue; }
            if (aba == ABA_RELOGIO) relogio_tratar(&e);
            else if (aba == ABA_APARENCIA) aparencia_tratar(&e);
            else if (aba == ABA_PROGRAMAS) programas_tratar(&e);
            else if (aba == ABA_SOM) som_tratar(&e);
            else if (aba == ABA_REDE) rede_tratar(&e);
            else if (aba == ABA_IA) ia_tratar(&e);
            continue;
        }
        if (aba == ABA_RELOGIO && tempo_ms() - ultimo >= 1000) { ultimo = tempo_ms(); relogio_agora(); }
        if (aba == ABA_REDE && tempo_ms() - ultimo >= 1000) { ultimo = tempo_ms(); rede_estado_desenhar(); }
        dormir(40);
    }
    jan_fechar(j);
    return 0;
}
