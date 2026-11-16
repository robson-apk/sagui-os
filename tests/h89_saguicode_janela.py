#!/usr/bin/env python3
"""H89 - o Sagui Code numa janela (com o servidor falso do H69).

Critérios:
  - o menu do sagui abre a janela "Sagui Code" (o núcleo do agente é o mesmo
    do Terminal, em agente.c);
  - um pedido que faz o modelo usar uma ferramenta (ler um arquivo) e depois
    responder: a janela mostra o pedido, o bloco da ferramenta com o
    resultado e a resposta; a sessão é guardada no pendrive
    (/usb/.saguios/saguicode/sessoes/0001.json) com o primeiro pedido como
    título;
  - "Nova sessão" começa outra conversa; clicar na primeira na lista traz o
    histórico de volta (os mesmos blocos) e o próximo pedido continua a
    conversa antiga;
  - o menu do botão direito renomeia uma sessão (o título muda no arquivo) e
    apaga outra (com confirmação);
  - a chave não aparece no log nem nas sessões guardadas.
"""
import os
import re
import socketserver
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, mtools, janela, FalhaTeste  # noqa: E402
import h69_h70_saguicode as h69  # noqa: E402

roteiro_h69 = h69.roteiro


def roteiro(corpo):
    """O roteiro do H69, com um pedido a mais: ler a nota e responder."""
    msgs = corpo["messages"]
    usuario = [m for m in msgs if m["role"] == "user"][-1]["content"]
    depois = msgs[[i for i, m in enumerate(msgs) if m["role"] == "user"][-1] + 1:]
    resultados = [m["content"] for m in depois if m["role"] == "tool"]
    if "mostre a nota" in usuario:
        if not resultados:
            return h69.chamada("ler_arquivo", {"caminho": "nota.txt"}, "Vou ler o arquivo.", idc="n1")
        return h69.texto_em_pedacos("A nota tem duas linhas: linha um e linha dois.")
    return roteiro_h69(corpo)


h69.roteiro = roteiro          # o Atendente do H69 procura o roteiro no módulo dele


def item_do_menu(m, desde, i):
    """Clica no item i do menu que acabou de abrir (posição pelo log)."""
    s = h69.esperar(m, "menu aberto (tipo 3", 10, desde)
    x, y = map(int, re.findall(r"menu aberto \(tipo 3, \d+ itens\) em \((\d+),(\d+)\)", s)[-1])
    m.clique(x + 20, y + 4 + i * 26 + 10)


def main():
    servidor = socketserver.ThreadingTCPServer(("127.0.0.1", 0), h69.Atendente)
    porta = servidor.server_address[1]
    threading.Thread(target=servidor.serve_forever, daemon=True).start()
    pen = criar_pendrive("h89-pen", pastas=(".saguios", "projetos"), arquivos={
        ".saguios/chaves.txt": f"openrouter = {h69.CHAVE}\n",
        ".saguios/configuracoes.txt": f"ia.servidor = http://10.0.2.2:{porta}/api/v1\nia.modelo = anthropic/claude-teste\n",
        "projetos/nota.txt": b"linha um\nlinha dois\n",
    })
    try:
        with Maquina("h89", "ide", extras=h69.REDE + ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen), memoria="768M") as m:
            m.esperar_serial("sagui:/$", 120)
            m.esperar_serial("[REDE] DHCP: endereco", 30)
            time.sleep(2)
            # O menu Iniciar (grade 5 x 4): "Sagui Code" é o item 16.
            antes = len(m.serial())
            m.clique(40, 12)
            time.sleep(0.8)
            m.clique(44 + 1 * 108 + 54, 134 + 3 * 92 + 30)
            s = h69.esperar(m, "saguicode-janela: pronta", 30, antes)
            verificar("executar /bin/saguicode-janela" in s, "o menu do sagui abre a janela do Sagui Code")
            verificar("modelo anthropic/claude-teste, 0 sessoes, chave sim, pendrive" in s,
                      "a janela leu o modelo, a chave e o pendrive")
            time.sleep(1)
            jan = janela(m.serial(), "Sagui Code")
            entrada = (jan["x"] + 222 + 60, jan["y"] + jan["a"] - 8 - 36)
            nova = (jan["x"] + 100, jan["y"] + 44 + 32 + 13)

            def sessao(i):          # i-ésimo item da lista de sessões
                return jan["x"] + 60, jan["y"] + 44 + 68 + 2 + i * 20 + 10

            # Um pedido com ferramenta.
            m.clique(*entrada)
            antes = len(m.serial())
            m.digitar("mostre a nota\n", 0.05)
            s = h69.esperar(m, "saguicode-janela: sessao 0001 salva", 60, antes)
            verificar("saguicode-janela: pedido enviado (13 letras)" in s, "Enter na caixa manda o pedido")
            verificar("saguicode-janela: ferramenta mostrada: Lendo nota.txt" in s, "o bloco da ferramenta aparece (\"Lendo nota.txt\")")
            verificar("ferramenta ler_arquivo terminou: 20 bytes lidos" in s, "o bloco mostra o resultado da ferramenta")
            verificar("saguicode-janela: resposta mostrada" in s, "a resposta final aparece")
            verificar("(pendrive, r=0)" in s, "a sessão foi guardada no pendrive")
            lido = [x for x in h69.pedidos[-1]["corpo"]["messages"] if x["role"] == "tool"]
            verificar(lido and lido[-1]["content"] == "linha um\nlinha dois\n", "o modelo recebeu o conteúdo do arquivo")
            time.sleep(1)
            m.captura("H89-1-sessao")
            # Outra sessão.
            antes = len(m.serial())
            m.clique(*nova)
            h69.esperar(m, "saguicode-janela: sessao nova", 10, antes)
            m.clique(*entrada)
            m.digitar("diga oi\n", 0.05)
            h69.esperar(m, "saguicode-janela: sessao 0002 salva", 60, antes)
            verificar(len([x for x in h69.pedidos[-1]["corpo"]["messages"] if x["role"] == "user"]) == 1,
                      "a sessão nova começa sem a conversa anterior")
            # De volta à primeira (a lista mostra as mais novas em cima).
            antes = len(m.serial())
            m.clique(*sessao(1))
            s = h69.esperar(m, "saguicode-janela: sessao 0001 aberta", 15, antes)
            verificar("sessao 0001 aberta (4 mensagens, 4 blocos)" in s,
                      "o histórico volta: pedido, texto, bloco da ferramenta e resposta")
            time.sleep(1)
            m.captura("H89-2-ferramenta")
            antes = len(m.serial())
            m.clique(*entrada)
            m.digitar("diga oi de novo\n", 0.05)
            h69.esperar(m, "saguicode-janela: sessao 0001 salva", 60, antes)
            usuarios = [x["content"] for x in h69.pedidos[-1]["corpo"]["messages"] if x["role"] == "user"]
            verificar(usuarios == ["mostre a nota", "diga oi de novo"], "o pedido seguinte continua a conversa reaberta")
            # Renomear a sessão 0002 (a de cima: a lista põe o maior número
            # em cima), que não é a aberta, pelo menu do botão direito.
            antes = len(m.serial())
            m.clique(*sessao(0), botao=2)
            item_do_menu(m, antes, 1)
            h69.esperar(m, '"Renomear sess', 10, antes)
            time.sleep(0.5)
            for _ in range(10):
                m.tecla("backspace", 0.04)
            m.digitar("oi renomeada\n", 0.05)
            h69.esperar(m, "saguicode-janela: sessao 0002 renomeada", 10, antes)
            verificar(True, "o menu do botão direito renomeia uma sessão")
            # Uma terceira sessão, apagada pelo menu (com confirmação).
            antes = len(m.serial())
            m.clique(*nova)
            m.clique(*entrada)
            m.digitar("diga oi mais uma vez\n", 0.05)
            h69.esperar(m, "saguicode-janela: sessao 0003 salva", 60, antes)
            antes = len(m.serial())
            m.clique(*sessao(0), botao=2)
            item_do_menu(m, antes, 2)
            h69.esperar(m, '"Apagar sess', 10, antes)
            time.sleep(0.5)
            m.tecla("ret")
            s = h69.esperar(m, "saguicode-janela: sessao nova", 10, antes)
            verificar("saguicode-janela: sessao 0003 apagada" in s, "o menu apaga a sessão aberta (e começa outra)")
            verificar(h69.CHAVE not in m.serial(), "a chave não aparece no log")
        verificar(not h69.erros_servidor, "todos os pedidos foram JSON em UTF-8 válido")
        um, rc = mtools(pen, "mtype", "p:/.saguios/saguicode/sessoes/0001.json")
        verificar(rc == 0 and um.startswith('{"titulo":"mostre a nota"') and "diga oi de novo" in um and h69.CHAVE not in um,
                  "0001.json: título, a conversa inteira e nada da chave")
        dois, rc = mtools(pen, "mtype", "p:/.saguios/saguicode/sessoes/0002.json")
        verificar(rc == 0 and dois.startswith('{"titulo":"oi renomeada","modelo"'), "0002.json com o título novo")
        _, rc = mtools(pen, "mtype", "p:/.saguios/saguicode/sessoes/0003.json")
        verificar(rc != 0, "0003.json foi apagado")
    finally:
        servidor.shutdown()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
