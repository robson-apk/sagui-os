#!/usr/bin/env python3
"""H92 - o visor de atividades JClic.

Critérios:
  - o leitor de XML, o projeto e as regras (userland/jogos/jclic: xml.c,
    projeto.c, motor.c) passam no teste do computador de desenvolvimento
    (tests/jogos/teste_jclic.c), que resolve as atividades do exemplo;
  - no SaguiOS: "jclic" mostra a lista de /jogos/jclic; o duplo clique abre o
    exemplo sagui.jclic.zip (ZIP com DEFLATE) com as 6 atividades;
  - as atividades são resolvidas com cliques e letras, como uma pessoa faria:
    memória, associação, quebra-cabeça de troca, caça-palavras e cruzadinha.
"""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, janela, RAIZ, FalhaTeste  # noqa: E402

PASTA = os.path.join(RAIZ, "build", "testes")
EXEMPLO = os.path.join(RAIZ, "userland", "jogos", "jclic", "exemplo", "sagui.jclic")


def teste_hospedeiro():
    os.makedirs(PASTA, exist_ok=True)
    exe = os.path.join(PASTA, "teste_jclic")
    j = os.path.join(RAIZ, "userland", "jogos", "jclic")
    subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", "-O2", "-DTESTE_HOSPEDEIRO", "-I", j, "-o", exe,
                    os.path.join(RAIZ, "tests", "jogos", "teste_jclic.c")] +
                   [os.path.join(j, f) for f in ("xml.c", "projeto.c", "motor.c")], check=True)
    r = subprocess.run([exe, EXEMPLO], capture_output=True, text=True)
    for linha in r.stdout.splitlines():
        print("  " + linha)
    verificar(r.returncode == 0, "as regras do JClic resolvem o projeto de exemplo no computador de desenvolvimento")


def esperar(m, padrao, tempo, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        s = m.serial()[desde:]
        if re.search(padrao, s):
            return s
        time.sleep(0.3)
    raise FalhaTeste(f"não apareceu no log: {padrao!r}")


def caixas(s):
    """{(p, i): (orig, cx, cy)} das caixas da última atividade."""
    r = {}
    for p, i, o, x, y, w, h in re.findall(r"jclic: caixa p(\d) i(\d+) orig (\d+) em \((\d+),(\d+),(\d+),(\d+)\)", s):
        r[(int(p), int(i))] = (int(o), int(x) + int(w) // 2, int(y) + int(h) // 2)
    return r


def main():
    teste_hospedeiro()
    with Maquina("h92-jc", "ide", memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        antes = len(m.serial())
        m.comando("jclic")
        s = esperar(m, r"jclic: lista pronta \(\d+ projetos\)", 20, antes)
        itens = dict((c, int(i)) for i, c in re.findall(r"jclic: item (\d+) (\S+)", s))
        verificar("/jogos/jclic/sagui.jclic.zip" in itens, "a lista mostra o exemplo de /jogos/jclic")
        time.sleep(1)
        jan = janela(m.serial(), "Atividades JClic")
        X, Y = jan["x"], jan["y"]

        def clicar(x, y, **k):
            m.clique(X + x, Y + y, **k)

        antes = len(m.serial())
        clicar(100, 76 + 2 + itens["/jogos/jclic/sagui.jclic.zip"] * 20 + 10, duplo=True)
        s = esperar(m, r"jclic: atividade 1 de 6: boas-vindas", 15, antes)
        verificar('jclic: projeto "O sagui e a banana" (6 atividades)' in s, "o ZIP abre e o projeto tem 6 atividades")
        time.sleep(1)
        m.captura("H92-14-jclic-boas-vindas")

        # Memória: as duas cartas de cada par.
        antes = len(m.serial())
        m.tecla("n")
        s = esperar(m, r"jclic: atividade 2 de 6: silabas \(jogo da mem", 10, antes)
        time.sleep(0.5)
        cx = caixas(s)
        pares = {}
        for (p, i), (o, x, y) in cx.items():
            pares.setdefault(o, []).append((x, y))
        primeiro = True
        for o, ((x1, y1), (x2, y2)) in sorted(pares.items()):
            clicar(x1, y1)
            clicar(x2, y2)
            if primeiro:
                time.sleep(0.3)
                m.captura("H92-15-jclic-memoria")
                primeiro = False
        esperar(m, r"jclic: resolvida \(silabas\)", 10, antes)
        verificar(True, "memória: abrir os pares resolve a atividade")

        # Associação: cada fruta com a sua cor.
        antes = len(m.serial())
        m.tecla("n")
        s = esperar(m, r"jclic: atividade 3 de 6: cores", 10, antes)
        time.sleep(0.5)
        cx = caixas(s)
        for (p, i), (o, x, y) in sorted(cx.items()):
            if p != 0:
                continue
            bx, by = next((x2, y2) for (p2, _), (o2, x2, y2) in cx.items() if p2 == 1 and o2 == o)
            clicar(x, y)
            clicar(bx, by)
        esperar(m, r"jclic: resolvida \(cores\)", 10, antes)
        verificar(True, "associação: cada fruta com a sua cor")
        m.captura("H92-16-jclic-associacao")

        # Quebra-cabeça de troca: o teste acompanha as trocas.
        antes = len(m.serial())
        m.tecla("n")
        s = esperar(m, r"jclic: atividade 4 de 6: frase", 10, antes)
        time.sleep(0.5)
        cx = caixas(s)
        pos = {i: (x, y) for (p, i), (o, x, y) in cx.items()}
        orig = {i: o for (p, i), (o, x, y) in cx.items()}
        for i in sorted(pos):
            if orig[i] != i:
                k = next(k for k in orig if orig[k] == i)
                clicar(*pos[i])
                clicar(*pos[k])
                orig[i], orig[k] = orig[k], orig[i]
        esperar(m, r"jclic: resolvida \(frase\)", 10, antes)
        verificar(True, "quebra-cabeça de troca: a frase fica em ordem")

        # Caça-palavras: o teste procura as palavras nas letras da grade.
        antes = len(m.serial())
        m.tecla("n")
        s = esperar(m, r"jclic: atividade 5 de 6: palavras", 10, antes)
        time.sleep(0.5)
        gx, gy, casa = map(int, re.findall(r"jclic: grade x0=(\d+) y0=(\d+) casa=(\d+)", s)[-1])
        grade = [l for _, l in re.findall(r"jclic: g(\d+) (\S+)", s)]
        for palavra in ("SAGUI", "BANANA", "FOLHA", "MATA", "GALHO"):
            achou = None
            for y in range(len(grade)):
                for x in range(len(grade[0])):
                    for dx, dy in ((1, 0), (0, 1), (1, 1), (1, -1), (-1, 0), (0, -1), (-1, -1), (-1, 1)):
                        fx, fy = x + dx * (len(palavra) - 1), y + dy * (len(palavra) - 1)
                        if 0 <= fx < len(grade[0]) and 0 <= fy < len(grade) and \
                                all(grade[y + dy * k][x + dx * k] == palavra[k] for k in range(len(palavra))):
                            achou = achou or (x, y, fx, fy)
            x, y, fx, fy = achou
            clicar(gx + x * casa + casa // 2, gy + y * casa + casa // 2)
            clicar(gx + fx * casa + casa // 2, gy + fy * casa + casa // 2)
        esperar(m, r"jclic: resolvida \(palavras\)", 10, antes)
        verificar(True, "caça-palavras: as 5 palavras achadas com dois cliques cada")
        m.captura("H92-17-jclic-caca-palavras")

        # Cruzadinha: clica em cada casa e digita a letra.
        antes = len(m.serial())
        m.tecla("n")
        s = esperar(m, r"jclic: atividade 6 de 6: cruzadinha", 10, antes)
        time.sleep(0.5)
        gx, gy, casa = map(int, re.findall(r"jclic: grade x0=(\d+) y0=(\d+) casa=(\d+)", s)[-1])
        grade = [l for _, l in re.findall(r"jclic: g(\d+) (\S+)", s)]
        for y, linha in enumerate(grade):
            for x, c in enumerate(linha):
                if c != "*":
                    clicar(gx + x * casa + casa // 2, gy + y * casa + casa // 2)
                    m.digitar(c.lower(), 0.05)
        esperar(m, r"jclic: resolvida \(cruzadinha\)", 10, antes)
        verificar(True, "cruzadinha: as letras digitadas completam a grade")
        time.sleep(0.5)
        m.captura("H92-18-jclic-cruzadinha")
    return 0


if __name__ == "__main__":
    sys.exit(main())
