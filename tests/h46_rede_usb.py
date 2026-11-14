#!/usr/bin/env python3
"""H46 (parte 1) - adaptador USB de rede (CDC Ethernet), para o G470.

Critério nesta parte: a leitura dos descritores acha o modo CDC Ethernet (ECM)
num adaptador com a configuração do fabricante primeiro e a ECM depois (como o
RTL8153 do TP-Link UE300), recusa configurações sem ECM, só NCM, quebradas ou
cortadas, e lê o MAC do texto. Roda no computador (kernel/usb/ecm_descritores.c
não depende do resto do kernel).

O caminho completo (bulk pelo EHCI, DHCP pela rede) só dá para testar com o
adaptador de verdade: o adaptador USB que o QEMU simula é de velocidade plena e
o EHCI só fala alta velocidade (D024, D034). O roteiro está em
docs/teste-v2-rede-usb.md.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import RAIZ, SAIDA  # noqa: E402


def main():
    os.makedirs(SAIDA, exist_ok=True)
    prog = os.path.join(SAIDA, "teste_ecm")
    r = subprocess.run(["gcc", "-m32", "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover", "-Wall", "-Wextra",
                        "-Werror", "-I", os.path.join(RAIZ, "tests", "usb", "simulado"), "-I", os.path.join(RAIZ, "kernel", "inc"),
                        "-o", prog, os.path.join(RAIZ, "tests", "usb", "teste_ecm.c"),
                        os.path.join(RAIZ, "kernel", "usb", "ecm_descritores.c")], capture_output=True, text=True)
    if r.returncode != 0:
        print("FALHOU: não compilou\n" + r.stderr[-1500:])
        return 1
    r = subprocess.run([prog], capture_output=True, text=True)
    print("\n".join("  " + l for l in r.stdout.splitlines()))
    if r.returncode != 0:
        print("FALHOU")
        return 1
    print("PASSOU")
    return 0


if __name__ == "__main__":
    sys.exit(main())
