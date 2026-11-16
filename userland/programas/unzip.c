/* unzip (deszipar) - lista e extrai pacotes .zip (H93).
 *
 *   unzip [-l] [-t] [-o] [-q] [-d pasta] pacote.zip
 * Sem opções, extrai na pasta atual (ou na do -d, criada se faltar). -l
 * lista, -t só confere (CRC de cada arquivo), -o deixa substituir
 * arquivos que já existem, -q não mostra os nomes.
 *
 * Formato: PKWARE APPNOTE.TXT 6.3.x. Lemos o pacote pelo fim: o registro de
 * fim do diretório central (0x06054B50, procurado de trás para a frente,
 * porque pode haver um comentário de até 65535 bytes depois dele) diz
 * onde está o diretório central; cada registro dele (0x02014B50) tem o
 * nome, os tamanhos, o CRC e onde fica o cabeçalho local (0x04034B50),
 * depois do qual vêm os dados. Usar o diretório central (e não os
 * cabeçalhos locais) é o que a APPNOTE recomenda: os tamanhos podem não
 * estar no cabeçalho local quando o bit 3 das opções está ligado.
 * Métodos 0 (guardado) e 8 (DEFLATE). Criptografados (bit 0) e ZIP64 são
 * recusados com uma mensagem.
 *
 * Nomes: com o bit 11 ligado estão em UTF-8; sem ele, a APPNOTE diz que é
 * a página de código 437 do IBM PC, convertida aqui para Latin-1 pela
 * tabela abaixo (o que não existe em Latin-1 vira '_').
 *
 * Segurança: nomes absolutos ou com ".." são recusados ("zip slip": um
 * pacote malicioso tentando gravar fora da pasta escolhida), e um arquivo
 * que já existe só é substituído com -o. Cada arquivo extraído tem o
 * tamanho e o CRC conferidos com o que o diretório central promete. */
#include "sagui.h"
#include "compactar.h"

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

/* Página de código 437, de 0x80 a 0xFF, em Latin-1 (0 = não existe em
 * Latin-1). Fonte: a tabela da página de código 437 do IBM PC. */
static const uint8_t cp437[128] = {
    0xC7, 0xFC, 0xE9, 0xE2, 0xE4, 0xE0, 0xE5, 0xE7, 0xEA, 0xEB, 0xE8, 0xEF, 0xEE, 0xEC, 0xC4, 0xC5,
    0xC9, 0xE6, 0xC6, 0xF4, 0xF6, 0xF2, 0xFB, 0xF9, 0xFF, 0xD6, 0xDC, 0xA2, 0xA3, 0xA5, 0, 0,
    0xE1, 0xED, 0xF3, 0xFA, 0xF1, 0xD1, 0xAA, 0xBA, 0xBF, 0, 0xAC, 0xBD, 0xBC, 0xA1, 0xAB, 0xBB,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,          /* 0xB0-0xDF: desenho de molduras */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0xDF, 0, 0, 0, 0, 0xB5, 0, 0, 0, 0, 0, 0, 0, 0, 0,    /* 0xE0-0xEF: letras gregas (ß, µ) */
    0, 0xB1, 0, 0, 0, 0, 0xF7, 0, 0xB0, 0, 0xB7, 0, 0, 0xB2, 0, 0xA0,
};

typedef struct {
    char nome[512];          /* em Latin-1 */
    uint32_t crc, compactado, original, deslocamento;
    uint32_t opcoes, metodo, data_dos;
    bool pasta;
} item_t;

static bool so_listar, so_testar, sobrescrever, quieto;
static int erros;

static void converter_nome(const uint8_t *bruto, uint32_t n, uint32_t opcoes, char *nome, int max)
{
    if (opcoes & 0x0800) { nome_utf8_para_latin1((const char *)bruto, (int)n, nome, max); return; }
    int k = 0;
    for (uint32_t i = 0; i < n && k < max - 1; i++) {
        uint8_t c = bruto[i];
        nome[k++] = (char)(c < 0x80 ? c : cp437[c - 0x80] ? cp437[c - 0x80] : '_');
    }
    nome[k] = 0;
}

/* Lê o registro central na posição *q (e avança). */
static int ler_item(const uint8_t *d, uint32_t fim_central, uint32_t *q, item_t *it)
{
    uint32_t p = *q;
    if (p + 46 > fim_central || le32(d + p) != 0x02014B50) return COMP_ERRO_DADOS;
    uint32_t ln = le16(d + p + 28), lx = le16(d + p + 30), lc = le16(d + p + 32);
    if (p + 46 + ln + lx + lc > fim_central) return COMP_ERRO_DADOS;
    it->opcoes = le16(d + p + 8);
    it->metodo = le16(d + p + 10);
    it->data_dos = le32(d + p + 12);
    it->crc = le32(d + p + 16);
    it->compactado = le32(d + p + 20);
    it->original = le32(d + p + 24);
    it->deslocamento = le32(d + p + 42);
    converter_nome(d + p + 46, ln, it->opcoes, it->nome, sizeof it->nome);
    size_t l = tam_texto(it->nome);
    it->pasta = l && (it->nome[l - 1] == '/' || it->nome[l - 1] == '\\');
    *q = p + 46 + ln + lx + lc;
    return 0;
}

/* Os dados de um item, descompactados e conferidos (alocados). */
static int extrair_dados(const uint8_t *d, uint32_t n, const item_t *it, uint8_t **saida)
{
    *saida = NULL;
    if (it->opcoes & 1) return COMP_ERRO_METODO;   /* criptografado */
    if (it->original == 0xFFFFFFFFu || it->compactado == 0xFFFFFFFFu || it->deslocamento == 0xFFFFFFFFu)
        return COMP_ERRO_METODO;                   /* ZIP64 */
    if (it->metodo != 0 && it->metodo != 8) return COMP_ERRO_METODO;
    if (it->original > COMP_LIMITE) return COMP_ERRO_CHEIO;
    uint32_t p = it->deslocamento;
    if ((uint64_t)p + 30 > n || le32(d + p) != 0x04034B50) return COMP_ERRO_DADOS;
    uint64_t ini = (uint64_t)p + 30 + le16(d + p + 26) + le16(d + p + 28);
    if (ini + it->compactado > n) return COMP_ERRO_TRUNCADO;
    uint8_t *o = aloca(it->original + 1);
    if (!o) return COMP_ERRO_MEMORIA;
    int r;
    if (it->metodo == 0) {
        if (it->compactado != it->original) { libera(o); return COMP_ERRO_DADOS; }
        memcpy(o, d + ini, it->original);
        r = (int)it->original;
    } else {
        r = inflar(d + ini, it->compactado, o, it->original);
        if (r == COMP_ERRO_CHEIO) r = COMP_ERRO_DADOS;   /* dá mais do que o diretório promete */
    }
    if (r >= 0 && (uint32_t)r != it->original) r = COMP_ERRO_TRUNCADO;
    if (r >= 0 && crc32_somar(0, o, it->original) != it->crc) r = COMP_ERRO_CRC;
    if (r < 0) { libera(o); return r; }
    *saida = o;
    return r;
}

static void uso(void)
{
    imprimir_em(ERROS, "uso: unzip [-l] [-t] [-o] [-q] [-d pasta] pacote.zip\n"
                       "  -l lista  -t confere  -o substitui o que já existe  -d pasta de destino\n");
}

int principal(int argc, char **argv)
{
    const char *nome_pacote = NULL, *destino = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (compara_texto(a, "-d") == 0) {
            if (i + 1 >= argc) { uso(); return 2; }
            destino = argv[++i];
            continue;
        }
        if (a[0] == '-' && a[1]) {
            for (const char *c = a + 1; *c; c++) {
                if (*c == 'l') so_listar = true;
                else if (*c == 't') so_testar = true;
                else if (*c == 'o') sobrescrever = true;
                else if (*c == 'q') quieto = true;
                else { imprimir_em(ERROS, "unzip: opção desconhecida -%c\n", *c); uso(); return 2; }
            }
            continue;
        }
        if (!nome_pacote) nome_pacote = a;
    }
    if (!nome_pacote) { uso(); return 2; }
    uint8_t *d;
    int nr = ler_arquivo_inteiro(nome_pacote, &d);
    if (nr < 0) { imprimir_em(ERROS, "unzip: %s: %s\n", nome_pacote, comp_nome_erro(nr)); return 1; }
    uint32_t n = (uint32_t)nr;
    /* O fim do diretório central: 22 bytes, procurado de trás para a frente. */
    int64_t p = (int64_t)n - 22, limite = (int64_t)n - 22 - 65535;
    if (limite < 0) limite = 0;
    while (p >= limite && le32(d + p) != 0x06054B50) p--;
    if (p < limite) {
        imprimir_em(ERROS, "unzip: %s: não é um pacote zip (ou está cortado)\n", nome_pacote);
        libera(d);
        return 1;
    }
    uint32_t qtd = le16(d + p + 10), tam_central = le32(d + p + 12), ini_central = le32(d + p + 16);
    if (qtd == 0xFFFF || ini_central == 0xFFFFFFFFu) {
        imprimir_em(ERROS, "unzip: %s: pacote ZIP64, não suportado\n", nome_pacote);
        libera(d);
        return 1;
    }
    if ((uint64_t)ini_central + tam_central > (uint64_t)p) {
        imprimir_em(ERROS, "unzip: %s: diretório central fora do lugar\n", nome_pacote);
        libera(d);
        return 1;
    }
    if (!so_listar && !so_testar && destino) {
        int r = criar_pastas(destino);
        if (r < 0) { imprimir_em(ERROS, "unzip: %s: %s\n", destino, nome_erro(r)); libera(d); return 1; }
    }
    item_t *it = aloca(sizeof *it);
    if (!it) { libera(d); return 1; }
    uint32_t q = ini_central, total = 0;
    if (so_listar) imprimir("    tamanho  data        hora   nome\n");
    char caminho[512];
    for (uint32_t i = 0; i < qtd; i++) {
        int r = ler_item(d, (uint32_t)p, &q, it);
        if (r < 0) { imprimir_em(ERROS, "unzip: %s: diretório central estragado\n", nome_pacote); erros++; break; }
        if (so_listar) {
            /* Data do MS-DOS: dia, mês, ano; hora:minuto. */
            uint32_t dt = it->data_dos & 0xFFFF, hr = it->data_dos >> 16;
            imprimir("%11u  %02u/%02u/%04u  %02u:%02u  %s\n", it->original, dt & 31, (dt >> 5) & 15,
                     (dt >> 9) + 1980, hr >> 11, (hr >> 5) & 63, it->nome);
            total += it->original;
            continue;
        }
        if (!caminho_seguro(it->nome, destino, caminho, sizeof caminho)) {
            imprimir_em(ERROS, "unzip: %s: caminho perigoso (absoluto ou com \"..\"), recusado\n", it->nome);
            erros++;
            continue;
        }
        if (it->pasta) {
            if (so_testar) continue;
            r = criar_pastas(caminho);
            if (r < 0) { imprimir_em(ERROS, "unzip: %s: %s\n", caminho, nome_erro(r)); erros++; }
            else if (!quieto) imprimir("  pasta: %s\n", caminho);
            continue;
        }
        if (!so_testar && existe_arquivo(caminho) && !sobrescrever) {
            imprimir_em(ERROS, "unzip: %s já existe (use -o para substituir)\n", caminho);
            erros++;
            continue;
        }
        uint8_t *dados;
        r = extrair_dados(d, n, it, &dados);
        if (r < 0) { imprimir_em(ERROS, "unzip: %s: %s\n", it->nome, comp_nome_erro(r)); erros++; continue; }
        if (so_testar) {
            if (!quieto) imprimir("  ok: %s\n", it->nome);
            libera(dados);
            continue;
        }
        r = criar_pastas_ate(caminho);
        if (r >= 0) r = gravar_arquivo_inteiro(caminho, dados, it->original);
        libera(dados);
        if (r < 0) { imprimir_em(ERROS, "unzip: %s: %s\n", caminho, nome_erro(r)); erros++; continue; }
        if (!quieto) imprimir("  extraído: %s\n", caminho);
    }
    if (so_listar) imprimir("%11u  %u itens\n", total, qtd);
    else if (so_testar && !erros) imprimir("%s: tudo certo\n", nome_pacote);
    libera(it);
    libera(d);
    return erros ? 1 : 0;
}
