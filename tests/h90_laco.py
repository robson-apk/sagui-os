#!/usr/bin/env python3
"""H90 - laço de seleção translúcido na área de trabalho.

Ideia e primeira versão: commit 0b3c462, do Robson (ramo area-lixeira, do
código da 1.0), refeito para o código atual.

Critérios:
  - arrastar no vazio desenha um retângulo translúcido: durante o arraste, um
    pixel dentro do laço muda de cor, mas o fundo continua aparecendo (não é
    uma cor sólida);
  - ao soltar, os ícones tocados ficam marcados (o kernel conta quantos);
  - um clique num ícone marca só ele;
  - clicar no vazio desmarca;
  - capturas para a revisão a olho.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, cor_do_pixel  # noqa: E402


def marcados(m, desde):
    r = re.findall(r"\[GUI\] laco: .* (\d+) icones marcados", m.serial()[desde:])
    if not r:
        raise FalhaTeste("o laço não terminou")
    return int(r[-1])


def esperar(m, texto, tempo=10, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def main():
    with Maquina("h90", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        # O Terminal abre por cima do meio da tela. Os ícones ficam em duas
        # colunas (x de 20 a 116 e de 130 a 226; cada linha desce 92 pixels a
        # partir de y = 40); na segunda só há o "Sobre", na primeira linha.
        # O laço vai de (170, 300) a (10, 140): toca Terminal e Editor.
        antes_png = m.captura("H90-0-antes")
        ponto = (150, 250)                      # vazio, dentro do laço
        fundo = cor_do_pixel(antes_png, *ponto)

        n = len(m.serial())
        m.mouse_para(170, 300)
        m.monitor("mouse_button 1")             # segura o botão (mouse_mover só move)
        time.sleep(0.2)
        m.mouse_mover(10, 140)
        time.sleep(0.6)
        durante = m.captura("H90-1-laco")
        cor = cor_do_pixel(durante, *ponto)
        dif = sum(abs(a - b) for a, b in zip(cor, fundo))
        verificar(8 < dif < 200, f"o laço é translúcido (fundo {fundo}, com o laço {cor})")
        m.monitor("mouse_button 0")
        esperar(m, "[GUI] laco:", 10, n)
        k = marcados(m, n)
        verificar(k == 2, f"o laço marcou os 2 ícones que tocou ({k})")
        time.sleep(0.6)
        m.captura("H90-2-marcados")

        # Clique num ícone: marca só ele (a Calculadora, y de 316 a 404). O
        # destaque pinta o canto vazio da célula; o do Terminal, desmarcado,
        # volta ao fundo.
        sem = cor_do_pixel(antes_png, 24, 330)
        m.clique(68, 350)
        time.sleep(0.8)
        png = m.captura("H90-3-um-icone")
        com = cor_do_pixel(png, 24, 330)
        verificar(sum(abs(a - b) for a, b in zip(com, sem)) > 8, f"o clique marca a Calculadora ({sem} -> {com})")
        verificar(cor_do_pixel(png, 24, 146) == cor_do_pixel(antes_png, 24, 146), "e desmarca o Terminal")

        # Clique no vazio desmarca: um laço de 1 pixel conta 0 marcados.
        n = len(m.serial())
        m.clique(150, 600)
        esperar(m, "[GUI] laco:", 10, n)
        verificar(marcados(m, n) == 0, "clicar no vazio desmarca tudo")


if __name__ == "__main__":
    main()
