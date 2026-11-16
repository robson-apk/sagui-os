#!/usr/bin/env python3
"""H92 - Caça-Bananas (o jogo de achar as bananas-bomba).

Critérios:
  - as regras (userland/jogos/cacabananas/regras.c) passam no teste do
    computador de desenvolvimento (tests/jogos/teste_cacabananas.c): primeiro
    clique seguro, abertura em cadeia, bandeiras, acorde, vitória e derrota;
  - no SaguiOS: a janela "Caça-Bananas" abre; o primeiro clique abre uma área
    (o log mostra onde ficaram as bombas); o botão direito põe uma bandeira;
  - abrir todas as casas sem bomba pelo teclado ganha (o sagui com o cacho);
  - numa partida nova (F2), abrir uma banana-bomba perde.
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
    exe = os.path.join(PASTA, "teste_cacabananas")
    subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", "-O2", "-DTESTE_HOSPEDEIRO",
                    "-I", os.path.join(RAIZ, "userland", "jogos", "cacabananas"), "-o", exe,
                    os.path.join(RAIZ, "tests", "jogos", "teste_cacabananas.c"),
                    os.path.join(RAIZ, "userland", "jogos", "cacabananas", "regras.c")], check=True)
    r = subprocess.run([exe], capture_output=True, text=True)
    for linha in r.stdout.splitlines():
        print("  " + linha)
    verificar(r.returncode == 0, "as regras do Caça-Bananas passam no teste do computador de desenvolvimento")


def esperar(m, padrao, tempo, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        s = m.serial()[desde:]
        if re.search(padrao, s):
            return s
        time.sleep(0.3)
    raise FalhaTeste(f"não apareceu no log: {padrao!r}")


def bombas(s):
    linhas = re.findall(r"cacabananas: b(\d+) ([.*]+)", s)
    return [l for _, l in linhas[-9:]]


def main():
    teste_hospedeiro()
    with Maquina("h92-cb", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        m.comando("cacabananas --semente 1234")
        s = esperar(m, r"cacabananas: pronto", 20)
        time.sleep(1)
        jan = janela(m.serial(), "Caça-Bananas")
        x0, y0, casa = map(int, re.findall(r"tabuleiro x0=(\d+) y0=(\d+) casa=(\d+)", s)[-1])

        def ponto(cx, cy):
            return jan["x"] + x0 + cx * casa + casa // 2, jan["y"] + y0 + cy * casa + casa // 2

        antes = len(m.serial())
        m.clique(*ponto(4, 4))
        s = esperar(m, r"clique 4,4 -> jogando abertas=\d+", 10, antes)
        abertas = int(re.findall(r"clique 4,4 -> jogando abertas=(\d+)", s)[-1])
        verificar(abertas >= 9, f"o primeiro clique abre uma área ({abertas} casas) e não acha bomba")
        mapa = bombas(m.serial())
        verificar(len(mapa) == 9 and sum(l.count("*") for l in mapa) == 10, "o campo Fácil tem 10 bananas-bomba")
        bx, by = next((x, y) for y in range(9) for x in range(9) if mapa[y][x] == "*")
        antes = len(m.serial())
        m.clique(*ponto(bx, by), botao=2)
        esperar(m, rf"clique {bx},{by} -> jogando abertas=\d+ bandeiras=1", 10, antes)
        verificar(True, "o botão direito põe a bandeirinha do sagui")
        m.captura("H92-3-cacabananas")

        # Abre pelo teclado todas as casas sem bomba: o cursor está na
        # bandeira; vai ao canto (0,0) e percorre as linhas.
        antes = len(m.serial())
        for _ in range(bx):
            m.tecla("left", 0.03)
        for _ in range(by):
            m.tecla("up", 0.03)
        for y in range(9):
            for x in range(9):
                cx = x if y % 2 == 0 else 8 - x      # vai e volta, em zigue-zague
                if mapa[y][cx] == ".":
                    m.tecla("spc", 0.06)
                if x < 8:
                    m.tecla("right" if y % 2 == 0 else "left", 0.03)
            if y < 8:
                m.tecla("down", 0.03)
        s = esperar(m, r"-> ganhou abertas=71 bandeiras=10", 20, antes)
        verificar("perdeu" not in s, "abrir todas as casas sem bomba ganha a partida")
        time.sleep(1)
        m.captura("H92-4-cacabananas-vitoria")

        antes = len(m.serial())
        m.tecla("f2")
        esperar(m, r"cacabananas: nova -> espera", 10, antes)
        m.clique(*ponto(4, 4))
        s = esperar(m, r"clique 4,4 -> jogando", 10, antes)
        mapa = bombas(m.serial())
        bx, by = next((x, y) for y in range(9) for x in range(9) if mapa[y][x] == "*")
        antes = len(m.serial())
        m.clique(*ponto(bx, by))
        esperar(m, rf"clique {bx},{by} -> perdeu", 10, antes)
        verificar(True, "abrir uma banana-bomba perde a partida")
        time.sleep(1)
        m.captura("H92-5-cacabananas-bum")
    return 0


if __name__ == "__main__":
    sys.exit(main())
