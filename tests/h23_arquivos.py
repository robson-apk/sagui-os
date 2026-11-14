#!/usr/bin/env python3
"""H23 - Arquivos (e o fim do critério da H25).

Critério: teste navega até historia/, abre linux.txt no Editor, cria uma
pasta em tmp/ e a apaga. Também: a nota criada no Editor é reaberta pelo
Arquivos e o conteúdo confere (critério da H25).
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar  # noqa: E402

TEXTO = "Olá, SaguiOS! Ação e coração."


def janela(serial, titulo):
    m = re.findall(rf'criada por \d+: "{re.escape(titulo)}" (\d+)x(\d+) em \((\d+),(\d+)\)', serial)[-1]
    return dict(l=int(m[0]), a=int(m[1]), x=int(m[2]) + 2, y=int(m[3]) + 24, fx=int(m[2]), fy=int(m[3]))


def fechar(m, j):
    m.clique(j["fx"] + j["l"] + 4 - 2 - 20 + 8, j["fy"] + 12)


def item(a, i):
    return a["x"] + 146 + 120, a["y"] + 70 + 2 + i * 20 + 8   # a lista começa depois da lateral (H34)


def main():
    with Maquina("h23", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        # Nota criada no Editor (H25), para reabrir pelo Arquivos.
        m.comando("editor")
        m.esperar_serial("editor: pronto", 20)
        time.sleep(0.4)
        m.digitar(TEXTO, 0.05)
        m.tecla("ctrl-s")
        m.esperar_serial('"Salvar como"', 10)
        time.sleep(0.3)
        m.digitar("nota.txt\n", 0.05)
        m.esperar_serial("editor: salvo /tmp/nota.txt", 10)
        fechar(m, janela(m.serial(), "Editor - sem nome"))
        m.esperar_serial("editor: fechado", 10)
        time.sleep(0.5)

        m.comando("arquivos")
        m.esperar_serial("arquivos: pronto", 20)
        time.sleep(0.5)
        a = janela(m.serial(), "Arquivos")
        verificar("arquivos: pasta / (11 itens)" in m.serial(), "Arquivos abre na raiz: 9 pastas (com fontes/, H82) e 2 arquivos")
        m.captura("H23-arquivos-raiz")

        m.clique(*item(a, 3), duplo=True)                         # historia/ (depois de arte, bin e fontes)
        m.esperar_serial("arquivos: pasta /historia (2 itens)", 10)
        verificar(True, "duplo clique na pasta historia entra nela")
        m.clique(*item(a, 0), duplo=True)                         # linux.txt
        m.esperar_serial("editor: aberto /historia/linux.txt", 10)
        verificar(True, "duplo clique em linux.txt abre no Editor")
        time.sleep(0.5)
        m.captura("H23-linux-no-editor")
        fechar(m, janela(m.serial(), "Editor - sem nome"))
        m.esperar_serial("editor: fechado", 10)
        time.sleep(0.4)

        m.clique(a["x"] + 8 + 45, a["y"] + 7 + 13)               # Voltar
        m.esperar_serial("arquivos: pasta / (11 itens)", 10)
        m.clique(*item(a, 8), duplo=True)                         # tmp/ (depois de arte, bin, fontes, historia, jogos, sistema, sons e testes)
        m.esperar_serial("arquivos: pasta /tmp (1 itens)", 10)
        m.clique(*item(a, 0), duplo=True)                         # nota.txt
        m.esperar_serial("editor: aberto /tmp/nota.txt (29 bytes)", 10)
        verificar(True, "H25: a nota do Editor reabre pelo Arquivos com o mesmo tamanho (29 bytes)")
        time.sleep(0.5)
        png = m.captura("H23-nota-reaberta")
        fechar(m, janela(m.serial(), "Editor - sem nome"))
        m.esperar_serial("editor: fechado", 10)
        time.sleep(0.4)

        m.clique(a["x"] + 106 + 55, a["y"] + 7 + 13)              # Nova pasta
        m.esperar_serial('"Nova pasta"', 10)
        time.sleep(0.3)
        m.digitar("teste\n", 0.05)
        m.esperar_serial("arquivos: pasta criada /tmp/teste", 10)
        m.esperar_serial("arquivos: pasta /tmp (2 itens)", 10)   # a lista é relida logo depois de criar
        verificar(True, "pasta /tmp/teste criada e aparece na lista")
        m.clique(*item(a, 0))                                      # seleciona teste/
        m.clique(a["x"] + 224 + 45, a["y"] + 7 + 13)              # Apagar
        m.esperar_serial('"Apagar"', 10)
        time.sleep(0.4)
        m.captura("H23-confirmar-apagar")
        d = janela(m.serial(), "Apagar")
        m.clique(d["x"] + 380 - 16 - 45, d["y"] + d["a"] - 40 + 13)   # Sim
        m.esperar_serial("arquivos: apagado /tmp/teste", 10)
        fim = time.time() + 10                                     # a lista é relida logo depois de apagar
        while m.serial().count("arquivos: pasta /tmp (1 itens)") < 2 and time.time() < fim:
            time.sleep(0.2)
        verificar(m.serial().count("arquivos: pasta /tmp (1 itens)") >= 2, "pasta apagada (com confirmação) e some da lista")
        fechar(m, a)                                               # o foco volta para o Terminal
        m.esperar_serial("(arquivos) terminou", 10)
        time.sleep(0.4)
        out = m.comando("ls /tmp")
        verificar("teste" not in out and "nota.txt" in out, "ls /tmp no Terminal confere")
    return 0


if __name__ == "__main__":
    sys.exit(main())
