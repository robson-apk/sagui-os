#!/usr/bin/env python3
"""H92 - Paciência Banana (a paciência de dez colunas).

Critérios:
  - as regras (userland/jogos/paciencia/regras.c) passam no teste do
    computador de desenvolvimento (tests/jogos/teste_paciencia.c);
  - no SaguiOS: a janela "Paciência Banana" abre com a partida da semente;
    a dica (H) mostra uma jogada, e arrastar a carta até o destino faz a
    jogada; um clique numa carta a leva para o melhor lugar; o clique no
    monte dá 10 cartas; Ctrl+Z desfaz;
  - na partida "quase ganha", o clique no ás completa a última sequência e
    aparece a tela de vitória;
  - a partida com os quatro naipes abre.
"""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, RAIZ, FalhaTeste  # noqa: E402

PASTA = os.path.join(RAIZ, "build", "testes")


def teste_hospedeiro():
    os.makedirs(PASTA, exist_ok=True)
    exe = os.path.join(PASTA, "teste_paciencia")
    fontes = os.path.join(RAIZ, "userland", "jogos", "paciencia")
    subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", "-O2", "-DTESTE_HOSPEDEIRO", "-I", fontes, "-o", exe,
                    os.path.join(RAIZ, "tests", "jogos", "teste_paciencia.c"), os.path.join(fontes, "regras.c")], check=True)
    r = subprocess.run([exe], capture_output=True, text=True)
    for linha in r.stdout.splitlines():
        print("  " + linha)
    verificar(r.returncode == 0, "as regras da Paciência Banana passam no teste do computador de desenvolvimento")


def esperar(m, padrao, tempo, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        s = m.serial()[desde:]
        if re.search(padrao, s):
            return s
        time.sleep(0.3)
    raise FalhaTeste(f"não apareceu no log: {padrao!r}")


DICA = r"paciencia: dica col (\d+) carta (\d+) \((\d+),(\d+)\) -> col (\d+) \((\d+),(\d+)\)"


def main():
    teste_hospedeiro()
    with Maquina("h92-pa", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("paciencia --semente 42")
        s = esperar(m, r"paciencia: pronto", 20)
        verificar("partida nova (naipes 1, semente 42)" in s and "nova -> monte=50 feitas=0 pontos=500" in s,
                  "a partida começa com 50 cartas no monte e 500 pontos")
        time.sleep(1.5)
        jan = janela(m.serial(), "Paciência Banana")
        x0, passo, topo, cl, ca, base = map(int, re.findall(
            r"layout x0=(\d+) passo=(\d+) topo=(\d+) carta=(\d+)x(\d+) base=(\d+)", s)[-1])
        m.captura("H92-6-paciencia")

        # Dica e arrastar.
        antes = len(m.serial())
        m.tecla("h")
        s = esperar(m, DICA, 10, antes)
        de, i, sx, sy, para, dx, dy = map(int, re.findall(DICA, s)[-1])
        time.sleep(0.5)
        m.captura("H92-7-paciencia-dica")
        antes = len(m.serial())
        m.arrastar(jan["x"] + sx, jan["y"] + sy, jan["x"] + dx, jan["y"] + dy)
        s = esperar(m, rf"jogada col {de} carta {i} -> col {para} -> monte=50 feitas=0 pontos=499 jogadas=1", 10, antes)
        verificar(True, f"arrastar a carta da dica (coluna {de} para a {para}) faz a jogada")

        # Clique simples: a próxima dica, feita com um clique na carta.
        antes = len(m.serial())
        m.tecla("h")
        s = esperar(m, DICA, 10, antes)
        de, i, sx, sy, para, dx, dy = map(int, re.findall(DICA, s)[-1])
        antes = len(m.serial())
        m.clique(jan["x"] + sx, jan["y"] + sy)
        s = esperar(m, rf"jogada col {de} carta {i} -> col \d+ -> monte=50 feitas=0 pontos=498 jogadas=2", 10, antes)
        verificar(True, "um clique leva a carta para o melhor lugar")

        # O monte e o desfazer.
        antes = len(m.serial())
        m.clique(jan["x"] + 800 - 10 - cl // 2, jan["y"] + base + ca // 2)
        esperar(m, r"dar -> monte=40", 10, antes)
        verificar(True, "o clique no monte dá uma carta para cada coluna")
        time.sleep(0.5)
        m.captura("H92-8-paciencia-dar")
        antes = len(m.serial())
        m.tecla("ctrl-z")
        esperar(m, r"desfazer -> monte=50 feitas=0 pontos=498 jogadas=2", 10, antes)
        verificar(True, "Ctrl+Z desfaz a última jogada")
        m.clique(jan["fx"] + jan["l"] + 4 - 2 - 20 + 8, jan["fy"] + 12)     # fecha
        time.sleep(1)

        # A vitória.
        antes = len(m.serial())
        m.comando("paciencia --quase")
        esperar(m, r"paciencia: pronto", 20, antes)
        time.sleep(1.5)
        jan = janela(m.serial(), "Paciência Banana")
        antes = len(m.serial())
        m.clique(jan["x"] + x0 + passo + cl // 2, jan["y"] + topo + 10)
        s = esperar(m, r"paciencia: ganhou \(pontos \d+\)", 10, antes)
        verificar("sequencia completa (8 de 8)" in s, "o ás completa a oitava sequência e o jogo termina com vitória")
        time.sleep(1)
        m.captura("H92-9-paciencia-vitoria")
        # Os quatro naipes, para ver as cartas de copas, ouros e paus.
        m.clique(jan["fx"] + jan["l"] + 4 - 2 - 20 + 8, jan["fy"] + 12)     # fecha
        time.sleep(1)
        antes = len(m.serial())
        m.comando("paciencia --naipes 4 --semente 7")
        s = esperar(m, r"paciencia: pronto \(naipes 4\)", 20, antes)
        verificar("partida nova (naipes 4, semente 7)" in s, "a partida com 4 naipes abre")
        time.sleep(1.5)
        m.captura("H92-10-paciencia-4-naipes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
