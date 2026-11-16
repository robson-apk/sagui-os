#!/usr/bin/env python3
"""H81 - o visual do Robson (pull request #1) somado ao da 2.0.

Critérios:
  - ícones 64x64 suavizados na área de trabalho (ARGB, tools/mkarte.py) e
    40x40 na doca;
  - fundos novos: "Brasil com sol" (desenhado por código) e "Mata Atlântica"
    (imagem /arte/mata-atlantica.sgi);
  - temas novos: "Mata" (barra verde, filete amarelo, botões redondos
    coloridos) e "Azul";
  - menus do botão direito: na área vazia, na barra de título e o menu
    próprio do Terminal (SIS_MENU_JANELA / EV_MENU);
  - barra de tarefas como opção à doca;
  - capturas de cada tema e fundo.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, cor_do_pixel  # noqa: E402


def esperar(m, texto, tempo=30, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def config(m, chave, valor):
    m.comando(f"config {chave} {valor}", tempo=20)
    time.sleep(1.2)


def main():
    with Maquina("h81", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        t = janela(m.serial(), "Terminal")

        # Ícone 64x64 suavizado: a pasta amarela do Arquivos (x 20..116, y 40..).
        png = m.captura("H81-1-icones")
        cor = cor_do_pixel(png, 20 + 48, 40 + 40)
        verificar(cor[0] > 200 and cor[1] > 150 and cor[2] < 120, f"ícone de Arquivos em 64x64 (pasta amarela {cor})")

        # Fundos.
        antes = len(m.serial())
        config(m, "aparencia.fundo", "mata-atlantica")
        esperar(m, "[GUI] fundo: /arte/mata-atlantica.sgi 1024x768", 20, antes)
        verificar(True, "fundo Mata Atlântica: a imagem foi lida (1024x768)")
        png = m.captura("H81-2-fundo-mata-atlantica")
        # O céu da imagem é amarelado em cima, no meio (fora das janelas).
        c = cor_do_pixel(png, 990, 300)
        verificar(c != (0, 0, 0), f"a imagem aparece no fundo {c}")
        config(m, "aparencia.fundo", "sol")
        png = m.captura("H81-3-fundo-brasil-com-sol")
        c = cor_do_pixel(png, 1000, 740)
        verificar(c[2] > c[0], f"Brasil com sol: rio azul embaixo {c}")

        # Tema Mata: barra verde forte, filete amarelo, botões coloridos.
        config(m, "aparencia.tema", "mata")
        png = m.captura("H81-4-tema-mata")
        filete = cor_do_pixel(png, t["fx"] + 100, t["fy"] + 23)
        verificar(filete[0] > 180 and filete[1] > 150 and filete[2] < 100, f"tema Mata: filete amarelo embaixo da barra de título {filete}")
        mini = cor_do_pixel(png, t["fx"] + t["l"] + 4 - 2 - 20 - 24 + 8, t["fy"] + 4 + 5)
        verificar(mini[0] > 180 and mini[1] > 130 and mini[2] < 120, f"tema Mata: botão de minimizar amarelo {mini}")
        config(m, "aparencia.tema", "azul")
        png = m.captura("H81-5-tema-azul")
        barra = cor_do_pixel(png, t["fx"] + 100, t["fy"] + 12)
        verificar(barra[2] > barra[0] and barra[2] > barra[1], f"tema Azul: barra de título azul {barra}")

        # Menus do botão direito.
        config(m, "aparencia.tema", "mata")
        config(m, "aparencia.fundo", "mata-atlantica")
        antes = len(m.serial())
        m.clique(990, 120, botao=2)
        esperar(m, "menu aberto (tipo 1, 4 itens)", 10, antes)
        m.captura("H81-6-menu-area")
        m.clique(980, 740)                                   # fora: fecha
        antes = len(m.serial())
        m.clique(t["fx"] + 200, t["fy"] + 12, botao=2)
        esperar(m, "menu aberto (tipo 2, 3 itens)", 10, antes)
        m.captura("H81-7-menu-titulo")
        m.clique(t["fx"] + 200 + 20, t["fy"] + 12 + 4 + 5)   # "Minimizar"
        esperar(m, "minimizada", 10, antes)
        verificar(True, "menu da barra de título: Minimizar funciona")
        # Volta pela doca (o Terminal é o 2º fixo).
        dx = (1024 - (5 * 52 + 16)) // 2
        m.clique(dx + 8 + 52 + 26, 768 - 6 - 56 + 24)
        time.sleep(1)
        # Menu próprio do Terminal: "Limpar a tela" (3º item) manda Ctrl+L ao shell.
        antes = len(m.serial())
        m.clique(t["x"] + 300, t["y"] + 200, botao=2)
        esperar(m, "menu aberto (tipo 3, 6 itens)", 10, antes)   # 6: entrou "Copiar a seleção"
        m.captura("H81-8-menu-terminal")
        m.clique(t["x"] + 300 + 20, t["y"] + 200 + 4 + 2 * 26 + 10)
        esperar(m, "terminal: menu 2", 10, antes)
        verificar(True, "menu do Terminal (SIS_MENU_JANELA): a escolha chegou ao programa em EV_MENU")

        # Barra de tarefas no lugar da doca.
        sem = m.captura("H81-9a-sem-barra", guardar=False)
        config(m, "aparencia.barra", "tarefas")
        antes = len(m.serial())
        m.digitar("calculadora\n", 0.04)
        esperar(m, "criada", 20, antes)
        time.sleep(1.5)
        png = m.captura("H81-9-barra-de-tarefas")
        # Os botões (Terminal e Calculadora) pintam a barra de cima onde antes
        # estava a frase; a doca some do pé da tela.
        verificar(cor_do_pixel(png, 120 + 100, 20) != cor_do_pixel(sem, 120 + 100, 20), "barra de tarefas: botões na barra de cima")
        verificar(cor_do_pixel(png, 512, 768 - 30) != cor_do_pixel(sem, 512, 768 - 30), "a doca saiu do pé da tela")
        antes = len(m.serial())
        m.clique(120 + 60, 12)                                # botão do Terminal: vem para a frente
        esperar(m, "barra de tarefas: Terminal", 10, antes)
        verificar(True, "clicar no botão da barra de tarefas traz a janela")
        config(m, "aparencia.barra", "doca")
        config(m, "aparencia.tema", "escuro")
        # O Sagui Code no menu Iniciar (grade 5 x 4): o item 16 abre a janela;
        # o 17, "Sagui Code (terminal)", abre um Terminal já com ele.
        antes = len(m.serial())
        m.clique(40, 12)
        time.sleep(0.8)
        m.clique(44 + 2 * 108 + 54, 134 + 3 * 92 + 30)
        esperar(m, "executar /bin/saguicode ->", 15, antes)
        verificar(True, "o menu do sagui abre o Sagui Code num Terminal")
    return 0


if __name__ == "__main__":
    sys.exit(main())
