#!/usr/bin/env python3
"""H61 - leitor de HTML do Sagui Navegador.

Critério: páginas de teste com HTML mal formado (tags sem fechar, fechadas
fora de ordem, parágrafos dentro de blocos, listas e tabelas sem as tags de
fechamento, entidades com e sem ';', UTF-8, Latin-1 e Windows-1252) montam a
árvore esperada (tests/navegador/html/*.esperado, em Latin-1, conferidas à
mão uma vez). Além disso, 300 versões embaralhadas e cortadas dessas páginas
e de bytes aleatórios não derrubam o leitor (ele nunca desiste).

O leitor (userland/navegador/html.c) é compilado aqui com o sagui.h simulado e
roda no computador; a árvore é impressa por tests/navegador/teste_html.c.
"""
import glob
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import FalhaTeste, RAIZ, SAIDA  # noqa: E402

PASTA = os.path.join(SAIDA, "h61-html")
PROGRAMA = os.path.join(PASTA, "teste_html")
CASOS = os.path.join(RAIZ, "tests", "navegador", "html")
sorte = random.Random(61)


def verificar(condicao, mensagem, detalhe=""):
    if not condicao:
        raise FalhaTeste(f"{mensagem}: {detalhe}" if detalhe else mensagem)
    print("  ok:", mensagem)


def main():
    os.makedirs(PASTA, exist_ok=True)
    r = subprocess.run(["gcc", "-m32", "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover",
                        "-Wall", "-Wextra", "-Werror",
                        "-I", os.path.join(RAIZ, "tests", "navegador", "simulado"), "-o", PROGRAMA,
                        os.path.join(RAIZ, "tests", "navegador", "teste_html.c"),
                        os.path.join(RAIZ, "userland", "navegador", "html.c")], capture_output=True, text=True)
    verificar(r.returncode == 0, "leitor de HTML compila (com verificação de memória)", r.stderr[-2000:])
    paginas = sorted(glob.glob(os.path.join(CASOS, "*.html")))
    verificar(len(paginas) >= 4, f"{len(paginas)} páginas de teste")
    for p in paginas:
        r = subprocess.run([PROGRAMA, p], capture_output=True, timeout=30)
        verificar(r.returncode == 0, f"{os.path.basename(p)} lida", r.stderr.decode(errors="replace")[-1500:])
        with open(p[:-5] + ".esperado", "rb") as f:
            esperado = f.read()
        if r.stdout != esperado:
            a, b = r.stdout.decode("latin-1").splitlines(), esperado.decode("latin-1").splitlines()
            i = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))
            raise FalhaTeste(f"{os.path.basename(p)}: árvore diferente na linha {i + 1}: "
                             f"{a[i] if i < len(a) else '(fim)'!r} x {b[i] if i < len(b) else '(fim)'!r}")
        print(f"  ok: {os.path.basename(p)} montou a árvore esperada ({len(esperado.splitlines())} linhas)")
    # Charset vindo do servidor ganha do <meta>.
    r = subprocess.run([PROGRAMA, os.path.join(CASOS, "02-latin1.html"), "1"], capture_output=True, timeout=30)
    verificar(r.returncode == 0 and b"utf8: 1" in r.stdout and b"a?" in r.stdout,
              "charset do Content-Type tem prioridade sobre o <meta>")
    # Robustez: páginas cortadas, embaralhadas e lixo.
    bases = [open(p, "rb").read() for p in paginas]
    for k in range(300):
        b = bytearray(sorte.choice(bases))
        if k % 3 == 0:
            b = b[:sorte.randrange(len(b) + 1)]
        elif k % 3 == 1:
            for _ in range(sorte.randrange(1, 40)):
                i = sorte.randrange(len(b))
                b[i] = sorte.choice(b"<>/&;\"'=! \x00\xc3\xff#x")
        else:
            b = bytearray(sorte.randrange(256) for _ in range(sorte.randrange(1, 3000)))
        caminho = os.path.join(PASTA, "estragada.html")
        with open(caminho, "wb") as f:
            f.write(b)
        r = subprocess.run([PROGRAMA, caminho], capture_output=True, timeout=30)
        if r.returncode != 0:
            os.replace(caminho, os.path.join(PASTA, f"falhou-{k}.html"))
            raise FalhaTeste(f"página estragada {k} derrubou o leitor: {r.stderr.decode(errors='replace')[-800:]}")
    verificar(True, "300 páginas estragadas lidas sem erro de memória")


if __name__ == "__main__":
    try:
        main()
        print("PASSOU")
    except FalhaTeste as e:
        print(f"FALHOU: {e}")
        sys.exit(1)
