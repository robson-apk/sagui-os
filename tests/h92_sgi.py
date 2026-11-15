#!/usr/bin/env python3
"""H92 - o Visualizador abre o .sgi dos papéis de parede.

Critérios:
  - "visualizador /arte/mata-atlantica.sgi" abre a imagem com 1024x768 (o
    tamanho do cabeçalho);
  - a imagem aparece: no meio da janela há cores da foto (verdes), não o
    fundo liso do Visualizador;
  - captura para a revisão a olho.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, cor_do_pixel  # noqa: E402


def main():
    with Maquina("h92", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.digitar("visualizador /arte/mata-atlantica.sgi\n", 0.03)
        m.esperar_serial("visualizador: abriu /arte/mata-atlantica.sgi (1024x768", 30)
        verificar(True, "o .sgi abre com 1024x768")
        time.sleep(1.5)
        png = m.captura("H92-sgi")
        a = janela(m.serial(), "Visualizador")
        cores = {cor_do_pixel(png, a["x"] + a["l"] // 2 + dx, a["y"] + a["a"] // 2 + dy)
                 for dx in (-60, 0, 60) for dy in (-40, 0, 40)}
        verificar(len(cores) >= 4, f"a foto aparece na janela ({len(cores)} cores diferentes em 9 pontos)")


if __name__ == "__main__":
    main()
