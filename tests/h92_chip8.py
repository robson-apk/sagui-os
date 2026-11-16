#!/usr/bin/env python3
"""H92 - o emulador de CHIP-8 e o montador chip8asm.

Critérios:
  - o núcleo (userland/jogos/chip8/nucleo.c) e o montador
    (userland/jogos/chip8asm/montador.c) passam no teste do computador de
    desenvolvimento (tests/jogos/teste_chip8.c): instruções, VF, saltos,
    BCD, desenho e colisão, SUPER-CHIP, teclas e temporizadores; as ROMs de
    exemplo (rootfs/jogos/chip8/*.asm, escritas para o SaguiOS) montam e
    rodam 10 s de máquina sem erro, com a tela mudando e bips;
  - no SaguiOS: "chip8 /jogos/chip8/sagui.ch8" abre a janela e roda a 60
    quadros por segundo (o log conta os quadros e a tela muda);
  - "chip8" sem ROM mostra a lista de /jogos/chip8; um duplo clique abre o
    Rebate, e a tecla W chega ao programa;
  - "chip8asm" monta o sagui.asm dentro do SaguiOS e o resultado roda.
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
    exe = os.path.join(PASTA, "teste_chip8")
    j = os.path.join(RAIZ, "userland", "jogos")
    subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", "-O2", "-DTESTE_HOSPEDEIRO", "-I", os.path.join(j, "chip8"),
                    "-I", os.path.join(j, "chip8asm"), "-o", exe, os.path.join(RAIZ, "tests", "jogos", "teste_chip8.c"),
                    os.path.join(j, "chip8", "nucleo.c"), os.path.join(j, "chip8asm", "montador.c")], check=True)
    roms = sorted(os.path.join(RAIZ, "rootfs", "jogos", "chip8", f)
                  for f in os.listdir(os.path.join(RAIZ, "rootfs", "jogos", "chip8")) if f.endswith(".asm"))
    r = subprocess.run([exe] + roms, capture_output=True, text=True)
    for linha in r.stdout.splitlines():
        print("  " + linha)
    verificar(r.returncode == 0 and len(roms) >= 2, "o núcleo, o montador e as ROMs de exemplo passam no teste do computador de desenvolvimento")


def esperar(m, padrao, tempo, desde=0):
    fim = time.time() + tempo
    while time.time() < fim:
        s = m.serial()[desde:]
        if re.search(padrao, s):
            return s
        time.sleep(0.3)
    raise FalhaTeste(f"não apareceu no log: {padrao!r}")


def fechar(m, titulo):
    jan = janela(m.serial(), titulo)
    m.clique(jan["fx"] + jan["l"] + 4 - 2 - 20 + 8, jan["fy"] + 12)
    time.sleep(1)


def main():
    teste_hospedeiro()
    with Maquina("h92-c8", "ide") as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        antes = len(m.serial())
        m.comando("chip8 /jogos/chip8/sagui.ch8")
        s = esperar(m, r"chip8: quadro 360 ", 40, antes)
        verificar("chip8: rom /jogos/chip8/sagui.ch8 carregada (123 bytes, CHIP-8)" in s, "a ROM de exemplo carrega")
        somas = re.findall(r"chip8: quadro (\d+) soma ([0-9a-f]+)", s)
        verificar(len(set(x for _, x in somas)) >= 2, f"a tela muda enquanto o sagui quica ({len(somas)} registros)")
        ini = time.time()
        esperar(m, r"chip8: quadro 600 ", 30, antes)
        verificar(True, f"os quadros andam (600 quadros; os últimos 240 em {time.time() - ini:.1f} s)")
        time.sleep(0.5)
        m.captura("H92-11-chip8-sagui")
        fechar(m, "CHIP-8")

        antes = len(m.serial())
        m.comando("chip8")
        s = esperar(m, r"chip8: lista pronta \((\d+) roms\)", 20, antes)
        itens = dict((c, int(i)) for i, c in re.findall(r"chip8: item (\d+) (\S+)", s))
        verificar("/jogos/chip8/rebate.ch8" in itens and "/jogos/chip8/sagui.ch8" in itens, "a lista mostra as ROMs de /jogos/chip8")
        time.sleep(1)
        m.captura("H92-12-chip8-lista")
        jan = janela(m.serial(), "CHIP-8")
        i = itens["/jogos/chip8/rebate.ch8"]
        antes = len(m.serial())
        m.clique(jan["x"] + 100, jan["y"] + 68 + 2 + i * 20 + 10, duplo=True)
        esperar(m, r"chip8: rom /jogos/chip8/rebate.ch8 carregada \(\d+ bytes, CHIP-8\)", 10, antes)
        verificar(True, "duplo clique na lista abre o Rebate")
        time.sleep(1)
        antes = len(m.serial())
        m.monitor("sendkey w 600")
        s = esperar(m, r"chip8: teclas 0020", 10, antes)
        verificar(True, "a tecla W chega ao programa como a tecla 5 do CHIP-8")
        esperar(m, r"chip8: quadro 240 ", 20, antes)
        verificar("chip8: parou" not in m.serial()[antes:], "o Rebate roda sem erro")
        m.captura("H92-13-chip8-rebate")
        fechar(m, "CHIP-8")

        m.comando("chip8asm /jogos/chip8/sagui.asm -o /tmp/s.ch8")
        antes = len(m.serial())
        m.comando("chip8 /tmp/s.ch8")
        esperar(m, r"chip8: rom /tmp/s.ch8 carregada \(123 bytes, CHIP-8\)", 20, antes)
        esperar(m, r"chip8: quadro 120 ", 20, antes)
        verificar(True, "o chip8asm do SaguiOS monta o sagui.asm igual ao do build (123 bytes) e a ROM roda")
    return 0


if __name__ == "__main__":
    sys.exit(main())
