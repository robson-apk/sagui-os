#!/usr/bin/env python3
"""H79 (pedidos do Eli no G470) - copiar e colar entre programas.

Critérios:
  - no Terminal, arrastar o mouse sobre um texto copia para a área de cópia
    do sistema; Ctrl+Shift+V cola no prompt;
  - no Editor, Ctrl+V cola o que veio do Terminal; Ctrl+A e Ctrl+C copiam o
    texto do Editor, e o Terminal cola de volta.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, cor_do_pixel  # noqa: E402


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def main():
    with Maquina("h79cp", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        t = janela(m.serial(), "Terminal")

        def centro(lin, col):
            return t["x"] + 4 + col * 8 + 4, t["y"] + 24 + 4 + lin * 16 + 8

        # Linha 0: saudação; linha 1: o comando; linha 2: a saída.
        m.comando("echo copie-isto-123")
        antes = len(m.serial())
        m.arrastar(*centro(2, 0), *centro(2, 13))
        esperar(m, "terminal: copiado (14 letras)", 10, antes)
        verificar(True, "arrastar no Terminal copiou 14 letras para a área de cópia do sistema")
        verificar("area de copia: 14 bytes" in m.serial()[antes:], "o kernel guardou o texto na área de cópia")

        # Ctrl+Shift+V cola no prompt.
        antes = len(m.serial())
        m.digitar("echo x", 0.04)
        m.tecla("ctrl-shift-v")
        m.tecla("ret")
        esperar(m, "xcopie-isto-123", 10, antes)
        verificar(True, "Ctrl+Shift+V colou no prompt do Terminal")

        # No Editor: Ctrl+V cola o que veio do Terminal.
        antes = len(m.serial())
        m.digitar("editor\n", 0.04)
        esperar(m, "editor: pronto", 20, antes)
        time.sleep(1)
        e = janela(m.serial(), "Editor - sem nome")
        m.clique(e["x"] + 60, e["y"] + 40 + 4 + 30)
        png0 = m.captura("H79-editor-vazio", guardar=False)
        m.tecla("ctrl-v")
        time.sleep(0.8)
        png = m.captura("H79-colado-no-editor")
        linha = [(e["x"] + 6 + 5 + k, e["y"] + 40 + 4 + 4 + 8) for k in range(0, 14 * 8)]
        mudou = sum(cor_do_pixel(png, x, y) != cor_do_pixel(png0, x, y) for x, y in linha)
        verificar(mudou > 20, f"Ctrl+V colou o texto no Editor ({mudou} pixels mudaram na primeira linha)")

        # E de volta: Ctrl+A, Ctrl+C no Editor; o Terminal cola.
        m.tecla("ctrl-a")
        m.tecla("ctrl-c")
        time.sleep(0.5)
        antes = len(m.serial())
        m.clique(t["fx"] + 20, t["fy"] + 12)                   # foco no Terminal (a barra de título fica à vista)
        m.digitar("echo y", 0.04)
        m.tecla("ctrl-shift-v")
        m.tecla("ret")
        esperar(m, "ycopie-isto-123", 10, antes)
        verificar(True, "o texto copiado no Editor (Ctrl+A, Ctrl+C) colou no Terminal")
    return 0


if __name__ == "__main__":
    sys.exit(main())
