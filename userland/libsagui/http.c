/* http.c - cliente HTTP/1.1 para os programas (H49).
 *
 * Uma página na web é pedida com um texto simples por uma conexão TCP:
 *
 *     GET /caminho HTTP/1.1
 *     Host: exemplo.com
 *     Connection: close
 *
 * A resposta começa com a linha de situação ("HTTP/1.1 200 OK"), cabeçalhos
 * ("Nome: valor") e uma linha vazia; depois vem o conteúdo. O tamanho do
 * conteúdo vem em Content-Length, ou o conteúdo vem em pedaços
 * ("Transfer-Encoding: chunked": cada pedaço começa com o tamanho em
 * hexadecimal), ou simplesmente vai até a conexão fechar. Respostas 301, 302,
 * 303, 307 e 308 mandam para outro endereço (Location), que é seguido até 5
 * vezes.
 *
 * O transporte é abstrato (um "canal"): TCP puro para http:// e, a partir da
 * H53, TLS por cima do TCP para https://. */
#include "sagui.h"
#include "http.h"

/* ------------------------------------------------------------ endereço */

int url_separar(const char *url, url_t *u)
{
    memset(u, 0, sizeof *u);
    const char *p = url;
    if (compara_texto_n(p, "http://", 7) == 0) { p += 7; u->seguro = false; u->porta = 80; }
    else if (compara_texto_n(p, "https://", 8) == 0) { p += 8; u->seguro = true; u->porta = 443; }
    else return ERRO_INVALIDO;
    int k = 0;
    while (*p && *p != '/' && *p != ':' && *p != '?' && k < (int)sizeof u->servidor - 1) u->servidor[k++] = *p++;
    u->servidor[k] = 0;
    if (!k) return ERRO_INVALIDO;
    if (*p == ':') {
        p++;
        int porta = 0;
        while (*p >= '0' && *p <= '9') porta = porta * 10 + (*p++ - '0');
        if (porta <= 0 || porta > 65535) return ERRO_INVALIDO;
        u->porta = porta;
    }
    if (*p != '/') { u->caminho[0] = '/'; copia_texto(u->caminho + 1, p, sizeof u->caminho - 1); }
    else copia_texto(u->caminho, p, sizeof u->caminho);
    return 0;
}

/* ------------------------------------------------------------ canal */

static int canal_ler(canal_t *c, void *buf, unsigned max)
{
    if (c->tls) return ponte_tls_ler(c->tls, buf, max);
    return rede_receber(c->socket, buf, max, NULL);
}

static int canal_escrever(canal_t *c, const void *dados, unsigned n)
{
    if (c->tls) return ponte_tls_escrever(c->tls, dados, n);
    return tcp_enviar_tudo(c->socket, dados, n);
}

int canal_abrir(canal_t *c, const url_t *u, char *erro, int max_erro)
{
    memset(c, 0, sizeof *c);
    c->socket = -1;
    unsigned ip;
    int r = dns_resolver(u->servidor, &ip);
    if (r < 0) { formatar_texto(erro, (size_t)max_erro, "%s: %s", u->servidor, r == ERRO_NAO_EXISTE ? "nome não encontrado" : nome_erro(r)); return r; }
    c->socket = tcp_conectar(ip, u->porta, 15000);
    if (c->socket < 0) { formatar_texto(erro, (size_t)max_erro, "%s: não conectou", u->servidor); return c->socket; }
    if (u->seguro) {
        c->tls = ponte_tls_conectar(c->socket, u->servidor, erro, max_erro);
        if (!c->tls) { rede_fechar(c->socket); c->socket = -1; return ERRO_REDE; }
    }
    return 0;
}

void canal_fechar(canal_t *c)
{
    if (c->tls) ponte_tls_fechar(c->tls);
    if (c->socket >= 0) rede_fechar(c->socket);
    c->tls = NULL;
    c->socket = -1;
}

/* Lê um byte, esperando o que for preciso (dentro de um limite de tempo).
 * Usa um buffer próprio para não chamar o kernel byte a byte. */
static int ler_byte(canal_t *c)
{
    if (c->pos < c->tam) return (unsigned char)c->buf[c->pos++];
    unsigned inicio = tempo_ms();
    for (;;) {
        int n = canal_ler(c, c->buf, sizeof c->buf);
        if (n > 0) { c->tam = (unsigned)n; c->pos = 1; return (unsigned char)c->buf[0]; }
        if (n < 0) return -1;                           /* fim ou erro */
        if (tempo_ms() - inicio > 30000) return -1;
        dormir(5);
    }
}

static int linha_http(canal_t *c, char *linha, int max)
{
    int k = 0;
    for (;;) {
        int b = ler_byte(c);
        if (b < 0) { linha[k] = 0; return k ? k : -1; }
        if (b == '\n') break;
        if (b != '\r' && k < max - 1) linha[k++] = (char)b;
    }
    linha[k] = 0;
    return k;
}

static bool cabecalho(const char *linha, const char *nome, const char **valor)
{
    size_t n = tam_texto(nome);
    for (size_t i = 0; i < n; i++) {
        char a = linha[i], b = nome[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return false;
    }
    if (linha[n] != ':') return false;
    const char *v = linha + n + 1;
    while (*v == ' ') v++;
    *valor = v;
    return true;
}

/* ------------------------------------------------------------ pedido */

int http_obter(const char *url_inicial, http_receptor_t receptor, void *ctx, http_resposta_t *resp)
{
    return http_pedir("GET", url_inicial, NULL, NULL, 0, receptor, ctx, resp);
}

/* Um pedido qualquer (GET ou POST, H69). "extra" são cabeçalhos a mais, já
 * no formato "Nome: valor\r\n" (pode ser NULL); "corpo" vai depois deles,
 * com Content-Length. Pedidos com corpo não seguem redirecionamentos. */
int http_pedir(const char *metodo, const char *url_inicial, const char *extra, const void *corpo, unsigned n_corpo,
               http_receptor_t receptor, void *ctx, http_resposta_t *resp)
{
    return http_pedir_completo(metodo, url_inicial, extra, corpo, n_corpo, receptor, ctx, resp, NULL);
}

/* O mesmo, mostrando os cabeçalhos de cada resposta (H82: os cookies e o
 * fetch do Cipó precisam deles). "cabecalhos" recebe primeiro a linha de
 * situação ("HTTP/1.1 200 OK") e depois cada linha "Nome: valor", com o
 * endereço que respondeu; também nas respostas de redirecionamento. */
int http_pedir_completo(const char *metodo, const char *url_inicial, const char *extra, const void *corpo, unsigned n_corpo,
                        http_receptor_t receptor, void *ctx, http_resposta_t *resp, http_cabecalhos_t cabecalhos)
{
    char url[512];
    copia_texto(url, url_inicial, sizeof url);
    memset(resp, 0, sizeof *resp);
    for (int redirecionamentos = 0; redirecionamentos < 6; redirecionamentos++) {
        url_t u;
        if (url_separar(url, &u) < 0) { copia_texto(resp->erro, "endereço inválido (use http:// ou https://)", sizeof resp->erro); return ERRO_INVALIDO; }
        canal_t c;
        if (canal_abrir(&c, &u, resp->erro, sizeof resp->erro) < 0) return ERRO_REDE;
        char pedido[6144];   /* com os cookies do Cipó (H82) */
        bool porta_padrao = u.porta == (u.seguro ? 443 : 80);
        char host[80];
        if (porta_padrao) copia_texto(host, u.servidor, sizeof host);
        else formatar_texto(host, sizeof host, "%s:%d", u.servidor, u.porta);
        char tamanho_corpo[48] = "";
        if (corpo) formatar_texto(tamanho_corpo, sizeof tamanho_corpo, "Content-Length: %u\r\n", n_corpo);
        int n = formatar_texto(pedido, sizeof pedido,
                               "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: SaguiOS/2.2.0 (+https://github.com/erigobeli/sagui-os)\r\nAccept: */*\r\n"
                               "Accept-Encoding: identity\r\nConnection: close\r\n%s%s\r\n", metodo, u.caminho, host,
                               extra ? extra : "", tamanho_corpo);
        if (n >= (int)sizeof pedido - 1) { canal_fechar(&c); copia_texto(resp->erro, "cabeçalhos grandes demais", sizeof resp->erro); return ERRO_INVALIDO; }
        if (canal_escrever(&c, pedido, (unsigned)n) < 0 || (corpo && n_corpo && canal_escrever(&c, corpo, n_corpo) < 0)) { canal_fechar(&c); copia_texto(resp->erro, "a conexão caiu ao mandar o pedido", sizeof resp->erro); return ERRO_REDE; }
        char linha[2048];   /* Set-Cookie e Content-Security-Policy passam fácil de 512 */
        if (linha_http(&c, linha, sizeof linha) < 0 || compara_texto_n(linha, "HTTP/1.", 7) != 0) {
            canal_fechar(&c);
            copia_texto(resp->erro, "resposta inválida do servidor", sizeof resp->erro);
            return ERRO_REDE;
        }
        resp->situacao = texto_para_int(linha + 9);
        if (cabecalhos) cabecalhos(ctx, url, linha);
        long tamanho = -1;
        bool pedacos = false;
        char destino[512] = "";
        resp->tipo[0] = 0;
        while (linha_http(&c, linha, sizeof linha) > 0) {
            const char *v;
            if (cabecalhos) cabecalhos(ctx, url, linha);
            if (cabecalho(linha, "Content-Length", &v)) tamanho = texto_para_int(v);
            else if (cabecalho(linha, "Transfer-Encoding", &v) && compara_texto_n(v, "chunked", 7) == 0) pedacos = true;
            else if (cabecalho(linha, "Location", &v)) copia_texto(destino, v, sizeof destino);
            else if (cabecalho(linha, "Content-Type", &v)) copia_texto(resp->tipo, v, sizeof resp->tipo);
        }
        if (resp->situacao >= 300 && resp->situacao < 400 && destino[0] && !corpo) {
            canal_fechar(&c);
            if (destino[0] == '/')
                formatar_texto(url, sizeof url, "%s://%s%s", u.seguro ? "https" : "http", host, destino);
            else copia_texto(url, destino, sizeof url);
            registrar("http: %d -> %s", resp->situacao, url);
            continue;
        }
        copia_texto(resp->url_final, url, sizeof resp->url_final);
        /* Corpo. */
        char bloco[4096];
        int ok = 0;
        if (pedacos) {
            for (;;) {
                if (linha_http(&c, linha, sizeof linha) < 0) { ok = ERRO_REDE; break; }
                unsigned t = 0;
                for (const char *p = linha; *p; p++) {
                    char h = *p;
                    if (h >= '0' && h <= '9') t = t * 16 + (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') t = t * 16 + (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') t = t * 16 + (unsigned)(h - 'A' + 10);
                    else break;
                }
                if (t == 0) break;
                while (t > 0) {
                    unsigned k = 0;
                    while (k < t && k < sizeof bloco) {
                        int b = ler_byte(&c);
                        if (b < 0) { ok = ERRO_REDE; break; }
                        bloco[k++] = (char)b;
                    }
                    if (ok < 0) break;
                    if (receptor(ctx, bloco, k) < 0) { ok = ERRO_INVALIDO; break; }
                    resp->recebidos += k;
                    t -= k;
                }
                if (ok < 0) break;
                linha_http(&c, linha, sizeof linha);   /* o \r\n depois do pedaço */
            }
        } else {
            for (;;) {
                if (tamanho >= 0 && (long)resp->recebidos >= tamanho) break;
                unsigned k = 0;
                /* O que já está no buffer da leitura de linhas vem primeiro. */
                while (c.pos < c.tam && k < sizeof bloco && (tamanho < 0 || (long)(resp->recebidos + k) < tamanho)) bloco[k++] = c.buf[c.pos++];
                if (k == 0) {
                    int b = ler_byte(&c);
                    if (b < 0) { if (tamanho >= 0) ok = ERRO_REDE; break; }
                    bloco[k++] = (char)b;
                }
                if (receptor(ctx, bloco, k) < 0) { ok = ERRO_INVALIDO; break; }
                resp->recebidos += k;
            }
        }
        canal_fechar(&c);
        if (ok < 0 && !resp->erro[0]) copia_texto(resp->erro, ok == ERRO_INVALIDO ? "parado por quem pediu" : "a conexão caiu no meio", sizeof resp->erro);
        return ok;
    }
    copia_texto(resp->erro, "redirecionamentos demais", sizeof resp->erro);
    return ERRO_REDE;
}
