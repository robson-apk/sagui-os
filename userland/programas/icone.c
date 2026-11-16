/* icone - ações do menu do botão direito nos ícones da área de trabalho
 * (H79). O kernel desenha o menu, mas não tem caixas de texto:
 * quem pergunta é este programa, com os diálogos do kit gráfico.
 *   icone renomear-programa /usb/programas/x x   troca o nome do arquivo
 *   icone renomear-icone /bin/terminal Terminal    só o nome que aparece (icone.<programa>)
 *   icone remover /usb/programas/x x               apaga (com confirmação)
 *   icone esconder /bin/banana Banana              some da área de trabalho (programas.ocultos) */
#include "sagui.h"
#include "gui.h"

int principal(int argc, char **argv)
{
    if (argc < 4) { imprimir_em(ERROS, "uso: icone renomear-programa|renomear-icone|remover|esconder caminho nome\n"); return 2; }
    const char *acao = argv[1], *caminho = argv[2], *nome = argv[3];
    const char *programa = acha_ultimo_char(caminho, '/');
    programa = programa ? programa + 1 : caminho;
    if (compara_texto(acao, "renomear-programa") == 0 || compara_texto(acao, "renomear-icone") == 0) {
        char novo[48];
        copia_texto(novo, nome, sizeof novo);
        char pergunta[96];
        formatar_texto(pergunta, sizeof pergunta, "Novo nome para \"%s\":", nome);
        if (!pedir_texto("Renomear", pergunta, novo, sizeof novo) || !novo[0] || compara_texto(novo, nome) == 0) return 0;
        if (compara_texto(acao, "renomear-icone") == 0) {
            char chave[48];
            formatar_texto(chave, sizeof chave, "icone.%s", programa);
            int r = config_gravar(chave, novo);
            registrar("icone: %s agora se chama %s (%d)", programa, novo, r);
            if (r == 0) mensagem("Renomear", "Sem pendrive de dados: o nome novo vale até desligar.");
            return 0;
        }
        if (acha_char(novo, '/')) { mensagem("Renomear", "O nome não pode ter /."); return 1; }
        char destino[128];
        formatar_texto(destino, sizeof destino, "/usb/programas/%s", novo);
        int r = renomear(caminho, destino);
        registrar("icone: %s -> %s (%d)", caminho, destino, r);
        if (r < 0) { char m[160]; formatar_texto(m, sizeof m, "Não consegui renomear:\n%s", nome_erro(r)); mensagem("Renomear", m); return 1; }
        return 0;
    }
    if (compara_texto(acao, "remover") == 0) {
        char pergunta[128];
        formatar_texto(pergunta, sizeof pergunta, "Apagar o programa \"%s\" do pendrive?", nome);
        if (!confirmar("Remover", pergunta)) return 0;
        int r = apagar(caminho);
        registrar("icone: removido %s (%d)", caminho, r);
        if (r < 0) { char m[160]; formatar_texto(m, sizeof m, "Não consegui apagar:\n%s", nome_erro(r)); mensagem("Remover", m); return 1; }
        return 0;
    }
    if (compara_texto(acao, "esconder") == 0) {
        char lista[160] = "";
        config_ler("programas.ocultos", lista, sizeof lista);
        if (lista[0]) junta_texto(lista, ",", sizeof lista);
        junta_texto(lista, programa, sizeof lista);
        int r = config_gravar("programas.ocultos", lista);
        registrar("icone: escondido %s (%d)", programa, r);
        mensagem("Esconder", "O ícone sumiu. Para mostrar de novo: Configurações > Programas.");
        return 0;
    }
    imprimir_em(ERROS, "icone: ação desconhecida %s\n", acao);
    return 2;
}
