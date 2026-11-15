#!/usr/bin/env python3
"""H94 - encaixe das janelas ao arrastar para a borda da tela.

Critérios (pelo log do kernel e pela captura):
  - arrastar a barra de título até a borda esquerda mostra uma prévia
    translúcida e, ao soltar, a janela ocupa a metade esquerda;
  - arrastar a janela encaixada de volta ao meio devolve o tamanho de antes;
  - arrastar até a borda de cima maximiza;
  - arrastar até o canto direito de baixo encaixa num quarto.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, cor_do_pixel  # noqa: E402


def esperar(m, texto, tempo=10, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def arrastar(m, x0, y0, x1, y1, captura=None):
    m.mouse_para(x0, y0)
    m.monitor("mouse_button 1")
    time.sleep(0.3)
    m.mouse_mover((x0 + x1) // 2, (y0 + y1) // 2)
    time.sleep(0.2)
    m.mouse_mover(x1, y1)
    time.sleep(0.6)
    png = m.captura(captura) if captura else None
    m.monitor("mouse_button 0")
    time.sleep(0.8)
    return png


def tamanho(m, desde, jid):
    r = re.findall(rf"janela {jid} redimensionada para (\d+)x(\d+) em \((-?\d+),(-?\d+)\)", m.serial()[desde:])
    if not r:
        raise FalhaTeste("a janela não mudou de tamanho")
    return tuple(int(v) for v in r[-1])


def main():
    with Maquina("h94", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        n0 = len(m.serial())
        m.digitar("arquivos\n", 0.03)
        esperar(m, '"Arquivos"', 30, n0)
        time.sleep(1.5)
        c = janela(m.serial(), "Arquivos")
        jid = re.findall(r'\[GUI\] janela (\d+) criada por \d+: "Arquivos"', m.serial())[-1]
        l0, a0 = c["l"], c["a"]
        W = int(re.findall(r"(\d+)x\d+x32", m.serial())[0]) if re.search(r"(\d+)x\d+x32", m.serial()) else 1024
        tx, ty = c["fx"] + 120, c["fy"] + 10              # um ponto da barra de título

        # Borda esquerda: metade da tela.
        n = len(m.serial())
        png = arrastar(m, tx, ty, 0, 380, "H94-1-previa-esquerda")
        fundo_previa = cor_do_pixel(png, 200, 380)
        esperar(m, f"janela {jid} encaixada na zona 2", 10, n)
        l, a, x, y = tamanho(m, n, jid)
        verificar(x == 0 and abs((l + 4) - W // 2) <= 1, f"metade esquerda: {l}x{a} em ({x},{y}), tela {W}")
        print(f"  pixel da prévia: {fundo_previa}")
        m.captura("H94-2-esquerda")

        # De volta ao meio: tamanho de antes.
        n = len(m.serial())
        arrastar(m, 150, y + 10, 500, 300)
        l, a, x, y = tamanho(m, n, jid)
        verificar((l, a) == (l0, a0), f"volta ao tamanho de antes ({l}x{a}, era {l0}x{a0})")
        x, y = (int(v) for v in re.findall(rf"janela {jid} movida para \((-?\d+),(-?\d+)\)", m.serial()[n:])[-1])

        # Borda de cima: maximiza.
        n = len(m.serial())
        arrastar(m, x + 120, y + 10, 500, 0)
        esperar(m, f"janela {jid} maximizada", 10, n)
        verificar(True, "a borda de cima maximiza")
        m.captura("H94-3-maximizada")

        # Canto de baixo à direita: um quarto.
        n = len(m.serial())
        arrastar(m, 300, 40, W - 1, 690)
        esperar(m, f"janela {jid} encaixada na zona 7", 10, n)
        l, a, x, y = tamanho(m, n, jid)
        verificar(abs(x - W // 2) <= 1, f"quarto de baixo à direita: {l}x{a} em ({x},{y})")
        m.captura("H94-4-quarto")


if __name__ == "__main__":
    main()
