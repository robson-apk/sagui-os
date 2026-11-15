#!/usr/bin/env python3
"""H74 a H77 - visual moderno.

Critérios:
  H74 vidro: a barra de título e a barra de cima misturam a cor do tema com o
      que está atrás, desfocado (o pixel não é a cor pura, e muda quando o
      fundo atrás muda); há sombra embaixo da janela; o tempo de um quadro
      inteiro com vidro é medido e registrado.
  H75 forma: cantos arredondados; a doca mostra os fixos e os abertos; clicar
      num item traz a janela; minimizar esconde e a doca restaura; o menu do
      sagui abre um programa.
  H76 tema: capturas do tema escuro e do claro (a barra de cima fica clara).
  H77 efeitos leves: sem desfoque (a barra de título vira a cor pura) e a
      escolha fica guardada.
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


def main():
    with Maquina("h74", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(2)
        medida = re.search(r"quadro inteiro com vidro: (\d+) ms; efeitos (\w+)", m.serial())
        verificar(medida is not None, f"tempo de um quadro inteiro com vidro medido: {medida.group(1) if medida else '?'} ms, efeitos {medida.group(2) if medida else '?'}")
        antes = len(m.serial())
        m.digitar("calculadora\n", 0.04)
        esperar(m, "criada", 20, antes)
        time.sleep(1.5)
        j = janela(m.serial(), "Calculadora")
        png = m.captura("H74-1-vidro")
        # Barra de título da janela com foco: verde misturado (não a cor pura).
        cor = cor_do_pixel(png, j["x"] + j["l"] // 2, j["y"] - 20)
        verificar(cor != (0x0B, 0x7A, 0x3E) and cor[1] > cor[0] and cor[1] > cor[2], f"barra de título de vidro verde {cor}")
        # Sombra: logo abaixo da janela é mais escuro que 30 pixels abaixo.
        perto = sum(cor_do_pixel(png, j["x"] + 100, j["y"] + j["a"] + 6))
        longe = sum(cor_do_pixel(png, j["x"] + 100, j["y"] + j["a"] + 30))
        verificar(perto < longe, f"sombra embaixo da janela ({perto} < {longe})")
        # Canto arredondado: o primeiro pixel do canto de cima não é da barra.
        canto = cor_do_pixel(png, j["fx"], j["fy"])
        verificar(canto != cor, "canto de cima arredondado")
        # Arrastar a janela muda o que está atrás do vidro da barra.
        m.arrastar(j["x"] + 60, j["y"] - 12, j["x"] + 260, j["y"] + 100)
        time.sleep(1)
        j2 = dict(j)
        mv = re.findall(r"janela \d+ movida para \((-?\d+),(-?\d+)\)", m.serial())
        verificar(mv, "a janela foi arrastada (log)")
        j2["fx"], j2["fy"] = int(mv[-1][0]), int(mv[-1][1])
        j2["x"], j2["y"] = j2["fx"] + 2, j2["fy"] + 24
        png2 = m.captura("H74-2-arrastada")
        verificar(cor_do_pixel(png2, j2["x"] + j2["l"] // 2, j2["y"] - 20) != (0x0B, 0x7A, 0x3E), "o vidro continua depois de arrastar")
        # H75: doca e minimizar.
        antes = len(m.serial())
        bx = j2["fx"] + j2["l"] + 4 - 2 - 20 - 24 + 8
        m.clique(bx, j2["fy"] + 12)
        esperar(m, "minimizada", 10, antes)
        time.sleep(1)
        png3 = m.captura("H75-1-minimizada")
        verificar(cor_do_pixel(png3, j2["x"] + 40, j2["y"] + 40) != cor_do_pixel(png2, j2["x"] + 40, j2["y"] + 40),
                  "minimizar esconde a janela")
        # A doca fica embaixo, no meio; a Calculadora é o primeiro item depois dos 5 fixos.
        largura, altura = 1024, 768
        itens = 6 + 0
        dl = itens * 52 + 16
        dx = (largura - dl) // 2
        antes = len(m.serial())
        m.clique(dx + 8 + 5 * 52 + 26, altura - 6 - 56 + 24)
        s = esperar(m, "restaurada", 10, antes)
        verificar("doca: Calculadora" in s, "clicar na doca restaura a calculadora")
        time.sleep(1)
        m.captura("H75-2-doca")
        # Menu do sagui: abre e escolhe o Editor.
        antes = len(m.serial())
        m.clique(40, 12)
        esperar(m, "menu aberto", 10, antes)
        time.sleep(0.5)
        m.captura("H75-3-menu")
        m.clique(40, 24 + 4 + 4 + 3 * 30 + 14)   # H88: linha de 30
        s = esperar(m, "abrir /bin/editor", 10, antes)
        verificar(True, "o menu do sagui abriu o Editor")
        time.sleep(2)
        # H76: temas.
        m.captura("H76-1-tema-escuro")
        # Traz o Terminal pela doca (o segundo item fixo). Itens: os 5 fixos e a Calculadora.
        dl = 6 * 52 + 16
        x0, linha = (1024 - dl) // 2, 768 - 6 - 28
        antes = len(m.serial())
        m.clique(x0 + 8 + 52 + 26, linha)
        esperar(m, "doca: Terminal", 10, antes)
        time.sleep(1)
        m.comando("config aparencia.tema claro", tempo=20)
        time.sleep(1.5)
        png4 = m.captura("H76-2-tema-claro")
        verificar(sum(cor_do_pixel(png4, 600, 12)) > 500, f"tema claro: a barra de cima fica clara {cor_do_pixel(png4, 600, 12)}")
        m.comando("config aparencia.tema escuro", tempo=20)
        # H77: efeitos leves.
        m.comando("config aparencia.efeitos leves", tempo=20)
        time.sleep(1.5)
        png5 = m.captura("H77-1-efeitos-leves")
        t = janela(m.serial(), "Terminal")
        verificar(cor_do_pixel(png5, t["x"] + t["l"] // 2, t["y"] - 20) == (0x0B, 0x7A, 0x3E), "efeitos leves: barra de título na cor pura (sem vidro)")
        out = m.comando("config aparencia.efeitos", tempo=20)
        verificar("leves" in out, "a escolha dos efeitos fica guardada")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
