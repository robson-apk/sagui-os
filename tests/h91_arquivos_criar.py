#!/usr/bin/env python3
"""H91 - Arquivos: "Novo" no menu, nome editado na própria linha, F2, laço e
apagar vários.

Critérios:
  - o botão direito no vazio da lista oferece Nova pasta, Novo arquivo de
    texto e Nova página HTML;
  - "Nova pasta" cria a pasta e abre a edição do nome na própria linha (sem
    janela de diálogo); digitar troca o nome marcado, e Enter renomeia;
  - "Novo arquivo de texto" cria "Novo arquivo.txt" com só "Novo arquivo"
    marcado: digitar "notas" dá "notas.txt";
  - F2 renomeia o item selecionado na linha;
  - Esc desiste e mantém o nome;
  - arrastar no vazio da lista desenha o laço translúcido e marca os itens
    tocados; Delete apaga os marcados de uma vez (vão para a lixeira);
  - nenhuma janela de diálogo abre para dar nome (o log não mostra janela nova
    entre criar e renomear).
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, janela, cor_do_pixel  # noqa: E402

LATERAL = 140
LISTA_X, LISTA_Y = LATERAL + 6, 70
ITEM = 20


def esperar(m, texto, tempo=15, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        if texto in m.serial()[desde:]:
            return m.serial()[desde:]
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def menu(m, x, y, item, desde):
    """Botão direito em (x, y) e clique no item de número "item" do menu."""
    m.clique(x, y, botao=2)
    s = esperar(m, "[GUI] menu aberto", 10, desde)
    cx, cy = map(int, re.findall(r"menu aberto \(tipo \d+, \d+ itens\) em \((\d+),(\d+)\)", s)[-1])
    m.clique(cx + 30, cy + 4 + item * 26 + 13)


def main():
    with Maquina("h91", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.digitar("arquivos /tmp\n", 0.03)
        esperar(m, "arquivos: pronto", 30)
        time.sleep(1)
        a = janela(m.serial(), "Arquivos")
        x0, y0 = a["x"], a["y"]
        vazio = (x0 + LISTA_X + 300, y0 + LISTA_Y + 250)

        # Nova pasta pelo menu do vazio: edita na linha; "Projetos" + Enter.
        n = len(m.serial())
        menu(m, *vazio, 0, n)
        s = esperar(m, "arquivos: renomeando Nova pasta", 10, n)
        verificar("criado /tmp/Nova pasta" in s, "o menu do vazio cria a Nova pasta")
        time.sleep(0.5)
        m.captura("H91-1-nome-na-linha")
        verificar("criada por" not in m.serial()[n:], "nenhuma janela de diálogo abriu para pedir o nome")
        m.digitar("Projetos\n", 0.05)
        esperar(m, "renomeado /tmp/Nova pasta -> /tmp/Projetos", 10, n)
        verificar(True, "digitar na linha e Enter renomeiam a pasta (Projetos)")

        # Entra na pasta e cria três arquivos de texto.
        n = len(m.serial())
        m.digitar("\n", 0.05)                       # Enter abre a pasta selecionada
        esperar(m, "arquivos: pasta /tmp/Projetos", 10, n)
        time.sleep(0.5)
        for k, nome in enumerate(["notas", "lista", "ideias"]):
            n = len(m.serial())
            menu(m, *vazio, 1, n)                   # Novo arquivo de texto
            esperar(m, "arquivos: renomeando Novo arquivo.txt", 10, n)
            m.digitar(nome + "\n", 0.05)
            esperar(m, f"renomeado /tmp/Projetos/Novo arquivo.txt -> /tmp/Projetos/{nome}.txt", 10, n)
        verificar(True, "Novo arquivo de texto marca só o nome: \"notas\" virou notas.txt (e lista, ideias)")

        # F2 renomeia; Esc desiste.
        n = len(m.serial())
        m.tecla("f2")
        esperar(m, "arquivos: renomeando ideias.txt", 10, n)
        m.digitar("planos\n", 0.05)
        esperar(m, "renomeado /tmp/Projetos/ideias.txt -> /tmp/Projetos/planos.txt", 10, n)
        verificar(True, "F2 renomeia na linha (ideias.txt -> planos.txt)")
        n = len(m.serial())
        m.tecla("f2")
        esperar(m, "arquivos: renomeando planos.txt", 10, n)
        m.digitar("xyz", 0.05)
        m.tecla("esc")
        time.sleep(0.8)
        verificar("renomeado" not in m.serial()[n:], "Esc desiste e o nome fica")

        # Laço: do vazio à direita da primeira linha até a terceira.
        n = len(m.serial())
        m.mouse_para(x0 + LISTA_X + 400, y0 + LISTA_Y + 6)
        m.monitor("mouse_button 1")
        time.sleep(0.2)
        m.mouse_mover(x0 + LISTA_X + 30, y0 + LISTA_Y + 2 + 3 * ITEM + 30)   # até abaixo do último
        time.sleep(0.6)
        png = m.captura("H91-2-laco")
        m.monitor("mouse_button 0")
        s = esperar(m, "arquivos: 3 marcados", 10, n)
        verificar(True, "o laço marcou os 3 arquivos")
        # translúcido: um pixel do laço abaixo dos itens, sobre o branco da lista
        cor = cor_do_pixel(png, x0 + LISTA_X + 300, y0 + LISTA_Y + 2 + 3 * ITEM + 15)
        verificar(cor != (255, 255, 255) and min(cor) > 150, f"o laço é translúcido sobre a lista ({cor})")

        n = len(m.serial())
        m.tecla("delete")
        esperar(m, '"Apagar"', 10, n)
        time.sleep(0.8)
        m.captura("H91-3-apagar-varios")
        m.digitar("\n", 0.05)                       # Sim
        s = esperar(m, "arquivos: pasta /tmp/Projetos (0 itens)", 15, n)
        verificar(s.count("para a lixeira") == 3, f"Delete mandou os 3 marcados para a lixeira ({s.count('para a lixeira')})")


if __name__ == "__main__":
    main()
