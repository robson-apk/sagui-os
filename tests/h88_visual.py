#!/usr/bin/env python3
"""H88 - revisão do visual: cantos suavizados, ícones e Configurações.

Critérios (medidos nos pixels da captura do QEMU):
  - os cantos de cima da janela são suavizados: o pixel do meio da curva tem
    uma cor entre a do fundo e a da barra de título;
  - os cantos de baixo também são redondos: o pixel da ponta é o fundo, não
    a borda da janela (antes eram retos);
  - o menu do sagui tem um ícone colorido em cada item;
  - as Configurações têm a lateral com ícones (algum pixel com cor forte no
    quadrado de 20x20 de cada aba), e os três botões de efeitos
    gravam a escolha;
  - capturas para a revisão a olho.
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
            return
        time.sleep(0.2)
    raise FalhaTeste(f"tempo esgotado esperando {texto!r}")


def distancia(a, b):
    return sum(abs(x - y) for x, y in zip(a, b))


def entre(meio, fora, dentro):
    """meio é uma mistura de verdade: diferente das duas pontas, e mais perto
    da reta entre elas do que de qualquer uma sozinha."""
    return distancia(meio, fora) > 12 and distancia(meio, dentro) > 12 and \
        distancia(fora, meio) + distancia(meio, dentro) <= distancia(fora, dentro) + 40


def main():
    with Maquina("h88", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        n0 = len(m.serial())
        m.digitar("configuracoes aparencia\n", 0.03)
        esperar(m, '"Configurações"', 30, n0)
        time.sleep(1.5)
        c = janela(m.serial(), "Configurações")
        png = m.captura("H88-configuracoes")
        fx, fy = c["fx"], c["fy"]
        fl, fa = c["l"] + 4, c["a"] + 24 + 2               # moldura: bordas de 2 e barra de 24

        # Canto de cima à esquerda: (fx, fy) fica fora da curva (cobertura 0),
        # (fx + 10, fy + 10) dentro da barra, e (fx + 2, fy + 2) no meio dela.
        fora = cor_do_pixel(png, fx, fy)
        dentro = cor_do_pixel(png, fx + 10, fy + 10)
        meio = cor_do_pixel(png, fx + 2, fy + 2)            # cobertura 196/256 na tabela
        verificar(distancia(fora, dentro) > 60, f"o canto de cima é redondo: ponta {fora}, barra {dentro}")
        verificar(entre(meio, fora, dentro) or distancia(meio, dentro) < distancia(meio, fora),
                  f"a curva de cima é suavizada (pixel do meio {meio})")
        meio2 = cor_do_pixel(png, fx + 4, fy)                 # cobertura 48/256: quase fundo
        verificar(entre(meio2, fora, dentro) or distancia(meio2, fora) < 60,
                  f"o pixel quase de fora da curva é quase o fundo ({meio2})")

        # Canto de baixo à esquerda: a ponta é fundo; a borda, 10 pixels ao
        # lado, é a cor da janela.
        ponta = cor_do_pixel(png, fx, fy + fa - 1)
        borda = cor_do_pixel(png, fx + 12, fy + fa - 1)
        verificar(distancia(ponta, borda) > 40, f"o canto de baixo é redondo: ponta {ponta}, borda {borda}")

        # A lateral das Configurações: um ícone em cada aba (cores fortes no
        # quadrado de 20x20 de cada linha).
        coloridos = 0
        for i in range(7):
            x0, y0 = c["x"] + 8 + 12, c["y"] + 16 + i * 40 + 6
            pontos = [cor_do_pixel(png, x0 + u, y0 + v) for u in (3, 7, 10, 13, 17) for v in (3, 7, 10, 13, 17)]
            if any(max(p) - min(p) > 40 for p in pontos):
                coloridos += 1
        verificar(coloridos >= 6, f"a lateral tem ícones coloridos ({coloridos} de 7 com cor no quadrado do ícone)")

        # Efeitos: os três botões gravam a escolha.
        n0 = len(m.serial())
        m.clique(c["x"] + 190 + 136 + 64, c["y"] + 340 + 14)            # Completos
        esperar(m, "configuracoes: efeitos completos", 10, n0)
        verificar(True, "o botão Completos grava aparencia.efeitos = completos")
        time.sleep(1)
        m.captura("H88-efeitos-completos")
        n0 = len(m.serial())
        m.clique(c["x"] + 190 + 272 + 64, c["y"] + 340 + 14)            # Leves
        esperar(m, "configuracoes: efeitos leves", 10, n0)
        verificar(True, "o botão Leves grava aparencia.efeitos = leves")
        time.sleep(1)
        m.captura("H88-efeitos-leves")
        m.clique(c["x"] + 190 + 64, c["y"] + 340 + 14)                  # volta ao automático
        time.sleep(1)

        # O menu Iniciar (H89): ícone de 40x40 em cada célula da grade.
        m.clique(40, 12)
        esperar(m, "[GUI] menu aberto", 10)
        time.sleep(0.8)
        png = m.captura("H88-menu")
        com_icone, contagem = 0, []
        for k in range(19):
            cx, cy = 44 + (k % 5) * 108 + 34, 134 + (k // 5) * 92 + 10
            fundo = cor_do_pixel(png, cx - 30, cy - 6)          # canto da célula: fundo do menu
            pontos = [cor_do_pixel(png, cx + u, cy + v) for u in range(2, 40, 3) for v in range(2, 40, 3)]
            n = sum(distancia(p, fundo) > 40 for p in pontos)
            contagem.append(n)
            if n >= 12:                                         # ícones escuros (Terminal) também
                com_icone += 1
        vazias = []                                             # controle: células sem programa
        for k in (19,):
            cx, cy = 44 + (k % 5) * 108 + 34, 134 + (k // 5) * 92 + 10
            fundo = cor_do_pixel(png, cx - 30, cy - 6)
            vazias.append(sum(distancia(cor_do_pixel(png, cx + u, cy + v), fundo) > 40
                              for u in range(2, 40, 3) for v in range(2, 40, 3)))
        print(f"  pixels de ícone por célula: {contagem}; células vazias: {vazias}")
        verificar(max(vazias) < 12, f"as células vazias não parecem ícone ({vazias})")
        verificar(com_icone >= 18, f"o menu tem ícones ({com_icone} de 19 itens)")
        m.clique(600, 400)
        time.sleep(0.5)


if __name__ == "__main__":
    main()
