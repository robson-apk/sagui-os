#!/usr/bin/env python3
"""H93 (parte 1) - DEFLATE, gzip e CRC-32 da libsagui, no computador.

Critério: o compactar.c da libsagui, compilado no computador de
desenvolvimento com verificação de memória, (1) compacta em todos os níveis
dados variados (vazio, um byte, texto, binário, aleatório, repetições longas,
mais de 1 MB) e o zlib do Python descompacta igual, em DEFLATE puro e em
gzip; (2) descompacta o que o zlib e o gzip do Python produzem (todos os
níveis, vários membros gzip, zlib com Adler-32); (3) calcula CRC-32 e
Adler-32 iguais aos do Python; (4) recusa dados estragados sem erro de
memória; (5) compacta texto pelo menos quase tão bem quanto o zlib nível 6.
"""
import gzip
import os
import random
import subprocess
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import FalhaTeste, RAIZ, SAIDA  # noqa: E402

PASTA = os.path.join(SAIDA, "h93-compactar")
PROG = os.path.join(PASTA, "teste_compactar")
sorte = random.Random(93)


def verificar(cond, msg, detalhe=""):
    if not cond:
        raise FalhaTeste(f"{msg}: {detalhe}" if detalhe else msg)
    print("  ok:", msg)


def rodar(*args):
    return subprocess.run([PROG, *args], capture_output=True, timeout=300,
                          env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1"))


def arquivo(nome, dados):
    c = os.path.join(PASTA, nome)
    with open(c, "wb") as f:
        f.write(dados)
    return c


def amostras():
    texto = open(os.path.join(RAIZ, "userland", "libsagui", "compactar.c"), "rb").read()
    palavras = [b"sagui", b"banana", b"galho", b"floresta", b"pula", b"come", b"\n", b" "]
    return {
        "vazio": b"",
        "um": b"x",
        "dois": b"ab",
        "texto": texto,
        "palavras": b" ".join(sorte.choice(palavras) for _ in range(60000)),
        "aleatorio": bytes(sorte.randrange(256) for _ in range(70000)),
        "zeros": bytes(300000),
        "repete": b"abc" * 50000 + b"xyz",
        "binario": open(os.path.join(RAIZ, "build", "kernel.bin"), "rb").read()[:400000]
                   if os.path.exists(os.path.join(RAIZ, "build", "kernel.bin")) else texto * 5,
        "grande": (texto * 40)[:1500000],
        "longe": (lambda a: a + bytes(sorte.randrange(256) for _ in range(30000)) + a)(bytes(sorte.randrange(256) for _ in range(2500))),
    }


def main():
    os.makedirs(PASTA, exist_ok=True)
    r = subprocess.run(["gcc", "-m32", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover",
                        "-Wall", "-Wextra", "-Werror",
                        "-I", os.path.join(RAIZ, "tests", "navegador", "simulado"),
                        "-I", os.path.join(RAIZ, "userland", "inc"), "-o", PROG,
                        os.path.join(RAIZ, "tests", "compactar", "teste_compactar.c"),
                        os.path.join(RAIZ, "userland", "libsagui", "compactar.c")], capture_output=True, text=True)
    verificar(r.returncode == 0, "compactar.c compila no computador (com verificação de memória)", r.stderr[-1500:])

    saida = os.path.join(PASTA, "saida")
    for nome, dados in amostras().items():
        ent = arquivo(nome, dados)
        for nivel in (0, 1, 4, 6, 9):
            r = rodar("deflar", str(nivel), ent, saida)
            verificar(r.returncode == 0, f"deflar nível {nivel} {nome}", (r.stdout + r.stderr).decode(errors="replace")[-800:])
            comp = open(saida, "rb").read()
            try:
                volta = zlib.decompress(comp, -15)
            except zlib.error as e:
                raise FalhaTeste(f"o zlib do Python recusou nosso DEFLATE ({nome}, nível {nivel}): {e}")
            if volta != dados:
                raise FalhaTeste(f"{nome} nível {nivel}: o zlib do Python descompactou diferente")
            r = rodar("inflar", saida, saida + ".volta")
            if r.returncode != 0 or open(saida + ".volta", "rb").read() != dados:
                raise FalhaTeste(f"{nome} nível {nivel}: nosso inflar não desfez nosso deflar ({r.stdout!r})")
        ref = len(zlib.compress(dados, 6)) - 6
        nosso = len(comp)
        print(f"     {nome}: {len(dados)} -> nível 9: {nosso} bytes (zlib nível 6: {ref})")
        verificar(True, f"{nome}: todos os níveis voltam iguais pelo zlib do Python e pelo nosso inflar")
        if nome in ("texto", "palavras", "grande", "binario"):
            verificar(nosso <= ref * 1.10 + 64, f"{nome}: compactação perto da do zlib (nosso {nosso}, zlib {ref})")
        if nome == "aleatorio":
            verificar(nosso <= len(dados) + len(dados) // 1000 + 64, f"aleatório quase não cresce ({nosso})")
        # Do Python para nós: DEFLATE puro em vários níveis e estratégias.
        for nivel in (0, 1, 6, 9):
            for estrategia in (zlib.Z_DEFAULT_STRATEGY, zlib.Z_FIXED, zlib.Z_HUFFMAN_ONLY):
                c = zlib.compressobj(nivel, zlib.DEFLATED, -15, 9, estrategia)
                z = c.compress(dados) + c.flush()
                r = rodar("inflar", arquivo("py.deflate", z), saida)
                if r.returncode != 0 or open(saida, "rb").read() != dados:
                    raise FalhaTeste(f"nosso inflar errou o DEFLATE do Python ({nome}, nível {nivel}, estratégia {estrategia}): {r.stdout!r}")
            z = zlib.compress(dados, nivel)
            r = rodar("zlib", arquivo("py.zlib", z), saida)
            if r.returncode != 0 or open(saida, "rb").read() != dados:
                raise FalhaTeste(f"inflar_zlib errou o zlib do Python ({nome}, nível {nivel})")
        verificar(True, f"{nome}: DEFLATE e zlib do Python (níveis 0-9, códigos fixos e só Huffman) descompactados")
        # gzip nos dois sentidos.
        r = rodar("gzip", "6", ent, saida)
        verificar(r.returncode == 0 and gzip.decompress(open(saida, "rb").read()) == dados, f"{nome}: nosso gzip abre no Python")
        g = open(saida, "rb").read()
        verificar(g[3] & 8 and g[10:19] == b"nome.txt\0", "  com o nome original no cabeçalho (FNAME)")
        r = rodar("gunzip", arquivo("py.gz", gzip.compress(dados, 9)), saida)
        verificar(r.returncode == 0 and open(saida, "rb").read() == dados, f"{nome}: o gzip do Python abre no nosso")
        # CRC-32 e Adler-32.
        r = rodar("crc", ent)
        crc, adler = r.stdout.decode().split()
        verificar(int(crc, 16) == zlib.crc32(dados) and int(adler, 16) == zlib.adler32(dados), f"{nome}: CRC-32 e Adler-32 iguais aos do Python")

    # Vários membros num .gz (como "cat a.gz b.gz > c.gz") e zeros no fim.
    multi = gzip.compress(b"primeiro ") + gzip.compress(b"segundo ", 1) + gzip.compress(b"terceiro") + bytes(10)
    r = rodar("gunzip", arquivo("multi.gz", multi), saida)
    verificar(r.returncode == 0 and open(saida, "rb").read() == b"primeiro segundo terceiro", "gzip com três membros e zeros no fim")
    # Estragados: CRC errado, cortado, bytes trocados.
    base = gzip.compress(amostras()["texto"], 6)
    ruim = bytearray(base)
    ruim[-8] ^= 1
    r = rodar("gunzip", arquivo("crc.gz", bytes(ruim)), saida)
    verificar(r.returncode == 1 and b"-103" in r.stdout, "CRC errado é recusado")
    r = rodar("gunzip", arquivo("cortado.gz", base[:len(base) // 2]), saida)
    verificar(r.returncode == 1, "gzip cortado pela metade é recusado")
    bases = [zlib.compress(d, n)[2:-4] for d in (amostras()["texto"][:20000], b"abc" * 3000) for n in (1, 9)]
    for k in range(250):
        b = bytearray(sorte.choice(bases))
        if k % 3 == 0:
            b = b[:sorte.randrange(len(b) + 1)]
        for _ in range(sorte.randrange(1, 6)):
            if b:
                b[sorte.randrange(len(b))] = sorte.randrange(256)
        r = rodar("inflar", arquivo("estragado", bytes(b)), saida)
        if r.returncode not in (0, 1):
            raise FalhaTeste(f"DEFLATE estragado {k} derrubou o inflar: {r.stderr.decode(errors='replace')[-800:]}")
    verificar(True, "250 fluxos estragados sem erro de memória nem vazamento")


if __name__ == "__main__":
    try:
        main()
        print("PASSOU")
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
