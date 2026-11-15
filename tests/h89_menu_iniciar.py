#!/usr/bin/env python3
"""H89 - menu Iniciar com todos os programas, busca e categorias.

Critérios:
  - o menu abre no botão do sagui e mostra a grade com os programas;
  - digitar com o menu aberto vai para a busca (sem acento acha acentuado:
    "musica" acha "Música") e Enter abre o primeiro resultado;
  - clicar numa categoria filtra a grade (Jogos mostra Banana);
  - clicar num ícone da grade abre o programa;
  - Esc fecha o menu;
  - capturas para a revisão a olho.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar  # noqa: E402

MENU_X, MENU_Y = 4, 24 + 4
GRADE_X, GRADE_Y = MENU_X + 24, MENU_Y + 106
CHIPS_Y = MENU_Y + 62


def esperar(m, texto, tempo=20, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def abrir_menu(m):
    n = len(m.serial())
    m.clique(40, 12)
    esperar(m, "[GUI] menu aberto", 10, n)
    time.sleep(0.6)


def main():
    with Maquina("h89", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)

        abrir_menu(m)
        m.captura("H89-menu-todos")
        verificar(True, "o menu Iniciar abre")

        # Busca sem acento: "musica" acha Música; Enter abre.
        n = len(m.serial())
        m.digitar("musica", 0.05)
        esperar(m, '[GUI] menu: busca "musica"', 10, n)
        time.sleep(0.5)
        m.captura("H89-busca")
        verificar(True, "o teclado vai para a busca do menu")
        m.digitar("\n", 0.05)
        esperar(m, "[GUI] menu: Música", 10, n)
        esperar(m, "/bin/musica", 10, n)
        verificar(True, "Enter abre o primeiro resultado (Música)")
        time.sleep(1.5)

        # Categoria Jogos: Todos(0) Internet(1) Escritório(2) Mídia(3) Jogos(4).
        abrir_menu(m)
        larg = {"Todos": 5, "Internet": 8, "Escritório": 10, "Mídia": 5}
        x = MENU_X + 16
        for nome in ["Todos", "Internet", "Escritório", "Mídia"]:
            x += larg[nome] * 8 + 22 + 6
        n = len(m.serial())
        m.clique(x + 20, CHIPS_Y + 13)
        esperar(m, "[GUI] menu: categoria Jogos", 10, n)
        time.sleep(0.5)
        m.captura("H89-jogos")
        verificar(True, "o clique na categoria Jogos filtra")
        # O primeiro jogo é a Banana.
        m.clique(GRADE_X + 50, GRADE_Y + 40)
        esperar(m, "[GUI] menu: Banana", 10, n)
        verificar(True, "o clique na grade abre o programa (Banana)")
        time.sleep(1.5)

        # Esc fecha.
        abrir_menu(m)
        n = len(m.serial())
        m.tecla("esc")
        time.sleep(0.5)
        m.clique(GRADE_X + 50, GRADE_Y + 40)     # com o menu fechado, não abre nada
        time.sleep(0.8)
        verificar("[GUI] menu: Cip" not in m.serial()[n:], "Esc fecha o menu")


if __name__ == "__main__":
    main()
